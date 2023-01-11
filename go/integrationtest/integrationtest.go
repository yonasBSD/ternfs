package main

import (
	"encoding/hex"
	"flag"
	"fmt"
	"os"
	"path"
	"runtime/debug"
	"strings"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func handleRecover(log eggs.LogLevels, terminateChan chan any, err any) {
	if err != nil {
		log.RaiseAlert(err.(error))
		log.Info("PANIC %v. Stacktrace:\n", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			log.Info(line)
		}
		fmt.Print(string(debug.Stack()))
		terminateChan <- err
	}
}

func runTest(blockServicesKeys map[msgs.BlockServiceId][16]byte, what string, run func(stats *harnessStats)) {
	stats := harnessStats{}

	fmt.Printf("running %s\n", what)
	t0 := time.Now()
	run(&stats)
	elapsed := time.Since(t0)
	totalShardRequests := int64(0)
	totalShardNanos := int64(0)
	for i := 0; i < 256; i++ {
		totalShardRequests += stats.shardReqsCounts[i]
		totalShardNanos += stats.shardReqsNanos[i]
	}
	totalCDCRequests := int64(0)
	totalCDCNanos := int64(0)
	for i := 0; i < 256; i++ {
		totalCDCRequests += stats.cdcReqsCounts[i]
		totalCDCNanos += stats.cdcReqsNanos[i]
	}

	fmt.Printf("  ran test in %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		fmt.Printf("  shard reqs avg times:\n")
		for i := 0; i < 256; i++ {
			if stats.shardReqsCounts[i] == 0 {
				continue
			}
			fmt.Printf("    %-30v %10v %10v\n", msgs.ShardMessageKind(i), stats.shardReqsCounts[i], time.Duration(stats.shardReqsNanos[i]/stats.shardReqsCounts[i]))
		}
	}
	if totalCDCRequests > 0 {
		fmt.Printf("  CDC reqs avg times:\n")
		for i := 0; i < 256; i++ {
			if stats.cdcReqsCounts[i] == 0 {
				continue
			}
			fmt.Printf("    %-30v %10v %10v\n", msgs.CDCMessageKind(i), stats.cdcReqsCounts[i], time.Duration(stats.cdcReqsNanos[i]/stats.cdcReqsCounts[i]))
		}
	}

	cleanupAfterTest(blockServicesKeys)
}

func runTests(terminateChan chan any, log eggs.LogLevels, blockServices []eggs.BlockService) {
	defer func() { handleRecover(log, terminateChan, recover()) }()

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

	fileHistoryOpts := fileHistoryTestOpts{
		steps:           10 * 1000, // perform 10k actions
		checkpointEvery: 100,       // get times every 100 actions
		targetFiles:     1000,      // how many files we want
		lowFiles:        500,
		threads:         5,
	}
	runTest(
		blockServicesKeys,
		fmt.Sprintf("file history test, %v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(stats *harnessStats) {
			fileHistoryTest(log, &fileHistoryOpts, stats, blockServicesKeys)
		},
	)

	fsTestOpts := fsTestOpts{
		numDirs:  1 * 1000,   // we need at least 256 directories, to have at least one dir per shard
		numFiles: 100 * 1000, // around 100 files per dir
		depth:    4,
	}
	runTest(
		blockServicesKeys,
		fmt.Sprintf("simple fs test, %v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(stats *harnessStats) {
			fsTest(log, &fsTestOpts, stats, blockServicesKeys)
		},
	)

	terminateChan <- nil
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

	log := &eggs.LogToStdout{}

	shardExe := eggs.BuildShardExe(log, &cppBuildOpts)
	cdcExe := eggs.BuildCDCExe(log, &cppBuildOpts)
	shuckleExe := eggs.BuildShuckleExe(log)

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

	// Start CDC
	procs.StartCDC(&eggs.CDCOpts{
		Exe:      cdcExe,
		Dir:      path.Join(*dataDir, "cdc"),
		Verbose:  *verbose,
		Valgrind: *valgrind,
	})

	waitShuckleFor := 10 * time.Second
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	blockServices := eggs.WaitForShuckle(fmt.Sprintf("localhost:%v", shucklePort), hddBlockServices+flashBlockServices, waitShuckleFor)

	// Start shards
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		procs.StartShard(&eggs.ShardOpts{
			Exe:            shardExe,
			Dir:            path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			Verbose:        *verbose,
			Shid:           shid,
			Valgrind:       *valgrind,
			WaitForShuckle: true,
		})
	}

	waitShardFor := 20 * time.Second
	fmt.Printf("waiting for shards for %v...\n", waitShardFor)
	for i := 0; i < 256; i++ {
		eggs.WaitForShard(msgs.ShardId(i), waitShardFor)
	}

	fmt.Printf("operational 🤖\n")

	// start tests
	go func() { runTests(terminateChan, log, blockServices) }()

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		cleanupDbDir = false
		panic(err)
	}
}
