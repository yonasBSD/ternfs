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
	id   msgs.InodeId
	path string
}

type parwalkEnv struct {
	wg       sync.WaitGroup
	chans    []chan parwarlkReq
	clients  []*Client
	callback func(client *Client, parent msgs.InodeId, id msgs.InodeId, path string) error
}

func (env *parwalkEnv) visit(
	log *Logger,
	parent msgs.InodeId,
	id msgs.InodeId,
	path string,
) error {
	if err := env.callback(env.clients[id.Shard()], parent, id, path); err != nil {
		return err
	}
	if parent != msgs.NULL_INODE_ID && parent.Shard() == id.Shard() {
		// same shard, handle right now
		env.process(log, id, path)
	} else {
		// pass to other shard
		env.wg.Add(1)
		env.chans[id.Shard()] <- parwarlkReq{
			id:   id,
			path: path,
		}
	}
	return nil
}

func (env *parwalkEnv) process(
	log *Logger,
	id msgs.InodeId,
	path string,
) error {
	// nothing to do
	if id.Type() != msgs.DIRECTORY {
		return nil
	}
	// recurse down
	readReq := &msgs.ReadDirReq{
		DirId: id,
	}
	readResp := &msgs.ReadDirResp{}
	for {
		if err := env.clients[id.Shard()].ShardRequest(log, id.Shard(), readReq, readResp); err != nil {
			return fmt.Errorf("failed to read dir %v: %v", id, err)
		}
		for _, e := range readResp.Results {
			newPath := path + "/" + e.Name
			if err := env.visit(log, id, e.TargetId, newPath); err != nil {
				return err
			}
		}
		if readResp.NextHash == 0 {
			break
		}
		readReq.StartHash = readResp.NextHash
	}
	return nil
}

func ParwalkWithClients(
	log *Logger,
	clients []*Client,
	callback func(client *Client, parent msgs.InodeId, id msgs.InodeId, path string) error,
) error {
	// compute
	env := parwalkEnv{
		chans:    make([]chan parwarlkReq, 256),
		clients:  clients,
		callback: callback,
	}
	for i := 0; i < 256; i++ {
		env.chans[i] = make(chan parwarlkReq, 100000)
	}
	var overallError error
	for i := 0; i < 256; i++ {
		ch := env.chans[i]
		go func() {
			for {
				if overallError != nil { // drain forever, not ideal but feeling lazy and will work
					for {
						<-ch
						env.wg.Done()
					}
				}
				req := <-ch
				if err := env.process(log, req.id, req.path); err != nil {
					overallError = err
				}
				env.wg.Done()
			}
		}()
	}
	if err := env.visit(log, msgs.NULL_INODE_ID, msgs.ROOT_DIR_INODE_ID, ""); err != nil {
		return err
	}
	env.wg.Wait()
	return overallError
}

func Parwalk(
	log *Logger,
	shuckleAddress string,
	callback func(client *Client, parent msgs.InodeId, id msgs.InodeId, path string) error,
) error {
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
