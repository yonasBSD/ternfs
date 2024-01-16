package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type CollectDirectoriesStats struct {
	VisitedDirectories    uint64
	VisitedEdges          uint64
	CollectedEdges        uint64
	DestructedDirectories uint64
}

type CollectDirectoriesState struct {
	Stats             CollectDirectoriesStats
	WorkersQueuesSize [256]uint64
	Cursors           [256]msgs.InodeId
}

// returns whether all the edges were removed
func applyPolicy(
	log *Logger,
	client *Client,
	stats *CollectDirectoriesStats,
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
				err = client.CDCRequest(log, &req, &msgs.CrossShardHardUnlinkFileResp{})
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

func CollectDirectory(log *Logger, client *Client, dirInfoCache *DirInfoCache, stats *CollectDirectoriesStats, dirId msgs.InodeId) error {
	log.Debug("%v: collecting", dirId)
	atomic.AddUint64(&stats.VisitedDirectories, 1)

	policy := &msgs.SnapshotPolicy{}
	if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, policy); err != nil {
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
				allRemoved, err := applyPolicy(log, client, stats, dirId, policy, edges)
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
				allRemoved, err := applyPolicy(log, client, stats, dirId, policy, edges)
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
		statReq := msgs.StatDirectoryReq{
			Id: dirId,
		}
		statResp := msgs.StatDirectoryResp{}
		err := client.ShardRequest(log, dirId.Shard(), &statReq, &statResp)
		if err != nil {
			return fmt.Errorf("error while stat'ing directory: %w", err)
		}
		if statResp.Owner == msgs.NULL_INODE_ID {
			log.Debug("%v: removing directory inode, since it has no edges and no owner", dirId)
			req := msgs.HardUnlinkDirectoryReq{
				DirId: dirId,
			}
			err := client.CDCRequest(log, &req, &msgs.HardUnlinkDirectoryResp{})
			if err != nil {
				return fmt.Errorf("error while trying to remove directory inode: %w", err)
			}
			atomic.AddUint64(&stats.DestructedDirectories, 1)
		}
	}
	return nil
}

func collectDirectoriesWorker(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	rateLimit *RateLimit,
	stats *CollectDirectoriesState,
	shid msgs.ShardId,
	workersChan chan msgs.InodeId,
	terminateChan chan any,
) {
	for {
		if rateLimit != nil {
			rateLimit.Acquire()
		}
		dir := <-workersChan
		if dir == msgs.NULL_INODE_ID {
			log.Debug("collect directories worker for shard %v terminating len=%v cap=%v", shid, len(workersChan), cap(workersChan))
			workersChan <- msgs.NULL_INODE_ID // terminate the other workers, too
			log.Debug("collect directories worker for shard %v terminated", shid)
			return
		}
		log.Debug("received worker request for shard %v len=%v cap=%v", shid, len(workersChan), cap(workersChan))
		atomic.StoreUint64(&stats.WorkersQueuesSize[shid], uint64(len(workersChan)))
		if err := CollectDirectory(log, client, dirInfoCache, &stats.Stats, dir); err != nil {
			log.Info("could not destruct directory %v, terminating: %v", dir, err)
			select {
			case terminateChan <- err:
			default:
			}
			return
		}
	}
}

func collectDirectoriesScraper(
	log *Logger,
	client *Client,
	state *CollectDirectoriesState,
	shid msgs.ShardId,
	workerChan chan msgs.InodeId,
	terminateChan chan any,
) {
	req := &msgs.VisitDirectoriesReq{
		BeginId: state.Cursors[shid],
	}
	resp := &msgs.VisitDirectoriesResp{}
	for {
		err := client.ShardRequest(log, shid, req, resp)
		if err != nil {
			select {
			case terminateChan <- fmt.Errorf("could not visit directories: %w", err):
			default:
			}
			return
		}
		for _, id := range resp.Ids {
			workerChan <- id
		}
		state.Cursors[shid] = resp.NextId
		req.BeginId = resp.NextId
		if req.BeginId == 0 {
			log.Debug("directory scraping done for shard %v, terminating workers", shid)
			workerChan <- msgs.NULL_INODE_ID
			return
		}
	}
}

type CollectDirectoriesOpts struct {
	NumWorkersPerShard int
	WorkersQueueSize   int
	// How much we should wait between collection iterations in a single shard.
	// If negative, we'll stop.
	QuietPeriod time.Duration
}

func CollectDirectories(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	rateLimit *RateLimit,
	opts *CollectDirectoriesOpts,
	state *CollectDirectoriesState,
	shid msgs.ShardId,
) error {
	log.Info("starting to collect directories in shard %v", shid)

	if opts.NumWorkersPerShard <= 0 {
		panic(fmt.Errorf("bad NumWorkersPerShard %v", opts.NumWorkersPerShard))
	}

	terminateChan := make(chan any, 1)
	workerChan := make(chan msgs.InodeId, opts.WorkersQueueSize)

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		collectDirectoriesScraper(log, client, state, shid, workerChan, terminateChan)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkersPerShard)
	for j := 0; j < opts.NumWorkersPerShard; j++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			collectDirectoriesWorker(log, client, dirInfoCache, rateLimit, state, shid, workerChan, terminateChan)
			workersWg.Done()
		}()
	}
	go func() {
		workersWg.Wait()
		log.Info("all workers terminated for shard %v, we're done", shid)
		terminateChan <- nil
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not collect directories in shard %v: %v", shid, err)
		return err.(error)
	}
}

func CollectDirectoriesInAllShards(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	rateLimit *RateLimit,
	opts *CollectDirectoriesOpts,
	state *CollectDirectoriesState,
) error {
	terminateChan := make(chan any, 1)

	var wg sync.WaitGroup
	wg.Add(256)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				if err := CollectDirectories(log, client, dirInfoCache, rateLimit, opts, state, shid); err != nil {
					panic(err)
				}
				if opts.QuietPeriod < 0 {
					break
				} else {
					log.Info("waiting for %v before starting to collect directories again in shard %v", opts.QuietPeriod, shid)
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
		log.Info("could not collect directories: %v", err)
		return err.(error)
	}
}
