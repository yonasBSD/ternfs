package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
	"xtx/eggsfs/msgs"
)

type CollectDirectoriesStats struct {
	VisitedDirectories    uint64
	VisitedEdges          uint64
	CollectedEdges        uint64
	DestructedDirectories uint64
}

type CollectDirectoriesState struct {
	Stats            CollectDirectoriesStats
	WorkersQueueSize uint64
	Cursors          [256]msgs.InodeId
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
		// Note that there is a race condition -- we do the stat quite a bit before
		// this. But this call will just fail if the directory has an owner.
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
	stats *CollectDirectoriesState,
	workersChan chan msgs.InodeId,
	terminateChan chan any,
) {
	for {
		dir := <-workersChan
		if dir == msgs.NULL_INODE_ID {
			log.Debug("worker terminating")
			workersChan <- msgs.NULL_INODE_ID // terminate the other workers, too
			log.Debug("worker terminated")
			return
		}
		atomic.StoreUint64(&stats.WorkersQueueSize, uint64(len(workersChan)))
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
	terminateChan chan any,
	sendChan chan msgs.InodeId,
	shards []msgs.ShardId,
) {
	reqs := make([]msgs.VisitDirectoriesReq, len(shards))
	for i := range reqs {
		reqs[i].BeginId = state.Cursors[shards[i]]
	}
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
				select {
				case terminateChan <- fmt.Errorf("could not visit directories: %w", err):
				default:
				}
				return
			}
			for _, id := range resp.Ids {
				sendChan <- id
			}
			state.Cursors[shid] = resp.NextId
			req.BeginId = resp.NextId
		}
		if allDone {
			log.Debug("directory scraping done, terminating workers")
			sendChan <- msgs.NULL_INODE_ID
			return
		}
	}
}

type CollectDirectoriesOpts struct {
	NumWorkers       int
	WorkersQueueSize int
}

func CollectDirectories(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	opts *CollectDirectoriesOpts,
	state *CollectDirectoriesState,
	shards []msgs.ShardId,
) error {
	log.Info("starting to collect directories in shards %+v", shards)

	if opts.NumWorkers <= 0 {
		panic(fmt.Errorf("the number of workers should be positive, got %v", opts.NumWorkers))
	}
	terminateChan := make(chan any, 1)
	workersChan := make(chan msgs.InodeId, opts.WorkersQueueSize)

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		collectDirectoriesScraper(log, client, state, terminateChan, workersChan, shards)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkers)
	for i := 0; i < opts.NumWorkers; i++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			collectDirectoriesWorker(log, client, dirInfoCache, state, workersChan, terminateChan)
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
		log.Info("stats after one collect directories iteration: %+v", state)
		return nil
	} else {
		log.Info("could not scrub files: %v", err)
		return err.(error)
	}
}

func CollectDirectoriesInAllShards(log *Logger, client *Client, dirInfoCache *DirInfoCache, opts *CollectDirectoriesOpts) error {
	state := CollectDirectoriesState{}
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	if err := CollectDirectories(log, client, dirInfoCache, opts, &state, shards); err != nil {
		return err
	}
	return nil
}
