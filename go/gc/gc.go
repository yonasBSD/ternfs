package main

import (
	"bytes"
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

type gc struct {
	logger       *log.Logger
	shid         msgs.ShardId
	role         string
	buffer       []byte
	socket       *net.UDPConn
	shardTimeout time.Duration
	policy       policy
}

func (gc *gc) RaiseAlert(err error) {
	gc.logger.Printf("%s[%d]: ALERT: %v\n", gc.role, gc.shid, err)
}

func (gc *gc) log(s string) {
	gc.logger.Printf("%s[%d]: %s\n", gc.role, gc.shid, s)
}

type destructionStats struct {
	visitedFiles     uint64
	destructedFiles  uint64
	destructedSpans  uint64
	destructedBlocks uint64
}

func (gc *gc) destructFile(stats *destructionStats, file *msgs.TransientFile) error {
	stats.visitedFiles++
	return nil
}

func shardSocket(shid msgs.ShardId) (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{Port: shid.Port()})
	if err != nil {
		return nil, fmt.Errorf("could not create socket: %w", err)
	}
	return socket, nil
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func (gc *gc) destructInner() (destructionStats, error) {
	var stats destructionStats
	socket, err := shardSocket(gc.shid)
	if err != nil {
		return stats, err
	}
	defer socket.Close()
	buffer := make([]byte, msgs.UDP_MTU)
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		err := request.ShardRequestSocket(gc, socket, buffer, gc.shardTimeout, &req, &resp)
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

func (gc *gc) destruct() {
	stats, err := gc.destructInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	gc.log(fmt.Sprintf("stats after one destruction iteration: %+v", stats))
	gc.log(fmt.Sprintf("finished destructing files, will sleep for %v", sleepDuration))
	time.Sleep(sleepDuration)
}

type collectStats struct {
	visitedDirectories uint64
	visitedEdges       uint64
	collectedEdges     uint64
}

func (gc *gc) applyPolicy(stats *collectStats, edges []msgs.Edge) error {
	stats.visitedEdges = stats.visitedEdges + uint64(len(edges))
	now := msgs.Now()
	toCollect := gc.policy.edgesToRemove(now, edges)
	stats.collectedEdges += uint64(toCollect)
	return nil
}

func (gc *gc) collectInDirectory(stats *collectStats, id msgs.InodeId) error {
	stats.visitedDirectories++
	edges := make([]msgs.Edge, 0)
	req := msgs.FullReadDirReq{
		DirId: id,
	}
	resp := msgs.FullReadDirResp{}
	for {
		err := request.ShardRequestSocket(gc, gc.socket, gc.buffer, gc.shardTimeout, &req, &resp)
		if err != nil {
			return err
		}
		for _, result := range resp.Results {
			if len(edges) > 0 && (edges[0].NameHash != result.NameHash || !bytes.Equal(edges[0].Name, result.Name)) {
				gc.applyPolicy(stats, edges)
				edges = edges[:0]
			}
			edges = append(edges, result)
		}
		if resp.Finished {
			gc.applyPolicy(stats, edges)
			break
		}
		lastResult := &resp.Results[len(resp.Results)-1]
		req.StartHash = lastResult.NameHash
		req.StartName = lastResult.Name
		req.StartTime = lastResult.CreationTime + 1
	}
	return nil
}

func (gc *gc) collectInner() (collectStats, error) {
	var stats collectStats
	socket, err := shardSocket(gc.shid)
	if err != nil {
		return stats, err
	}
	defer socket.Close()
	gc.socket = socket
	gc.buffer = make([]byte, msgs.UDP_MTU)
	req := msgs.VisitDirectoriesReq{}
	resp := msgs.VisitDirectoriesResp{}
	for {
		err := request.ShardRequestSocket(gc, gc.socket, gc.buffer, gc.shardTimeout, &req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit directories: %w", err)
		}
		for _, id := range resp.Ids {
			if id.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("bad directory inode 0x%x", id))
			}
			if err := gc.collectInDirectory(&stats, id); err != nil {
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

func (gc *gc) collect() {
	stats, err := gc.collectInner()
	if err != nil {
		gc.RaiseAlert(err)
	}
	sleepDuration := time.Minute
	gc.log(fmt.Sprintf("stats after one GC iteration: %+v", stats))
	gc.log(fmt.Sprintf("finished GC'ing files, will sleep for %v", sleepDuration))
	time.Sleep(sleepDuration)
}

func (gc *gc) run(panicChan chan error, body func(gc *gc)) {
	defer func() {
		if err := recover(); err != nil {
			gc.RaiseAlert(fmt.Errorf("PANIC %v", err))
			gc.log(fmt.Sprintf("PANIC %v. Stacktrace:", err))
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				gc.log(line)
			}
			panicChan <- fmt.Errorf("%s[%v]: PANIC %v", gc.role, gc.shid, err)
		}
	}()
	for {
		body(gc)
	}
}

func main() {
	logger := log.New(os.Stderr, "", log.Lshortfile)
	panicChan := make(chan error, 1)
	for shid := 0; shid < 256; shid++ {
		destructor := gc{
			role:   "destructor",
			shid:   msgs.ShardId(shid),
			logger: logger,
		}
		go destructor.run(panicChan, (*gc).destruct)
		collector := gc{
			role:   "collector",
			shid:   msgs.ShardId(shid),
			logger: logger,
		}
		go collector.run(panicChan, (*gc).collect)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
