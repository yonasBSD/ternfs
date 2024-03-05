package cleanup

import (
	"fmt"
	"sync"
	"sync/atomic"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type DestructFilesStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	SkippedSpans     uint64
	DestructedBlocks uint64
	FailedFiles      uint64
	Cycles           [256]uint32
}

type DestructFilesState struct {
	Stats             DestructFilesStats
	WorkersQueuesSize [256]uint64
	Cursors           [256]msgs.InodeId
}

type CouldNotReachBlockServices []msgs.BlockServiceId

func (c CouldNotReachBlockServices) Error() string {
	return fmt.Sprintf("could not reach block services: %+v", c)
}

func DestructFile(
	log *lib.Logger,
	c *client.Client,
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
	couldNotReachBlockServices := []msgs.BlockServiceId{}
	for {
		err := c.ShardRequest(log, id.Shard(), &initReq, &initResp)
		if err == msgs.FILE_EMPTY {
			break // TODO: kinda ugly to rely on this for control flow...
		}
		if err != nil {
			return fmt.Errorf("%v: could not initiate span removal: %w", id, err)
		}
		allBlocksErased := true
		if len(initResp.Blocks) > 0 {
			certifyReq.ByteOffset = initResp.ByteOffset
			certifyReq.Proofs = make([]msgs.BlockProof, len(initResp.Blocks))
			for i := range initResp.Blocks {
				block := &initResp.Blocks[i]
				// There's no point trying to erase blocks for stale block services which are
				// not decommissioned -- they're almost certainly temporarly offline, and we'll
				// be stuck forever since in GC we run with infinite timeout. Just skip.
				if block.BlockServiceFlags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_STALE) && !block.BlockServiceFlags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
					log.Debug("skipping block %v in file %v since its block service %v is stale", block.BlockId, id, block.BlockServiceId)
					allBlocksErased = false
					found := false
					for _, otherBlockService := range couldNotReachBlockServices {
						if otherBlockService == block.BlockServiceId {
							found = true
							break
						}
					}
					if !found {
						couldNotReachBlockServices = append(couldNotReachBlockServices, block.BlockServiceId)
					}
				}
				// Check if the block was stale/decommissioned/no_write, in which case
				// there might be nothing we can do here, for now.
				acceptFailure := block.BlockServiceFlags&(msgs.EGGSFS_BLOCK_SERVICE_STALE|msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED|msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE) != 0
				proof, err := c.EraseBlock(log, block)
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
			if allBlocksErased {
				err = c.ShardRequest(log, id.Shard(), &certifyReq, &certifyResp)
				if err != nil {
					return fmt.Errorf("%v: could not certify span removal %+v: %w", id, certifyReq, err)
				}
			}
		}
		if len(couldNotReachBlockServices) == 0 {
			atomic.AddUint64(&stats.DestructedSpans, 1)
		}
	}
	// Now purge the inode
	if len(couldNotReachBlockServices) == 0 {
		err := c.ShardRequest(log, id.Shard(), &msgs.RemoveInodeReq{Id: id}, &msgs.RemoveInodeResp{})
		if err != nil {
			return fmt.Errorf("%v: could not remove transient file inode after removing spans: %w", id, err)
		}
		atomic.AddUint64(&stats.DestructedFiles, 1)
		return nil
	} else {
		return CouldNotReachBlockServices(couldNotReachBlockServices)
	}
}

type destructFileRequest struct {
	id       msgs.InodeId
	deadline msgs.EggsTime
	cookie   [8]byte
}

func destructFilesWorker(
	log *lib.Logger,
	c *client.Client,
	stats *DestructFilesState,
	shid msgs.ShardId,
	workersChan chan *destructFileRequest,
	terminateChan chan any,
) {
	for {
		req, ok := <-workersChan
		if !ok {
			log.Debug("destruct files worker terminating")
			return
		}
		atomic.StoreUint64(&stats.WorkersQueuesSize[shid], uint64(len(workersChan)))
		if err := DestructFile(log, c, &stats.Stats, req.id, req.deadline, req.cookie); err != nil {
			// this is OK, we'll get there eventually
			_, isCouldNotReach := err.(CouldNotReachBlockServices)
			if isCouldNotReach {
				log.Debug("could not reach block services when destructing %v, ignoring: %+v", req.id, err)
			} else {
				log.Info("could not destruct file %v, terminating: %v", req.id, err)
				select {
				case terminateChan <- err:
				default:
				}
				return
			}
		}
	}
}

func destructFilesScraper(
	log *lib.Logger,
	c *client.Client,
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
		err := c.ShardRequest(log, shid, req, resp)
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
			close(workerChan)
			return
		}
	}
}

type DestructFilesOptions struct {
	NumWorkersPerShard int
	WorkersQueueSize   int
}

func DestructFiles(
	log *lib.Logger,
	c *client.Client,
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
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		destructFilesScraper(log, c, stats, terminateChan, shid, workersChan)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkersPerShard)
	for j := 0; j < opts.NumWorkersPerShard; j++ {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			destructFilesWorker(log, c, stats, shid, workersChan, terminateChan)
			workersWg.Done()
		}()
	}
	go func() {
		workersWg.Wait()
		log.Info("all workers terminated, we're done with shard %v", shid)
		atomic.AddUint32(&stats.Stats.Cycles[shid], 1)
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
	log *lib.Logger,
	c *client.Client,
	opts *DestructFilesOptions,
	stats *DestructFilesState,
) error {
	terminateChan := make(chan any, 1)

	var wg sync.WaitGroup
	wg.Add(256)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			if err := DestructFiles(log, c, opts, stats, shid); err != nil {
				panic(err)
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
