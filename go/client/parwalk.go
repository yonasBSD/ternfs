// When you want to traverse the filesystem, but you also want the
// filepath. We have some workers per shard, to try to parallelize
// the work nicely. However there is a work-stealing of sorts otherwise
// it's very easy to end in deadlocks.
package client

import (
	"fmt"
	"path/filepath"
	"sync"
	"xtx/eggsfs/lib"
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
	callback func(parent msgs.InodeId, id msgs.InodeId, path string, creationTime msgs.EggsTime) error
}

func (env *parwalkEnv) visit(
	log *lib.Logger,
	homeShid msgs.ShardId,
	parent msgs.InodeId,
	id msgs.InodeId,
	path string,
	creationTime msgs.EggsTime,
) error {
	log.Debug("visiting %q, %v", path, id)
	if err := env.callback(parent, id, path, creationTime); err != nil {
		return err
	}
	if parent != msgs.NULL_INODE_ID && homeShid == id.Shard() {
		// same shard, handle right now
		env.process(log, homeShid, id, path)
	} else {
		req := parwarlkReq{
			id:   id,
			path: path,
		}
		select {
		// pass to other shard
		case env.chans[id.Shard()] <- req:
			env.wg.Add(1)
		// queue is full, do it yourself
		default:
			env.process(log, homeShid, id, path)
		}
	}
	return nil
}

func (env *parwalkEnv) process(
	log *lib.Logger,
	homeShid msgs.ShardId,
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
			newPath := filepath.Join(path, e.Name)
			if err := env.visit(log, homeShid, id, e.TargetId, newPath, e.CreationTime); err != nil {
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
	log *lib.Logger,
	client *Client,
	workersPerShard int,
	root string,
	callback func(parent msgs.InodeId, id msgs.InodeId, path string, creationTime msgs.EggsTime) error,
) error {
	if workersPerShard < 1 {
		panic(fmt.Errorf("workersPerShard=%d < 1", workersPerShard))
	}
	// compute
	env := parwalkEnv{
		chans:    make([]chan parwarlkReq, 256),
		client:   client,
		callback: callback,
	}
	for i := 0; i < 256; i++ {
		env.chans[i] = make(chan parwarlkReq, 10_000)
	}
	errChan := make(chan error, 1)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		ch := env.chans[shid]
		for j := 0; j < workersPerShard; j++ {
			go func() {
				for {
					req, more := <-ch
					if !more {
						return
					}
					if err := env.process(log, shid, req.id, req.path); err != nil {
						for _, ch := range env.chans {
							close(ch)
						}
						select {
						case errChan <- err:
						default:
						}
						return
					}
					env.wg.Done()
				}
			}()
		}
	}
	rootId, creationTime, parentId, err := client.ResolvePathWithParent(log, root)
	if err != nil {
		return err
	}
	if err := env.visit(log, 0, parentId, rootId, root, creationTime); err != nil {
		return err
	}
	go func() {
		env.wg.Wait()
		errChan <- nil
	}()
	return <-errChan
}
