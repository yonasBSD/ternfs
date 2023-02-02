package main

import (
	"crypto/aes"
	"crypto/cipher"
	"flag"
	"fmt"
	"os"
	"path"
	"regexp"
	"runtime/debug"
	"runtime/pprof"
	"strings"
	"sync"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func formatNanos(nanos int64) string {
	var amount float64
	var unit string
	if nanos < 1e3 {
		amount = float64(nanos)
		unit = "ns"
	} else if nanos < 1e6 {
		amount = float64(nanos) / 1e3
		unit = "µs"
	} else if nanos < 1e9 {
		amount = float64(nanos) / 1e6
		unit = "ms"
	} else if nanos < 1e12 {
		amount = float64(nanos) / 1e9
		unit = "s "
	} else if nanos < 1e12*60 {
		amount = float64(nanos) / (1e9 * 60.0)
		unit = "m "
	} else {
		amount = float64(nanos) / (1e9 * 60.0 * 60.0)
		unit = "h "
	}
	return fmt.Sprintf("%7.2f%s", amount, unit)
}

var stacktraceLock sync.Mutex

func handleRecover(log eggs.LogLevels, terminateChan chan any, err any) {
	if err != nil {
		log.RaiseAlert(err)
		stacktraceLock.Lock()
		fmt.Fprintf(os.Stderr, "PANIC %v. Stacktrace:\n", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			fmt.Fprintln(os.Stderr, line)
		}
		stacktraceLock.Unlock()
		terminateChan <- err
	}
}

func formatCounters(what string, counters *eggs.ReqCounters) {
	fmt.Printf("    %s reqs count/attempts/avg/total:\n", what)
	for i := 0; i < 256; i++ {
		if counters.Count[i] == 0 {
			continue
		}
		fmt.Printf("      %-30v %10v %6.2f %7s %7s\n", msgs.ShardMessageKind(i), counters.Count[i], float64(counters.Attempts[i])/float64(counters.Count[i]), formatNanos(counters.Nanos[i]/counters.Count[i]), formatNanos(counters.Nanos[i]))
	}
}

func runTest(
	log eggs.LogLevels,
	shuckleAddress string,
	mbs eggs.MockableBlockServices,
	filter *regexp.Regexp,
	name string,
	extra string,
	run func(mbs eggs.MockableBlockServices, counters *eggs.ClientCounters),
) {
	if !filter.Match([]byte(name)) {
		fmt.Printf("skipping test %s\n", name)
		return
	}

	counters := &eggs.ClientCounters{}

	fmt.Printf("running %s, %s\n", name, extra)
	t0 := time.Now()
	run(mbs, counters)
	elapsed := time.Since(t0)

	totalShardRequests := counters.Shard.TotalRequests()
	totalCDCRequests := counters.CDC.TotalRequests()
	fmt.Printf("  ran test in %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters("shard", &counters.Shard)
	}
	if totalCDCRequests > 0 {
		formatCounters("CDC", &counters.CDC)
	}

	counters = &eggs.ClientCounters{}
	t0 = time.Now()
	cleanupAfterTest(log, shuckleAddress, counters, mbs)
	elapsed = time.Since(t0)
	totalShardRequests = counters.Shard.TotalRequests()
	totalCDCRequests = counters.CDC.TotalRequests()
	fmt.Printf("  cleanup took %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters("shard", &counters.Shard)
	}
	if totalCDCRequests > 0 {
		formatCounters("CDC", &counters.CDC)
	}
}

func runTests(terminateChan chan any, log eggs.LogLevels, shuckleAddress string, blockServices []msgs.BlockServiceInfo, fuseMountPoint string, short bool, filter *regexp.Regexp) {
	defer func() { handleRecover(log, terminateChan, recover()) }()

	blockServicesKeys := make(map[msgs.BlockServiceId]cipher.Block)
	for _, blockService := range blockServices {
		cipher, err := aes.NewCipher(blockService.SecretKey[:])
		if err != nil {
			panic(fmt.Errorf("could not create AES-128 key: %w", err))
		}
		blockServicesKeys[blockService.Id] = cipher
	}

	realBlockServices := eggs.RealBlockServices{}
	mockedBlockServices := &eggs.MockedBlockServices{Keys: blockServicesKeys}

	fileHistoryOpts := fileHistoryTestOpts{
		steps:           10 * 1000, // perform 10k actions
		checkpointEvery: 100,       // get times every 100 actions
		targetFiles:     1000,      // how many files we want
		lowFiles:        500,
		threads:         5,
	}
	if short {
		fileHistoryOpts.threads = 2
	}
	runTest(
		log,
		shuckleAddress,
		mockedBlockServices,
		filter,
		"file history test",
		fmt.Sprintf("%v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(mbs eggs.MockableBlockServices, counters *eggs.ClientCounters) {
			fileHistoryTest(log, shuckleAddress, mbs, &fileHistoryOpts, counters)
		},
	)

	noBlocksFsTestOpts := fsTestOpts{
		numDirs:     1 * 1000,  // we need at least 256 directories, to have at least one dir per shard
		numFiles:    20 * 1000, // around 20 files per dir
		depth:       4,
		maxFileSize: 100 << 20, // 100MiB
		spanSize:    10 << 20,  // 10MiB
	}
	if short {
		noBlocksFsTestOpts.numDirs = 200
		noBlocksFsTestOpts.numFiles = 10 * 200
	}
	runTest(
		log,
		shuckleAddress,
		mockedBlockServices,
		filter,
		"simple fs test",
		fmt.Sprintf("%v dirs, %v files, %v depth", noBlocksFsTestOpts.numDirs, noBlocksFsTestOpts.numFiles, noBlocksFsTestOpts.depth),
		func(mbs eggs.MockableBlockServices, counters *eggs.ClientCounters) {
			fsTest(log, shuckleAddress, &noBlocksFsTestOpts, counters, mbs, "")
		},
	)

	blocksFsTestOpts := fsTestOpts{
		numDirs:     50,
		numFiles:    1000,
		depth:       2,
		maxFileSize: 10 << 20, // 10MiB
		spanSize:    1 << 20,  // 1MiB
	}
	runTest(
		log,
		shuckleAddress,
		realBlockServices,
		filter,
		"fs test with blocks",
		fmt.Sprintf("%v dirs, %v files, %v depth, ~%vMiB stored", blocksFsTestOpts.numDirs, blocksFsTestOpts.numFiles, blocksFsTestOpts.depth, (blocksFsTestOpts.maxFileSize*blocksFsTestOpts.numFiles)>>21),
		func(mbs eggs.MockableBlockServices, counters *eggs.ClientCounters) {
			fsTest(log, shuckleAddress, &blocksFsTestOpts, counters, mbs, "")
		},
	)

	runTest(
		log,
		shuckleAddress,
		realBlockServices,
		filter,
		"fs test with fuse",
		fmt.Sprintf("%v dirs, %v files, %v depth, ~%vMiB stored", blocksFsTestOpts.numDirs, blocksFsTestOpts.numFiles, blocksFsTestOpts.depth, (blocksFsTestOpts.maxFileSize*blocksFsTestOpts.numFiles)>>21),
		func(mbs eggs.MockableBlockServices, counters *eggs.ClientCounters) {
			fsTest(log, shuckleAddress, &blocksFsTestOpts, counters, mbs, fuseMountPoint)
		},
	)

	terminateChan <- nil
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	buildType := flag.String("build-type", "alpine", "C++ build type, one of alpine/release/debug/sanitized/valgrind")
	verbose := flag.Bool("verbose", false, "Note that verbose won't do much for the shard unless you build with debug.")
	dataDir := flag.String("data-dir", "", "Directory where to store the EggsFS data. If not present a temporary directory will be used.")
	preserveDbDir := flag.Bool("preserve-data-dir", false, "Whether to preserve the temp data dir (if we're using a temp data dir).")
	filter := flag.String("filter", "", "Regex to match against test names -- only matching ones will be ran.")
	profile := flag.Bool("profile", false, "Run with profiling (this includes the C++ and Go binaries and the test driver). Implies -preserve-data-dir")
	incomingPacketDrop := flag.Float64("incoming-packet-drop", 0.0, "Simulate packet drop in shard (the argument is the probability that any packet will be dropped). This one will drop the requests on arrival.")
	outgoingPacketDrop := flag.Float64("outgoing-packet-drop", 0.0, "Simulate packet drop in shard (the argument is the probability that any packet will be dropped). This one will process the requests, but drop the responses.")
	short := flag.Bool("short", false, "Run a shorter version of the tests (useful with packet drop flags)")
	flag.Parse()
	noRunawayArgs()

	if *verbose && *buildType != "debug" {
		fmt.Printf("We're building with build type %v, which is not \"debug\", and you also passed in -verbose.\nBe aware that you won't get debug messages for C++ binaries.\n\n", *buildType)
	}

	filterRe := regexp.MustCompile(*filter)

	cleanupDbDir := false
	tmpDataDir := *dataDir == ""
	if tmpDataDir {
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

	if *profile {
		f, err := os.Create(path.Join(*dataDir, "test-profile"))
		if err != nil {
			panic(err)
		}
		pprof.StartCPUProfile(f)
		defer pprof.StopCPUProfile()
	}

	logFile := path.Join(*dataDir, "test-log")
	var logOut *os.File
	{
		var err error
		logOut, err = os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not open file %v: %v", logFile, err)
			os.Exit(1)
		}
		defer logOut.Close()
	}
	log := &eggs.LogLogger{
		Verbose: *verbose,
		Logger:  eggs.NewLogger(logOut),
	}

	fmt.Printf("building shard/cdc/blockservice/shuckle\n")
	cppExes := eggs.BuildCppExes(log, *buildType)
	shuckleExe := eggs.BuildShuckleExe(log)
	blockServiceExe := eggs.BuildBlockServiceExe(log)
	eggsFuseExe := eggs.BuildEggsFuseExe(log)

	terminateChan := make(chan any, 1)

	procs := eggs.NewManagedProcesses(terminateChan)
	defer procs.Close()

	// Start shuckle
	shucklePort := uint16(10000)
	shuckleAddress := fmt.Sprintf("localhost:%v", shucklePort)
	procs.StartShuckle(log, &eggs.ShuckleOpts{
		Exe:         shuckleExe,
		BincodePort: shucklePort,
		HttpPort:    shucklePort + 1,
		Verbose:     *verbose,
		Dir:         path.Join(*dataDir, "shuckle"),
	})

	// Start block services. Right now this is just used for shuckle to
	// get block services, since we don't actually write to the block services
	// yet (it's just too slow).
	hddBlockServices := 10
	flashBlockServices := 5
	for i := 0; i < hddBlockServices+flashBlockServices; i++ {
		storageClass := msgs.HDD_STORAGE
		if i >= hddBlockServices {
			storageClass = msgs.FLASH_STORAGE
		}
		procs.StartBlockService(log, &eggs.BlockServiceOpts{
			Exe:            blockServiceExe,
			Path:           path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
			StorageClass:   storageClass,
			FailureDomain:  fmt.Sprintf("%d", i),
			Verbose:        *verbose,
			ShuckleAddress: fmt.Sprintf("localhost:%d", shucklePort),
			NoTimeCheck:    true,
			OwnIp:          "127.0.0.1",
			Profile:        *profile,
		})
	}

	if *outgoingPacketDrop > 0 {
		fmt.Printf("will drop %0.2f%% of packets after executing requests\n", *outgoingPacketDrop*100.0)
	}
	if *incomingPacketDrop > 0 {
		fmt.Printf("will drop %0.2f%% of packets before executing requests\n", *outgoingPacketDrop*100.0)
	}

	// Start CDC
	procs.StartCDC(log, &eggs.CDCOpts{
		Exe:            cppExes.CDCExe,
		Dir:            path.Join(*dataDir, "cdc"),
		Verbose:        *verbose && *buildType == "debug",
		Valgrind:       *buildType == "valgrind",
		Perf:           *profile,
		ShuckleAddress: shuckleAddress,
		OwnIp:          "127.0.0.1",
	})

	// Start shards
	numShards := 256
	for i := 0; i < numShards; i++ {
		shid := msgs.ShardId(i)
		shopts := eggs.ShardOpts{
			Exe:                cppExes.ShardExe,
			Dir:                path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			Verbose:            *verbose && *buildType == "debug",
			Shid:               shid,
			Valgrind:           *buildType == "valgrind",
			Perf:               *profile,
			IncomingPacketDrop: *incomingPacketDrop,
			OutgoingPacketDrop: *outgoingPacketDrop,
			ShuckleAddress:     shuckleAddress,
			OwnIp:              "127.0.0.1",
		}
		procs.StartShard(log, &shopts)
	}

	waitShuckleFor := 5 * time.Second
	if *buildType == "valgrind" || *profile {
		waitShuckleFor = 30 * time.Second
	}
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	blockServices := eggs.WaitForShuckle(log, fmt.Sprintf("localhost:%v", shucklePort), hddBlockServices+flashBlockServices, waitShuckleFor).BlockServices

	fuseMountPoint := procs.StartFuse(log, &eggs.FuseOpts{
		Exe:            eggsFuseExe,
		Path:           path.Join(*dataDir, "eggsfuse"),
		Verbose:        *verbose,
		Wait:           true,
		ShuckleAddress: shuckleAddress,
		Profile:        *profile,
	})

	fmt.Printf("operational 🤖\n")

	// start tests
	go func() {
		runTests(terminateChan, log, shuckleAddress, blockServices, fuseMountPoint, *short, filterRe)
	}()

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		panic(err)
	}
	// we haven't panicked, allow to cleanup the db dir if appropriate
	cleanupDbDir = tmpDataDir && !*preserveDbDir && !*profile
}
