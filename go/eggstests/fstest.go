// Very simple test creating some directory tree and reading it back
package main

import (
	"bytes"
	"fmt"
	"io"
	"math"
	"os"
	"path"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

type fsTestOpts struct {
	numDirs  int // how many dirs (in total) to create
	numFiles int // how many files (in total) to create
	depth    int // directory tree depth
	// these two should sum up to be < 1
	emptyFileProb   float64
	inlineFileProb  float64
	maxFileSize     int
	spanSize        int
	checkThreads    int
	corruptFileProb float64

	migrate bool
}

type fsTestHarness[Id comparable] interface {
	createDirectory(log *lib.Logger, owner Id, name string) (Id, msgs.EggsTime)
	rename(log *lib.Logger, targetId Id, oldOwner Id, oldCreationTime msgs.EggsTime, oldName string, newOwner Id, newName string) (Id, msgs.EggsTime)
	createFile(log *lib.Logger, owner Id, spanSize uint32, name string, size uint64, dataSeed uint64) (Id, msgs.EggsTime)
	// if false, the harness does not support reading files (e.g. we're mocking block services)
	checkFileData(log *lib.Logger, id Id, size uint64, dataSeed uint64)
	// files, directories
	readDirectory(log *lib.Logger, dir Id) ([]string, []string)
	removeFile(log *lib.Logger, dir Id, name string)
	removeDirectory(log *lib.Logger, dir Id, name string)
}

type apiFsTestHarness struct {
	client       *lib.Client
	dirInfoCache *lib.DirInfoCache
	readBufPool  *lib.BufPool
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
	log *lib.Logger, owner msgs.InodeId, spanSize uint32, name string, size uint64, dataSeed uint64,
) (msgs.InodeId, msgs.EggsTime) {
	return createFile(log, c.client, c.dirInfoCache, owner, spanSize, name, size, dataSeed, c.readBufPool)
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

func checkFileData(id any, from int, to int, actualData []byte, expectedData []byte) {
	if !bytes.Equal(actualData, expectedData) {
		dir, err := os.MkdirTemp("", "eggs-fstest-files.")
		if err != nil {
			panic(fmt.Errorf("mismatching data (%v,%v) for file %v, could not create temp directory", from, to, id))
		}
		expectedPath := path.Join(dir, "expected")
		actualPath := path.Join(dir, "actual")
		if err := os.WriteFile(expectedPath, expectedData, 0644); err != nil {
			panic(fmt.Errorf("mismatching data (%v,%v), could not create data file", from, to))
		}
		if err := os.WriteFile(actualPath, actualData, 0644); err != nil {
			panic(fmt.Errorf("mismatching data (%v,%v), could not create data file", from, to))
		}
		panic(fmt.Errorf("mismatching data (%v,%v) for file %v, expected data is in %v, found data is in %v", from, to, id, expectedPath, actualPath))
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

func (c *apiFsTestHarness) checkFileData(log *lib.Logger, id msgs.InodeId, size uint64, dataSeed uint64) {
	actualData := readFile(log, c.readBufPool, c.client, id, size)
	defer c.readBufPool.Put(actualData)
	expectedData := c.readBufPool.Get(int(size))
	defer c.readBufPool.Put(expectedData)
	wyhash.New(dataSeed).Read(*expectedData)
	checkFileData(id, 0, int(size), *actualData, *expectedData)
}

func (c *apiFsTestHarness) removeFile(log *lib.Logger, ownerId msgs.InodeId, name string) {
	lookupResp := msgs.LookupResp{}
	if err := c.client.ShardRequest(log, ownerId.Shard(), &msgs.LookupReq{DirId: ownerId, Name: name}, &lookupResp); err != nil {
		panic(err)
	}
	if err := c.client.ShardRequest(log, ownerId.Shard(), &msgs.SoftUnlinkFileReq{OwnerId: ownerId, FileId: lookupResp.TargetId, Name: name, CreationTime: lookupResp.CreationTime}, &msgs.SoftUnlinkFileResp{}); err != nil {
		panic(err)
	}
}

func (c *apiFsTestHarness) removeDirectory(log *lib.Logger, ownerId msgs.InodeId, name string) {
	lookupResp := msgs.LookupResp{}
	if err := c.client.ShardRequest(log, ownerId.Shard(), &msgs.LookupReq{DirId: ownerId, Name: name}, &lookupResp); err != nil {
		panic(err)
	}
	if err := c.client.CDCRequest(log, &msgs.SoftUnlinkDirectoryReq{OwnerId: ownerId, TargetId: lookupResp.TargetId, Name: name, CreationTime: lookupResp.CreationTime}, &msgs.SoftUnlinkDirectoryResp{}); err != nil {
		panic(err)
	}
}

var _ = (fsTestHarness[msgs.InodeId])((*apiFsTestHarness)(nil))

type posixFsTestHarness struct {
	bufPool *lib.BufPool
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

func getInodeId(log *lib.Logger, path string) msgs.InodeId {
	info, err := os.Stat(path)
	if err != nil {
		panic(err)
	}
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		panic(fmt.Errorf("unexpected non-stat_t"))
	}
	id := msgs.InodeId(stat.Ino)
	if id == 0 { // TODO why does this happen?
		id = msgs.ROOT_DIR_INODE_ID
	}
	return id
}

func (c *posixFsTestHarness) createFile(
	log *lib.Logger, dirFullPath string, spanSize uint32, name string, size uint64, dataSeed uint64,
) (fileFullPath string, t msgs.EggsTime) {
	fileFullPath = path.Join(dirFullPath, name)

	actualDataBuf := c.bufPool.Get(int(size))
	defer c.bufPool.Put(actualDataBuf)
	rand := wyhash.New(dataSeed)
	rand.Read(*actualDataBuf)
	var f *os.File
	f, err := os.Create(fileFullPath)
	if err != nil {
		panic(err)
	}
	log.LogStack(1, lib.DEBUG, "posix create file %v (%v size)", fileFullPath, size)
	if size > 0 {
		// write in randomly sized chunks
		chunks := int(rand.Uint32()%10) + 1
		offsets := make([]int, chunks+1)
		offsets[0] = 0
		for i := 1; i < chunks; i++ {
			offsets[i] = int(rand.Uint64() % size)
		}
		offsets[chunks] = int(size)
		sort.Ints(offsets)
		for i := 0; i < chunks; i++ {
			log.Debug("writing from %v to %v (pid %v)", offsets[i], offsets[i+1], os.Getpid())
			if _, err := f.Write((*actualDataBuf)[offsets[i]:offsets[i+1]]); err != nil {
				panic(err)
			}
		}
	}
	if err := f.Close(); err != nil {
		panic(err)
	}
	return fileFullPath, 0
}

func (c *posixFsTestHarness) readDirectory(log *lib.Logger, dirFullPath string) (files []string, dirs []string) {
	log.LogStack(1, lib.DEBUG, "posix readdir for %v", dirFullPath)
	fileInfo, err := os.ReadDir(dirFullPath)
	if err != nil {
		panic(err)
	}
	for _, fi := range fileInfo {
		if fi.IsDir() {
			dirs = append(dirs, fi.Name())
		} else {
			files = append(files, fi.Name())
		}
	}
	return files, dirs
}

func (c *posixFsTestHarness) checkFileData(log *lib.Logger, fullFilePath string, size uint64, dataSeed uint64) {
	log.Debug("checking data for file %v", fullFilePath)
	fullSize := int(size)
	expectedData := c.bufPool.Get(fullSize)
	defer c.bufPool.Put(expectedData)
	rand := wyhash.New(dataSeed)
	rand.Read(*expectedData)
	actualData := c.bufPool.Get(fullSize)
	defer c.bufPool.Put(actualData)
	f, err := os.Open(fullFilePath)
	if err != nil {
		panic(err)
	}
	defer f.Close()
	log.Debug("checking for file %v of expected len %v", fullFilePath, fullSize)
	// First do some random reads, hopefully stimulating span caches in some interesting way
	if fullSize > 1 {
		for i := 0; i < 10; i++ {
			offset := int(rand.Uint64() % uint64(fullSize-1))
			size := 1 + int(rand.Uint64()%uint64(fullSize-offset-1))
			log.Debug("reading from %v to %v in file of size %v", offset, offset+size, fullSize)
			if _, err := f.Seek(int64(offset), 0); err != nil {
				panic(err)
			}
			expectedPartialData := (*expectedData)[offset : offset+size]
			actualPartialData := (*actualData)[offset : offset+size]
			if _, err := io.ReadFull(f, actualPartialData); err != nil {
				panic(err)
			}
			checkFileData(fullFilePath, offset, offset+size, actualPartialData, expectedPartialData)
		}
	}
	// Then we check the whole thing
	if _, err := f.Seek(0, 0); err != nil {
		panic(err)
	}
	_, err = io.ReadFull(f, *actualData)
	if err != nil {
		panic(err)
	}
	checkFileData(fullFilePath, 0, fullSize, *actualData, *expectedData)
}

func (c *posixFsTestHarness) removeFile(log *lib.Logger, ownerId string, name string) {
	os.Remove(path.Join(ownerId, name))
}

func (c *posixFsTestHarness) removeDirectory(log *lib.Logger, ownerId string, name string) {
	os.Remove(path.Join(ownerId, name))
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
	id       Id
	size     uint64
	dataSeed uint64
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

func (state *fsTestState[Id]) calcFileSize(log *lib.Logger, opts *fsTestOpts, rand *wyhash.Rand) (size uint64) {
	p := rand.Float64()
	if p < opts.emptyFileProb || opts.maxFileSize == 0 {
		size = 0
	} else if p < opts.emptyFileProb+opts.inlineFileProb {
		size = 1 + rand.Uint64()%254
	} else {
		size = 1 + rand.Uint64()%uint64(opts.maxFileSize)
	}
	state.totalFilesSize += size
	log.Debug("creating file with size %v, total size %v (max %v, p=%v)", size, state.totalFilesSize, opts.maxFileSize, p)
	return size
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
	size := state.calcFileSize(log, opts, rand)
	dataSeed := rand.Uint64()
	id, creationTime := harness.createFile(
		log, dir.id, uint32(opts.spanSize), strconv.Itoa(name), size, dataSeed,
	)
	dir.children.files[name] = fsTestChild[fsTestFile[Id]]{
		body: fsTestFile[Id]{
			id:       id,
			size:     size,
			dataSeed: dataSeed,
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
	size := state.calcFileSize(log, opts, rand)
	dataSeed := rand.Uint64()
	tmpParentId := state.dir(tmpDirPath).id
	id, creationTime := harness.createFile(
		log, tmpParentId, uint32(opts.spanSize), "tmp", size, dataSeed,
	)
	newId, creationTime := harness.rename(log, id, tmpParentId, creationTime, "tmp", dir.id, strconv.Itoa(name))
	dir.children.files[name] = fsTestChild[fsTestFile[Id]]{
		body: fsTestFile[Id]{
			id:       newId,
			size:     size,
			dataSeed: dataSeed,
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
		name, err := strconv.Atoi(fileName)
		if err != nil {
			panic(err)
		}
		file, present := d.children.files[name]
		log.Debug("checking file %v (size %v)", fileName, file.body.size)
		if !present {
			panic(fmt.Errorf("file %v not found", name))
		}
		harness.checkFileData(
			log, file.body.id, file.body.size, file.body.dataSeed,
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

func (d *fsTestDir[Id]) clean(log *lib.Logger, harness fsTestHarness[Id]) {
	files, dirs := harness.readDirectory(log, d.id)
	for _, fileName := range files {
		log.Debug("removing file %v", fileName)
		harness.removeFile(log, d.id, fileName)
	}
	for _, dirName := range dirs {
		log.Debug("cleaning dir %v", dirName)
		name, err := strconv.Atoi(dirName)
		if err != nil {
			panic(err)
		}
		dir, present := d.children.directories[name]
		if !present {
			panic(fmt.Errorf("directory %v not found", name))
		}
		dir.body.clean(log, harness)
		harness.removeDirectory(log, d.id, dirName)
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

// returns how many blocks were corrupted
func corruptFiles(
	log *lib.Logger,
	shuckleAddress string,
	client *lib.Client,
	opts *fsTestOpts,
	rand *wyhash.Rand,
) uint64 {
	blockServicesToDataDirs := make(map[msgs.BlockServiceId]string)
	{
		resp, err := lib.ShuckleRequest(log, nil, shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			panic(err)
		}
		body := resp.(*msgs.AllBlockServicesResp)
		for _, block := range body.BlockServices {
			blockServicesToDataDirs[block.Id] = block.Path
		}
	}
	filesReq := msgs.VisitFilesReq{}
	filesResp := msgs.VisitFilesResp{}
	corrupted := uint64(0)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		if err := client.ShardRequest(log, shid, &filesReq, &filesResp); err != nil {
			panic(err)
		}
		for _, file := range filesResp.Ids {
			if rand.Float64() > opts.corruptFileProb {
				continue
			}
			fileSpansReq := msgs.FileSpansReq{
				FileId:     file,
				ByteOffset: 0,
			}
			fileSpansResp := msgs.FileSpansResp{}
			for {
				if err := client.ShardRequest(log, file.Shard(), &fileSpansReq, &fileSpansResp); err != nil {
					panic(err)
				}
				for spanIx := range fileSpansResp.Spans {
					span := &fileSpansResp.Spans[spanIx]
					if span.Header.StorageClass == msgs.INLINE_STORAGE {
						continue
					}
					body := span.Body.(*msgs.FetchedBlocksSpan)
					P := body.Parity.ParityBlocks()
					if P < 1 {
						continue
					}
					// corrupt at least one, at most P
					numBlocksToCorrupt := 1 + rand.Uint64()%uint64(P-1)
					log.Debug("will corrupt %v blocks in %v", numBlocksToCorrupt, file)
					blocksToCorruptIxs := make([]int, len(body.Blocks))
					for i := range blocksToCorruptIxs {
						blocksToCorruptIxs[i] = i
					}
					for i := 0; i < int(numBlocksToCorrupt); i++ {
						swapWith := i + int(rand.Uint64()%uint64(len(blocksToCorruptIxs)-i-1))
						blocksToCorruptIxs[i], blocksToCorruptIxs[swapWith] = blocksToCorruptIxs[swapWith], blocksToCorruptIxs[i]
					}
					blocksToCorruptIxs = blocksToCorruptIxs[:numBlocksToCorrupt]
					for ix := range blocksToCorruptIxs {
						block := body.Blocks[ix]
						path := lib.BlockIdToPath(blockServicesToDataDirs[fileSpansResp.BlockServices[block.BlockServiceIx].Id], block.BlockId)
						if rand.Uint64()&1 == 0 {
							log.Debug("removing block %v at %q", block.BlockId, path)
							// remove block
							if err := os.Remove(path); err != nil {
								panic(err)
							}
						} else {
							log.Debug("corrupting block %v at %q", block.BlockId, path)
							// corrupt block
							offset := int64(rand.Uint64() % (uint64(body.CellSize) * uint64(body.Stripes)))
							file, err := os.OpenFile(path, os.O_RDWR, 0644)
							if err != nil {
								panic(err)
							}
							buf := make([]byte, 1)
							_, err = file.ReadAt(buf, offset)
							if err != nil {
								panic(err)
							}
							buf[0] ^= 0xFF
							_, err = file.WriteAt(buf, offset)
							if err != nil {
								panic(err)
							}
						}
						corrupted++
					}
				}
				if fileSpansResp.NextOffset == 0 {
					break
				}
				fileSpansReq.ByteOffset = fileSpansResp.NextOffset
			}
		}
		filesReq.BeginId = filesResp.NextId
		if filesReq.BeginId == 0 {
			break
		}
	}
	return corrupted
}

func fsTestInternal[Id comparable](
	log *lib.Logger,
	client *lib.Client,
	state *fsTestState[Id],
	shuckleAddress string,
	opts *fsTestOpts,
	counters *lib.ClientCounters,
	harness fsTestHarness[Id],
	rootId Id,
) {
	if opts.checkThreads == 0 {
		panic(fmt.Errorf("must specify at least one check thread"))
	}
	t0 := time.Now()
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
	log.Info("created directories in %s", time.Since(t0))
	t0 = time.Now()
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
	log.Info("created files in %s", time.Since(t0))
	if opts.corruptFileProb > 0 {
		// now flip bits in 10% of files, to test scrubbing
		log.Info("corrupting %v%% of files", opts.corruptFileProb*100)
		corruptedBlocks := corruptFiles(log, shuckleAddress, client, opts, rand)
		log.Info("corrupted %v blocks", corruptedBlocks)
		// Now, scrub the corrupted blocks away. It would be nice to do this _after_
		// we've checked the files, so that we also test recovery on the read side,
		// but we currently just fail on bad CRC (just because I haven't got around
		// to implementing recovery)
		log.Info("scrubbing files")
		{
			var stats lib.ScrubState
			// 100 attempts since we might be running with block service killer
			if err := lib.ScrubFilesInAllShards(log, client, &lib.ScrubOptions{MaximumCheckAttempts: 100, NumWorkers: 10, WorkersQueueSize: 100, CheckerQueueSize: 100, QuietPeriod: -1}, &stats); err != nil {
				panic(err)
			}
			if stats.Migrate.MigratedBlocks != corruptedBlocks {
				panic(fmt.Errorf("expected to have migrated %v blocks, but migrated=%v", corruptedBlocks, stats.Migrate.MigratedBlocks))
			}
		}
	}
	t0 = time.Now()
	// finally, check that our view of the world is the real view of the world
	log.Info("checking directories/files")
	errsChans := make([](chan any), opts.checkThreads)
	for i := 0; i < opts.checkThreads; i++ {
		errChan := make(chan any)
		errsChans[i] = errChan
		go func() {
			defer func() {
				err := recover()
				if err != nil {
					log.Info("stacktrace for %v:", err)
					for _, line := range strings.Split(string(debug.Stack()), "\n") {
						log.Info(line)
					}
				}
				errChan <- err

			}()
			state.rootDir.check(log, harness)
		}()
	}
	for i := 0; i < opts.checkThreads; i++ {
		err := <-errsChans[i]
		if err != nil {
			panic(fmt.Errorf("checking thread %v failed: %v", i, err))
		}
	}
	state.rootDir.check(log, harness)
	log.Info("checked files in %s", time.Since(t0))
	t0 = time.Now()
	if opts.migrate {
		// Now, try to migrate away from one block service, to stimulate that code path
		// in tests somewhere.
		if opts.maxFileSize > 0 {
			client, err := lib.NewClient(log, nil, shuckleAddress)
			if err != nil {
				panic(err)
			}
			client.SetCounters(counters)
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
		log.Info("migrated files in %s", time.Since(t0))
		t0 = time.Now()
		// And check the state again, don't bother with multiple threads thoush
		state.rootDir.check(log, harness)
		log.Info("checked files in %s", time.Since(t0))
	}
	t0 = time.Now()
	// Now, remove everything -- the cleanup would do this anyway, but we want to stimulate
	// the removal paths in the filesystem tests.
	state.rootDir.clean(log, harness)
	log.Info("cleaned files in %s", time.Since(t0))
}

func fsTest(
	log *lib.Logger,
	shuckleAddress string,
	opts *fsTestOpts,
	counters *lib.ClientCounters,
	realFs string, // if non-empty, will run the tests using this mountpoint
) {
	client, err := lib.NewClient(log, nil, shuckleAddress)
	if err != nil {
		panic(err)
	}
	defer client.Close()
	client.SetCounters(counters)
	if realFs == "" {
		harness := &apiFsTestHarness{
			client:       client,
			dirInfoCache: lib.NewDirInfoCache(),
			readBufPool:  lib.NewBufPool(),
		}
		state := fsTestState[msgs.InodeId]{
			totalDirs: 1, // root dir
			rootDir:   *newFsTestDir(msgs.ROOT_DIR_INODE_ID),
		}
		fsTestInternal[msgs.InodeId](log, client, &state, shuckleAddress, opts, counters, harness, msgs.ROOT_DIR_INODE_ID)
	} else {
		harness := &posixFsTestHarness{
			bufPool: lib.NewBufPool(),
		}
		state := fsTestState[string]{
			totalDirs: 1, // root dir
			rootDir:   *newFsTestDir(realFs),
		}
		fsTestInternal[string](log, client, &state, shuckleAddress, opts, counters, harness, realFs)
	}
}
