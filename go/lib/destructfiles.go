package lib

import (
	"fmt"
	"sync"
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
	Stats             DestructFilesStats
	WorkersQueuesSize [256]uint64
	Cursors           [256]msgs.InodeId
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
	shid msgs.ShardId,
	workersChan chan *destructFileRequest,
	terminateChan chan any,
) {
	for {
		req := <-workersChan
		if req == nil {
			log.Debug("destruct files worker terminating")
			workersChan <- nil // terminate the other workers, too
			log.Debug("destruct files worker terminated")
			return
		}
		atomic.StoreUint64(&stats.WorkersQueuesSize[shid], uint64(len(workersChan)))
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
	shid msgs.ShardId,
	workerChan chan *destructFileRequest,
) {
	req := &msgs.VisitTransientFilesReq{
		BeginId: state.Cursors[shid],
	}
	resp := &msgs.VisitTransientFilesResp{}
	for {
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
			file := &resp.Files[ix]
			if now < file.DeadlineTime {
				log.Debug("%v: deadline not expired (deadline=%v, now=%v), not destructing", file.Id, file.DeadlineTime, now)
				continue
			}
			workerChan <- &destructFileRequest{
				id:       file.Id,
				deadline: file.DeadlineTime,
				cookie:   file.Cookie,
			}
		}
		state.Cursors[shid] = resp.NextId
		req.BeginId = resp.NextId
		if req.BeginId == 0 {
			// this will terminate all the senders
			log.Debug("file scraping done for shard %v, terminating workers", shid)
			workerChan <- nil
			return
		}
	}
}

type DestructFilesOptions struct {
	NumWorkersPerShard int
	WorkersQueueSize   int
	// How much we should wait between collection iterations in a single shard.
	// If negative, we'll stop.
	QuietPeriod time.Duration
}

func DestructFiles(
	log *Logger,
	client *Client,
	opts *DestructFilesOptions,
	stats *DestructFilesState,
	shid msgs.ShardId,
) error {
	if opts.NumWorkersPerShard <= 0 {
		panic(fmt.Errorf("the number of workers should be positive, got %v", opts.NumWorkersPerShard))
	}
	terminateChan := make(chan any, 1)
	workersChan := make(chan *destructFileRequest, opts.WorkersQueueSize)

	log.Info("destructing files in shard %v", shid)

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		destructFilesScraper(log, client, stats, terminateChan, shid, workersChan)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkersPerShard)
	for j := 0; j < opts.NumWorkersPerShard; j++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			destructFilesWorker(log, client, stats, shid, workersChan, terminateChan)
			workersWg.Done()
		}()
	}
	go func() {
		workersWg.Wait()
		log.Info("all workers terminated, we're done with shard %v", shid)
		terminateChan <- nil
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not destruct files in shard %v: %v", shid, err)
		return err.(error)
	}
}

func DestructFilesInAllShards(
	log *Logger,
	client *Client,
	opts *DestructFilesOptions,
	stats *DestructFilesState,
) error {
	terminateChan := make(chan any, 1)

	var wg sync.WaitGroup
	wg.Add(256)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				if err := DestructFiles(log, client, opts, stats, shid); err != nil {
					panic(err)
				}
				if opts.QuietPeriod < 0 {
					break
				} else {
					log.Info("waiting for %v before starting to destruct files again in shard %v", opts.QuietPeriod, shid)
					time.Sleep(opts.QuietPeriod)
				}
			}
			wg.Done()
		}()
	}
	go func() {
		wg.Wait()
		terminateChan <- nil
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not destruct files: %v", err)
		return err.(error)
	}
}
