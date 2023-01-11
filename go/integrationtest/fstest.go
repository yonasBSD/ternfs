// Very simple test creating some directory tree and reading it back
package main

import (
	"fmt"
	"math"
	"math/rand"
	"strconv"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

type fsTestOpts struct {
	numDirs  int // how many dirs (in total) to create
	numFiles int // how many files (in total) to create
	depth    int // directory tree depth
}

type fsTestDir struct {
	id       msgs.InodeId
	children fsTestChildren
}

type fsTestChild[T any] struct {
	creationTime msgs.EggsTime
	body         T
}

// We always use integers as names
type fsTestChildren struct {
	files       map[int]fsTestChild[msgs.InodeId]
	directories map[int]fsTestChild[*fsTestDir]
}

func newFsTestDir(id msgs.InodeId) *fsTestDir {
	return &fsTestDir{
		id: id,
		children: fsTestChildren{
			files:       make(map[int]fsTestChild[msgs.InodeId]),
			directories: make(map[int]fsTestChild[*fsTestDir]),
		},
	}
}

type fsTestState struct {
	totalDirs  int
	totalFiles int
	rootDir    *fsTestDir
}

func (s *fsTestDir) dir(path []int) *fsTestDir {
	if len(path) == 0 {
		return s
	}
	child, childFound := s.children.directories[path[0]]
	if !childFound {
		panic("dir not found")
	}
	return child.body.dir(path[1:])
}

func (s *fsTestState) dir(path []int) *fsTestDir {
	return s.rootDir.dir(path)
}

func (state *fsTestState) incrementDirs(log eggs.LogLevels, opts *fsTestOpts) {
	if state.totalDirs >= opts.numDirs {
		panic("ran out of dirs!")
	}
	state.totalDirs++
	if state.totalDirs%100 == 0 {
		log.Info("%v out of %v dirs created", state.totalDirs, opts.numDirs)
	}
}

func (state *fsTestState) makeDir(log eggs.LogLevels, harness *harness, opts *fsTestOpts, parent []int, name int) []int {
	state.incrementDirs(log, opts)
	dir := state.dir(parent)
	_, dirExists := dir.children.directories[name]
	if dirExists {
		panic("conflicting name (dir)")
	}
	_, fileExists := dir.children.files[name]
	if fileExists {
		panic("conflicting name (files)")
	}
	parentId := dir.id
	req := msgs.MakeDirectoryReq{
		OwnerId: parentId,
		Name:    strconv.Itoa(name),
		Info:    msgs.SetDirectoryInfo{Inherited: true},
	}
	resp := msgs.MakeDirectoryResp{}
	harness.cdcReq(&req, &resp)
	dir.children.directories[name] = fsTestChild[*fsTestDir]{
		body:         newFsTestDir(resp.Id),
		creationTime: resp.CreationTime,
	}
	path := append(parent, name)
	return path
}

func (state *fsTestState) makeDirFromTemp(log eggs.LogLevels, harness *harness, opts *fsTestOpts, parent []int, name int, tmpParent []int) []int {
	state.incrementDirs(log, opts)
	dir := state.dir(parent)
	_, dirExists := dir.children.directories[name]
	if dirExists {
		panic("conflicting name (dir)")
	}
	_, fileExists := dir.children.files[name]
	if fileExists {
		panic("conflicting name (files)")
	}
	var id msgs.InodeId
	var tmpCreationTime msgs.EggsTime
	tmpParentId := state.dir(tmpParent).id
	{
		req := msgs.MakeDirectoryReq{
			OwnerId: tmpParentId,
			Name:    "tmp",
			Info:    msgs.SetDirectoryInfo{Inherited: true},
		}
		resp := msgs.MakeDirectoryResp{}
		harness.cdcReq(&req, &resp)
		id = resp.Id
		tmpCreationTime = resp.CreationTime
	}
	var creationTime msgs.EggsTime
	{
		parentId := dir.id
		req := msgs.RenameDirectoryReq{
			TargetId:        id,
			OldOwnerId:      tmpParentId,
			OldCreationTime: tmpCreationTime,
			OldName:         "tmp",
			NewOwnerId:      parentId,
			NewName:         strconv.Itoa(name),
		}
		resp := msgs.RenameDirectoryResp{}
		harness.cdcReq(&req, &resp)
		creationTime = resp.CreationTime
	}
	dir.children.directories[name] = fsTestChild[*fsTestDir]{
		body:         newFsTestDir(id),
		creationTime: creationTime,
	}
	path := append(parent, name)
	return path
}

func (state *fsTestState) incrementFiles(log eggs.LogLevels, opts *fsTestOpts) {
	if state.totalFiles >= opts.numFiles {
		panic("ran out of files!")
	}
	state.totalFiles++
	if state.totalFiles%100 == 0 {
		log.Info("%v out of %v dirs created", state.totalFiles, opts.numFiles)
	}
}

func (state *fsTestState) makeFile(log eggs.LogLevels, harness *harness, opts *fsTestOpts, rand *rand.Rand, dirPath []int, name int) {
	state.incrementFiles(log, opts)
	dir := state.dir(dirPath)
	_, dirExists := dir.children.directories[name]
	if dirExists {
		panic("conflicting name (dir)")
	}
	_, fileExists := dir.children.files[name]
	if fileExists {
		panic("conflicting name (files)")
	}
	size := rand.Uint64() % (uint64(100) << 20) // up to 20MiB
	id, creationTime := harness.createFile(dir.id, strconv.Itoa(name), size)
	dir.children.files[name] = fsTestChild[msgs.InodeId]{
		body:         id,
		creationTime: creationTime,
	}
}

func (state *fsTestState) makeFileFromTemp(log eggs.LogLevels, harness *harness, opts *fsTestOpts, rand *rand.Rand, dirPath []int, name int, tmpDirPath []int) {
	state.incrementFiles(log, opts)
	dir := state.dir(dirPath)
	_, dirExists := dir.children.directories[name]
	if dirExists {
		panic("conflicting name (dir)")
	}
	_, fileExists := dir.children.files[name]
	if fileExists {
		panic("conflicting name (files)")
	}
	size := rand.Uint64() % (uint64(100) << 20) // up to 20MiB
	tmpParentId := state.dir(tmpDirPath).id
	id, creationTime := harness.createFile(tmpParentId, "tmp", size)
	if tmpParentId == dir.id {
		req := msgs.SameDirectoryRenameReq{
			DirId:           dir.id,
			TargetId:        id,
			OldName:         "tmp",
			OldCreationTime: creationTime,
			NewName:         strconv.Itoa(name),
		}
		resp := msgs.SameDirectoryRenameResp{}
		harness.shardReq(dir.id.Shard(), &req, &resp)
		creationTime = resp.NewCreationTime
	} else {
		req := msgs.RenameFileReq{
			TargetId:        id,
			OldOwnerId:      tmpParentId,
			OldCreationTime: creationTime,
			OldName:         "tmp",
			NewOwnerId:      dir.id,
			NewName:         strconv.Itoa(name),
		}
		resp := msgs.RenameFileResp{}
		harness.cdcReq(&req, &resp)
		creationTime = resp.CreationTime
	}
	dir.children.files[name] = fsTestChild[msgs.InodeId]{
		body:         id,
		creationTime: creationTime,
	}
}

func (d *fsTestDir) check(log eggs.LogLevels, harness *harness) {
	edges := harness.readDir(d.id)
	if len(edges) != len(d.children.files)+len(d.children.directories) {
		panic(fmt.Errorf("bad number of edges -- got %v, expected %v + %v", len(edges), len(d.children.files), len(d.children.files)))
	}
	for _, edge := range edges {
		name, err := strconv.Atoi(edge.name)
		if err != nil {
			panic(err)
		}
		if edge.targetId.Type() == msgs.DIRECTORY {
			_, present := d.children.directories[name]
			if !present {
				panic(fmt.Errorf("directory %v not found", name))
			}
		} else {
			_, present := d.children.files[name]
			if !present {
				panic(fmt.Errorf("file %v not found", name))
			}
		}
	}
	// recurse down
	for _, dir := range d.children.directories {
		dir.body.check(log, harness)
	}
}

func fsTest(
	log eggs.LogLevels,
	opts *fsTestOpts,
	counters *eggs.ClientCounters,
	blockServicesKeys map[msgs.BlockServiceId][16]byte,
) {
	client, err := eggs.NewClient(nil, counters, nil)
	if err != nil {
		panic(err)
	}
	defer client.Close()
	harness := newHarness(log, client, blockServicesKeys)
	state := fsTestState{
		totalDirs: 1, // root dir
		rootDir:   newFsTestDir(msgs.ROOT_DIR_INODE_ID),
	}
	branching := int(math.Log(float64(opts.numDirs)) / math.Log(float64(opts.depth)))
	source := rand.NewSource(0)
	rand := rand.New(source)
	// Create directories by first creating the first n-1 levels according to branching above
	allDirs := [][]int{
		{}, // root
	}
	lastLevelDirs := [][]int{}
	for depth := 1; depth <= opts.depth; depth++ {
		depthDirs := int(math.Pow(float64(branching), float64(depth)))
		for i := 0; i < depthDirs; i++ {
			parentPath := []int{}
			j := i
			for len(parentPath)+1 != depth {
				j = j / branching
				parentPath = append([]int{j}, parentPath...)
			}
			var path []int
			// create and then move for 1/5 of the dirs
			if rand.Uint32()%5 == 0 {
				tmpParentPath := allDirs[int(rand.Uint32())%len(allDirs)]
				path = state.makeDirFromTemp(log, harness, opts, parentPath, i, tmpParentPath)
			} else {
				path = state.makeDir(log, harness, opts, parentPath, i)
			}
			allDirs = append(allDirs, path)
			if depth == opts.depth {
				lastLevelDirs = append(lastLevelDirs, path)
			}
		}
	}
	// Then create the leaves at random. To stimulate CDC paths (also afterwards in GC), create
	// them and then move them.
	for state.totalDirs < opts.numDirs {
		parentPath := lastLevelDirs[int(rand.Uint32())%len(lastLevelDirs)]
		// create and then move for 1/5 of the dirs
		if rand.Uint32()%5 == 0 {
			tmpParentPath := allDirs[int(rand.Uint32())%len(allDirs)]
			state.makeDirFromTemp(log, harness, opts, parentPath, state.totalDirs, tmpParentPath)
		} else {
			state.makeDir(log, harness, opts, parentPath, state.totalDirs)
		}
	}
	// now create files, random locations
	for state.totalFiles < opts.numFiles {
		dir := allDirs[int(rand.Uint32())%len(allDirs)]
		if rand.Uint32()%5 == 0 {
			tmpParentPath := allDirs[int(rand.Uint32())%len(allDirs)]
			state.makeFileFromTemp(log, harness, opts, rand, dir, state.totalDirs+state.totalFiles, tmpParentPath)
		} else {
			state.makeFile(log, harness, opts, rand, dir, state.totalDirs+state.totalFiles)
		}
	}
	// finally, check that our view of the world is the real view of the world
	state.rootDir.check(log, harness)
}
