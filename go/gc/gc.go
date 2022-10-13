package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"runtime/debug"
	"strings"
	"time"

	"xtx/eggsfs/common"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

type shardError struct {
	shard common.ShardId
	err   interface{}
}

type destructor struct {
	logger *log.Logger
	shid   common.ShardId
}

func (destructor *destructor) RaiseAlert(err error) {
	destructor.logger.Printf("destructor[%d]: ALERT: %v\n", destructor.shid, err)
}

func (destructor *destructor) log(s string) {
	destructor.logger.Printf("destructor[%d]: %s\n", destructor.shid, s)
}

func (destructor *destructor) destructFile(file *msgs.TransientFile) error {
	return nil
}

type destructionStats struct {
	destructedFiles  int
	destructedSpans  int
	destructedBlocks int
}

// Collects dead transient files, and expunges them. Stops when
// all files have been traversed. Useful for testing a single iteration.
func (destructor *destructor) singleIteration() (destructionStats, error) {
	var stats destructionStats
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{Port: destructor.shid.Port()})
	if err != nil {
		return stats, fmt.Errorf("could not create socket: %w", err)
	}
	defer socket.Close()
	buffer := make([]byte, common.UDP_MTU)
	req := msgs.VisitTransientFilesReq{}
	resp := msgs.VisitTransientFilesResp{}
	for {
		err := request.ShardRequestSocket(destructor, socket, buffer, 2*time.Second, &req, &resp)
		if err != nil {
			return stats, fmt.Errorf("could not visit transient files: %w", err)
		}
		for ix := range resp.Files {
			file := &resp.Files[ix]
			if err := destructor.destructFile(file); err != nil {
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

func (destructor *destructor) run(panicChan chan error) {
	defer func() {
		if err := recover(); err != nil {
			destructor.RaiseAlert(fmt.Errorf("PANIC %v", err))
			destructor.log(fmt.Sprintf("PANIC %v. Stacktrace:", err))
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				destructor.log(line)
			}
			panicChan <- fmt.Errorf("destructor[%v]: PANIC %v", destructor.shid, err)
		}
	}()
	sleepDuration := time.Minute
	for {
		stats, err := destructor.singleIteration()
		if err != nil {
			destructor.RaiseAlert(err)
		}
		destructor.log(fmt.Sprintf("stats after one destruction loop: %+v", stats))
		destructor.log(fmt.Sprintf("finished destructing files, will sleep for %v", sleepDuration))
		time.Sleep(sleepDuration)
	}
}

func main() {
	logger := log.New(os.Stderr, "", log.Lshortfile)
	panicChan := make(chan error, 1)
	for shid := 0; shid < 1; shid++ {
		destructor := destructor{
			shid:   common.ShardId(shid),
			logger: logger,
		}
		destructor.run(panicChan)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
