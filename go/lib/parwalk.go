// When you want to do some work per-shard, but you also want the
// full file path.
//
// This code is an MVP, it doesn't properly handle queueing between
// threads (100,000 queue len mitigates this), and also leaks goroutines
// blocked forever.
package lib

import (
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
	client   *Client
	callback func(parent msgs.InodeId, id msgs.InodeId, path string) error
}

func (env *parwalkEnv) visit(
	log *Logger,
	parent msgs.InodeId,
	id msgs.InodeId,
	path string,
) error {
	log.Debug("visiting %q, %v", path, id)
	if err := env.callback(parent, id, path); err != nil {
		return err
	}
	if parent != msgs.NULL_INODE_ID && parent.Shard() == id.Shard() {
		// same shard, handle right now
		env.process(log, id, path)
	} else {
		// pass to other shard
		env.wg.Add(1)
		req := parwarlkReq{
			id:   id,
			path: path,
		}
		env.chans[id.Shard()] <- req
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
		if err := env.client.ShardRequest(log, id.Shard(), readReq, readResp); err != nil {
			log.Info("failed to read dir %v at path %q, it might have been deleted in the meantime: %v", id, path, err)
			return nil
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

func Parwalk(
	log *Logger,
	client *Client,
	root string,
	callback func(parent msgs.InodeId, id msgs.InodeId, path string) error,
) error {
	// compute
	env := parwalkEnv{
		chans:    make([]chan parwarlkReq, 256),
		client:   client,
		callback: callback,
	}
	for i := 0; i < 256; i++ {
		env.chans[i] = make(chan parwarlkReq, 10_000_000)
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
	rootId, parentId, err := client.ResolvePathWithParent(log, root)
	if err != nil {
		return err
	}
	if err := env.visit(log, parentId, rootId, root); err != nil {
		return err
	}
	env.wg.Wait()
	return overallError
}
