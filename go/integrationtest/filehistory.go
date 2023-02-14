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

type fileHistoryCreateFile struct {
	name string
	size uint64
}

type fileHistoryDeleteFile struct {
	name string
}

type fileHistoryRenameFile struct {
	oldName string
	newName string
}

// trace

type fileHistoryCreatedFile struct {
	name         string
	id           msgs.InodeId
	creationTime msgs.EggsTime
}

type fileHistoryRenamedFile struct {
	oldName         string
	newName         string
	newCreationTime msgs.EggsTime
}

type fileHistoryCheckpoint struct {
	time msgs.EggsTime
}

type fileHistoryFile struct {
	name         string
	id           msgs.InodeId
	creationTime msgs.EggsTime
}
type fileHistoryFiles struct {
	files  []fileHistoryFile
	byName map[string]int
}

func (files *fileHistoryFiles) addFile(name string, id msgs.InodeId, creationTime msgs.EggsTime) {
	if _, wasPresent := files.byName[name]; wasPresent {
		panic(fmt.Errorf("unexpected overwrite of %s", name))
	}
	files.files = append(files.files, fileHistoryFile{name: name, id: id, creationTime: creationTime})
	files.byName[name] = len(files.files) - 1
}

/*
func (files *files) id(name string) msgs.InodeId {
	ix, present := files.byName[name]
	if !present {
		panic(fmt.Errorf("name not found %v", name))
	}
	return files.files[ix].id
}
*/

func (files *fileHistoryFiles) file(name string) fileHistoryFile {
	ix, present := files.byName[name]
	if !present {
		panic(fmt.Errorf("name not found %v", name))
	}
	return files.files[ix]
}

func (files *fileHistoryFiles) deleteFile(name string) {
	if _, wasPresent := files.byName[name]; !wasPresent {
		panic(fmt.Errorf("name not found %v", name))
	}
	ix := files.byName[name]
	lastIx := len(files.files) - 1
	if ix != lastIx {
		files.files[ix] = files.files[lastIx]
		files.byName[files.files[ix].name] = ix
	}
	files.files = files.files[:lastIx]
	delete(files.byName, name)
}

func genCreateFile(filePrefix string, rand *rand.Rand, files *fileHistoryFiles) fileHistoryCreateFile {
	var name string
	for {
		name = fmt.Sprintf("%s%x", filePrefix, rand.Uint32())
		_, wasPresent := files.byName[name]
		if !wasPresent {
			delete(files.byName, name)
			var size uint64
			// one out of three files as inline storage
			if rand.Uint64()%3 == 0 {
				size = 1 + rand.Uint64()%255
			} else {
				size = 1 + rand.Uint64()%(uint64(100)<<20) // up to 20MiB
			}
			return fileHistoryCreateFile{
				name: name,
				size: size,
			}
		}
	}
}

func genDeleteFile(filePrefix string, rand *rand.Rand, files *fileHistoryFiles) fileHistoryDeleteFile {
	file := &files.files[int(rand.Uint32())%len(files.files)]
	return fileHistoryDeleteFile{name: file.name}
}

func genRenameFile(filePrefix string, rand *rand.Rand, files *fileHistoryFiles) fileHistoryRenameFile {
	oldName := genDeleteFile(filePrefix, rand, files).name
	newName := genCreateFile(filePrefix, rand, files).name
	return fileHistoryRenameFile{
		oldName: oldName,
		newName: newName,
	}
}

func genRenameOverrideFile(filePrefix string, rand *rand.Rand, files *fileHistoryFiles) fileHistoryRenameFile {
	oldName := genDeleteFile(filePrefix, rand, files).name
	newName := genDeleteFile(filePrefix, rand, files).name
	if oldName == newName {
		return genRenameFile(filePrefix, rand, files)
	}
	return fileHistoryRenameFile{
		oldName: oldName,
		newName: newName,
	}
}

func checkCheckpoint(prefix string, files *fileHistoryFiles, allEdges []edge) {
	edges := []edge{}
	for _, edge := range allEdges {
		if !strings.HasPrefix(edge.name, prefix) {
			continue
		}
		edges = append(edges, edge)
	}
	if len(edges) != len(files.files) {
		panic(fmt.Errorf("expected %d edges, got %d", len(files.files), len(edges)))
	}
	for _, edge := range edges {
		id := files.file(edge.name).id
		if id != edge.targetId {
			panic(fmt.Errorf("expected targetId %v for edge %v, but got %v", id, edge.name, id))
		}
	}
}

func runCheckpoint(log *eggs.Logger, client *eggs.Client, prefix string, files *fileHistoryFiles) fileHistoryCheckpoint {
	edges := readDir(log, client, msgs.ROOT_DIR_INODE_ID)
	checkCheckpoint(prefix, files, edges)
	resp := msgs.StatDirectoryResp{}
	shardReq(log, client, msgs.ROOT_DIR_INODE_ID.Shard(), &msgs.StatDirectoryReq{Id: msgs.ROOT_DIR_INODE_ID}, &resp)
	return fileHistoryCheckpoint{
		time: resp.Mtime,
	}
}

func runStep(log *eggs.Logger, client *eggs.Client, mbs eggs.MockableBlockServices, files *fileHistoryFiles, stepAny any) any {
	switch step := stepAny.(type) {
	case fileHistoryCreateFile:
		// 10 MiB spans
		id, creationTime := createFile(
			log, client, mbs,
			msgs.ROOT_DIR_INODE_ID, uint64(10)<<20, step.name, step.size,
			func(size uint64) []byte {
				return make([]byte, size)
			},
		)
		files.addFile(step.name, id, creationTime)
		return fileHistoryCreatedFile{
			name:         step.name,
			id:           id,
			creationTime: creationTime,
		}
	case fileHistoryDeleteFile:
		f := files.file(step.name)
		shardReq(
			log, client,
			msgs.ROOT_DIR_INODE_ID.Shard(),
			&msgs.SoftUnlinkFileReq{OwnerId: msgs.ROOT_DIR_INODE_ID, FileId: f.id, CreationTime: f.creationTime, Name: step.name}, &msgs.SoftUnlinkFileResp{},
		)
		files.deleteFile(step.name)
		return step
	case fileHistoryRenameFile:
		f := files.file(step.oldName)
		req := msgs.SameDirectoryRenameReq{DirId: msgs.ROOT_DIR_INODE_ID, TargetId: f.id, OldCreationTime: f.creationTime, OldName: step.oldName, NewName: step.newName}
		resp := msgs.SameDirectoryRenameResp{}
		shardReq(log, client, msgs.ROOT_DIR_INODE_ID.Shard(), &req, &resp)
		files.deleteFile(step.oldName)
		if _, wasPresent := files.byName[step.newName]; wasPresent {
			// overwrite
			files.deleteFile(step.newName)
		}
		files.addFile(step.newName, f.id, resp.NewCreationTime)
		return fileHistoryRenamedFile{
			oldName:         step.oldName,
			newName:         step.newName,
			newCreationTime: resp.NewCreationTime,
		}
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

func replayCheckpoint(prefix string, files *fileHistoryFiles, fullEdges []fullEdge, t msgs.EggsTime) {
	edges := edgesAsOf(fullEdges, t)
	checkCheckpoint(prefix, files, edges)
}

func replayStep(prefix string, files *fileHistoryFiles, fullEdges []fullEdge, stepAny any) {
	switch step := stepAny.(type) {
	case fileHistoryCreatedFile:
		files.addFile(step.name, step.id, step.creationTime)
	case fileHistoryDeleteFile:
		files.deleteFile(step.name)
	case fileHistoryRenamedFile:
		targetId := files.file(step.oldName).id
		files.deleteFile(step.oldName)
		if _, wasPresent := files.byName[step.newName]; wasPresent {
			// overwrite
			files.deleteFile(step.newName)
		}
		files.addFile(step.newName, targetId, step.newCreationTime)
	case fileHistoryCheckpoint:
		replayCheckpoint(prefix, files, fullEdges, step.time)
	default:
		panic(fmt.Errorf("bad step %T", stepAny))
	}
}

func fileHistoryStepSingle(log *eggs.Logger, client *eggs.Client, mbs *eggs.MockedBlockServices, opts *fileHistoryTestOpts, seed int64, filePrefix string) {
	// loop for n steps. at every step:
	// * if we have never reached the target files, then just create a file.
	// * if we have, create/delete/rename/rename with override at random.
	// * every checkpointEvery steps, use readDir to make sure that we have the files that we expect.
	//
	// after we've finished, re-run through everything and re-check at ever checkpointSteps using
	// fullReadDir.
	trace := []any{}

	reachedTargetFiles := false
	fls := fileHistoryFiles{
		files:  []fileHistoryFile{},
		byName: make(map[string]int),
	}
	source := rand.NewSource(seed)
	rand := rand.New(source)
	for stepIx := 0; stepIx < opts.steps; stepIx++ {
		if stepIx%opts.checkpointEvery == 0 {
			log.Info("%v: reached checkpoint at step %v", filePrefix, stepIx)
			checkpoint := runCheckpoint(log, client, filePrefix, &fls)
			trace = append(trace, checkpoint)
		}
		var step any
		if len(fls.files) < opts.lowFiles {
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
		reachedTargetFiles = reachedTargetFiles || len(fls.files) >= opts.targetFiles
		trace = append(trace, runStep(log, client, mbs, &fls, step))
	}
	fls = fileHistoryFiles{
		files:  []fileHistoryFile{},
		byName: make(map[string]int),
	}
	fullEdges := fullReadDir(log, client, msgs.ROOT_DIR_INODE_ID)
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
	log *eggs.Logger,
	shuckleAddress string,
	mbs0 eggs.MockableBlockServices,
	opts *fileHistoryTestOpts,
	counters *eggs.ClientCounters,
) {
	mbs := mbs0.(*eggs.MockedBlockServices)

	terminateChan := make(chan any, 1)

	go func() {
		defer func() { handleRecover(log, terminateChan, recover()) }()
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
				defer func() { handleRecover(log, terminateChan, recover()) }()
				shid := msgs.ShardId(0)
				client, err := eggs.NewClient(log, shuckleAddress, &shid, counters, nil)
				if err != nil {
					panic(err)
				}
				defer client.Close()
				fileHistoryStepSingle(log, client, mbs, opts, seed, prefix)
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
