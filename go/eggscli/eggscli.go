package main

import (
	"bytes"
	"crypto/aes"
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type commandSpec struct {
	flags *flag.FlagSet
	run   func()
}

var commands map[string]commandSpec

func noRunawayArgs(flag *flag.FlagSet) {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func usage() {
	commandsStrs := []string{}
	for c := range commands {
		commandsStrs = append(commandsStrs, c)
	}
	fmt.Fprintf(os.Stderr, "Usage: %v %v\n\n", os.Args[0], strings.Join(commandsStrs, "|"))
	fmt.Fprintf(os.Stderr, "Global options:\n\n")
	flag.PrintDefaults()
}

type fileSizesReq struct {
	id   msgs.InodeId
	path string
}

type fileSizesEnv struct {
	wg            sync.WaitGroup
	chans         []chan fileSizesReq
	examinedDirs  uint64
	examinedFiles uint64
}

func (env *fileSizesEnv) sendReq(log *lib.Logger, req *fileSizesReq) {
	env.wg.Add(1)
	env.chans[req.id.Shard()] <- *req
	if req.id.Type() == msgs.DIRECTORY {
		if atomic.AddUint64(&env.examinedDirs, 1)%1000000 == 0 {
			log.Info("examined %v dirs, %v files", env.examinedDirs, env.examinedFiles)
		}
	} else {
		if atomic.AddUint64(&env.examinedFiles, 1)%1000000 == 0 {
			log.Info("examined %v dirs, %v files", env.examinedDirs, env.examinedFiles)
		}
	}
}

func (env *fileSizesEnv) dir(log *lib.Logger, client *lib.Client, id msgs.InodeId, path string) {
	readReq := &msgs.ReadDirReq{
		DirId: id,
	}
	readResp := &msgs.ReadDirResp{}
	for {
		if err := client.ShardRequest(log, id.Shard(), readReq, readResp); err != nil {
			log.ErrorNoAlert("failed to read dir %v: %v", id, err)
			return
		}
		for _, e := range readResp.Results {
			newPath := path + "/" + e.Name
			if e.TargetId.Shard() != id.Shard() {
				env.sendReq(log, &fileSizesReq{
					id:   e.TargetId,
					path: newPath,
				})
			} else {
				if e.TargetId.Type() == msgs.DIRECTORY {
					env.dir(log, client, e.TargetId, newPath)
				} else {
					env.file(log, client, e.TargetId, newPath)
				}
			}
		}
		if readResp.NextHash == 0 {
			break
		}
		readReq.StartHash = readResp.NextHash
	}
}

func (env *fileSizesEnv) file(log *lib.Logger, client *lib.Client, id msgs.InodeId, path string) {
	logicalSize := uint64(0)
	physicalSize := uint64(0)
	spansReq := &msgs.FileSpansReq{
		FileId: id,
	}
	spansResp := &msgs.FileSpansResp{}
	for {
		if err := client.ShardRequest(log, id.Shard(), spansReq, spansResp); err != nil {
			log.ErrorNoAlert("could not read spans for %v: %v", id, err)
			return
		}
		for _, s := range spansResp.Spans {
			logicalSize += uint64(s.Header.Size)
			if s.Header.StorageClass != msgs.INLINE_STORAGE {
				body := s.Body.(*msgs.FetchedBlocksSpan)
				physicalSize += uint64(body.CellSize) * uint64(body.Parity.Blocks()) * uint64(body.Stripes)
			}
		}
		if spansResp.NextOffset == 0 {
			break
		}
		spansReq.ByteOffset = spansResp.NextOffset
	}
	fmt.Printf("%v,%q,%v,%v\n", id, path, logicalSize, physicalSize)
}

func outputFileSizes(log *lib.Logger, shuckleAddress string) {
	// never timeout
	timeout := lib.NewReqTimeouts(lib.DefaultShardTimeout.Initial, lib.DefaultShardTimeout.Max, 0, lib.DefaultShardTimeout.Growth, lib.DefaultShardTimeout.Jitter)
	// max mtu
	lib.SetMTU(msgs.MAX_UDP_MTU)
	// compute
	env := fileSizesEnv{
		chans: make([]chan fileSizesReq, 256),
	}
	for i := 0; i < 256; i++ {
		env.chans[i] = make(chan fileSizesReq, 100000)
	}
	for i := 0; i < 256; i++ {
		ch := env.chans[i]
		client, err := lib.NewClient(log, timeout, shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		go func() {
			for {
				req := <-ch
				if req.id.Type() == msgs.DIRECTORY {
					env.dir(log, client, req.id, req.path)
				} else {
					env.file(log, client, req.id, req.path)
				}
				env.wg.Done()
			}
		}()
	}
	env.sendReq(log, &fileSizesReq{
		id:   msgs.ROOT_DIR_INODE_ID,
		path: "",
	})
	env.wg.Wait()
	log.Info("examined %v dirs, %v files", env.examinedDirs, env.examinedFiles)
}

func main() {
	flag.Usage = usage
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")

	var log *lib.Logger

	commands = make(map[string]commandSpec)

	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC. If not present, they'll all be collected.")
	collectRun := func() {
		dirInfoCache := lib.NewDirInfoCache()
		if *collectDirIdU64 == 0 {
			if err := lib.CollectDirectoriesInAllShards(log, &lib.GCOptions{ShuckleAddress: *shuckleAddress}, dirInfoCache); err != nil {
				panic(err)
			}
		} else {
			dirId := msgs.InodeId(*collectDirIdU64)
			if dirId.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a directory", dirId))
			}
			client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
			if err != nil {
				panic(fmt.Errorf("could not create shard client: %v", err))
			}
			defer client.Close()
			stats := lib.CollectStats{}
			if err := lib.CollectDirectory(log, client, dirInfoCache, &stats, dirId); err != nil {
				panic(fmt.Errorf("could not collect %v, stats: %+v, err: %v", dirId, stats, err))
			}
			log.Info("finished collecting %v, stats: %+v", dirId, stats)
		}
	}
	commands["collect"] = commandSpec{
		flags: collectCmd,
		run:   collectRun,
	}

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructFileIdU64 := destructCmd.Uint64("file", 0, "Transient file id to destruct. If not present, they'll all be destructed.")
	destructFileCookieU64 := destructCmd.Uint64("cookie", 0, "Transient file cookie. Must be present if file is specified.")
	destructRun := func() {
		if *destructFileIdU64 == 0 {
			if err := lib.DestructFilesInAllShards(log, &lib.GCOptions{ShuckleAddress: *shuckleAddress}); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a file/symlink", fileId))
			}
			client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
			if err != nil {
				panic(err)
			}
			defer client.Close()
			stats := lib.DestructionStats{}
			var destructFileCookie [8]byte
			binary.LittleEndian.PutUint64(destructFileCookie[:], *destructFileCookieU64)
			err = lib.DestructFile(log, client, &stats, fileId, 0, destructFileCookie)
			if err != nil {
				panic(fmt.Errorf("could not destruct %v, stats: %+v, err: %v", fileId, stats, err))
			}
			log.Info("finished destructing %v, stats: %+v", fileId, stats)
		}
	}
	commands["destruct"] = commandSpec{
		flags: destructCmd,
		run:   destructRun,
	}

	migrateCmd := flag.NewFlagSet("migrate", flag.ExitOnError)
	migrateBlockService := migrateCmd.Uint64("bs", 0, "Block service to migrate from.")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "File in which to migrate blocks. If not present, all files will be migrated.")
	migrateShard := migrateCmd.Int("shard", -1, "Shard to migrate into. If not present, all shards will be migrated")
	migrateRun := func() {
		if *migrateBlockService == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		blockServiceId := msgs.BlockServiceId(*migrateBlockService)
		stats := lib.MigrateStats{}
		if *migrateShard != -1 && *migrateFileIdU64 != 0 {
			fmt.Fprintf(os.Stderr, "You passed in both -shard and -file, not sure what to do.\n")
			os.Exit(2)
		}
		if *migrateShard > 255 {
			fmt.Fprintf(os.Stderr, "Invalid shard %v.\n", *migrateShard)
			os.Exit(2)
		}
		if *migrateFileIdU64 == 0 && *migrateShard < 0 {
			client, err := lib.NewClient(log, nil, *shuckleAddress, 256)
			if err != nil {
				panic(err)
			}
			if err := lib.MigrateBlocksInAllShards(log, client, &stats, blockServiceId); err != nil {
				panic(err)
			}
		} else if *migrateFileIdU64 != 0 {
			fileId := msgs.InodeId(*migrateFileIdU64)
			client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
			if err != nil {
				panic(fmt.Errorf("could not create shard socket: %v", err))
			}
			defer client.Close()
			stats := lib.MigrateStats{}
			if err := lib.MigrateBlocksInFile(log, client, &stats, blockServiceId, fileId); err != nil {
				panic(fmt.Errorf("error while migrating file %v away from block service %v: %v", fileId, blockServiceId, err))
			}
			log.Info("finished migrating %v away from block service %v, stats: %+v", fileId, blockServiceId, stats)
		} else {
			shid := msgs.ShardId(*migrateShard)
			client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
			if err != nil {
				panic(err)
			}
			if err := lib.MigrateBlocks(log, client, &stats, shid, blockServiceId); err != nil {
				panic(err)
			}
		}
	}
	commands["migrate"] = commandSpec{
		flags: migrateCmd,
		run:   migrateRun,
	}

	shardReqCmd := flag.NewFlagSet("shard-req", flag.ExitOnError)
	shardReqShard := shardReqCmd.Uint("shard", 0, "Shard to send the req too")
	shardReqKind := shardReqCmd.String("kind", "", "")
	shardReqReq := shardReqCmd.String("req", "", "Request body, in JSON")
	shardReqRun := func() {
		req, resp, err := msgs.MkShardMessage(*shardReqKind)
		if err != nil {
			panic(err)
		}
		if err := json.Unmarshal([]byte(*shardReqReq), &req); err != nil {
			panic(fmt.Errorf("could not decode shard req: %w", err))
		}
		shard := msgs.ShardId(*shardReqShard)
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		if err := client.ShardRequest(log, shard, req, resp); err != nil {
			panic(err)
		}
		out, err := json.MarshalIndent(resp, "", "  ")
		if err != nil {
			panic(fmt.Errorf("could not encode response %+v to json: %w", resp, err))
		}
		os.Stdout.Write(out)
		fmt.Println()
	}
	commands["shard-req"] = commandSpec{
		flags: shardReqCmd,
		run:   shardReqRun,
	}

	cdcReqCmd := flag.NewFlagSet("cdc-req", flag.ExitOnError)
	cdcReqKind := cdcReqCmd.String("kind", "", "")
	cdcReqReq := cdcReqCmd.String("req", "", "Request body, in JSON")
	cdcReqRun := func() {
		req, resp, err := msgs.MkCDCMessage(*cdcReqKind)
		if err != nil {
			panic(err)
		}
		if err := json.Unmarshal([]byte(*cdcReqReq), &req); err != nil {
			panic(fmt.Errorf("could not decode cdc req: %w", err))
		}
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		if err := client.CDCRequest(log, req, resp); err != nil {
			panic(err)
		}
		out, err := json.MarshalIndent(resp, "", "  ")
		if err != nil {
			panic(fmt.Errorf("could not encode response %+v to json: %w", resp, err))
		}
		os.Stdout.Write(out)
		fmt.Println()
	}
	commands["cdc-req"] = commandSpec{
		flags: cdcReqCmd,
		run:   cdcReqRun,
	}

	setDirInfoCmd := flag.NewFlagSet("set-dir-info", flag.ExitOnError)
	setDirInfoIdU64 := setDirInfoCmd.Uint64("id", 0, "InodeId for the directory to set the policy of.")
	setDirInfoIdTag := setDirInfoCmd.String("tag", "", "One of SNAPSHOT|SPAN|BLOCK")
	setDirInfoPolicy := setDirInfoCmd.String("body", "", "Policy, in JSON")
	setDirInfoRun := func() {
		entry := msgs.TagToDirInfoEntry(msgs.DirInfoTagFromName(*setDirInfoIdTag))
		if err := json.Unmarshal([]byte(*setDirInfoPolicy), entry); err != nil {
			panic(fmt.Errorf("could not decode directory info: %w", err))
		}
		id := msgs.InodeId(*setDirInfoIdU64)
		fmt.Printf("Will set dir info %v to directory %v:\n", entry.Tag(), id)
		fmt.Printf("%+v\n", entry)
		for {
			var action string
			fmt.Printf("Proceed? y/n ")
			fmt.Scanln(&action)
			if action == "y" {
				break
			}
			if action == "n" {
				fmt.Printf("BYE\n")
				os.Exit(0)
			}
		}
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		if err := client.MergeDirectoryInfo(log, id, entry); err != nil {
			panic(err)
		}
	}
	commands["set-dir-info"] = commandSpec{
		flags: setDirInfoCmd,
		run:   setDirInfoRun,
	}

	removeDirInfoCmd := flag.NewFlagSet("remove-dir-info", flag.ExitOnError)
	removeDirInfoU64 := removeDirInfoCmd.Uint64("id", 0, "InodeId for the directory to unset the policy of.")
	removeDirInfoTag := removeDirInfoCmd.String("tag", "", "One of SNAPSHOT|SPAN|BLOCK")
	removeDirInfoRun := func() {
		id := msgs.InodeId(*removeDirInfoU64)
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		if err := client.RemoveDirectoryInfoEntry(log, id, msgs.DirInfoTagFromName(*removeDirInfoTag)); err != nil {
			panic(err)
		}
	}
	commands["remove-dir-info"] = commandSpec{
		flags: removeDirInfoCmd,
		run:   removeDirInfoRun,
	}

	cpIntoCmd := flag.NewFlagSet("cp-into", flag.ExitOnError)
	cpIntoInput := cpIntoCmd.String("i", "", "What to copy, if empty stdin.")
	cpIntoOut := cpIntoCmd.String("o", "", "Where to write the file to in Eggs")
	cpIntoRun := func() {
		path := filepath.Clean("/" + *cpIntoOut)
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		var input io.Reader
		if *cpIntoInput == "" {
			input = os.Stdin
		} else {
			var err error
			input, err = os.Open(*cpIntoInput)
			if err != nil {
				panic(err)
			}
		}
		bufPool := lib.NewBufPool()
		fileId, err := client.CreateFile(log, bufPool, lib.NewDirInfoCache(), path, input)
		if err != nil {
			panic(err)
		}
		log.Info("File created as %v", fileId)
	}
	commands["cp-into"] = commandSpec{
		flags: cpIntoCmd,
		run:   cpIntoRun,
	}

	cpOutofCmd := flag.NewFlagSet("cp-outof", flag.ExitOnError)
	cpOutofInput := cpOutofCmd.String("i", "", "What to copy from eggs.")
	cpOutofOut := cpOutofCmd.String("o", "", "Where to write the file to. Stdout if empty.")
	cpOutofRun := func() {
		out := os.Stdout
		if *cpOutofOut != "" {
			var err error
			out, err = os.Create(*cpOutofOut)
			if err != nil {
				panic(err)
			}
		}
		client, err := lib.NewClient(log, nil, *shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		id, err := client.ResolvePath(log, *cpOutofInput)
		if err != nil {
			panic(err)
		}
		bufPool := lib.NewBufPool()
		r, err := client.ReadFile(log, bufPool, id)
		if err != nil {
			panic(err)
		}
		if _, err := out.ReadFrom(r); err != nil {
			panic(err)
		}
	}
	commands["cp-outof"] = commandSpec{
		flags: cpOutofCmd,
		run:   cpOutofRun,
	}

	blockReqCmd := flag.NewFlagSet("write-block-req", flag.ExitOnError)
	blockReqBlockId := blockReqCmd.Uint64("b", 0, "Block id")
	blockReqBlockService := blockReqCmd.Uint64("bs", 0, "Block service")
	blockReqFile := blockReqCmd.String("file", "", "")
	blockReqRun := func() {
		resp, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			panic(err)
		}
		blockServices := resp.(*msgs.AllBlockServicesResp)
		var blockServiceInfo msgs.BlockServiceInfo
		for _, bsInfo := range blockServices.BlockServices {
			if bsInfo.Id == msgs.BlockServiceId(*blockReqBlockService) {
				blockServiceInfo = bsInfo
				break
			}
		}
		cipher, err := aes.NewCipher(blockServiceInfo.SecretKey[:])
		if err != nil {
			panic(err)
		}
		fileContents, err := ioutil.ReadFile(*blockReqFile)
		if err != nil {
			panic(err)
		}
		req := msgs.WriteBlockReq{
			BlockId: msgs.BlockId(*blockReqBlockId),
			Crc:     msgs.Crc(crc32c.Sum(0, fileContents)),
			Size:    uint32(len(fileContents)),
		}
		req.Certificate = lib.BlockWriteCertificate(cipher, blockServiceInfo.Id, &req)
		log.Info("request: %+v", req)
	}
	commands["write-block-req"] = commandSpec{
		flags: blockReqCmd,
		run:   blockReqRun,
	}

	testBlockWriteCmd := flag.NewFlagSet("test-block-write", flag.ExitOnError)
	testBlockWriteBlockService := testBlockWriteCmd.String("bs", "", "Block service. If comma-separated, they'll be written in parallel to the specified ones.")
	testBlockWriteSize := testBlockWriteCmd.Uint("size", 0, "Size (must fit in u32)")
	testBlockWriteRun := func() {
		resp, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			panic(err)
		}
		blockServices := resp.(*msgs.AllBlockServicesResp)
		bsInfos := []msgs.BlockServiceInfo{}
		for _, str := range strings.Split(*testBlockWriteBlockService, ",") {
			bsId, err := strconv.ParseUint(str, 0, 64)
			if err != nil {
				panic(err)
			}
			found := false
			for _, bsInfo := range blockServices.BlockServices {
				if bsInfo.Id == msgs.BlockServiceId(bsId) {
					bsInfos = append(bsInfos, bsInfo)
					found = true
					break
				}
			}
			if !found {
				panic(fmt.Errorf("could not find block service %q", str))
			}
		}
		conns := make([]*net.TCPConn, len(bsInfos))
		for i := 0; i < len(conns); i++ {
			conn, err := lib.BlockServiceConnection(log, bsInfos[i].Ip1, bsInfos[i].Port1, bsInfos[i].Ip2, bsInfos[i].Port2)
			if err != nil {
				panic(err)
			}
			conns[i] = conn
		}
		contents := make([]byte, *testBlockWriteSize)
		var wait sync.WaitGroup
		wait.Add(len(conns))
		t := time.Now()
		for i := 0; i < len(conns); i++ {
			conn := conns[i]
			bsId := bsInfos[i].Id
			go func() {
				thisErr := lib.TestWrite(log, conn, bsId, bytes.NewReader(contents), uint64(len(contents)))
				if thisErr != nil {
					err = thisErr
				}
				wait.Done()
			}()
		}
		wait.Wait()
		elapsed := time.Since(t)
		if err != nil {
			panic(err)
		}
		log.Info("writing %v bytes to %v block services took %v (%fGB/s)", *testBlockWriteSize, len(conns), time.Since(t), (float64(*testBlockWriteSize*uint(len(conns)))/1e9)/elapsed.Seconds())
	}
	commands["test-block-write"] = commandSpec{
		flags: testBlockWriteCmd,
		run:   testBlockWriteRun,
	}

	blockserviceFlagsCmd := flag.NewFlagSet("blockservice-flags", flag.ExitOnError)
	blockserviceFlagsId := blockserviceFlagsCmd.Int64("id", 0, "Block service id")
	blockserviceFlagsSet := blockserviceFlagsCmd.String("set", "", "Flag to set")
	blockserviceFlagsUnset := blockserviceFlagsCmd.String("unset", "", "Flag to unset")
	blockserviceFlagsRun := func() {
		if *blockserviceFlagsSet != "" && *blockserviceFlagsUnset != "" {
			fmt.Fprintf(os.Stderr, "cannot use -set and -unset at the same time\n")
			os.Exit(2)
		}
		var flag msgs.BlockServiceFlags
		var mask uint8
		if *blockserviceFlagsSet != "" {
			var err error
			flag, err = msgs.BlockServiceFlagFromName(*blockserviceFlagsSet)
			if err != nil {
				panic(err)
			}
			mask = uint8(flag)
		}
		if *blockserviceFlagsSet != "" {
			flagMask, err := msgs.BlockServiceFlagFromName(*blockserviceFlagsSet)
			if err != nil {
				panic(err)
			}
			mask = uint8(flagMask)
		}
		_, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.SetBlockServiceFlagsReq{
			Id:        msgs.BlockServiceId(*blockserviceFlagsId),
			Flags:     flag,
			FlagsMask: mask,
		})
		if err != nil {
			panic(err)
		}
	}
	commands["blockservice-flags"] = commandSpec{
		flags: blockserviceFlagsCmd,
		run:   blockserviceFlagsRun,
	}

	fileSizesCmd := flag.NewFlagSet("file-sizes", flag.ExitOnError)
	fileSizesRun := func() { outputFileSizes(log, *shuckleAddress) }
	commands["file-sizes"] = commandSpec{
		flags: fileSizesCmd,
		run:   fileSizesRun,
	}

	flag.Parse()

	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "No command provided.\n\n")
		flag.Usage()
		os.Exit(2)
	}

	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log = lib.NewLogger(os.Stderr, &lib.LoggerOptions{Level: level})

	spec, found := commands[flag.Args()[0]]
	if !found {
		fmt.Fprintf(os.Stderr, "Bad subcommand %v provided.\n\n", flag.Args()[0])
		flag.Usage()
		os.Exit(2)
	}
	spec.flags.Parse(flag.Args()[1:])
	noRunawayArgs(spec.flags)
	spec.run()
}
