// Very simple test creating some directory tree and reading it back
package main

import (
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"math"
	"math/rand"
	"os"
	"path"
	"strconv"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

type fsTestOpts struct {
	numDirs  int // how many dirs (in total) to create
	numFiles int // how many files (in total) to create
	depth    int // directory tree depth
	// these two should sum up to be < 1
	emptyFileProb  float64
	inlineFileProb float64
	maxFileSize    int
	spanSize       int
}

type fsTestHarness[Id comparable] interface {
	createDirectory(log *lib.Logger, owner Id, name string) (Id, msgs.EggsTime)
	rename(log *lib.Logger, targetId Id, oldOwner Id, oldCreationTime msgs.EggsTime, oldName string, newOwner Id, newName string) (Id, msgs.EggsTime)
	createFile(log *lib.Logger, owner Id, spanSize uint32, name string, size uint64, trailingZeros uint32, dataSeed uint64) (Id, msgs.EggsTime)
	// if false, the harness does not support reading files (e.g. we're mocking block services)
	checkFileData(log *lib.Logger, id Id, size uint64, trailingZeros uint32, dataSeed uint64)
	// files, directories
	readDirectory(log *lib.Logger, dir Id) ([]string, []string)
}

type apiFsTestHarness struct {
	client       *lib.Client
	dirInfoCache *lib.DirInfoCache
	// buffer we use both to create and to read files.
	fileContentsBuf []byte
	readBufPool     *lib.ReadSpanBufPool
}

func (c *apiFsTestHarness) createDirectory(log *lib.Logger, owner msgs.InodeId, name string) (id msgs.InodeId, creationTime msgs.EggsTime) {
	// TODO random parity
	req := msgs.MakeDirectoryReq{
		OwnerId: owner,
		Name:    name,
	}
	resp := msgs.MakeDirectoryResp{}
	cdcReq(log, c.client, &req, &resp)
	return resp.Id, resp.CreationTime
}

func (c *apiFsTestHarness) rename(
	log *lib.Logger,
	targetId msgs.InodeId,
	oldOwner msgs.InodeId,
	oldCreationTime msgs.EggsTime,
	oldName string,
	newOwner msgs.InodeId,
	newName string,
) (msgs.InodeId, msgs.EggsTime) {
	if oldOwner == newOwner {
		req := msgs.SameDirectoryRenameReq{
			TargetId:        targetId,
			DirId:           oldOwner,
			OldName:         oldName,
			OldCreationTime: oldCreationTime,
			NewName:         newName,
		}
		resp := msgs.SameDirectoryRenameResp{}
		shardReq(log, c.client, oldOwner.Shard(), &req, &resp)
		return targetId, resp.NewCreationTime
	} else if targetId.Type() == msgs.DIRECTORY {
		req := msgs.RenameDirectoryReq{
			TargetId:        targetId,
			OldOwnerId:      oldOwner,
			OldCreationTime: oldCreationTime,
			OldName:         oldName,
			NewOwnerId:      newOwner,
			NewName:         newName,
		}
		resp := msgs.RenameDirectoryResp{}
		cdcReq(log, c.client, &req, &resp)
		return targetId, resp.CreationTime
	} else {
		req := msgs.RenameFileReq{
			TargetId:        targetId,
			OldOwnerId:      oldOwner,
			OldCreationTime: oldCreationTime,
			OldName:         oldName,
			NewOwnerId:      newOwner,
			NewName:         newName,
		}
		resp := msgs.RenameFileResp{}
		cdcReq(log, c.client, &req, &resp)
		return targetId, resp.CreationTime
	}
}

func (c *apiFsTestHarness) createFile(
	log *lib.Logger, owner msgs.InodeId, spanSize uint32, name string, size uint64, trailingZeros uint32, dataSeed uint64,
) (msgs.InodeId, msgs.EggsTime) {
	return createFile(log, c.client, c.dirInfoCache, owner, spanSize, name, size, trailingZeros, dataSeed, &c.fileContentsBuf)
}

func (c *apiFsTestHarness) readDirectory(log *lib.Logger, dir msgs.InodeId) (files []string, dirs []string) {
	edges := readDir(log, c.client, dir)
	for _, edge := range edges {
		if edge.targetId.Type() == msgs.DIRECTORY {
			dirs = append(dirs, edge.name)
		} else {
			files = append(files, edge.name)
		}
	}
	return files, dirs
}

func checkFileData(actualData []byte, expectedData []byte) {
	if !bytes.Equal(actualData, expectedData) {
		dir, err := os.MkdirTemp("", "eggs-fstest-files.")
		if err != nil {
			panic(fmt.Errorf("mismatching data, could not create temp directory"))
		}
		expectedPath := path.Join(dir, "expected")
		actualPath := path.Join(dir, "actual")
		if err := os.WriteFile(expectedPath, expectedData, 0644); err != nil {
			panic(fmt.Errorf("mismatching data, could not create data file"))
		}
		if err := os.WriteFile(actualPath, actualData, 0644); err != nil {
			panic(fmt.Errorf("mismatching data, could not create data file"))
		}
		panic(fmt.Errorf("mismatching data, expected data is in %v, found data is in %v", expectedPath, actualPath))
	}

}

func ensureLen(buf []byte, l int) []byte {
	lenBefore := len(buf)
	if l <= cap(buf) {
		buf = buf[:l]
	} else {
		buf = buf[:cap(buf)]
		buf = append(buf, make([]byte, l-len(buf))...)
	}
	// memset? what's that?
	for i := lenBefore; i < len(buf); i++ {
		buf[i] = 0
	}
	return buf
}

func (c *apiFsTestHarness) checkFileData(log *lib.Logger, id msgs.InodeId, size uint64, trailingZeros uint32, dataSeed uint64) {
	fileData := readFile(log, c.readBufPool, c.client, id, &c.fileContentsBuf)
	fullSize := int(size) + int(trailingZeros)
	c.fileContentsBuf = ensureLen(c.fileContentsBuf, fullSize)
	expectedData := c.fileContentsBuf[:int(size)]
	wyhash.New(dataSeed).Read(expectedData[:int(size)])
	checkFileData(fileData, expectedData)
}

var _ = (fsTestHarness[msgs.InodeId])((*apiFsTestHarness)(nil))

type posixFsTestHarness struct {
	expectedDataBuf []byte
	actualDataBuf   []byte
}

func (*posixFsTestHarness) createDirectory(log *lib.Logger, owner string, name string) (fullPath string, creationTime msgs.EggsTime) {
	fullPath = path.Join(owner, name)
	log.LogStack(1, lib.DEBUG, "posix mkdir %v", fullPath)
	if err := os.Mkdir(fullPath, 0777); err != nil {
		panic(err)
	}
	return fullPath, 0
}

func (*posixFsTestHarness) rename(
	log *lib.Logger,
	targetFullPath string,
	oldDir string,
	oldCreationTime msgs.EggsTime,
	oldName string,
	newDir string,
	newName string,
) (string, msgs.EggsTime) {
	if targetFullPath != path.Join(oldDir, oldName) {
		panic(fmt.Errorf("mismatching %v and %v", targetFullPath, path.Join(oldDir, oldName)))
	}
	newFullPath := path.Join(newDir, newName)
	log.LogStack(1, lib.DEBUG, "posix rename %v -> %v", targetFullPath, newFullPath)
	if err := os.Rename(targetFullPath, path.Join(newDir, newName)); err != nil {
		panic(err)
	}
	return newFullPath, 0
}

func (c *posixFsTestHarness) createFile(
	log *lib.Logger, dirFullPath string, spanSize uint32, name string, size uint64, trailingZeros uint32, dataSeed uint64,
) (fileFullPath string, t msgs.EggsTime) {
	c.actualDataBuf = ensureLen(c.actualDataBuf, int(size)+int(trailingZeros))
	wyhash.New(dataSeed).Read(c.actualDataBuf[:size])
	fileFullPath = path.Join(dirFullPath, name)
	log.LogStack(1, lib.DEBUG, "posix create file %v", fileFullPath)
	if err := os.WriteFile(fileFullPath, c.actualDataBuf, 0644); err != nil {
		panic(err)
	}
	return fileFullPath, 0
}

func (c *posixFsTestHarness) readDirectory(log *lib.Logger, dirFullPath string) (files []string, dirs []string) {
	log.LogStack(1, lib.DEBUG, "posix readdir for %v", dirFullPath)
	fileInfo, err := ioutil.ReadDir(dirFullPath)
	if err != nil {
		panic(err)
	}
	for _, fi := range fileInfo {
		if fi.Mode().IsDir() {
			dirs = append(dirs, fi.Name())
		} else {
			files = append(files, fi.Name())
		}
	}
	return files, dirs
}

func (c *posixFsTestHarness) checkFileData(log *lib.Logger, fullFilePath string, size uint64, trailingZeros uint32, dataSeed uint64) {
	fullSize := int(size) + int(trailingZeros)
	c.expectedDataBuf = ensureLen(c.expectedDataBuf, fullSize)
	wyhash.New(dataSeed).Read(c.expectedDataBuf[:int(size)])
	c.actualDataBuf = ensureLen(c.actualDataBuf, fullSize)
	f, err := os.Open(fullFilePath)
	if err != nil {
		panic(err)
	}
	defer f.Close()
	// First we check the whole thing
	if _, err := io.ReadFull(f, c.actualDataBuf); err != nil {
		panic(err)
	}
	checkFileData(c.expectedDataBuf, c.actualDataBuf)
	// then we start doing random reads around
	if fullSize > 1 {
		for i := 0; i < 10; i++ {
			offset := int(rand.Uint64()) % (fullSize - 1)
			size := 1 + int(rand.Uint64())%(fullSize-offset-1)
			if _, err := f.Seek(int64(offset), 0); err != nil {
				panic(err)
			}
			expectedPartialData := c.expectedDataBuf[offset : offset+size]
			actualPartialData := c.actualDataBuf[offset : offset+size]
			if _, err := io.ReadFull(f, actualPartialData); err != nil {
				panic(err)
			}
			checkFileData(actualPartialData, expectedPartialData)
		}
	}
}

var _ = (fsTestHarness[string])((*posixFsTestHarness)(nil))

type fsTestDir[Id comparable] struct {
	id       Id
	children fsTestChildren[Id]
}

type fsTestChild[T any] struct {
	creationTime msgs.EggsTime
	body         T
}

type fsTestFile[Id comparable] struct {
	id            Id
	size          uint64
	trailingZeros uint32
	dataSeed      uint64
}

// We always use integers as names
type fsTestChildren[Id comparable] struct {
	files       map[int]fsTestChild[fsTestFile[Id]]
	directories map[int]fsTestChild[fsTestDir[Id]]
}

func newFsTestDir[Id comparable](id Id) *fsTestDir[Id] {
	return &fsTestDir[Id]{
		id: id,
		children: fsTestChildren[Id]{
			files:       make(map[int]fsTestChild[fsTestFile[Id]]),
			directories: make(map[int]fsTestChild[fsTestDir[Id]]),
		},
	}
}

type fsTestState[Id comparable] struct {
	totalDirs      int
	totalFiles     int
	totalFilesSize uint64
	rootDir        fsTestDir[Id]
}

func (s *fsTestDir[Id]) dir(path []int) *fsTestDir[Id] {
	if len(path) == 0 {
		return s
	}
	child, childFound := s.children.directories[path[0]]
	if !childFound {
		panic("dir not found")
	}
	return child.body.dir(path[1:])
}

func (s *fsTestState[Id]) dir(path []int) *fsTestDir[Id] {
	return s.rootDir.dir(path)
}

func (state *fsTestState[Id]) incrementDirs(log *lib.Logger, opts *fsTestOpts) {
	if state.totalDirs >= opts.numDirs {
		panic("ran out of dirs!")
	}
	state.totalDirs++
	if state.totalDirs%100 == 0 {
		log.Info("%v out of %v dirs created", state.totalDirs, opts.numDirs)
	}
}

func (state *fsTestState[Id]) makeDir(log *lib.Logger, harness fsTestHarness[Id], opts *fsTestOpts, parent []int, name int) []int {
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
	dirId, creationTime := harness.createDirectory(log, parentId, strconv.Itoa(name))
	dir.children.directories[name] = fsTestChild[fsTestDir[Id]]{
		body:         *newFsTestDir(dirId),
		creationTime: creationTime,
	}
	path := append(parent, name)
	return path
}

func (state *fsTestState[Id]) makeDirFromTemp(log *lib.Logger, harness fsTestHarness[Id], opts *fsTestOpts, parent []int, name int, tmpParent []int) []int {
	dir := state.dir(parent)
	_, dirExists := dir.children.directories[name]
	if dirExists {
		panic("conflicting name (dir)")
	}
	_, fileExists := dir.children.files[name]
	if fileExists {
		panic("conflicting name (files)")
	}
	var id Id
	var tmpCreationTime msgs.EggsTime
	tmpParentId := state.dir(tmpParent).id
	if tmpParentId == dir.id {
		return state.makeDir(log, harness, opts, parent, name)
	}
	state.incrementDirs(log, opts)
	id, tmpCreationTime = harness.createDirectory(log, tmpParentId, "tmp")
	newId, creationTime := harness.rename(log, id, tmpParentId, tmpCreationTime, "tmp", dir.id, strconv.Itoa(name))
	dir.children.directories[name] = fsTestChild[fsTestDir[Id]]{
		body:         *newFsTestDir(newId),
		creationTime: creationTime,
	}
	path := append(parent, name)
	return path
}

func (state *fsTestState[Id]) incrementFiles(log *lib.Logger, opts *fsTestOpts) {
	if state.totalFiles >= opts.numFiles {
		panic("ran out of files!")
	}
	state.totalFiles++
	if state.totalFiles%100 == 0 {
		log.Info("%v out of %v files created, %vGB", state.totalFiles, opts.numFiles, float64(state.totalFilesSize)/1e9)
	}
}

func (state *fsTestState[Id]) calcFileSize(log *lib.Logger, opts *fsTestOpts, rand *wyhash.Rand) (size uint64, trailingZeros uint32) {
	p := rand.Float64()
	if p < opts.emptyFileProb {
		size = 0
	} else if p < opts.emptyFileProb+opts.inlineFileProb {
		size = 1 + rand.Uint64()%254
	} else {
		size = 1 + rand.Uint64()%uint64(opts.maxFileSize)
	}
	trailingZeros = rand.Uint32() % 100
	state.totalFilesSize += size
	log.Debug("creating file with size %v, trailing zeros %v, total size %v", size, trailingZeros, state.totalFilesSize)
	return size, trailingZeros
}

func (state *fsTestState[Id]) makeFile(log *lib.Logger, harness fsTestHarness[Id], opts *fsTestOpts, rand *wyhash.Rand, dirPath []int, name int) {
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
	size, trailingZeros := state.calcFileSize(log, opts, rand)
	dataSeed := rand.Uint64()
	id, creationTime := harness.createFile(
		log, dir.id, uint32(opts.spanSize), strconv.Itoa(name), size, trailingZeros, dataSeed,
	)
	dir.children.files[name] = fsTestChild[fsTestFile[Id]]{
		body: fsTestFile[Id]{
			id:            id,
			size:          size,
			trailingZeros: trailingZeros,
			dataSeed:      dataSeed,
		},
		creationTime: creationTime,
	}
}

func (state *fsTestState[Id]) makeFileFromTemp(log *lib.Logger, harness fsTestHarness[Id], opts *fsTestOpts, rand *wyhash.Rand, dirPath []int, name int, tmpDirPath []int) {
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
	size, trailingZeros := state.calcFileSize(log, opts, rand)
	dataSeed := rand.Uint64()
	tmpParentId := state.dir(tmpDirPath).id
	id, creationTime := harness.createFile(
		log, tmpParentId, uint32(opts.spanSize), "tmp", size, trailingZeros, dataSeed,
	)
	newId, creationTime := harness.rename(log, id, tmpParentId, creationTime, "tmp", dir.id, strconv.Itoa(name))
	dir.children.files[name] = fsTestChild[fsTestFile[Id]]{
		body: fsTestFile[Id]{
			id:            newId,
			size:          size,
			trailingZeros: trailingZeros,
			dataSeed:      dataSeed,
		},
		creationTime: creationTime,
	}
}

func (d *fsTestDir[Id]) check(log *lib.Logger, harness fsTestHarness[Id]) {
	files, dirs := harness.readDirectory(log, d.id)
	if len(files)+len(dirs) != len(d.children.files)+len(d.children.directories) {
		panic(fmt.Errorf("bad number of edges -- got %v + %v, expected %v + %v", len(files), len(dirs), len(d.children.files), len(d.children.files)))
	}
	for _, fileName := range files {
		log.Debug("checking file %v", fileName)
		name, err := strconv.Atoi(fileName)
		if err != nil {
			panic(err)
		}
		file, present := d.children.files[name]
		if !present {
			panic(fmt.Errorf("file %v not found", name))
		}
		harness.checkFileData(
			log, file.body.id, file.body.size, file.body.trailingZeros, file.body.dataSeed,
		)
	}
	for _, dirName := range dirs {
		log.Debug("checking dir %v", dirName)
		name, err := strconv.Atoi(dirName)
		if err != nil {
			panic(err)
		}
		_, present := d.children.directories[name]
		if !present {
			panic(fmt.Errorf("directory %v not found", name))
		}
	}
	// recurse down
	for _, dir := range d.children.directories {
		dir.body.check(log, harness)
	}
}

// Just the first block service id we can find
func findBlockServiceToPurge(log *lib.Logger, client *lib.Client) msgs.BlockServiceId {
	filesReq := msgs.VisitFilesReq{}
	filesResp := msgs.VisitFilesResp{}
	for {
		shardReq(log, client, 0, &filesReq, &filesResp)
		for _, file := range filesResp.Ids {
			spansReq := msgs.FileSpansReq{FileId: file}
			spansResp := msgs.FileSpansResp{}
			for {
				shardReq(log, client, 0, &spansReq, &spansResp)
				if len(spansResp.BlockServices) > 0 {
					return spansResp.BlockServices[0].Id
				}
				if spansResp.NextOffset == 0 {
					break
				}
				spansReq.ByteOffset = spansResp.NextOffset
			}
		}
		if filesResp.NextId == 0 {
			panic("could not find block service")
		}
	}
}

func fsTestInternal[Id comparable](
	log *lib.Logger,
	shuckleAddress string,
	opts *fsTestOpts,
	counters *lib.ClientCounters,
	harness fsTestHarness[Id],
	rootId Id,
) {
	state := fsTestState[Id]{
		totalDirs: 1, // root dir
		rootDir:   *newFsTestDir(rootId),
	}
	branching := int(math.Log(float64(opts.numDirs)) / math.Log(float64(opts.depth)))
	rand := wyhash.New(42)
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
	log.Info("creating files")
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
	log.Info("checking directories/files")
	state.rootDir.check(log, harness)
	// Now, try to migrate away from one block service, to stimulate that code path
	// in tests somewhere.
	{
		client, err := lib.NewClient(log, shuckleAddress, nil, counters, nil)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		blockServiceToPurge := findBlockServiceToPurge(log, client)
		log.Info("will migrate block service %v", blockServiceToPurge)
		migrateStats := lib.MigrateStats{}
		err = lib.MigrateBlocksInAllShards(log, client, &migrateStats, blockServiceToPurge)
		if err != nil {
			panic(fmt.Errorf("could not migrate: %w", err))
		}
		if migrateStats.MigratedBlocks == 0 {
			panic(fmt.Errorf("migrate didn't migrate any blocks"))
		}
	}
	// And check the state again
	state.rootDir.check(log, harness)
}

func fsTest(
	log *lib.Logger,
	shuckleAddress string,
	opts *fsTestOpts,
	counters *lib.ClientCounters,
	realFs string, // if non-empty, will run the tests using this mountpoint
) {
	client, err := lib.NewClient(log, shuckleAddress, nil, counters, nil)
	if realFs == "" {
		if err != nil {
			panic(err)
		}
		defer client.Close()
		harness := &apiFsTestHarness{
			client:       client,
			dirInfoCache: lib.NewDirInfoCache(),
			readBufPool:  lib.NewReadSpanBufPool(),
		}
		fsTestInternal[msgs.InodeId](log, shuckleAddress, opts, counters, harness, msgs.ROOT_DIR_INODE_ID)
	} else {
		harness := &posixFsTestHarness{}
		fsTestInternal[string](log, shuckleAddress, opts, counters, harness, realFs)
	}
}
