// A simple tests creating files and also looking at the history
package main

import (
	"fmt"
	"math/rand"
	"strings"
	"sync"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

// actions

type createFile struct {
	name string
	size uint64
}

type deleteFile struct {
	name string
}

type renameFile struct {
	oldName string
	newName string
}

// trace

type createdFile struct {
	name string
	id   msgs.InodeId
}

type checkpoint struct {
	time msgs.EggsTime
}

type files struct {
	names  []string
	ids    []msgs.InodeId
	byName map[string]int
}

func (files *files) addFile(name string, id msgs.InodeId) {
	if _, wasPresent := files.byName[name]; wasPresent {
		panic(fmt.Errorf("unexpected overwrite of %s", name))
	}
	files.names = append(files.names, name)
	files.ids = append(files.ids, id)
	files.byName[name] = len(files.ids) - 1
}

func (files *files) id(name string) msgs.InodeId {
	ix, present := files.byName[name]
	if !present {
		panic(fmt.Errorf("name not found %v", name))
	}
	return files.ids[ix]
}

func (files *files) deleteFile(name string) {
	if _, wasPresent := files.byName[name]; !wasPresent {
		panic(fmt.Errorf("name not found %v", name))
	}
	ix := files.byName[name]
	lastIx := len(files.ids) - 1
	if ix != lastIx {
		files.ids[ix] = files.ids[lastIx]
		files.names[ix] = files.names[lastIx]
		files.byName[files.names[ix]] = ix
	}
	files.ids = files.ids[:lastIx]
	files.names = files.names[:lastIx]
	delete(files.byName, name)
}

func genCreateFile(filePrefix string, rand *rand.Rand, files *files) createFile {
	var name string
	for {
		name = fmt.Sprintf("%s%x", filePrefix, rand.Uint32())
		_, wasPresent := files.byName[name]
		if !wasPresent {
			delete(files.byName, name)
			size := rand.Uint64() % (uint64(100) << 20) // up to 20MiB
			return createFile{
				name: name,
				size: size,
			}
		}
	}
}

func genDeleteFile(filePrefix string, rand *rand.Rand, files *files) deleteFile {
	return deleteFile{name: files.names[int(rand.Uint32())%len(files.names)]}
}

func genRenameFile(filePrefix string, rand *rand.Rand, files *files) renameFile {
	oldName := genDeleteFile(filePrefix, rand, files).name
	newName := genCreateFile(filePrefix, rand, files).name
	return renameFile{
		oldName: oldName,
		newName: newName,
	}
}

func genRenameOverrideFile(filePrefix string, rand *rand.Rand, files *files) renameFile {
	oldName := genDeleteFile(filePrefix, rand, files).name
	newName := genDeleteFile(filePrefix, rand, files).name
	if oldName == newName {
		return genRenameFile(filePrefix, rand, files)
	}
	return renameFile{
		oldName: oldName,
		newName: newName,
	}
}

func checkCheckpoint(prefix string, files *files, allEdges []edge) {
	edges := []edge{}
	for _, edge := range allEdges {
		if !strings.HasPrefix(edge.name, prefix) {
			continue
		}
		edges = append(edges, edge)
	}
	if len(edges) != len(files.names) {
		panic(fmt.Errorf("expected %d edges, got %d", len(files.names), len(edges)))
	}
	for _, edge := range edges {
		id := files.id(edge.name)
		if id != edge.targetId {
			panic(fmt.Errorf("expected targetId %v for edge %v, but got %v", id, edge.name, id))
		}
	}
}

func runCheckpoint(harness *harness, prefix string, files *files) checkpoint {
	edges := harness.readDir(msgs.ROOT_DIR_INODE_ID)
	checkCheckpoint(prefix, files, edges)
	resp := msgs.StatDirectoryResp{}
	harness.shardReq(msgs.ROOT_DIR_INODE_ID.Shard(), &msgs.StatDirectoryReq{Id: msgs.ROOT_DIR_INODE_ID}, &resp)
	return checkpoint{
		time: resp.Mtime,
	}
}

func runStep(harness *harness, files *files, stepAny any) any {
	switch step := stepAny.(type) {
	case createFile:
		id := harness.createFile(msgs.ROOT_DIR_INODE_ID, step.name, step.size)
		files.addFile(step.name, id)
		return createdFile{
			name: step.name,
			id:   id,
		}
	case deleteFile:
		fileId := files.id(step.name)
		harness.shardReq(msgs.ROOT_DIR_INODE_ID.Shard(), &msgs.SoftUnlinkFileReq{OwnerId: msgs.ROOT_DIR_INODE_ID, FileId: fileId, Name: step.name}, &msgs.SoftUnlinkFileResp{})
		files.deleteFile(step.name)
		return step
	case renameFile:
		targetId := files.id(step.oldName)
		harness.shardReq(
			msgs.ROOT_DIR_INODE_ID.Shard(),
			&msgs.SameDirectoryRenameReq{DirId: msgs.ROOT_DIR_INODE_ID, TargetId: targetId, OldName: step.oldName, NewName: step.newName},
			&msgs.SameDirectoryRenameResp{},
		)
		files.deleteFile(step.oldName)
		if _, wasPresent := files.byName[step.newName]; wasPresent {
			// overwrite
			files.deleteFile(step.newName)
		}
		files.addFile(step.newName, targetId)
		return step
	default:
		panic(fmt.Errorf("bad step %T", stepAny))
	}
}

func edgesAsOf(allEdges []fullEdge, t msgs.EggsTime) []edge {
	namesToEdge := make(map[string]fullEdge)

	for _, fullEdge := range allEdges {
		if fullEdge.creationTime > t {
			continue
		}
		namesToEdge[fullEdge.name] = fullEdge
	}

	edges := []edge{}
	for _, fullEdge := range namesToEdge {
		if fullEdge.targetId == msgs.NULL_INODE_ID {
			continue
		}
		edges = append(edges, edge{
			name:     fullEdge.name,
			targetId: fullEdge.targetId,
		})
	}

	return edges
}

func replayCheckpoint(prefix string, files *files, fullEdges []fullEdge, t msgs.EggsTime) {
	edges := edgesAsOf(fullEdges, t)
	checkCheckpoint(prefix, files, edges)
}

func replayStep(prefix string, files *files, fullEdges []fullEdge, stepAny any) {
	switch step := stepAny.(type) {
	case createdFile:
		files.addFile(step.name, step.id)
	case deleteFile:
		files.deleteFile(step.name)
	case renameFile:
		targetId := files.id(step.oldName)
		files.deleteFile(step.oldName)
		if _, wasPresent := files.byName[step.newName]; wasPresent {
			// overwrite
			files.deleteFile(step.newName)
		}
		files.addFile(step.newName, targetId)
	case checkpoint:
		replayCheckpoint(prefix, files, fullEdges, step.time)
	default:
		panic(fmt.Errorf("bad step %T", stepAny))
	}
}

func fileHistoryStepSingle(opts *fileHistoryTestOpts, harness *harness, seed int64, filePrefix string) {
	// loop for n steps. at every step:
	// * if we have never reached the target files, then just create a file.
	// * if we have, create/delete/rename/rename with override at random.
	// * every checkpointEvery steps, use readDir to make sure that we have the files that we expect.
	//
	// after we've finished, re-run through everything and re-check at ever checkpointSteps using
	// fullReadDir.
	trace := []any{}

	reachedTargetFiles := false
	fls := files{
		names:  []string{},
		ids:    []msgs.InodeId{},
		byName: make(map[string]int),
	}
	source := rand.NewSource(seed)
	rand := rand.New(source)
	for stepIx := 0; stepIx < opts.steps; stepIx++ {
		if stepIx%opts.checkpointEvery == 0 {
			checkpoint := runCheckpoint(harness, filePrefix, &fls)
			trace = append(trace, checkpoint)
		}
		var step any
		if len(fls.names) < opts.lowFiles {
			reachedTargetFiles = false
		}
		if !reachedTargetFiles {
			step = genCreateFile(filePrefix, rand, &fls)
		} else {
			which := rand.Uint32() % 4
			switch which {
			case 0:
				step = genCreateFile(filePrefix, rand, &fls)
			case 1:
				step = genDeleteFile(filePrefix, rand, &fls)
			case 2:
				step = genRenameFile(filePrefix, rand, &fls)
			case 3:
				step = genRenameOverrideFile(filePrefix, rand, &fls)
			default:
				panic(fmt.Errorf("bad which %d", which))
			}
		}
		reachedTargetFiles = reachedTargetFiles || len(fls.names) >= opts.targetFiles
		trace = append(trace, runStep(harness, &fls, step))
	}
	fls = files{
		names:  []string{},
		ids:    []msgs.InodeId{},
		byName: make(map[string]int),
	}
	fullEdges := harness.fullReadDir(msgs.ROOT_DIR_INODE_ID)
	for _, step := range trace {
		replayStep(filePrefix, &fls, fullEdges, step)
	}
}

type fileHistoryTestOpts struct {
	steps           int // how many actions to perform
	checkpointEvery int // how often to run the historical check
	targetFiles     int // how many files we want
	lowFiles        int
	threads         int // how many tests to run at once
}

func fileHistoryTest(
	log eggs.LogLevels,
	opts *fileHistoryTestOpts,
	stats *harnessStats,
	blockServicesKeys map[msgs.BlockServiceId][16]byte,
) {
	terminateChan := make(chan any, 1)

	go func() {
		defer func() { handleRecover(terminateChan, recover()) }()
		numTests := opts.threads
		if numTests > 15 {
			panic(fmt.Errorf("numTests %d too big for one-digit prefix", numTests))
		}
		var wait sync.WaitGroup
		wait.Add(numTests)
		for i := 0; i < numTests; i++ {
			prefix := fmt.Sprintf("%x", i)
			seed := int64(i)
			go func() {
				defer func() { handleRecover(terminateChan, recover()) }()
				client, err := eggs.NewShardSpecificClient(msgs.ShardId(0))
				if err != nil {
					panic(err)
				}
				defer client.Close()
				harness := newHarness(log, client, stats, blockServicesKeys)
				fileHistoryStepSingle(opts, harness, seed, prefix)
				wait.Done()
			}()
		}
		wait.Wait()

		terminateChan <- nil
	}()

	err := <-terminateChan

	if err != nil {
		panic(err)
	}
}
