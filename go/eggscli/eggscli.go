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

func outputFullFileSizes(log *lib.Logger, client *lib.Client) {
	var examinedDirs uint64
	var examinedFiles uint64
	err := lib.Parwalk(
		log,
		client,
		"/",
		func(parent msgs.InodeId, id msgs.InodeId, path string) error {
			if id.Type() == msgs.DIRECTORY {
				if atomic.AddUint64(&examinedDirs, 1)%1000000 == 0 {
					log.Info("examined %v dirs, %v files", examinedDirs, examinedFiles)
				}
			} else {
				if atomic.AddUint64(&examinedFiles, 1)%1000000 == 0 {
					log.Info("examined %v dirs, %v files", examinedDirs, examinedFiles)
				}
			}

			if id.Type() == msgs.DIRECTORY {
				return nil
			}

			logicalSize := uint64(0)
			physicalSize := uint64(0)
			spansReq := &msgs.FileSpansReq{
				FileId: id,
			}
			spansResp := &msgs.FileSpansResp{}
			for {
				if err := client.ShardRequest(log, id.Shard(), spansReq, spansResp); err != nil {
					log.ErrorNoAlert("could not read spans for %v: %v", id, err)
					return err
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
			return nil
		},
	)
	if err != nil {
		panic(err)
	}
}

func outputBriefFileSizes(log *lib.Logger, client *lib.Client) {
	// histogram
	histoBins := 256
	histo := lib.NewHistogram(histoBins, 1024, 1.1)
	var histoSizes [256][]uint64
	var wg sync.WaitGroup
	wg.Add(256)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		histoSizes[i] = make([]uint64, histoBins)
		go func() {
			filesReq := msgs.VisitFilesReq{}
			filesResp := msgs.VisitFilesResp{}
			for {
				if err := client.ShardRequest(log, shid, &filesReq, &filesResp); err != nil {
					log.ErrorNoAlert("could not get files in shard %v: %v, terminating this shard, results will be incomplete", shid, err)
					return
				}
				for _, file := range filesResp.Ids {
					statResp := msgs.StatFileResp{}
					if err := client.ShardRequest(log, shid, &msgs.StatFileReq{Id: file}, &statResp); err != nil {
						log.ErrorNoAlert("could not stat file %v in shard %v: %v, results will be incomplete", file, shid, err)
						continue
					}
					bin := histo.WhichBin(statResp.Size)
					histoSizes[shid][bin] += statResp.Size
				}
				if filesResp.NextId == msgs.NULL_INODE_ID {
					log.Info("finished with shard %v", shid)
					break
				}
				filesReq.BeginId = filesResp.NextId
			}
			wg.Done()
		}()
	}
	wg.Wait()
	log.Info("finished with all shards, will now output")
	for i, upperBound := range histo.Bins() {
		size := uint64(0)
		for j := 0; j < 256; j++ {
			size += histoSizes[j][i]
		}
		fmt.Printf("%v,%v\n", upperBound, size)
	}
}

func formatSize(bytes uint64) string {
	bytesf := float64(bytes)
	if bytes == 0 {
		return "0"
	}
	if bytes < 1e6 {
		return fmt.Sprintf("%.2fKB", bytesf/1e3)
	}
	if bytes < 1e9 {
		return fmt.Sprintf("%.2fMB", bytesf/1e6)
	}
	if bytes < 1e12 {
		return fmt.Sprintf("%.2fGB", bytesf/1e9)
	}
	if bytes < 1e15 {
		return fmt.Sprintf("%.2fTB", bytesf/1e12)
	}
	return fmt.Sprintf("%.2fPB", bytesf/1e15)
}

func main() {
	flag.Usage = usage
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	mtu := flag.String("mtu", "", "MTU to use, either an integer or \"max\"")
	shardInitialTimeout := flag.Duration("shard-initial-timeout", 0, "")
	shardMaxTimeout := flag.Duration("shard-max-timeout", 0, "")
	shardOverallTimeout := flag.Duration("shard-overall-timeout", -1, "")
	cdcInitialTimeout := flag.Duration("cdc-initial-timeout", 0, "")
	cdcMaxTimeout := flag.Duration("cdc-max-timeout", 0, "")
	cdcOverallTimeout := flag.Duration("cdc-overall-timeout", -1, "")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")

	var log *lib.Logger
	var client *lib.Client

	if *mtu != "" {
		if *mtu == "max" {
			lib.SetMTU(msgs.MAX_UDP_MTU)
		} else {
			mtuU, err := strconv.ParseUint(*mtu, 0, 16)
			if err != nil {
				fmt.Fprintf(os.Stderr, "could not parse mtu: %v", err)
				os.Exit(2)
			}
			lib.SetMTU(mtuU)
		}
	}

	commands = make(map[string]commandSpec)

	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC. If not present, they'll all be collected.")
	collectRun := func() {
		dirInfoCache := lib.NewDirInfoCache()
		if *collectDirIdU64 == 0 {
			if err := lib.CollectDirectoriesInAllShards(log, client, dirInfoCache); err != nil {
				panic(err)
			}
		} else {
			dirId := msgs.InodeId(*collectDirIdU64)
			if dirId.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a directory", dirId))
			}
			defer client.Close()
			var stats lib.CollectDirectoriesStats
			var cdcMu sync.Mutex
			if err := lib.CollectDirectory(log, client, dirInfoCache, &cdcMu, &stats, dirId); err != nil {
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
			if err := lib.DestructFilesInAllShards(log, client, &lib.GCOptions{}); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a file/symlink", fileId))
			}
			stats := lib.DestructFilesStats{}
			var destructFileCookie [8]byte
			binary.LittleEndian.PutUint64(destructFileCookie[:], *destructFileCookieU64)
			if err := lib.DestructFile(log, client, &stats, fileId, 0, destructFileCookie); err != nil {
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
	migrateId := migrateCmd.Int64("id", 0, "Block service id")
	migrateFailureDomain := migrateCmd.String("failure-domain", "", "Failure domain -- if this is used all block services in a given failure domain will be affected.")
	migrateFailureFlagStr := migrateCmd.String("flags", "0", "All block services with the given flags will be included. Use with care! Basically only really useful with DECOMMISSIONED.")
	migrateFailureNoFlagStr := migrateCmd.String("no-flags", "0", "Block services with the given flags will be excluded.")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "File in which to migrate blocks. If not present, all files will be migrated.")
	migrateShard := migrateCmd.Int("shard", -1, "Shard to migrate into. If not present, all shards will be migrated")
	migrateRun := func() {
		yesFlags, err := msgs.BlockServiceFlagsFromUnion(*migrateFailureFlagStr)
		if err != nil {
			panic(err)
		}
		noFlags, err := msgs.BlockServiceFlagsFromUnion(*migrateFailureNoFlagStr)
		if err != nil {
			panic(err)
		}
		if yesFlags&noFlags != 0 {
			fmt.Fprintf(os.Stderr, "Can't provide the same flag both in -flags and -no-flags\n")
			os.Exit(2)
		}
		log.Info("requesting block services")
		blockServicesResp, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			panic(err)
		}
		blockServices := blockServicesResp.(*msgs.AllBlockServicesResp)
		blockServicesToMigrate := make(map[string]*[]msgs.BlockServiceId) // by failure domain
		numBlockServicesToMigrate := 0
		for _, bs := range blockServices.BlockServices {
			if bs.Id == msgs.BlockServiceId(*migrateId) || bs.FailureDomain.String() == *migrateFailureDomain || (bs.Flags&yesFlags != 0 && bs.Flags&noFlags == 0) {
				numBlockServicesToMigrate++
				bss := blockServicesToMigrate[bs.FailureDomain.String()]
				if bss == nil {
					bss = &[]msgs.BlockServiceId{}
					blockServicesToMigrate[bs.FailureDomain.String()] = bss
				}
				*bss = append(*bss, bs.Id)
			}
		}
		if len(blockServicesToMigrate) == 0 {
			panic(fmt.Errorf("could not get any block service ids with failure domain %v, id %v, yes flags %v, no flags %v", migrateFailureDomain, msgs.BlockServiceId(*migrateId), yesFlags, noFlags))
		}
		if *migrateShard != -1 && *migrateFileIdU64 != 0 {
			fmt.Fprintf(os.Stderr, "You passed in both -shard and -file, not sure what to do.\n")
			os.Exit(2)
		}
		if *migrateShard > 255 {
			fmt.Fprintf(os.Stderr, "Invalid shard %v.\n", *migrateShard)
			os.Exit(2)
		}
		log.Info("will migrate in %v block services:", numBlockServicesToMigrate)
		for failureDomain, bss := range blockServicesToMigrate {
			for _, blockServiceId := range *bss {
				log.Info("%v, %v", failureDomain, blockServiceId)
			}
		}
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
		stats := lib.MigrateStats{}
		for failureDomain, bss := range blockServicesToMigrate {
			for _, blockServiceId := range *bss {
				log.Info("migrating block service %v, %v", blockServiceId, failureDomain)
				if *migrateFileIdU64 == 0 && *migrateShard < 0 {
					if err := lib.MigrateBlocksInAllShards(log, client, &stats, blockServiceId); err != nil {
						panic(err)
					}
				} else if *migrateFileIdU64 != 0 {
					fileId := msgs.InodeId(*migrateFileIdU64)
					if err := lib.MigrateBlocksInFile(log, client, &stats, blockServiceId, fileId); err != nil {
						panic(fmt.Errorf("error while migrating file %v away from block service %v: %v", fileId, blockServiceId, err))
					}
				} else {
					shid := msgs.ShardId(*migrateShard)
					if err := lib.MigrateBlocks(log, client, &stats, shid, blockServiceId); err != nil {
						panic(err)
					}
				}
				log.Info("finished migrating blocks away from block service %v, stats so far: %+v", blockServiceId, stats)
			}
		}
		log.Info("finished migrating away from all block services, stats: %+v", stats)
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
		fmt.Printf("Will send this CDC request: %T %+v\n", req, req)
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
	blockserviceFlagsFailureDomain := blockserviceFlagsCmd.String("failure-domain", "", "Failure domain -- if this is used all block services in a given failure domain will be affected.")
	blockserviceFlagsSet := blockserviceFlagsCmd.String("set", "", "Flag to set")
	blockserviceFlagsUnset := blockserviceFlagsCmd.String("unset", "", "Flag to unset")
	blockserviceFlagsRun := func() {
		if *blockserviceFlagsSet != "" && *blockserviceFlagsUnset != "" {
			fmt.Fprintf(os.Stderr, "cannot use -set and -unset at the same time\n")
			os.Exit(2)
		}
		if *blockserviceFlagsId != 0 && *blockserviceFlagsFailureDomain != "" {
			fmt.Fprintf(os.Stderr, "cannot use -id and -failure-domain at the same time\n")
			os.Exit(2)
		}
		if *blockserviceFlagsId == 0 && *blockserviceFlagsFailureDomain == "" {
			fmt.Fprintf(os.Stderr, "must provide one of -id and -failure-domain\n")
			os.Exit(2)
		}
		blockServiceIds := []msgs.BlockServiceId{}
		if *blockserviceFlagsId != 0 {
			blockServiceIds = append(blockServiceIds, msgs.BlockServiceId(*blockserviceFlagsId))
		}
		if *blockserviceFlagsFailureDomain != "" {
			log.Info("requesting block services")
			blockServicesResp, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.AllBlockServicesReq{})
			if err != nil {
				panic(err)
			}
			blockServices := blockServicesResp.(*msgs.AllBlockServicesResp)
			for _, bs := range blockServices.BlockServices {
				if bs.FailureDomain.String() == *blockserviceFlagsFailureDomain {
					blockServiceIds = append(blockServiceIds, bs.Id)
				}
			}
			if len(blockServiceIds) == 0 {
				panic(fmt.Errorf("could not get any block service ids for failure domain %v", blockserviceFlagsFailureDomain))
			}
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
		if *blockserviceFlagsUnset != "" {
			flagMask, err := msgs.BlockServiceFlagFromName(*blockserviceFlagsUnset)
			if err != nil {
				panic(err)
			}
			mask = uint8(flagMask)
		}
		for _, bsId := range blockServiceIds {
			log.Info("setting flags %v with mask %v for block service %v", flag, msgs.BlockServiceFlags(mask), bsId)
			_, err := lib.ShuckleRequest(log, nil, *shuckleAddress, &msgs.SetBlockServiceFlagsReq{
				Id:        bsId,
				Flags:     flag,
				FlagsMask: mask,
			})
			if err != nil {
				panic(err)
			}
		}
	}
	commands["blockservice-flags"] = commandSpec{
		flags: blockserviceFlagsCmd,
		run:   blockserviceFlagsRun,
	}

	fileSizesCmd := flag.NewFlagSet("file-sizes", flag.ExitOnError)
	fileSizesBrief := fileSizesCmd.Bool("brief", false, "")
	fileSizesRun := func() {
		if *fileSizesBrief {
			outputBriefFileSizes(log, client)
		} else {
			outputFullFileSizes(log, client)
		}
	}
	commands["file-sizes"] = commandSpec{
		flags: fileSizesCmd,
		run:   fileSizesRun,
	}

	findBlockCmd := flag.NewFlagSet("find-block", flag.ExitOnError)
	findBlockId := findBlockCmd.Uint64("id", 0, "Block id to find")
	findBlockRun := func() {
		blockId := msgs.BlockId(*findBlockId)
		log.Info("looking for %v", blockId)
		ch := make(chan any)
		analyzedFiles := uint64(0)
		startedAt := time.Now()
		go func() {
			err := lib.Parwalk(
				log,
				client,
				"/",
				func(parent, id msgs.InodeId, path string) error {
					if id.Type() == msgs.DIRECTORY {
						return nil
					}
					req := msgs.FileSpansReq{
						FileId: id,
					}
					resp := msgs.FileSpansResp{}
					for {
						if err := client.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
							log.ErrorNoAlert("could not get spans for file %q, id %v", id, path)
							return err
						}
						for _, span := range resp.Spans {
							if span.Header.StorageClass == msgs.INLINE_STORAGE {
								continue
							}
							body := span.Body.(*msgs.FetchedBlocksSpan)
							for _, block := range body.Blocks {
								if block.BlockId == blockId {
									log.Info("block id %v is in file %q, id %v", blockId, path, id)
									ch <- nil // terminate
									return nil
								}
							}
						}
						req.ByteOffset = resp.NextOffset
						if req.ByteOffset == 0 {
							break
						}
					}
					if atomic.AddUint64(&analyzedFiles, 1)%uint64(1_000_000) == 0 {
						log.Info("went through %v files (%0.2f files/s)", analyzedFiles, float64(analyzedFiles)/float64(time.Since(startedAt).Seconds()))
					}
					return nil
				},
			)
			ch <- err
		}()
		err := <-ch
		if err != nil {
			log.ErrorNoAlert("could not find block %v: %v", blockId, err)
			panic(err)
		}
	}
	commands["find-block"] = commandSpec{
		flags: findBlockCmd,
		run:   findBlockRun,
	}

	countFilesCmd := flag.NewFlagSet("count-files", flag.ExitOnError)
	countFilesRun := func() {
		var wg sync.WaitGroup
		ch := make(chan any)
		wg.Add(256)
		var numFiles uint64
		var numReqs uint64
		startedAt := time.Now()
		for i := 0; i < 256; i++ {
			shid := msgs.ShardId(i)
			go func() {
				req := msgs.VisitFilesReq{}
				resp := msgs.VisitFilesResp{}
				for {
					if err := client.ShardRequest(log, shid, &req, &resp); err != nil {
						ch <- err
						return
					}
					atomic.AddUint64(&numFiles, uint64(len(resp.Ids)))
					if atomic.AddUint64(&numReqs, 1)%uint64(1_000_000) == 0 {
						log.Info("went through %v files, %v reqs (%0.2f files/s, %0.2f req/s)", numFiles, numReqs, float64(numFiles)/float64(time.Since(startedAt).Seconds()), float64(numReqs)/float64(time.Since(startedAt).Seconds()))
					}
					req.BeginId = resp.NextId
					if req.BeginId == 0 {
						break
					}
				}
				wg.Done()
			}()
		}
		go func() {
			wg.Wait()
			ch <- nil
		}()
		err := <-ch
		if err != nil {
			panic(err)
		}
		log.Info("found %v files", numFiles)
	}
	commands["count-files"] = commandSpec{
		flags: countFilesCmd,
		run:   countFilesRun,
	}

	duCmd := flag.NewFlagSet("du", flag.ExitOnError)
	duDir := duCmd.String("path", "/", "")
	duHisto := duCmd.String("histogram", "", "Filepath in which to write size histogram (in CSV) to")
	duRun := func() {
		var numFiles uint64
		var numDirectories uint64
		var totalSize uint64
		histoSizeBins := make([]uint64, 256)
		histoCountBins := make([]uint64, 256)
		histogram := lib.NewHistogram(len(histoSizeBins), 255, 1.15) // max: ~900PB
		startedAt := time.Now()
		err := lib.Parwalk(
			log,
			client,
			*duDir,
			func(parent, id msgs.InodeId, path string) error {
				if id.Type() == msgs.DIRECTORY {
					atomic.AddUint64(&numDirectories, 1)
					return nil
				}
				resp := msgs.StatFileResp{}
				if err := client.ShardRequest(log, id.Shard(), &msgs.StatFileReq{Id: id}, &resp); err != nil {
					return err
				}
				atomic.AddUint64(&totalSize, resp.Size)
				if atomic.AddUint64(&numFiles, 1)%uint64(1_000_000) == 0 {
					log.Info("went through %v files (%v, %0.2f files/s), %v directories", numFiles, formatSize(totalSize), float64(numFiles)/float64(time.Since(startedAt).Seconds()), numDirectories)
				}
				bin := histogram.WhichBin(resp.Size)
				atomic.AddUint64(&histoCountBins[bin], 1)
				atomic.AddUint64(&histoSizeBins[bin], resp.Size)
				return nil
			},
		)
		if err != nil {
			panic(err)
		}
		log.Info("total size: %v (%v bytes), in %v files and %v directories", formatSize(totalSize), totalSize, numFiles, numDirectories)
		if *duHisto != "" {
			log.Info("writing size histogram to %q", *duHisto)
			histoCsvBuf := bytes.NewBuffer([]byte{})
			fmt.Fprintf(histoCsvBuf, "upper_bound,file_count,total_size\n")
			for i, upperBound := range histogram.Bins() {
				fmt.Fprintf(histoCsvBuf, "%v,%v,%v\n", upperBound, histoCountBins[i], histoSizeBins[i])
			}
			if err := os.WriteFile(*duHisto, histoCsvBuf.Bytes(), 0644); err != nil {
				log.ErrorNoAlert("could not write histo file %q, will print histogram here: %v", *duHisto, err)
				fmt.Print(histoCsvBuf.Bytes())
				panic(err)
			}
		}
	}
	commands["du"] = commandSpec{
		flags: duCmd,
		run:   duRun,
	}

	scrubFileCmd := flag.NewFlagSet("scrub-file", flag.ExitOnError)
	scrubFileId := scrubFileCmd.Uint64("id", 0, "The file to scrub")
	scrubFileRun := func() {
		file := msgs.InodeId(*scrubFileId)
		stats := &lib.ScrubState{}
		if err := lib.ScrubFile(log, client, stats, file); err != nil {
			panic(err)
		}
		log.Info("scrub stats: %+v", stats)
	}
	commands["scrub-file"] = commandSpec{
		flags: scrubFileCmd,
		run:   scrubFileRun,
	}

	scrubCmd := flag.NewFlagSet("scrub", flag.ExitOnError)
	scrubRun := func() {
		shards := make([]msgs.ShardId, 256)
		for i := 0; i < 256; i++ {
			shards[i] = msgs.ShardId(i)
		}
		stats := lib.ScrubState{}
		if err := lib.ScrubFiles(log, client, &stats, true, shards); err != nil {
			panic(err)
		}
	}
	commands["scrub"] = commandSpec{
		flags: scrubCmd,
		run:   scrubRun,
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

	client, err := lib.NewClient(log, nil, *shuckleAddress)
	if err != nil {
		panic(fmt.Errorf("could not create client: %v", err))
	}
	defer client.Close()

	shardTimeouts := lib.DefaultShardTimeout
	printTimeouts := false
	if *shardInitialTimeout > 0 {
		printTimeouts = true
		shardTimeouts.Initial = *shardInitialTimeout
	}
	if *shardMaxTimeout > 0 {
		printTimeouts = true
		shardTimeouts.Max = *shardMaxTimeout
	}
	if *shardOverallTimeout >= 0 {
		printTimeouts = true
		shardTimeouts.Overall = *shardOverallTimeout
	}
	client.SetShardTimeouts(&shardTimeouts)
	cdcTimeouts := lib.DefaultCDCTimeout
	if *cdcInitialTimeout > 0 {
		printTimeouts = true
		cdcTimeouts.Initial = *cdcInitialTimeout
	}
	if *cdcMaxTimeout > 0 {
		printTimeouts = true
		cdcTimeouts.Max = *cdcMaxTimeout
	}
	if *cdcOverallTimeout >= 0 {
		printTimeouts = true
		cdcTimeouts.Overall = *cdcOverallTimeout
	}
	client.SetCDCTimeouts(&cdcTimeouts)
	if printTimeouts {
		log.Info("shard timeouts: %+v", shardTimeouts)
		log.Info("CDC timeouts: %+v", cdcTimeouts)
	}

	spec.run()
}
