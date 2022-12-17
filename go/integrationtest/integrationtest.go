package main

import (
	"encoding/hex"
	"flag"
	"fmt"
	"math/rand"
	"os"
	"path"
	"runtime/debug"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

type edge struct {
	name     string
	targetId msgs.InodeId
}

type fullEdge struct {
	current      bool
	name         string
	targetId     msgs.InodeId
	creationTime msgs.EggsTime
}

// The idea here is that we'd run some parts of the test also directly hitting the filesystem.
type harness interface {
	createFile(name string, size uint64) msgs.InodeId
	deleteFile(name string, id msgs.InodeId)
	renameFile(targetId msgs.InodeId, oldName string, newName string)
	statDir() msgs.EggsTime
	readDir() []edge
	fullReadDir() []fullEdge
}

type harnessStats struct {
	reqs int64
	/*
		fileCreates        int64
		fileConstructNanos int64
		fileLinkNanos      int64
	*/
	fileDeletes       int64
	fileDeletesNanos  int64
	fileRenames       int64
	fileRenamesNanos  int64
	statDirs          int64
	statDirsNanos     int64
	readDirs          int64
	readDirsNanos     int64
	fullReadDirs      int64
	fullReadDirsNanos int64
}

type directHarness struct {
	shid              msgs.ShardId
	client            eggs.Client
	stats             *harnessStats
	blockServicesKeys map[msgs.BlockServiceId][16]byte
}

func (h *directHarness) shardReq(
	reqBody bincode.Packable,
	respBody bincode.Unpackable,
) {
	atomic.AddInt64(&h.stats.reqs, 1)
	err := h.client.ShardRequest(&eggs.LogToStdout{}, shid, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

func (h *directHarness) createFile(name string, size uint64) msgs.InodeId {
	// construct
	constructReq := msgs.ConstructFileReq{
		Type: msgs.FILE,
		Note: "",
	}
	constructResp := msgs.ConstructFileResp{}
	h.shardReq(&constructReq, &constructResp)
	// add spans
	spanSize := uint64(10) << 20 // 10MiB
	for offset := uint64(0); offset < size; offset += spanSize {
		thisSpanSize := eggs.Min(spanSize, size-offset)
		addSpanReq := msgs.AddSpanInitiateReq{
			FileId:       constructResp.Id,
			Cookie:       constructResp.Cookie,
			ByteOffset:   offset,
			StorageClass: 2,
			Parity:       msgs.MkParity(1, 1),
			Crc32:        [4]byte{0, 0, 0, 0},
			Size:         thisSpanSize,
			BlockSize:    thisSpanSize,
			BodyBlocks: []msgs.NewBlockInfo{
				{Crc32: [4]byte{0, 0, 0, 0}},
				{Crc32: [4]byte{0, 0, 0, 0}},
			},
		}
		addSpanResp := msgs.AddSpanInitiateResp{}
		h.shardReq(&addSpanReq, &addSpanResp)
		block0 := &addSpanResp.Blocks[0]
		block1 := &addSpanResp.Blocks[1]
		blockServiceKey0, blockServiceFound0 := h.blockServicesKeys[block0.BlockServiceId]
		if !blockServiceFound0 {
			panic(fmt.Errorf("could not find block service %v", block0.BlockServiceId))
		}
		blockServiceKey1, blockServiceFound1 := h.blockServicesKeys[block1.BlockServiceId]
		if !blockServiceFound1 {
			panic(fmt.Errorf("could not find block service %v", block1.BlockServiceId))
		}
		certifySpanReq := msgs.AddSpanCertifyReq{
			FileId:     constructResp.Id,
			Cookie:     constructResp.Cookie,
			ByteOffset: offset,
			Proofs: []msgs.BlockProof{
				{
					BlockId: block0.BlockId,
					Proof:   eggs.BlockAddProof(block0.BlockServiceId, block0.BlockId, blockServiceKey0),
				},
				{
					BlockId: block1.BlockId,
					Proof:   eggs.BlockAddProof(block1.BlockServiceId, block1.BlockId, blockServiceKey1),
				},
			},
		}
		certifySpanResp := msgs.AddSpanCertifyResp{}
		h.shardReq(&certifySpanReq, &certifySpanResp)
	}
	// link
	linkReq := msgs.LinkFileReq{
		FileId:  constructResp.Id,
		Cookie:  constructResp.Cookie,
		OwnerId: msgs.ROOT_DIR_INODE_ID,
		Name:    name,
	}
	h.shardReq(&linkReq, &msgs.LinkFileResp{})
	return constructResp.Id
}

func (h *directHarness) deleteFile(name string, id msgs.InodeId) {
	atomic.AddInt64(&h.stats.fileDeletes, 1)
	deleteReq := msgs.SoftUnlinkFileReq{
		OwnerId: msgs.ROOT_DIR_INODE_ID,
		FileId:  id,
		Name:    name,
	}
	t0 := time.Now()
	h.shardReq(&deleteReq, &msgs.SoftUnlinkFileResp{})
	atomic.AddInt64(&h.stats.fileDeletesNanos, time.Since(t0).Nanoseconds())
}

func (h *directHarness) renameFile(targetId msgs.InodeId, oldName string, newName string) {
	atomic.AddInt64(&h.stats.fileRenames, 1)
	renameReq := msgs.SameDirectoryRenameReq{
		TargetId: targetId,
		DirId:    msgs.ROOT_DIR_INODE_ID,
		OldName:  oldName,
		NewName:  newName,
	}
	t0 := time.Now()
	h.shardReq(&renameReq, &msgs.SameDirectoryRenameResp{})
	atomic.AddInt64(&h.stats.fileRenamesNanos, time.Since(t0).Nanoseconds())
}

func (h *directHarness) statDir() msgs.EggsTime {
	atomic.AddInt64(&h.stats.statDirs, 1)
	statReq := msgs.StatDirectoryReq{
		Id: msgs.ROOT_DIR_INODE_ID,
	}
	statResp := msgs.StatDirectoryResp{}
	t0 := time.Now()
	h.shardReq(&statReq, &statResp)
	atomic.AddInt64(&h.stats.statDirsNanos, time.Since(t0).Nanoseconds())
	return statResp.Mtime
}

func (h *directHarness) readDir() []edge {
	req := msgs.ReadDirReq{
		DirId:     msgs.ROOT_DIR_INODE_ID,
		StartHash: 0,
	}
	resp := msgs.ReadDirResp{}
	edges := []edge{}
	for {
		atomic.AddInt64(&h.stats.readDirs, 1)
		t0 := time.Now()
		h.shardReq(&req, &resp)
		atomic.AddInt64(&h.stats.readDirsNanos, time.Since(t0).Nanoseconds())
		for _, result := range resp.Results {
			edges = append(edges, edge{
				name:     result.Name,
				targetId: result.TargetId,
			})
		}
		req.StartHash = resp.NextHash
		if req.StartHash == 0 {
			break
		}
	}
	return edges
}

func (h *directHarness) fullReadDir() []fullEdge {
	req := msgs.FullReadDirReq{
		DirId: msgs.ROOT_DIR_INODE_ID,
	}
	resp := msgs.FullReadDirResp{}
	edges := []fullEdge{}
	for {
		atomic.AddInt64(&h.stats.fullReadDirs, 1)
		t0 := time.Now()
		h.shardReq(&req, &resp)
		atomic.AddInt64(&h.stats.fullReadDirsNanos, time.Since(t0).Nanoseconds())
		for _, result := range resp.Results {
			edges = append(edges, fullEdge{
				name:         result.Name,
				targetId:     result.TargetId.Id(),
				creationTime: result.CreationTime,
				current:      result.Current,
			})
		}
		req.Cursor = resp.Next
		if req.Cursor.StartHash == 0 {
			break
		}
	}
	return edges
}

func newDirectHarness(shid msgs.ShardId, stats *harnessStats, blockServicesKeys map[msgs.BlockServiceId][16]byte) *directHarness {
	client, err := eggs.NewShardSpecificClient(shid)
	if err != nil {
		panic(err)
	}
	return &directHarness{
		shid:              shid,
		client:            client,
		stats:             stats,
		blockServicesKeys: blockServicesKeys,
	}
}

const shid = msgs.ShardId(0) // hardcoded for now

// actions

type createFile struct {
	name string
	size uint64
}

type createdFile struct {
	name string
	id   msgs.InodeId
}

type deleteFile struct {
	name string
}

type renameFile struct {
	oldName string
	newName string
}

// trace

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

func runCheckpoint(harness harness, prefix string, files *files) checkpoint {
	edges := harness.readDir()
	checkCheckpoint(prefix, files, edges)
	return checkpoint{
		time: harness.statDir(),
	}
}

func runStep(harness harness, files *files, stepAny any) any {
	switch step := stepAny.(type) {
	case createFile:
		id := harness.createFile(step.name, step.size)
		files.addFile(step.name, id)
		return createdFile{
			name: step.name,
			id:   id,
		}
	case deleteFile:
		harness.deleteFile(step.name, files.id(step.name))
		files.deleteFile(step.name)
		return step
	case renameFile:
		targetId := files.id(step.oldName)
		harness.renameFile(targetId, step.oldName, step.newName)
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

func runTestSingle(harness harness, seed int64, filePrefix string) {
	steps := 10 * 1000     // perform 10k actions
	checkpointEvery := 100 // get times every 100 actions
	targetFiles := 1000    // how many files we want
	lowFiles := 500

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
	for stepIx := 0; stepIx < steps; stepIx++ {
		if stepIx%checkpointEvery == 0 {
			checkpoint := runCheckpoint(harness, filePrefix, &fls)
			trace = append(trace, checkpoint)
		}
		var step any
		if len(fls.names) < lowFiles {
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
		reachedTargetFiles = reachedTargetFiles || len(fls.names) >= targetFiles
		trace = append(trace, runStep(harness, &fls, step))
	}
	fls = files{
		names:  []string{},
		ids:    []msgs.InodeId{},
		byName: make(map[string]int),
	}
	fullEdges := harness.fullReadDir()
	for _, step := range trace {
		replayStep(filePrefix, &fls, fullEdges, step)
	}
}

func handleRecover(terminateChan chan any, err any) {
	if err != nil {
		fmt.Printf("PANIC %v. Stacktrace:\n", err)
		fmt.Print(string(debug.Stack()))
		terminateChan <- err
	}
}

func spawnTests(terminateChan chan any, blockServices []eggs.BlockService) {
	blockServicesKeys := make(map[msgs.BlockServiceId][16]byte)
	for _, blockService := range blockServices {
		key, err := hex.DecodeString(blockService.SecretKey)
		if err != nil {
			panic(fmt.Errorf("could not decode key for block service %v: %w", blockService.Id, err))
		}
		if len(key) != 16 {
			panic(fmt.Errorf("unexpected length for secret key for block service %v -- expected %v, got %v", blockService.Id, 16, len(key)))
		}
		var fixedKey [16]byte
		copy(fixedKey[:], key)
		blockServicesKeys[blockService.Id] = fixedKey
	}

	go func() {
		defer func() { handleRecover(terminateChan, recover()) }()
		numTests := uint8(5)
		if numTests > 15 {
			panic(fmt.Errorf("numTests %d too big for one-digit prefix", numTests))
		}
		var wait sync.WaitGroup
		wait.Add(int(numTests))
		fmt.Printf("running simple file creation/renaming/remove test with %d threads\n", numTests)
		t0 := time.Now()
		stats := harnessStats{}
		for i := 0; i < int(numTests); i++ {
			prefix := fmt.Sprintf("%x", i)
			seed := int64(i)
			go func() {
				defer func() { handleRecover(terminateChan, recover()) }()
				harness := newDirectHarness(shid, &stats, blockServicesKeys)
				defer harness.client.Close()
				runTestSingle(harness, seed, prefix)
				wait.Done()
			}()
		}
		wait.Wait()
		elapsed := time.Since(t0)
		fmt.Printf("ran test in %.2fs, %v requests performed, %.2f reqs/sec\n", float32(elapsed.Milliseconds())/100.0, stats.reqs, 1000.0*float32(stats.reqs/elapsed.Milliseconds()))
		// fmt.Printf("fileCreates:\t%v\t%v\n", stats.fileCreates, time.Duration(stats.fileLinkNanos/stats.fileCreates))
		fmt.Printf("fileDeletes:\t%v\t%v\n", stats.fileDeletes, time.Duration(stats.fileDeletesNanos/stats.fileDeletes))
		fmt.Printf("fileRenames:\t%v\t%v\n", stats.fileRenames, time.Duration(stats.fileRenamesNanos/stats.fileRenames))
		fmt.Printf("statDirs:\t%v\t%v\n", stats.statDirs, time.Duration(stats.statDirsNanos/stats.statDirs))
		fmt.Printf("readDirs:\t%v\t%v\n", stats.readDirs, time.Duration(stats.readDirsNanos/stats.readDirs))
		fmt.Printf("fullReadDirs:\t%v\t%v\n", stats.fullReadDirs, time.Duration(stats.fullReadDirsNanos/stats.fullReadDirs))

		client, err := eggs.NewShardSpecificClient(shid)
		if err != nil {
			panic(err)
		}
		dirInfo, err := eggs.GetDirectoryInfo(&eggs.LogToStdout{}, client, msgs.ROOT_DIR_INODE_ID)
		if err != nil {
			panic(err)
		}
		dirInfo.DeleteAfterVersions = 1
		if err := eggs.SetDirectoryInfo(&eggs.LogToStdout{}, client, msgs.ROOT_DIR_INODE_ID, false, dirInfo); err != nil {
			panic(err)
		}
		err = eggs.CollectDirectories(&eggs.LogToStdout{}, shid)
		if err != nil {
			panic(err)
		}
		err = eggs.DestructFiles(&eggs.LogToStdout{}, shid, blockServicesKeys)
		if err != nil {
			panic(err)
		}

		terminateChan <- nil
	}()
}

func main() {
	valgrind := flag.Bool("valgrind", false, "Whether to build for and run with valgrind.")
	sanitize := flag.Bool("sanitize", false, "Whether to build with sanitize.")
	debug := flag.Bool("debug", false, "Whether to build without optimizations.")
	verbose := flag.Bool("verbose", false, "Note that verbose won't do much for the shard unless you build with debug.")
	dataDir := flag.String("data-dir", "", "Directory where to store the EggsFS data. If not present a temporary directory will be used.")
	preserveDbDir := flag.Bool("preserve-data-dir", false, "Whether to preserve the temp data dir (if we're using a temp data dir).")
	coverage := flag.Bool("coverage", false, "Whether to build with coverage support. Right now applies only to the C++ shard code.")
	flag.Parse()

	if *verbose && !*debug {
		panic("You asked me to build without -debug, and with -verbose-shard. This is almost certainly wrong.")
	}

	cppBuildOpts := eggs.BuildCppOpts{
		Valgrind: *valgrind,
		Sanitize: *sanitize,
		Debug:    *debug,
		Coverage: *coverage,
	}
	shardExe := eggs.BuildShardExe(&eggs.LogToStdout{}, &cppBuildOpts)
	shuckleExe := eggs.BuildShuckleExe(&eggs.LogToStdout{})

	cleanupDbDir := false
	tmpDataDir := *dataDir == ""
	if tmpDataDir {
		cleanupDbDir = !*preserveDbDir
		dir, err := os.MkdirTemp("", "eggs-integrationtest.")
		if err != nil {
			panic(fmt.Errorf("could not create tmp data dir: %w", err))
		}
		*dataDir = dir
		fmt.Printf("running with temp data dir %v\n", *dataDir)
	}
	defer func() {
		if cleanupDbDir {
			fmt.Printf("cleaning up temp data dir %v\n", *dataDir)
			os.RemoveAll(*dataDir)
		} else if tmpDataDir {
			fmt.Printf("preserved temp data dir %v\n", *dataDir)
		}
	}()

	terminateChan := make(chan any, 1)

	procs := eggs.NewManagedProcesses(terminateChan)
	defer procs.Close()

	// Start shuckle
	shucklePort := uint16(39999)
	procs.StartShuckle(&eggs.ShuckleOpts{
		Exe:     shuckleExe,
		Port:    shucklePort,
		Verbose: *verbose,
		Dir:     path.Join(*dataDir, "shuckle"),
	})

	// Start block services. Right now this is just used for shuckle to
	// get block services, since we don't actually write to the block services
	// yet (it's just too slow).
	hddBlockServices := 10
	flashBlockServices := 5
	for i := 0; i < hddBlockServices+flashBlockServices; i++ {
		storageClass := "HDD"
		if i >= hddBlockServices {
			storageClass = "FLASH"
		}
		procs.StartBlockService(&eggs.BlockServiceOpts{
			Path:          path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
			Port:          40000 + uint16(i),
			StorageClass:  storageClass,
			FailureDomain: fmt.Sprintf("%d", i),
			Verbose:       *verbose,
			ShuckleHost:   fmt.Sprintf("localhost:%d", shucklePort),
		})
	}

	blockServices := eggs.WaitForShuckle(fmt.Sprintf("localhost:%v", shucklePort), hddBlockServices+flashBlockServices, 10*time.Second)

	// Start shard
	procs.StartShard(&eggs.ShardOpts{
		Exe:            shardExe,
		Dir:            path.Join(*dataDir, "shard"),
		Verbose:        *verbose,
		Shid:           shid,
		Valgrind:       *valgrind,
		WaitForShuckle: true,
	})

	eggs.WaitForShard(shid, 10*time.Second)

	spawnTests(terminateChan, blockServices)

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		cleanupDbDir = false
		panic(err)
	}
}
