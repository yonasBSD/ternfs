package lib

import (
	"fmt"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type DestructFilesStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	SkippedSpans     uint64
	DestructedBlocks uint64
	FailedFiles      uint64
}

type DestructFilesState struct {
	Stats   DestructFilesStats
	Cursors [256]msgs.InodeId
}

func DestructFile(
	log *Logger,
	client *Client,
	stats *DestructFilesStats,
	id msgs.InodeId,
	deadline msgs.EggsTime,
	cookie [8]byte,
) error {
	log.Debug("%v: destructing file, cookie=%v", id, cookie)
	atomic.AddUint64(&stats.VisitedFiles, 1)
	now := msgs.Now()
	if now < deadline {
		log.Debug("%v: deadline not expired (deadline=%v, now=%v), not destructing", id, deadline, now)
		return nil
	}
	// TODO need to think about transient files that already had dirty spans at the end.
	// Keep destructing spans until we have nothing
	initReq := msgs.RemoveSpanInitiateReq{
		FileId: id,
		Cookie: cookie,
	}
	initResp := msgs.RemoveSpanInitiateResp{}
	certifyReq := msgs.RemoveSpanCertifyReq{
		FileId: id,
		Cookie: cookie,
	}
	certifyResp := msgs.RemoveSpanCertifyResp{}
	for {
		err := client.ShardRequest(log, id.Shard(), &initReq, &initResp)
		if err == msgs.FILE_EMPTY {
			break // TODO: kinda ugly to rely on this for control flow...
		}
		if err != nil {
			return fmt.Errorf("%v: could not initiate span removal: %w", id, err)
		}
		if len(initResp.Blocks) > 0 {
			certifyReq.ByteOffset = initResp.ByteOffset
			certifyReq.Proofs = make([]msgs.BlockProof, len(initResp.Blocks))
			acceptFailure := make([]bool, len(initResp.Blocks))
			eraseChan := make(chan *BlockCompletion, len(initResp.Blocks))
			// first start all erase reqs at once
			for i, block := range initResp.Blocks {
				// Check if the block was stale/decommissioned/no_write, in which case
				// there might be nothing we can do here, for now.
				acceptFailure[i] = block.BlockServiceFlags&(msgs.EGGSFS_BLOCK_SERVICE_STALE|msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED|msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE) != 0
				if err := client.StartEraseBlock(log, &block, i, eraseChan); err != nil {
					if acceptFailure[i] {
						log.Debug("could not connect to stale/decommissioned block service %v while destructing file %v: %v", block.BlockServiceId, id, err)
						atomic.AddUint64(&stats.SkippedSpans, 1)
						return nil
					}
					return err
				}
			}
			// then wait for them
			for range initResp.Blocks {
				completion := <-eraseChan
				i := completion.Extra.(int)
				block := initResp.Blocks[i]
				if completion.Error != nil {
					if acceptFailure[i] {
						log.Debug("could not connect to stale/decommissioned block service %v while destructing file %v: %v", block.BlockServiceId, id, err)
						atomic.AddUint64(&stats.SkippedSpans, 1)
						return nil
					}
					return err
				}
				resp := completion.Resp.(*msgs.EraseBlockResp)
				certifyReq.Proofs[i].BlockId = block.BlockId
				certifyReq.Proofs[i].Proof = resp.Proof
				atomic.AddUint64(&stats.DestructedBlocks, 1)
			}
			err = client.ShardRequest(log, id.Shard(), &certifyReq, &certifyResp)
			if err != nil {
				return fmt.Errorf("%v: could not certify span removal %+v: %w", id, certifyReq, err)
			}
		}
		atomic.AddUint64(&stats.DestructedSpans, 1)
	}
	// Now purge the inode
	{
		err := client.ShardRequest(log, id.Shard(), &msgs.RemoveInodeReq{Id: id}, &msgs.RemoveInodeResp{})
		if err != nil {
			return fmt.Errorf("%v: could not remove transient file inode after removing spans: %w", id, err)
		}
	}
	atomic.AddUint64(&stats.DestructedFiles, 1)
	return nil
}

func destructFilesInternal(
	log *Logger,
	client *Client,
	state *DestructFilesState,
	retryOnFailure bool,
	shards []msgs.ShardId,
) error {
	alert := log.NewNCAlert(10 * time.Second)
	timeouts := NewReqTimeouts(time.Second, 10*time.Second, 0, 1.5, 0.2)
	reqs := make([]msgs.VisitTransientFilesReq, len(shards))
	for i := range reqs {
		reqs[i].BeginId = state.Cursors[shards[i]]
	}
	resps := make([]msgs.VisitTransientFilesResp, len(shards))
	someErrored := false
	for i := 0; ; i++ {
		allDone := true
		for j, shid := range shards {
			req := &reqs[j]
			resp := &resps[j]
			if i > 0 && req.BeginId == 0 {
				continue
			}
			allDone = false
			log.Debug("visiting files with %+v", req)
			err := client.ShardRequest(log, shid, req, resp)
			if err != nil {
				return fmt.Errorf("could not visit transient files: %w", err)
			}
			for ix := range resp.Files {
				file := &resp.Files[ix]
				if retryOnFailure {
					startedAt := time.Now()
					for {
						if err := DestructFile(log, client, &state.Stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
							log.RaiseNC(alert, "%+v: error while destructing file, will retry: %v", file, err)
							time.Sleep(timeouts.Next(startedAt))
						} else {
							log.ClearNC(alert)
							break
						}
					}
				} else {
					if err := DestructFile(log, client, &state.Stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
						log.RaiseAlert("%+v: error while destructing file: %v", file, err)
						atomic.AddUint64(&state.Stats.FailedFiles, 1)
						someErrored = true
					}
				}
			}
			state.Cursors[shid] = resp.NextId
			req.BeginId = resp.NextId
		}
		if allDone {
			break
		}
	}
	if someErrored {
		return fmt.Errorf("destructing some files failed, see logs")
	}
	return nil
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func DestructFiles(
	log *Logger,
	options *GCOptions,
	client *Client,
	stats *DestructFilesState,
	shards []msgs.ShardId,
) error {
	log.Info("starting to destruct files in shards %+v", shards)
	if err := destructFilesInternal(log, client, stats, options.RetryOnDestructFailure, shards); err != nil {
		return err
	}
	log.Info("stats after one destruct files iteration: %+v", stats)
	return nil
}

func DestructFilesInAllShards(
	log *Logger,
	client *Client,
	options *GCOptions,
) error {
	state := DestructFilesState{}
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	someErrored := false
	if err := destructFilesInternal(log, client, &state, options.RetryOnDestructFailure, shards); err != nil {
		// `destructFilesInternal` itself raises an alert, no need to raise two.
		log.Info("destructing files in shards %+v failed: %v", shards, err)
		someErrored = true
	}
	if someErrored {
		return fmt.Errorf("failed to destruct %v files, see logs", state.Stats.FailedFiles)
	}
	if state.Stats.SkippedSpans > 0 {
		log.RaiseAlert("skipped over %v spans, this is normal if some servers are (or were) down while garbage collecting", state.Stats.SkippedSpans)
	}
	log.Info("stats after one destruct files iteration in all shards: %+v", state.Stats)
	return nil
}
