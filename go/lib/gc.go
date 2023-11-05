package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type GCOptions struct {
	ShuckleTimeouts        *ReqTimeouts
	ShardTimeouts          *ReqTimeouts
	CDCTimeouts            *ReqTimeouts
	Counters               *ClientCounters
	RetryOnDestructFailure bool
}

func GCClient(log *Logger, shuckleAddress string, options *GCOptions) (*Client, error) {
	client, err := NewClient(log, options.ShuckleTimeouts, shuckleAddress)
	if err != nil {
		return nil, err
	}
	if options.ShardTimeouts != nil {
		client.SetShardTimeouts(options.ShardTimeouts)
	}
	if options.CDCTimeouts != nil {
		client.SetCDCTimeouts(options.CDCTimeouts)
	}
	if options.Counters != nil {
		client.SetCounters(options.Counters)
	}
	return client, nil
}

type GCStats struct {
	// file destruction
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	SkippedSpans     uint64
	DestructedBlocks uint64
	FailedFiles      uint64
	// directory collection
	VisitedDirectories    uint64
	VisitedEdges          uint64
	CollectedEdges        uint64
	DestructedDirectories uint64
	// zero block service stuff
	ZeroBlockServiceFilesRemoved uint64
}

func DestructFile(
	log *Logger,
	client *Client,
	stats *GCStats,
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
	stats *GCStats,
	retryOnFailure bool,
	shards []msgs.ShardId,
) error {
	alert := log.NewNCAlert(10 * time.Second)
	timeouts := NewReqTimeouts(time.Second, 10*time.Second, 0, 1.5, 0.2)
	reqs := make([]msgs.VisitTransientFilesReq, len(shards))
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
						if err := DestructFile(log, client, stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
							log.RaiseNC(alert, "%+v: error while destructing file, will retry: %v", file, err)
							time.Sleep(timeouts.Next(startedAt))
						} else {
							log.ClearNC(alert)
							break
						}
					}
				} else {
					if err := DestructFile(log, client, stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
						log.RaiseAlert("%+v: error while destructing file: %v", file, err)
						atomic.AddUint64(&stats.FailedFiles, 1)
						someErrored = true
					}
				}
			}
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
	stats *GCStats,
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
	stats := GCStats{}
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	someErrored := false
	if err := destructFilesInternal(log, client, &stats, options.RetryOnDestructFailure, shards); err != nil {
		// `destructFilesInternal` itself raises an alert, no need to raise two.
		log.Info("destructing files in shards %+v failed: %v", shards, err)
		someErrored = true
	}
	if someErrored {
		return fmt.Errorf("failed to destruct %v files, see logs", stats.FailedFiles)
	}
	if stats.SkippedSpans > 0 {
		log.RaiseAlert("skipped over %v spans, this is normal if some servers are (or were) down while garbage collecting", stats.SkippedSpans)
	}
	log.Info("stats after one destruct files iteration in all shards: %+v", stats)
	return nil
}

// returns whether all the edges were removed
func applyPolicy(
	log *Logger,
	client *Client,
	cdcMu *sync.Mutex,
	stats *GCStats,
	dirId msgs.InodeId,
	policy *msgs.SnapshotPolicy,
	edges []msgs.Edge,
) (bool, error) {
	log.Debug("%v: about to apply policy %+v for name %s", dirId, policy, edges[0].Name)
	atomic.AddUint64(&stats.VisitedEdges, uint64(len(edges)))
	now := msgs.Now()
	toCollect := edgesToRemove(policy, now, edges)
	log.Debug("%v: will remove %d edges out of %d", dirId, toCollect, len(edges))
	for _, edge := range edges[:toCollect] {
		if edge.Current {
			panic(fmt.Errorf("unexpected current edge to %v when trying to apply policy on dir %v", edge.TargetId, dirId))
		}
		var err error
		if edge.TargetId.Extra() { // the extra bit on a snapshot edge means owned
			if edge.TargetId.Id().Shard() == dirId.Shard() {
				// same shard, we can delete directly. We also know that this is not a directory (it's an
				// owned, but snapshot edge)
				log.Debug("%v: removing owned snapshot edge %+v", dirId, edge)
				req := msgs.SameShardHardFileUnlinkReq{
					OwnerId:      dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				err = client.ShardRequest(log, dirId.Shard(), &req, &msgs.SameShardHardFileUnlinkResp{})
			} else {
				// different shard, we need to go through the CDC
				log.Debug("%v: removing cross-shard owned edge %+v", dirId, edge)
				req := msgs.CrossShardHardUnlinkFileReq{
					OwnerId:      dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				cdcMu.Lock()
				err = client.CDCRequest(log, &req, &msgs.CrossShardHardUnlinkFileResp{})
				cdcMu.Unlock()
			}
		} else {
			// non-owned edge, we can just kill it without worrying about much.
			log.Debug("%v: removing non-owned edge %+v", dirId, edge)
			req := msgs.RemoveNonOwnedEdgeReq{
				DirId:        dirId,
				TargetId:     edge.TargetId.Id(),
				Name:         edge.Name,
				CreationTime: edge.CreationTime,
			}
			err = client.ShardRequest(log, dirId.Shard(), &req, &msgs.RemoveNonOwnedEdgeResp{})
		}
		if err != nil {
			return false, fmt.Errorf("error while collecting edge %+v in directory %v: %w", edge, dirId, err)
		}
		atomic.AddUint64(&stats.CollectedEdges, 1)
	}
	return toCollect == len(edges), nil
}

func CollectDirectory(log *Logger, client *Client, dirInfoCache *DirInfoCache, cdcMu *sync.Mutex, stats *GCStats, dirId msgs.InodeId) error {
	log.Debug("%v: collecting", dirId)
	atomic.AddUint64(&stats.VisitedDirectories, 1)

	statReq := msgs.StatDirectoryReq{
		Id: dirId,
	}
	statResp := msgs.StatDirectoryResp{}
	err := client.ShardRequest(log, dirId.Shard(), &statReq, &statResp)
	if err != nil {
		return fmt.Errorf("error while stat'ing directory: %w", err)
	}

	policy := &msgs.SnapshotPolicy{}
	_, err = client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, policy)
	if err != nil {
		return err
	}

	edges := make([]msgs.Edge, 0)
	req := msgs.FullReadDirReq{
		DirId: dirId,
	}
	resp := msgs.FullReadDirResp{}
	hasEdges := false
	for {
		err := client.ShardRequest(log, dirId.Shard(), &req, &resp)
		if err != nil {
			return err
		}
		log.Debug("%v: got %d edges in response", dirId, len(resp.Results))
		stop := resp.Next.StartName == ""
		for _, result := range resp.Results {
			// we've encountered a current edge, it's time to stop
			if result.Current {
				stop = true
				hasEdges = true
			}
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || edges[0].Name != result.Name) {
				allRemoved, err := applyPolicy(log, client, cdcMu, stats, dirId, policy, edges)
				if err != nil {
					return err
				}
				hasEdges = hasEdges || !allRemoved
				edges = edges[:0]
			}
			edges = append(edges, result)
		}
		if stop {
			if len(edges) > 0 {
				allRemoved, err := applyPolicy(log, client, cdcMu, stats, dirId, policy, edges)
				if err != nil {
					return err
				}
				hasEdges = hasEdges || !allRemoved
			}
			break
		}
		req.Flags = 0
		if resp.Next.Current {
			req.Flags = msgs.FULL_READ_DIR_CURRENT
		}
		req.StartName = resp.Next.StartName
		req.StartTime = resp.Next.StartTime
	}
	if !hasEdges && dirId != msgs.ROOT_DIR_INODE_ID {
		// Note that there is a race condition -- we do the stat quite a bit before
		// this. But this call will just fail if the directory has an owner.
		if statResp.Owner == msgs.NULL_INODE_ID {
			log.Debug("%v: removing directory inode, since it has no edges and no owner", dirId)
			req := msgs.HardUnlinkDirectoryReq{
				DirId: dirId,
			}
			cdcMu.Lock()
			err := client.CDCRequest(log, &req, &msgs.HardUnlinkDirectoryResp{})
			cdcMu.Unlock()
			if err != nil {
				return fmt.Errorf("error while trying to remove directory inode: %w", err)
			}
			atomic.AddUint64(&stats.DestructedDirectories, 1)
		}
	}
	return nil
}

func CollectDirectories(log *Logger, client *Client, dirInfoCache *DirInfoCache, cdcMu *sync.Mutex, stats *GCStats, shards []msgs.ShardId) error {
	log.Info("starting to collect directories in shards %+v", shards)
	reqs := make([]msgs.VisitDirectoriesReq, len(shards))
	resps := make([]msgs.VisitDirectoriesResp, len(shards))
	for i := 0; ; i++ {
		allDone := true
		for j, shid := range shards {
			req := &reqs[j]
			resp := &resps[j]
			if i > 0 && req.BeginId == 0 {
				continue
			}
			allDone = false
			err := client.ShardRequest(log, shid, req, resp)
			if err != nil {
				return fmt.Errorf("could not visit directories: %w", err)
			}
			for _, id := range resp.Ids {
				if id.Type() != msgs.DIRECTORY {
					panic(fmt.Errorf("bad directory inode %v", id))
				}
				if id.Shard() != shid {
					panic("bad shard")
				}
				if err := CollectDirectory(log, client, dirInfoCache, cdcMu, stats, id); err != nil {
					return fmt.Errorf("error while collecting inode %v: %w", id, err)
				}
			}
			req.BeginId = resp.NextId
		}
		if allDone {
			break
		}
	}
	log.Info("stats after one collect directories iteration: %+v", stats)
	return nil
}

func CollectDirectoriesInAllShards(log *Logger, client *Client, dirInfoCache *DirInfoCache) error {
	stats := GCStats{}
	var cdcMu sync.Mutex
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	if err := CollectDirectories(log, client, dirInfoCache, &cdcMu, &stats, shards); err != nil {
		return err
	}
	log.Info("stats after one all shards collect directories iteration: %+v", stats)
	return nil
}

func CollectZeroBlockServiceFiles(log *Logger, client *Client, stats *GCStats, shards []msgs.ShardId) error {
	log.Info("starting to collect block services with zero files in shards %+v", shards)
	reqs := make([]msgs.RemoveZeroBlockServiceFilesReq, len(shards))
	resps := make([]msgs.RemoveZeroBlockServiceFilesResp, len(shards))
	for i := 0; ; i++ {
		allDone := true
		for j, shid := range shards {
			req := &reqs[j]
			resp := &resps[j]
			if i > 0 && req.StartBlockService == 0 && req.StartFile == msgs.NULL_INODE_ID {
				continue
			}
			allDone = false
			err := client.ShardRequest(log, shid, req, resp)
			if err != nil {
				return fmt.Errorf("could not remove zero block services: %w", err)
			}
			req.StartBlockService = resp.NextBlockService
			req.StartFile = resp.NextFile
			atomic.AddUint64(&stats.ZeroBlockServiceFilesRemoved, resp.Removed)
		}
		if allDone {
			break
		}
	}
	log.Info("finished collecting zero block service files in shards %+v: %+v", shards, stats)
	return nil
}

func CollectZeroBlockServiceFilesInAllShards(log *Logger, client *Client) error {
	var stats GCStats
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	return CollectZeroBlockServiceFiles(log, client, &stats, shards)
}
