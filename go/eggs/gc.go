package eggs

import (
	"fmt"
	"xtx/eggsfs/msgs"
)

type DestructionStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	DestructedBlocks uint64
}

func DestructFile(
	log *Logger,
	client *Client,
	mbs MockableBlockServices,
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
				conn, err := mbs.GetBlockServiceConnection(log, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2)
				if err != nil {
					return err
				}
				proof, err = mbs.EraseBlock(log, conn, block)
				mbs.ReleaseBlockServiceConnection(log, conn)
				if err != nil {
					return fmt.Errorf("%v: could not erase block %+v: %w", id, block, err)
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
	shid msgs.ShardId,
	stats *DestructionStats,
	blockService MockableBlockServices,
) error {
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		log.Debug("visiting files with %+v", req)
		err := client.ShardRequest(log, shid, &req, &resp)
		if err != nil {
			return fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if err := DestructFile(log, client, blockService, stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
				return fmt.Errorf("%+v: error while destructing file: %w", file, err)
			}
		}
		req.BeginId = resp.NextId
		if resp.NextId == 0 {
			break
		}
	}
	return nil
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func DestructFiles(
	log *Logger, shuckleAddress string, counters *ClientCounters, shid msgs.ShardId, blockService MockableBlockServices,
) error {
	client, err := NewClient(log, shuckleAddress, &shid, counters, nil)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := DestructionStats{}
	if err := destructFilesInternal(log, client, shid, &stats, blockService); err != nil {
		return err
	}
	log.Info("stats after one destruct files iteration: %+v", stats)
	return nil
}

func DestructFilesInAllShards(
	log *Logger,
	shuckleAddress string,
	counters *ClientCounters,
	blockService MockableBlockServices,
) error {
	client, err := NewClient(log, shuckleAddress, nil, counters, nil)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := DestructionStats{}
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		if err := destructFilesInternal(log, client, shid, &stats, blockService); err != nil {
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
	dirId msgs.InodeId, dirInfo *msgs.DirectoryInfoBody, edges []msgs.Edge,
) (bool, error) {
	policy := SnapshotPolicy{
		DeleteAfterTime:     dirInfo.DeleteAfterTime,
		DeleteAfterVersions: dirInfo.DeleteAfterVersions,
	}
	log.Debug("%v: about to apply policy %+v for name %s", dirId, policy, edges[0].Name)
	stats.VisitedEdges = stats.VisitedEdges + uint64(len(edges))
	now := msgs.Now()
	toCollect := policy.edgesToRemove(now, edges)
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

	dirInfo, err := resolveDirectoryInfo(log, client, dirInfoCache, dirId, &statResp)
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
		stop := resp.Next.StartHash == 0
		for _, result := range resp.Results {
			// we've encountered a current edge, it's time to stop
			if result.Current {
				stop = true
				hasEdges = true
			}
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || edges[0].Name != result.Name) {
				allRemoved, err := applyPolicy(log, client, stats, dirId, dirInfo, edges)
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
				allRemoved, err := applyPolicy(log, client, stats, dirId, dirInfo, edges)
				if err != nil {
					return err
				}
				hasEdges = hasEdges || !allRemoved
			}
			break
		}
		req.Cursor = resp.Next
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

func collectDirectoriesInternal(log *Logger, client *Client, stats *CollectStats, shid msgs.ShardId) error {
	dirInfoCache := NewDirInfoCache()
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

func CollectDirectories(log *Logger, shuckleAddress string, counters *ClientCounters, shid msgs.ShardId) error {
	client, err := NewClient(log, shuckleAddress, &shid, counters, nil)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := CollectStats{}
	if err := collectDirectoriesInternal(log, client, &stats, shid); err != nil {
		return err
	}
	log.Info("stats after one collect directories iteration: %+v", stats)
	return nil
}

func CollectDirectoriesInAllShards(log *Logger, shuckleAddress string, counters *ClientCounters) error {
	client, err := NewClient(log, shuckleAddress, nil, counters, nil)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := CollectStats{}
	for i := 0; i < 256; i++ {
		if err := collectDirectoriesInternal(log, client, &stats, msgs.ShardId(i)); err != nil {
			return err
		}
	}
	log.Info("stats after one all shards collect directories iteration: %+v", stats)
	return nil
}
