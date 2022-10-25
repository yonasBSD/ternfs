package gc

import (
	"crypto/cipher"
	"fmt"
	"log"
	"net"
	"os"
	"runtime/debug"
	"strings"
	"time"

	"xtx/eggsfs/bincode"
	"xtx/eggsfs/cdckey"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

type GcEnv struct {
	Logger         *log.Logger
	Shid           msgs.ShardId
	Role           string
	ShardSocket    *net.UDPConn
	CDCSocket      *net.UDPConn
	Timeout        time.Duration
	SnapshotPolicy SnapshotPolicy
	Verbose        bool
	Dry            bool
	CDCKey         cipher.Block
}

func (gc *GcEnv) RaiseAlert(err error) {
	gc.Logger.Printf("%s[%d]: ALERT: %v\n", gc.Role, gc.Shid, err)
}

func (gc *GcEnv) Info(format string, v0 ...any) {
	v := make([]any, 2+len(v0))
	v[0] = gc.Role
	v[1] = gc.Shid
	copy(v[2:], v0)
	gc.Logger.Printf("%s[%d]: "+format+"\n", v...)
}

func (gc *GcEnv) Debug(format string, v0 ...any) {
	if gc.Verbose {
		v := make([]any, 2+len(v0))
		v[0] = gc.Role
		v[1] = gc.Shid
		copy(v[2:], v0)
		gc.Logger.Printf("%s[%d]: "+format+"\n", v...)
	}
}

func (gc *GcEnv) ShardRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return request.ShardRequestSocket(gc, gc.CDCKey, gc.ShardSocket, gc.Timeout, req, resp)
}

func (gc *GcEnv) CDCRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return request.CDCRequestSocket(gc, gc.CDCSocket, gc.Timeout, req, resp)
}

type DestructionStats struct {
	VisitedFiles     uint64
	DestructedFiles  uint64
	DestructedSpans  uint64
	DestructedBlocks uint64
}

func (gc *GcEnv) DestructFile(stats *DestructionStats, id msgs.InodeId, deadline msgs.EggsTime, cookie uint64) error {
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
func (gc *GcEnv) destructInner() (DestructionStats, error) {
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
				return stats, fmt.Errorf("%v: error while destructing file: %w", file, err)
			}
		}
		req.BeginId = resp.NextId
		if resp.NextId == 0 {
			break
		}
	}
	return stats, nil
}

func (gc *GcEnv) destruct() {
	stats, err := gc.destructInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	gc.Info("stats after one destruction iteration: %+v", stats)
	gc.Info("finished destructing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}

type CollectStats struct {
	visitedDirectories    uint64
	visitedEdges          uint64
	collectedEdges        uint64
	destructedDirectories uint64
}

// returns whether all the edges were removed
func (gc *GcEnv) applyPolicy(stats *CollectStats, edges []msgs.EdgeWithOwnership, dirId msgs.InodeId) (bool, error) {
	gc.Debug("%v: about to apply policy %+v for name %s", dirId, gc.SnapshotPolicy, edges[0].Name)
	stats.visitedEdges = stats.visitedEdges + uint64(len(edges))
	now := msgs.Now()
	toCollect := gc.SnapshotPolicy.edgesToRemove(now, edges)
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
		stats.collectedEdges++
	}
	return toCollect == len(edges), nil
}

func (gc *GcEnv) CollectDirectory(stats *CollectStats, dirId msgs.InodeId) error {
	gc.Debug("%v: collecting, dry=%v", dirId, gc.Dry)
	stats.visitedDirectories++
	edges := make([]msgs.EdgeWithOwnership, 0)
	req := msgs.FullReadDirReq{
		DirId: dirId,
	}
	resp := msgs.FullReadDirResp{}
	remove := dirId != msgs.ROOT_DIR_INODE_ID
	for {
		err := gc.ShardRequest(&req, &resp)
		if err != nil {
			return err
		}
		gc.Debug("%v: got %d edges in response", dirId, len(resp.Results))
		for _, result := range resp.Results {
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || edges[0].Name != result.Name) {
				allRemoved, err := gc.applyPolicy(stats, edges, dirId)
				if err != nil {
					return err
				}
				remove = remove && allRemoved
				edges = edges[:0]
			}
			edges = append(edges, result)
		}
		if resp.Finished {
			allRemoved, err := gc.applyPolicy(stats, edges, dirId)
			if err != nil {
				return err
			}
			remove = remove && allRemoved
			break
		}
		lastResult := &resp.Results[len(resp.Results)-1]
		req.StartHash = lastResult.NameHash
		req.StartName = lastResult.Name
		req.StartTime = lastResult.CreationTime + 1
	}
	if remove {
		req := msgs.StatReq{
			Id: dirId,
		}
		resp := msgs.StatResp{}
		err := gc.ShardRequest(&req, &resp)
		if err != nil {
			return fmt.Errorf("error while getting directory to check if we can remove it: %w", err)
		}
		// Note that the root directory will always have no owner, but we never end up
		// here (see `remove` definition)
		if msgs.InodeId(resp.SizeOrOwner) == msgs.NULL_INODE_ID {
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
			stats.destructedDirectories++
		}
	}
	return nil
}

func (gc *GcEnv) collectInner() (CollectStats, error) {
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

func (gc *GcEnv) collect() {
	stats, err := gc.collectInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	gc.Info("stats after one GC iteration: %+v", stats)
	gc.Info("finished GC'ing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}

func (gc *GcEnv) run(panicChan chan error, body func(gc *GcEnv)) {
	defer func() {
		if err := recover(); err != nil {
			gc.RaiseAlert(fmt.Errorf("PANIC %v", err))
			gc.Info("PANIC %v. Stacktrace:", err)
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				gc.Info(line)
			}
			panicChan <- fmt.Errorf("%s[%v]: PANIC %v", gc.Role, gc.Shid, err)
		}
	}()
	for {
		body(gc)
	}
}

func Run() {
	logger := log.New(os.Stderr, "", log.Lshortfile)
	panicChan := make(chan error, 1)
	policy := SnapshotPolicy{
		DeleteAfterTime:     time.Second,
		DeleteAfterVersions: 30,
	}
	env := GcEnv{
		Logger:         logger,
		Timeout:        10 * time.Second,
		CDCKey:         cdckey.CDCKey(),
		SnapshotPolicy: policy,
	}
	for shid := 0; shid < 256; shid++ {
		destructor := env
		env.Role = "destructor"
		env.Shid = msgs.ShardId(shid)
		go destructor.run(panicChan, (*GcEnv).destruct)
		collector := env
		env.Role = "collector"
		env.Shid = msgs.ShardId(shid)
		go collector.run(panicChan, (*GcEnv).collect)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
