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
	maxDepth int // max directory tree depth
}

type fsTestDir struct {
	id       msgs.InodeId
	children fsTestChildren
}

// We always use integers as names
type fsTestChildren struct {
	files       map[int]msgs.InodeId
	directories map[int]*fsTestDir
}

func newFsTestDir(id msgs.InodeId) *fsTestDir {
	return &fsTestDir{
		id: id,
		children: fsTestChildren{
			files:       make(map[int]msgs.InodeId),
			directories: make(map[int]*fsTestDir),
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
	return child.dir(path[1:])
}

func (s *fsTestState) dir(path []int) *fsTestDir {
	return s.rootDir.dir(path)
}

/*
func (s *fsTestChildren) id(path []int) msgs.InodeId {
	fileId, fileFound := s.files[path[0]]
	if fileFound {
		if len(path) > 1 {
			panic("bad path for file")
		}
		return fileId
	}
	dir, dirFound := s.directories[path[0]]
	if dirFound {
		if len(path) == 1 {
			return dir.id
		} else {
			return dir.children.id(path[1:])
		}
	}
	panic("could not find path")
}

func (s *fsTestState) id(path []int) msgs.InodeId {

	if len(path) == 0 {
		return msgs.ROOT_DIR_INODE_ID
	}
	return s.rootDir.id(path)
}
*/

func (state *fsTestState) makeDir(log eggs.LogLevels, harness *harness, opts *fsTestOpts, parent []int, name int) []int {
	if state.totalDirs >= opts.numDirs {
		panic("ran out of dirs!")
	}
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
	dir.children.directories[name] = newFsTestDir(resp.Id)
	state.totalDirs++
	path := append(parent, name)
	return path
}

func (state *fsTestState) makeFile(log eggs.LogLevels, harness *harness, opts *fsTestOpts, rand *rand.Rand, dirPath []int, name int) {
	if state.totalFiles >= opts.numFiles {
		panic("ran out of files!")
	}
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
	id := harness.createFile(dir.id, strconv.Itoa(name), size)
	state.totalFiles++
	dir.children.files[name] = id
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
		dir.check(log, harness)
	}
}

func fsTest(
	log eggs.LogLevels,
	opts *fsTestOpts,
	stats *harnessStats,
	blockServicesKeys map[msgs.BlockServiceId][16]byte,
) {
	client, err := eggs.NewAllShardsClient()
	if err != nil {
		panic(err)
	}
	defer client.Close()
	harness := newHarness(log, client, stats, blockServicesKeys)
	state := fsTestState{
		rootDir: newFsTestDir(msgs.ROOT_DIR_INODE_ID),
	}
	branching := int(math.Log(float64(opts.numDirs)) / math.Log(float64(opts.maxDepth)))
	// Create directories by first creating the first n-1 levels according to branching above
	allDirs := [][]int{}
	lastLevelDirs := [][]int{}
	for depth := 1; depth <= opts.maxDepth; depth++ {
		depthDirs := int(math.Pow(float64(branching), float64(depth)))
		for i := 0; i < depthDirs; i++ {
			parentPath := []int{}
			j := i
			for len(parentPath)+1 != depth {
				j = j / branching
				parentPath = append([]int{j}, parentPath...)
			}
			path := state.makeDir(log, harness, opts, parentPath, i)
			allDirs = append(allDirs, path)
			if depth == opts.maxDepth {
				lastLevelDirs = append(lastLevelDirs, path)
			}
		}
	}
	// then create the leaves at random
	source := rand.NewSource(0)
	rand := rand.New(source)
	for state.totalDirs < opts.numDirs {
		parentPath := lastLevelDirs[int(rand.Uint32())%len(lastLevelDirs)]
		state.makeDir(log, harness, opts, parentPath, state.totalDirs)
	}
	// now create files, random locations
	for state.totalFiles < opts.numFiles {
		dir := allDirs[int(rand.Uint32())%len(lastLevelDirs)]
		state.makeFile(log, harness, opts, rand, dir, state.totalDirs+state.totalFiles)
	}
	// finally, check that our view of the world is the real view of the world
	state.rootDir.check(log, harness)
}
