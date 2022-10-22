package gc

import (
	"fmt"
	"log"
	"net"
	"os"
	"runtime/debug"
	"strings"
	"time"

	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

type GcEnv struct {
	Logger       *log.Logger
	Shid         msgs.ShardId
	Role         string
	Buffer       []byte
	ShardSocket  *net.UDPConn
	ShardTimeout time.Duration
	Policy       Policy
	Verbose      bool
}

func (gc *GcEnv) RaiseAlert(err error) {
	gc.Logger.Printf("%s[%d]: ALERT: %v\n", gc.Role, gc.Shid, err)
}

func (gc *GcEnv) info(format string, v0 ...any) {
	v := make([]any, 2+len(v0))
	v[0] = gc.Role
	v[1] = gc.Shid
	copy(v[2:], v0)
	gc.Logger.Printf("%s[%d]: "+format+"\n", v...)
}

func (gc *GcEnv) debug(format string, v0 ...any) {
	if gc.Verbose {
		v := make([]any, 2+len(v0))
		v[0] = gc.Role
		v[1] = gc.Shid
		copy(v[2:], v0)
		gc.Logger.Printf("%s[%d]: "+format+"\n", v...)
	}
}

type DestructionStats struct {
	visitedFiles     uint64
	destructedFiles  uint64
	destructedSpans  uint64
	destructedBlocks uint64
}

func (gc *GcEnv) destructFile(stats *DestructionStats, file *msgs.TransientFile) error {
	stats.visitedFiles++
	return nil
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func (gc *GcEnv) destructInner() (DestructionStats, error) {
	var stats DestructionStats
	socket, err := request.ShardSocket(gc.Shid)
	if err != nil {
		return stats, err
	}
	defer socket.Close()
	buffer := make([]byte, msgs.UDP_MTU)
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		err := request.ShardRequestSocket(gc, socket, buffer, gc.ShardTimeout, &req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if err := gc.destructFile(&stats, file); err != nil {
				return stats, fmt.Errorf("error while destructing file %v: %w", file, err)
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
	gc.info("stats after one destruction iteration: %+v", stats)
	gc.info("finished destructing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}

type CollectStats struct {
	visitedDirectories uint64
	visitedEdges       uint64
	collectedEdges     uint64
}

func (gc *GcEnv) applyPolicy(stats *CollectStats, dry bool, edges []msgs.EdgeWithOwnership, dirId msgs.InodeId) error {
	gc.debug("%v: about to apply policy for name %s", dirId, edges[0].Name)
	stats.visitedEdges = stats.visitedEdges + uint64(len(edges))
	now := msgs.Now()
	toCollect := gc.Policy.edgesToRemove(now, edges)
	gc.debug("%v: will remove %d edges out of %d", dirId, toCollect, len(edges))
	for _, edge := range edges[:toCollect] {
		var err error
		if edge.TargetId.Owned() {
			if edge.TargetId.Id().Shard() == dirId.Shard() {
				// same shard, we can delete directly. We also know that this is not a directory (it's an
				// owned, but snapshot edge)
				gc.debug("%v: removing owned snapshot edge %+v", dirId, edge)
				if !dry {
					req := msgs.RemoveOwnedSnapshotFileEdgeReq{
						DirId:        dirId,
						TargetId:     edge.TargetId.Id(),
						Name:         edge.Name,
						CreationTime: edge.CreationTime,
					}
					resp := msgs.RemoveOwnedSnapshotFileEdgeResp{}
					err = request.ShardRequestSocket(gc, gc.ShardSocket, gc.Buffer, gc.ShardTimeout, &req, &resp)
				}
			} else {
				// different shard, we need to go through the CDC
				gc.debug("%v: removing cross-shard owned edge %+v", dirId, edge)
				if !dry {
					panic("cross-shard edge removal not implemented")
				}
			}
		} else {
			// non-owned edge, we can just kill it without worrying about much.
			gc.debug("%v: removing non-owned edge %+v", dirId, edge)
			if !dry {
				req := msgs.RemoveNonOwnedEdgeReq{
					DirId:        dirId,
					TargetId:     edge.TargetId.Id(),
					Name:         edge.Name,
					CreationTime: edge.CreationTime,
				}
				resp := msgs.RemoveNonOwnedEdgeResp{}
				err = request.ShardRequestSocket(gc, gc.ShardSocket, gc.Buffer, gc.ShardTimeout, &req, &resp)
			}
		}
		if err != nil {
			switch code := err.(type) {
			case msgs.ErrCode:
				if code == msgs.EDGE_NOT_FOUND {
					// this can happen if somebody got here before us. we choose not to
					// continue because if for some reason this edge does exist (say
					// because of a bug in this program) we don't want to leave a weird
					// edge state.
					gc.info("%v: could not find edge %+v while GC'ing (err %v), will stop removing edges for this directory", dirId, edge, code)
					return nil
				} else if code == msgs.DIRECTORY_NOT_FOUND {
					// this can happen if somebody got here before us, and we definitely
					// can't continue.
					gc.info("%v: could not find directory when removing edge %+v, will stop removing edges in this directory", dirId, edge)
					return nil
				}
			}
			return fmt.Errorf("error while collecting edge %+v in directory %v: %w", edge, dirId, err)
		}
		stats.collectedEdges++
	}
	return nil
}

func (gc *GcEnv) CollectInDirectory(stats *CollectStats, dry bool, dirId msgs.InodeId) error {
	gc.debug("%v: collecting, dry=%v", dirId, dry)
	stats.visitedDirectories++
	edges := make([]msgs.EdgeWithOwnership, 0)
	req := msgs.FullReadDirReq{
		DirId: dirId,
	}
	resp := msgs.FullReadDirResp{}
	for {
		err := request.ShardRequestSocket(gc, gc.ShardSocket, gc.Buffer, gc.ShardTimeout, &req, &resp)
		if err != nil {
			return err
		}
		gc.debug("%v: got %d edges in response", dirId, len(resp.Results))
		for _, result := range resp.Results {
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || edges[0].Name != result.Name) {
				gc.applyPolicy(stats, dry, edges, dirId)
				edges = edges[:0]
			}
			edges = append(edges, result)
		}
		if resp.Finished {
			gc.applyPolicy(stats, dry, edges, dirId)
			break
		}
		lastResult := &resp.Results[len(resp.Results)-1]
		req.StartHash = lastResult.NameHash
		req.StartName = lastResult.Name
		req.StartTime = lastResult.CreationTime + 1
	}
	return nil
}

func (gc *GcEnv) collectInner() (CollectStats, error) {
	var stats CollectStats
	socket, err := request.ShardSocket(gc.Shid)
	if err != nil {
		return stats, err
	}
	defer socket.Close()
	gc.ShardSocket = socket
	gc.Buffer = make([]byte, msgs.UDP_MTU)
	req := msgs.VisitDirectoriesReq{}
	resp := msgs.VisitDirectoriesResp{}
	for {
		err := request.ShardRequestSocket(gc, gc.ShardSocket, gc.Buffer, gc.ShardTimeout, &req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit directories: %w", err)
		}
		for _, id := range resp.Ids {
			if id.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("bad directory inode %v", id))
			}
			if err := gc.CollectInDirectory(&stats, false, id); err != nil {
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
	gc.info("stats after one GC iteration: %+v", stats)
	gc.info("finished GC'ing files, will sleep for %v", sleepDuration)
	time.Sleep(sleepDuration)
}

func (gc *GcEnv) run(panicChan chan error, body func(gc *GcEnv)) {
	defer func() {
		if err := recover(); err != nil {
			gc.RaiseAlert(fmt.Errorf("PANIC %v", err))
			gc.info("PANIC %v. Stacktrace:", err)
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				gc.info(line)
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
	policy := Policy{
		DeleteAfterTime:     time.Minute,
		DeleteAfterVersions: 30,
	}
	for shid := 0; shid < 256; shid++ {
		destructor := GcEnv{
			Role:         "destructor",
			Shid:         msgs.ShardId(shid),
			Logger:       logger,
			Policy:       policy,
			ShardTimeout: 10 * time.Second,
		}
		go destructor.run(panicChan, (*GcEnv).destruct)
		collector := GcEnv{
			Role:         "collector",
			Shid:         msgs.ShardId(shid),
			Logger:       logger,
			Policy:       policy,
			ShardTimeout: 10 * time.Second,
		}
		go collector.run(panicChan, (*GcEnv).collect)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
