// When you want to do some work per-shard, but you also want the
// full file path.
//
// This code is an MVP, it doesn't properly handle queueing between
// threads (100,000 queue len mitigates this), and also leaks goroutines
// blocked forever.
package lib

import (
	"fmt"
	"sync"
	"xtx/eggsfs/msgs"
)

type parwarlkReq struct {
	parent msgs.InodeId
	id     msgs.InodeId
	path   string
}

type parwalkEnv struct {
	wg    sync.WaitGroup
	chans []chan parwarlkReq
}

func (env *parwalkEnv) sendReq(log *Logger, req *parwarlkReq) {
	env.wg.Add(1)
	env.chans[req.id.Shard()] <- *req
}

func (env *parwalkEnv) dir(log *Logger, client *Client, id msgs.InodeId, path string) {
	readReq := &msgs.ReadDirReq{
		DirId: id,
	}
	readResp := &msgs.ReadDirResp{}
	for {
		if err := client.ShardRequest(log, id.Shard(), readReq, readResp); err != nil {
			log.ErrorNoAlert("failed to read dir %v: %v", id, err)
			return
		}
		for _, e := range readResp.Results {
			newPath := path + "/" + e.Name
			if e.TargetId.Shard() != id.Shard() {
				env.sendReq(log, &parwarlkReq{
					parent: id,
					id:     e.TargetId,
					path:   newPath,
				})
			} else {
				if e.TargetId.Type() == msgs.DIRECTORY {
					env.dir(log, client, e.TargetId, newPath)
				} else {
					env.file(log, client, e.TargetId, newPath)
				}
			}
		}
		if readResp.NextHash == 0 {
			break
		}
		readReq.StartHash = readResp.NextHash
	}
}

func (env *parwalkEnv) file(log *Logger, client *Client, id msgs.InodeId, path string) {
	logicalSize := uint64(0)
	physicalSize := uint64(0)
	spansReq := &msgs.FileSpansReq{
		FileId: id,
	}
	spansResp := &msgs.FileSpansResp{}
	for {
		if err := client.ShardRequest(log, id.Shard(), spansReq, spansResp); err != nil {
			log.ErrorNoAlert("could not read spans for %v: %v", id, err)
			return
		}
		for _, s := range spansResp.Spans {
			logicalSize += uint64(s.Header.Size)
			if s.Header.StorageClass != msgs.INLINE_STORAGE {
				body := s.Body.(*msgs.FetchedBlocksSpan)
				physicalSize += uint64(body.CellSize) * uint64(body.Parity.Blocks()) * uint64(body.Stripes)
			}
		}
		if spansResp.NextOffset == 0 {
			break
		}
		spansReq.ByteOffset = spansResp.NextOffset
	}
	fmt.Printf("%v,%q,%v,%v\n", id, path, logicalSize, physicalSize)
}

func ParwalkWithClients(
	log *Logger,
	clients []*Client,
	callback func(client *Client, parent msgs.InodeId, id msgs.InodeId, path string) error,
) error {
	// compute
	env := parwalkEnv{
		chans: make([]chan parwarlkReq, 256),
	}
	for i := 0; i < 256; i++ {
		env.chans[i] = make(chan parwarlkReq, 100000)
	}
	var overallError error
	for i := 0; i < 256; i++ {
		ch := env.chans[i]
		client := clients[i]
		go func() {
			for {
				if overallError != nil { // drain forever, not ideal but feeling lazy and will work
					for {
						<-ch
						env.wg.Done()
					}
				}
				req := <-ch
				if err := callback(client, req.parent, req.id, req.path); err != nil {
					overallError = err
				}
				if req.id.Type() == msgs.DIRECTORY {
					env.dir(log, client, req.id, req.path)
				} else {
					env.file(log, client, req.id, req.path)
				}
				env.wg.Done()
			}
		}()
	}
	env.sendReq(log, &parwarlkReq{
		parent: msgs.NULL_INODE_ID,
		id:     msgs.ROOT_DIR_INODE_ID,
		path:   "",
	})
	env.wg.Wait()
	return overallError
}

func Parwalk(log *Logger, shuckleAddress string, callback func(client *Client, parent msgs.InodeId, id msgs.InodeId, path string) error) error {
	var clients [256]*Client
	defer func() {
		for i := 0; i < 256; i++ {
			if clients[i] == nil {
				continue
			}
		}
	}()
	for i := 0; i < 256; i++ {
		var err error
		clients[i], err = NewClient(log, nil, shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
	}
	return ParwalkWithClients(log, clients[:], callback)
}
