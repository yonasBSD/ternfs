package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
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
	Stats           DestructFilesStats
	WorkerQueueSize uint64
	Cursors         [256]msgs.InodeId
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
	// we've already checked this, but it might have expired
	// while in the queue
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
			for i := range initResp.Blocks {
				block := &initResp.Blocks[i]
				// Check if the block was stale/decommissioned/no_write, in which case
				// there might be nothing we can do here, for now.
				acceptFailure := block.BlockServiceFlags&(msgs.EGGSFS_BLOCK_SERVICE_STALE|msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED|msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE) != 0
				proof, err := client.EraseBlock(log, block)
				if err != nil {
					if acceptFailure {
						log.Debug("could not connect to stale/decommissioned block service %v while destructing file %v: %v", block.BlockServiceId, id, err)
						atomic.AddUint64(&stats.SkippedSpans, 1)
						return nil
					}
					return err
				}
				certifyReq.Proofs[i].BlockId = block.BlockId
				certifyReq.Proofs[i].Proof = proof
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

type destructFileRequest struct {
	id       msgs.InodeId
	deadline msgs.EggsTime
	cookie   [8]byte
}

func destructFilesWorker(
	log *Logger,
	client *Client,
	stats *DestructFilesState,
	workersChan chan *destructFileRequest,
	terminateChan chan any,
) {
	for {
		req := <-workersChan
		if req == nil {
			log.Debug("worker terminating")
			workersChan <- nil // terminate the other senders, too
			log.Debug("worker terminated")
			return
		}
		atomic.StoreUint64(&stats.WorkerQueueSize, uint64(len(workersChan)))
		if err := DestructFile(log, client, &stats.Stats, req.id, req.deadline, req.cookie); err != nil {
			log.Info("could not destruct file %v, terminating: %v", req.id, err)
			select {
			case terminateChan <- err:
			default:
			}
			return
		}
	}

}

func destructFilesScraper(
	log *Logger,
	client *Client,
	state *DestructFilesState,
	terminateChan chan any,
	sendChan chan *destructFileRequest,
	shards []msgs.ShardId,
) {
	reqs := make([]msgs.VisitTransientFilesReq, len(shards))
	for i := range reqs {
		reqs[i].BeginId = state.Cursors[shards[i]]
	}
	resps := make([]msgs.VisitTransientFilesResp, len(shards))
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
				log.Info("could not visit transient files: %v", err)
				select {
				case terminateChan <- err:
				default:
				}
				return
			}
			now := msgs.Now()
			for ix := range resp.Files {
				atomic.AddUint64(&state.Stats.VisitedFiles, 1)
				file := &resp.Files[ix]
				if now < file.DeadlineTime {
					log.Debug("%v: deadline not expired (deadline=%v, now=%v), not destructing", file.Id, file.DeadlineTime, now)
					continue
				}
				sendChan <- &destructFileRequest{
					id:       file.Id,
					deadline: file.DeadlineTime,
					cookie:   file.Cookie,
				}
			}
			state.Cursors[shid] = resp.NextId
			req.BeginId = resp.NextId
		}
		if allDone {
			// this will terminate all the senders
			log.Debug("file scraping done, terminating senders")
			sendChan <- nil
			return
		}
	}
}

type DestructFilesOptions struct {
	NumWorkers       int
	WorkersQueueSize int
}

func DestructFiles(
	log *Logger,
	client *Client,
	opts *DestructFilesOptions,
	stats *DestructFilesState,
	shards []msgs.ShardId,
) error {
	if opts.NumWorkers <= 0 {
		panic(fmt.Errorf("the number of workers should be positive, got %v", opts.NumWorkers))
	}
	terminateChan := make(chan any, 1)
	workersChan := make(chan *destructFileRequest, opts.WorkersQueueSize)

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		destructFilesScraper(log, client, stats, terminateChan, workersChan, shards)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkers)
	for i := 0; i < opts.NumWorkers; i++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			destructFilesWorker(log, client, stats, workersChan, terminateChan)
			workersWg.Done()
		}()
	}
	go func() {
		workersWg.Wait()
		log.Info("all workers terminated, we're done")
		terminateChan <- nil
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not scrub files: %v", err)
		return err.(error)
	}
}

func DestructFilesInAllShards(
	log *Logger,
	client *Client,
	opts *DestructFilesOptions,
) error {
	state := &DestructFilesState{}
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	return DestructFiles(log, client, opts, state, shards)
}
