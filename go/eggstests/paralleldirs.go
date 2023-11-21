package main

import (
	"fmt"
	"math"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

type createdDir struct {
	id           msgs.InodeId
	creationTime msgs.EggsTime
}

func parallelDirsTest(
	log *lib.Logger,
	shuckleAddress string,
	counters *lib.ClientCounters,
) {
	numRootDirs := 10
	numThreads := 1000
	numThreadsDigits := int(math.Ceil(math.Log10(float64(numThreads))))
	actionsPerThread := 100

	entityName := func(tid int, i int) string {
		return fmt.Sprintf(fmt.Sprintf("%%0%dd-%%d", numThreadsDigits), tid, i)
	}

	client, err := lib.NewClient(log, nil, shuckleAddress)
	if err != nil {
		panic(err)
	}
	client.SetCounters(counters)

	log.Info("creating root dirs")
	rootDirs := []msgs.InodeId{}
	for i := 0; i < numRootDirs; i++ {
		resp := &msgs.MakeDirectoryResp{}
		if err := client.CDCRequest(log, &msgs.MakeDirectoryReq{OwnerId: msgs.ROOT_DIR_INODE_ID, Name: fmt.Sprintf("%v", i)}, resp); err != nil {
			panic(err)
		}
		rootDirs = append(rootDirs, resp.Id)
	}

	terminateChan := make(chan any)

	log.Info("creating directories in parallel")
	var wg sync.WaitGroup
	wg.Add(numThreads)
	done := uint64(0)
	dirs := make([]map[string]createdDir, numThreads)
	for i := 0; i < numThreads; i++ {
		tid := i
		dirs[tid] = make(map[string]createdDir)
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			rand := wyhash.New(uint64(tid))
			for i := 0; i < actionsPerThread; i++ {
				which := rand.Float64()
				// we mostly issue creates since only one dir rename
				// can exist at the same time.
				if len(dirs[tid]) < 2 || which < 0.8 { // create
					ownerIx := int(rand.Uint32()) % len(rootDirs)
					owner := rootDirs[ownerIx]
					resp := &msgs.MakeDirectoryResp{}
					if err := client.CDCRequest(log, &msgs.MakeDirectoryReq{OwnerId: owner, Name: entityName(tid, i)}, resp); err != nil {
						panic(err)
					}
					fullName := fmt.Sprintf("%v/%v", ownerIx, entityName(tid, i))
					log.Debug("creating %q", fullName)
					dirs[tid][fullName] = createdDir{
						id:           resp.Id,
						creationTime: resp.CreationTime,
					}
				} else { // rename
					// just pick the first one to rename
					var oldFullName string
					var targetId msgs.InodeId
					var oldCreationTime msgs.EggsTime
					for tmpDirName, x := range dirs[tid] {
						oldFullName = tmpDirName
						targetId = x.id
						oldCreationTime = x.creationTime
						break
					}
					// parse the string
					var oldOwner msgs.InodeId
					var oldName string
					{
						parts := strings.Split(oldFullName, "/")
						oldOwnerIx, err := strconv.Atoi(parts[0])
						if err != nil {
							panic(err)
						}
						oldOwner = rootDirs[oldOwnerIx]
						oldName = parts[1]
					}
					// pick a new owner, different than the previous one
					var newOwnerIx int
					var newOwner msgs.InodeId
					for {
						newOwnerIx = int(rand.Uint32()) % len(rootDirs)
						newOwner = rootDirs[newOwnerIx]
						if newOwner != oldOwner {
							break
						}
					}
					// do the rename
					req := &msgs.RenameDirectoryReq{
						TargetId:        targetId,
						OldOwnerId:      oldOwner,
						OldName:         oldName,
						OldCreationTime: oldCreationTime,
						NewOwnerId:      newOwner,
						NewName:         entityName(tid, i),
					}
					newFullName := fmt.Sprintf("%v/%v", newOwnerIx, entityName(tid, i))
					log.Debug("renaming %q to %q", oldFullName, newFullName)
					resp := &msgs.RenameDirectoryResp{}
					if err := client.CDCRequest(log, req, resp); err != nil {
						panic(err)
					}
					delete(dirs[tid], oldFullName)
					dirs[tid][newFullName] = createdDir{
						id:           targetId,
						creationTime: resp.CreationTime,
					}
				}
				if atomic.AddUint64(&done, 1)%256 == 0 {
					log.Info("went through %v/%v actions", atomic.LoadUint64(&done), numThreads*actionsPerThread)
				}
			}
			wg.Done()
		}()
	}
	go func() {
		wg.Wait()
		terminateChan <- nil
	}()

	log.Info("waiting for everything to be done")
	{
		err := <-terminateChan
		if err != nil {
			panic(err)
		}
	}

	log.Info("checking")
	expectedDirs := make(map[string]struct{})
	for i := 0; i < numThreads; i++ {
		for d := range dirs[i] {
			expectedDirs[d] = struct{}{}
		}
	}
	actualDirs := make(map[string]struct{})
	for i, id := range rootDirs {
		req := &msgs.ReadDirReq{DirId: id}
		resp := &msgs.ReadDirResp{}
		for {
			if err := client.ShardRequest(log, id.Shard(), req, resp); err != nil {
				panic(err)
			}
			for _, d := range resp.Results {
				actualDirs[fmt.Sprintf("%v/%v", i, d.Name)] = struct{}{}
			}
			req.StartHash = resp.NextHash
			if req.StartHash == 0 {
				break
			}
		}
	}
	someErrors := false
	for d := range expectedDirs {
		if _, found := actualDirs[d]; !found {
			someErrors = true
			log.Info("expected to find dir %q, but did not", d)
		}
	}
	for d := range actualDirs {
		if _, found := expectedDirs[d]; !found {
			someErrors = true
			log.Info("found dir %q, but did not expect it", d)
		}
	}
	if someErrors {
		panic(fmt.Errorf("mismatching directories, check logs"))
	}
	log.Info("finished checking")
}
