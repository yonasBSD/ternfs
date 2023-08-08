package lib

import (
	"fmt"
	"time"
	"xtx/eggsfs/msgs"
)

type GCOptions struct {
	ShuckleTimeouts        *ReqTimeouts
	ShuckleAddress         string
	ShardTimeouts          *ReqTimeouts
	CDCTimeouts            *ReqTimeouts
	Counters               *ClientCounters
	RetryOnDestructFailure bool
}

func gcClient(log *Logger, options *GCOptions, udpSockets int) (*Client, error) {
	client, err := NewClient(log, options.ShuckleTimeouts, options.ShuckleAddress, udpSockets)
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

type DestructionStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	DestructedBlocks uint64
}

func DestructFile(
	log *Logger,
	client *Client,
	stats *DestructionStats,
	id msgs.InodeId, deadline msgs.EggsTime, cookie [8]byte,
) error {
	log.Debug("%v: destructing file, cookie=%v", id, cookie)
	stats.VisitedFiles++
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
			for i, block := range initResp.Blocks {
				var proof [8]byte
				conn, err := client.GetWriteBlocksConn(log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2)
				if err != nil {
					return err
				}
				defer conn.Close()
				proof, err = EraseBlock(log, conn, &block)
				if err != nil {
					return fmt.Errorf("%v: could not erase block %+v: %w", id, block, err)
				} else {
					conn.Put()
				}
				stats.DestructedBlocks++
				certifyReq.Proofs[i].BlockId = block.BlockId
				certifyReq.Proofs[i].Proof = proof
			}
			err = client.ShardRequest(log, id.Shard(), &certifyReq, &certifyResp)
			if err != nil {
				return fmt.Errorf("%v: could not certify span removal %+v: %w", id, certifyReq, err)
			}
		}
		stats.DestructedSpans++
	}
	// Now purge the inode
	{
		err := client.ShardRequest(log, id.Shard(), &msgs.RemoveInodeReq{Id: id}, &msgs.RemoveInodeResp{})
		if err != nil {
			return fmt.Errorf("%v: could not remove transient file inode after removing spans: %w", id, err)
		}
	}
	stats.DestructedFiles++
	return nil
}

func destructFilesInternal(
	log *Logger,
	client *Client,
	retryOnFailure bool,
	shid msgs.ShardId,
	stats *DestructionStats,
) error {
	alert := log.NewNCAlert()
	timeouts := NewReqTimeouts(time.Second, 10*time.Second, 0, 1.5, 0.2)
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	someErrored := false
	for {
		log.Debug("visiting files with %+v", req)
		err := client.ShardRequest(log, shid, &req, &resp)
		if err != nil {
			return fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if !retryOnFailure {
				if err := DestructFile(log, client, stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
					log.RaiseAlert("%+v: error while destructing file: %v", file, err)
					someErrored = true
				}
			} else {
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
			}
		}
		req.BeginId = resp.NextId
		if resp.NextId == 0 {
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
func destructFiles(
	log *Logger,
	options *GCOptions,
	shid msgs.ShardId,
) error {
	log.Info("starting to destruct files in shard %v", shid)
	client, err := gcClient(log, options, 1)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := DestructionStats{}
	if err := destructFilesInternal(log, client, options.RetryOnDestructFailure, shid, &stats); err != nil {
		return err
	}
	log.Info("stats after one destruct files iteration: %+v", stats)
	return nil
}

func DestructFiles(
	log *Logger, options *GCOptions, shid msgs.ShardId,
) error {
	return destructFiles(log, options, shid)
}

func DestructFilesMockedBlockServices(
	log *Logger, options *GCOptions, shid msgs.ShardId,
) error {
	return destructFiles(log, options, shid)
}

func DestructFilesInAllShards(
	log *Logger,
	options *GCOptions,
) error {
	client, err := gcClient(log, options, 256)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := DestructionStats{}
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		if err := destructFilesInternal(log, client, options.RetryOnDestructFailure, shid, &stats); err != nil {
			return err
		}
	}
	log.Info("stats after one destruct files iteration in all shards: %+v", stats)
	return nil
}

type CollectStats struct {
	VisitedDirectories    uint64
	VisitedEdges          uint64
	CollectedEdges        uint64
	DestructedDirectories uint64
}

// returns whether all the edges were removed
func applyPolicy(
	log *Logger, client *Client, stats *CollectStats,
	dirId msgs.InodeId, policy *msgs.SnapshotPolicy, edges []msgs.Edge,
) (bool, error) {
	log.Debug("%v: about to apply policy %+v for name %s", dirId, policy, edges[0].Name)
	stats.VisitedEdges = stats.VisitedEdges + uint64(len(edges))
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
		stats.CollectedEdges++
	}
	return toCollect == len(edges), nil
}

func CollectDirectory(log *Logger, client *Client, dirInfoCache *DirInfoCache, stats *CollectStats, dirId msgs.InodeId) error {
	log.Debug("%v: collecting", dirId)
	stats.VisitedDirectories++

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
			stats.DestructedDirectories++
		}
	}
	return nil
}

func collectDirectoriesInternal(log *Logger, client *Client, dirInfoCache *DirInfoCache, stats *CollectStats, shid msgs.ShardId) error {
	req := msgs.VisitDirectoriesReq{}
	resp := msgs.VisitDirectoriesResp{}
	for {
		err := client.ShardRequest(log, shid, &req, &resp)
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
			if err := CollectDirectory(log, client, dirInfoCache, stats, id); err != nil {
				return fmt.Errorf("error while collecting inode %v: %w", id, err)
			}
		}
		req.BeginId = resp.NextId
		if req.BeginId == 0 {
			break
		}
	}
	return nil
}

func CollectDirectories(log *Logger, options *GCOptions, dirInfoCache *DirInfoCache, shid msgs.ShardId) error {
	client, err := gcClient(log, options, 1)
	if err != nil {
		return err
	}
	stats := CollectStats{}
	if err := collectDirectoriesInternal(log, client, dirInfoCache, &stats, shid); err != nil {
		return err
	}
	log.Info("stats after one collect directories iteration: %+v", stats)
	return nil
}

func CollectDirectoriesInAllShards(log *Logger, options *GCOptions, dirInfoCache *DirInfoCache) error {
	client, err := gcClient(log, options, 256)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := CollectStats{}
	for i := 0; i < 256; i++ {
		if err := collectDirectoriesInternal(log, client, dirInfoCache, &stats, msgs.ShardId(i)); err != nil {
			return err
		}
	}
	log.Info("stats after one all shards collect directories iteration: %+v", stats)
	return nil
}
