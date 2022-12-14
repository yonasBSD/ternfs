package eggs

import (
	"fmt"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type DestructionStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	DestructedBlocks uint64
}

func DestructFile(
	log LogLevels,
	client Client,
	blockServicesKeys map[msgs.BlockServiceId][16]byte,
	stats *DestructionStats,
	id msgs.InodeId, deadline msgs.EggsTime, cookie [8]byte,
) error {
	log.Debug("%v: destructing file, cookie=%v", id, cookie)
	stats.VisitedFiles++
	now := msgs.Now()
	if now < deadline {
		log.Debug("%v: deadline not expired (deadline=%v, now=%v), not destructing", id, deadline, now)
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
				if blockServicesKeys == nil {
					proof, err = EraseBlock(log, block)
					if err != nil {
						return fmt.Errorf("%v: could not erase block %+v: %w", id, block, err)
					}
				} else {
					key, wasPresent := blockServicesKeys[block.BlockServiceId]
					if !wasPresent {
						panic(fmt.Errorf("could not find key for block service %v", block.BlockServiceId))
					}
					proof = BlockDeleteProof(block.BlockServiceId, block.BlockId, key)
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

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
//
// If `blockServicesKeys` is present, spans won't be actually removed --
// we'll just generate the proof ourselves and certify. This is only useful
// for testing, obviously.
func DestructFiles(
	log LogLevels, shid msgs.ShardId, blockServicesKeys map[msgs.BlockServiceId][16]byte,
) error {
	client, err := NewShardSpecificClient(shid)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := DestructionStats{}
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		/*
			if stats.VisitedFiles%100 == 0 {
				log.Info("%v visited files, %v destructed files, %v destructed spans, %v destructed blocks", stats.VisitedFiles, stats.DestructedFiles, stats.DestructedSpans, stats.DestructedBlocks)
			}
		*/
		err := client.ShardRequest(log, shid, &req, &resp)
		if err != nil {
			return fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if err := DestructFile(log, client, blockServicesKeys, &stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
				return fmt.Errorf("%+v: error while destructing file: %w", file, err)
			}
		}
		req.BeginId = resp.NextId
		if resp.NextId == 0 {
			break
		}
	}
	log.Info("stats after one destruct files iteration: %+v", stats)
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
	log LogLevels, client Client, stats *CollectStats,
	dirId msgs.InodeId, dirInfo *msgs.DirectoryInfoBody, edges []msgs.Edge,
) (bool, error) {
	policy := SnapshotPolicy{
		DeleteAfterTime:     time.Duration(dirInfo.DeleteAfterTime),
		DeleteAfterVersions: int(dirInfo.DeleteAfterVersions),
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
				req := msgs.IntraShardHardFileUnlinkReq{
					OwnerId:      dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				err = client.ShardRequest(log, dirId.Shard(), &req, &msgs.IntraShardHardFileUnlinkResp{})
			} else {
				// different shard, we need to go through the CDC
				log.Debug("%v: removing cross-shard owned edge %+v", dirId, edge)
				req := msgs.HardUnlinkFileReq{
					OwnerId:      dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				err = client.CDCRequest(log, &req, &msgs.HardUnlinkFileResp{})
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

func requestDirectoryInfo(
	log LogLevels, client Client, dirInfoCache *DirInfoCache, dirId msgs.InodeId,
) (*msgs.DirectoryInfoBody, error) {
	statResp := msgs.StatDirectoryResp{}
	err := client.ShardRequest(log, dirId.Shard(), &msgs.StatDirectoryReq{Id: dirId}, &statResp)
	if err != nil {
		return nil, fmt.Errorf("could not send stat to external shard %v to resolve directory info: %w", dirId.Shard(), err)
	}

	return resolveDirectoryInfo(log, client, dirInfoCache, dirId, &statResp)
}

func resolveDirectoryInfo(
	log LogLevels, client Client, dirInfoCache *DirInfoCache, dirId msgs.InodeId, statResp *msgs.StatDirectoryResp,
) (*msgs.DirectoryInfoBody, error) {
	// we have the data directly in the stat response
	if len(statResp.Info) > 0 {
		infoBody := msgs.DirectoryInfoBody{}
		buf := bincode.Buf(statResp.Info)
		if err := infoBody.Unpack(&buf); err != nil {
			return nil, err
		}
		dirInfoCache.UpdateCachedDirInfo(dirId, &infoBody)
		return &infoBody, nil
	}

	// we have the data in the cache
	dirInfoBody := dirInfoCache.LookupCachedDirInfo(dirId)
	if dirInfoBody != nil {
		return dirInfoBody, nil
	}

	// we need to traverse upwards
	dirInfoBody, err := requestDirectoryInfo(log, client, dirInfoCache, statResp.Owner)
	if err != nil {
		return nil, err
	}

	return dirInfoBody, nil
}

func CollectDirectory(log LogLevels, client Client, dirInfoCache *DirInfoCache, stats *CollectStats, dirId msgs.InodeId) error {
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

func CollectDirectories(log LogLevels, shid msgs.ShardId) error {
	client, err := NewShardSpecificClient(shid)
	if err != nil {
		return err
	}
	defer client.Close()
	stats := CollectStats{}
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
			if err := CollectDirectory(log, client, dirInfoCache, &stats, id); err != nil {
				return fmt.Errorf("error while collecting inode %v: %w", id, err)
			}
		}
		req.BeginId = resp.NextId
		if req.BeginId == 0 {
			break
		}
	}
	log.Info("stats after one collect directories iteration: %+v", stats)
	return nil
}
