package janitor

import (
	"fmt"
	"time"

	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

type DestructionStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	DestructedBlocks uint64
}

func (gc *Env) DestructFile(stats *DestructionStats, id msgs.InodeId, deadline msgs.EggsTime, cookie uint64) error {
	gc.Debug("%v: destructing file, dry=%v, cookie=%v", id, gc.Dry, cookie)
	stats.VisitedFiles++
	now := msgs.Now()
	if now < deadline {
		gc.Debug("%v: deadline not expired (deadline=%v, now=%v), not destructing", id, deadline, now)
	}
	if gc.Dry {
		gc.Debug("%v: dry running, stopping destruction here", id)
		stats.DestructedFiles++
		return nil
	}
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
		err := gc.ShardRequest(&initReq, &initResp)
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
				proof, err := request.EraseBlock(gc, block)
				if err != nil {
					return fmt.Errorf("%v: could not erase block %+v: %w", id, block, err)
				}
				stats.DestructedBlocks++
				certifyReq.Proofs[i].BlockId = block.BlockId
				certifyReq.Proofs[i].Proof = proof
			}
			err = gc.ShardRequest(&certifyReq, &certifyResp)
			if err != nil {
				return fmt.Errorf("%v: could not certify span removal %+v: %w", id, certifyReq, err)
			}
		}
		stats.DestructedSpans++
	}
	// Now purge the inode
	{
		err := gc.ShardRequest(&msgs.RemoveInodeReq{Id: id}, &msgs.RemoveInodeResp{})
		if err != nil {
			return fmt.Errorf("%v: could not remove transient file inode after removing spans: %w", id, err)
		}
	}
	stats.DestructedFiles++
	return nil
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func (gc *Env) destructInner() (DestructionStats, error) {
	var stats DestructionStats

	shardSocket, err := request.ShardSocket(gc.Shid)
	if err != nil {
		return stats, err
	}
	defer shardSocket.Close()
	gc.ShardSocket = shardSocket

	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		err := gc.ShardRequest(&req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if err := gc.DestructFile(&stats, file.Id, file.DeadlineTime, file.Cookie); err != nil {
				return stats, fmt.Errorf("%+v: error while destructing file: %w", file, err)
			}
		}
		req.BeginId = resp.NextId
		if resp.NextId == 0 {
			break
		}
	}
	return stats, nil
}

func (gc *Env) Destruct() {
	stats, err := gc.destructInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	debug := stats.DestructedBlocks == 0
	gc.Log(debug, "stats after one destruction iteration: %+v", stats)
	gc.Log(debug, "finished destructing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}

type CollectStats struct {
	VisitedDirectories    uint64
	VisitedEdges          uint64
	CollectedEdges        uint64
	DestructedDirectories uint64
}

// returns whether all the edges were removed
func (gc *Env) applyPolicy(stats *CollectStats, edges []msgs.EdgeWithOwnership, dirId msgs.InodeId, dirInfo *msgs.DirectoryInfoBody) (bool, error) {
	policy := SnapshotPolicy{
		DeleteAfterTime:     time.Duration(dirInfo.DeleteAfterTime),
		DeleteAfterVersions: int(dirInfo.DeleteAfterVersions),
	}
	gc.Debug("%v: about to apply policy %+v for name %s", dirId, policy, edges[0].Name)
	stats.VisitedEdges = stats.VisitedEdges + uint64(len(edges))
	now := msgs.Now()
	toCollect := policy.edgesToRemove(now, edges)
	gc.Debug("%v: will remove %d edges out of %d", dirId, toCollect, len(edges))
	for _, edge := range edges[:toCollect] {
		var err error
		if edge.TargetId.Owned() {
			if edge.TargetId.Id().Shard() == dirId.Shard() {
				// same shard, we can delete directly. We also know that this is not a directory (it's an
				// owned, but snapshot edge)
				gc.Debug("%v: removing owned snapshot edge %+v", dirId, edge)
				if !gc.Dry {
					req := msgs.IntraShardHardFileUnlinkReq{
						OwnerId:      dirId,
						TargetId:     edge.TargetId.Id(),
						Name:         edge.Name,
						CreationTime: edge.CreationTime,
					}
					err = gc.ShardRequest(&req, &msgs.IntraShardHardFileUnlinkResp{})
				}
			} else {
				// different shard, we need to go through the CDC
				gc.Debug("%v: removing cross-shard owned edge %+v", dirId, edge)
				if !gc.Dry {
					req := msgs.HardUnlinkFileReq{
						OwnerId:      dirId,
						TargetId:     edge.TargetId.Id(),
						Name:         edge.Name,
						CreationTime: edge.CreationTime,
					}
					err = gc.CDCRequest(&req, &msgs.HardUnlinkFileResp{})
				}
			}
		} else {
			// non-owned edge, we can just kill it without worrying about much.
			gc.Debug("%v: removing non-owned edge %+v", dirId, edge)
			if !gc.Dry {
				req := msgs.RemoveNonOwnedEdgeReq{
					DirId:        dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				err = gc.ShardRequest(&req, &msgs.RemoveNonOwnedEdgeResp{})
			}
		}
		if err != nil {
			return false, fmt.Errorf("error while collecting edge %+v in directory %v: %w", edge, dirId, err)
		}
		stats.CollectedEdges++
	}
	return toCollect == len(edges), nil
}

func (gc *Env) resolveExternalDirectoryInfo(dirId msgs.InodeId) (*msgs.DirectoryInfoBody, error) {
	sock, err := request.ShardSocket(dirId.Shard())
	if err != nil {
		return nil, fmt.Errorf("could not open socket to perform stat in external shard %v to resolve directory info: %w", dirId.Shard(), err)
	}
	statResp := msgs.StatDirectoryResp{}
	err = request.ShardRequestSocket(gc, gc.CDCKey, sock, gc.Timeout, &msgs.StatDirectoryReq{Id: dirId}, &statResp)
	sock.Close()

	if err != nil {
		return nil, fmt.Errorf("could not send stat to external shard %v to resolve directory info: %w", dirId.Shard(), err)
	}

	return gc.resolveDirectoryInfo(dirId, &statResp)
}

func (gc *Env) resolveDirectoryInfo(dirId msgs.InodeId, statResp *msgs.StatDirectoryResp) (*msgs.DirectoryInfoBody, error) {
	// we have the data directly in the stat response
	if len(statResp.Info.Body) > 0 {
		dirInfoBody := &statResp.Info.Body[0]
		gc.updateCachedDirInfo(dirId, dirInfoBody)
		return dirInfoBody, nil
	}

	// we have the data in the cache
	dirInfoBody := gc.lookupCachedDirInfo(dirId)
	if dirInfoBody != nil {
		return dirInfoBody, nil
	}

	// we need to traverse upwards
	dirInfoBody, err := gc.resolveExternalDirectoryInfo(statResp.Owner)
	if err != nil {
		return nil, err
	}

	return dirInfoBody, nil
}

func (gc *Env) CollectDirectory(stats *CollectStats, dirId msgs.InodeId) error {
	gc.Debug("%v: collecting, dry=%v", dirId, gc.Dry)
	stats.VisitedDirectories++

	statReq := msgs.StatDirectoryReq{
		Id: dirId,
	}
	statResp := msgs.StatDirectoryResp{}
	err := gc.ShardRequest(&statReq, &statResp)
	if err != nil {
		return fmt.Errorf("error while stat'ing directory: %w", err)
	}

	dirInfo, err := gc.resolveDirectoryInfo(dirId, &statResp)
	if err != nil {
		return err
	}

	edges := make([]msgs.EdgeWithOwnership, 0)
	req := msgs.FullReadDirReq{
		DirId: dirId,
	}
	resp := msgs.FullReadDirResp{}
	remove := true
	for {
		err := gc.ShardRequest(&req, &resp)
		if err != nil {
			return err
		}
		gc.Debug("%v: got %d edges in response", dirId, len(resp.Results))
		for _, result := range resp.Results {
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || edges[0].Name != result.Name) {
				allRemoved, err := gc.applyPolicy(stats, edges, dirId, dirInfo)
				if err != nil {
					return err
				}
				remove = remove && allRemoved
				edges = edges[:0]
			}
			edges = append(edges, result)
		}
		if resp.Finished {
			if len(edges) > 0 {
				allRemoved, err := gc.applyPolicy(stats, edges, dirId, dirInfo)
				if err != nil {
					return err
				}
				remove = remove && allRemoved
			}
			break
		}
		lastResult := &resp.Results[len(resp.Results)-1]
		req.StartHash = lastResult.NameHash
		req.StartName = lastResult.Name
		req.StartTime = lastResult.CreationTime + 1
	}
	if dirId != msgs.ROOT_DIR_INODE_ID && remove {
		// Note that there is a race condition -- we do the stat quite a bit before
		// this. But this call will just fail if the directory has an owner.
		if statResp.Owner == msgs.NULL_INODE_ID {
			gc.Debug("%v: removing directory inode, since it has no edges and no owner", dirId)
			req := msgs.HardUnlinkDirectoryReq{
				DirId: dirId,
			}
			if !gc.Dry {
				err := gc.CDCRequest(&req, &msgs.HardUnlinkDirectoryResp{})
				if err != nil {
					return fmt.Errorf("error while trying to remove directory inode: %w", err)
				}
			}
			stats.DestructedDirectories++
		}
	}
	return nil
}

func (gc *Env) collectInner() (CollectStats, error) {
	var stats CollectStats

	shardSocket, err := request.ShardSocket(gc.Shid)
	if err != nil {
		return stats, err
	}
	defer shardSocket.Close()
	gc.ShardSocket = shardSocket
	cdcSocket, err := request.CDCSocket()
	if err != nil {
		return stats, err
	}
	defer cdcSocket.Close()
	gc.CDCSocket = cdcSocket

	req := msgs.VisitDirectoriesReq{}
	resp := msgs.VisitDirectoriesResp{}
	for {
		err := gc.ShardRequest(&req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit directories: %w", err)
		}
		for _, id := range resp.Ids {
			if id.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("bad directory inode %v", id))
			}
			if err := gc.CollectDirectory(&stats, id); err != nil {
				return stats, fmt.Errorf("error while collecting inode %v: %w", id, err)
			}
		}
		req.BeginId = resp.NextId
		if req.BeginId == 0 {
			break
		}
	}
	return stats, nil
}

func (gc *Env) Collect() {
	stats, err := gc.collectInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	debug := stats.CollectedEdges == 0 && stats.DestructedDirectories == 0
	gc.Log(debug, "stats after one GC iteration: %+v", stats)
	gc.Log(debug, "finished GC'ing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}
