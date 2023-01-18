package main

import (
	"encoding/hex"
	"flag"
	"fmt"
	"os"
	"path"
	"regexp"
	"runtime/debug"
	"strings"
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

func handleRecover(log eggs.LogLevels, terminateChan chan any, err any) {
	if err != nil {
		log.RaiseAlert(err.(error))
		log.Info("PANIC %v. Stacktrace:", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			log.Info(line)
		}
		terminateChan <- err
	}
}

func formatCounters(what string, counters *eggs.ReqCounters) {
	fmt.Printf("    %s reqs count/attempts/avg/total:\n", what)
	for i := 0; i < 256; i++ {
		if counters.Count[i] == 0 {
			continue
		}
		fmt.Printf("      %-30v %10v %6.2f %s %s\n", msgs.ShardMessageKind(i), counters.Count[i], float64(counters.Attempts[i])/float64(counters.Count[i]), formatNanos(counters.Nanos[i]/counters.Count[i]), formatNanos(counters.Nanos[i]))
	}
}

func runTest(log eggs.LogLevels, blockServicesKeys map[msgs.BlockServiceId][16]byte, filter *regexp.Regexp, name string, extra string, run func(env *eggs.ClientCounters)) {
	if !filter.Match([]byte(name)) {
		fmt.Printf("skipping test %s\n", name)
		return
	}

	counters := &eggs.ClientCounters{}

	fmt.Printf("running %s, %s\n", name, extra)
	t0 := time.Now()
	run(counters)
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
	cleanupAfterTest(log, counters, blockServicesKeys)
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

func runTests(terminateChan chan any, log eggs.LogLevels, blockServices []eggs.BlockService, short bool, filter *regexp.Regexp) {
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
	if short {
		fileHistoryOpts.threads = 2
	}
	runTest(
		log,
		blockServicesKeys,
		filter,
		"file history test",
		fmt.Sprintf("%v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(counters *eggs.ClientCounters) {
			fileHistoryTest(log, &fileHistoryOpts, counters, blockServicesKeys)
		},
	)

	fsTestOpts := fsTestOpts{
		numDirs:  1 * 1000,  // we need at least 256 directories, to have at least one dir per shard
		numFiles: 20 * 1000, // around 20 files per dir
		depth:    4,
	}
	if short {
		fsTestOpts.numDirs = 200
		fsTestOpts.numFiles = 10 * 200
	}
	runTest(
		log,
		blockServicesKeys,
		filter,
		"simple fs test",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *eggs.ClientCounters) {
			fsTest(log, &fsTestOpts, counters, blockServicesKeys)
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
	valgrind := flag.Bool("valgrind", false, "Whether to build for and run with valgrind.")
	sanitize := flag.Bool("sanitize", false, "Whether to build with sanitize.")
	debug := flag.Bool("debug", false, "Build without optimizations.")
	verbose := flag.Bool("verbose", false, "Note that verbose won't do much for the shard unless you build with debug.")
	dataDir := flag.String("data-dir", "", "Directory where to store the EggsFS data. If not present a temporary directory will be used.")
	preserveDbDir := flag.Bool("preserve-data-dir", false, "Whether to preserve the temp data dir (if we're using a temp data dir).")
	coverage := flag.Bool("coverage", false, "Build with coverage support. Right now applies only to the C++ shard code.")
	filter := flag.String("filter", "", "Regex to match against test names -- only matching ones will be ran.")
	perf := flag.Bool("perf", false, "Run the C++ binaries (shard & CDC) with `perf record`")
	incomingPacketDrop := flag.Float64("incoming-packet-drop", 0.0, "Simulate packet drop in shard & CDC (the argument is the probability that any packet will be dropped). This one will drop the requests on arrival.")
	outgoingPacketDrop := flag.Float64("outgoing-packet-drop", 0.0, "Simulate packet drop in shard & CDC (the argument is the probability that any packet will be dropped). This one will process the requests, but drop the responses.")
	short := flag.Bool("short", false, "Run a shorter version of the tests (useful with packet drop flags)")
	flag.Parse()
	noRunawayArgs()

	if *verbose && !*debug {
		fmt.Fprintf(os.Stderr, "You asked me to build without -debug, and with -verbose. This is almost certainly wrong, since you won't get debug messages in the shard/cdc without -debug.")
		os.Exit(2)
	}

	filterRe := regexp.MustCompile(*filter)

	cppBuildOpts := eggs.BuildCppOpts{
		Valgrind: *valgrind,
		Sanitize: *sanitize,
		Debug:    *debug,
		Coverage: *coverage,
	}

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

	logFile := path.Join(*dataDir, "go-log")
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

	shardExe := eggs.BuildShardExe(log, &cppBuildOpts)
	cdcExe := eggs.BuildCDCExe(log, &cppBuildOpts)
	shuckleExe := eggs.BuildShuckleExe(log)

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
	flashBlockServices := 0
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

	if *incomingPacketDrop > 0 || *outgoingPacketDrop > 0 {
		fmt.Printf("will drop %0.2f%% of packets\n", (*incomingPacketDrop+*outgoingPacketDrop)*100.0)
	}

	// Start CDC
	procs.StartCDC(&eggs.CDCOpts{
		Exe:                cdcExe,
		Dir:                path.Join(*dataDir, "cdc"),
		Verbose:            *verbose,
		Valgrind:           *valgrind,
		Perf:               *perf,
		IncomingPacketDrop: *incomingPacketDrop,
		OutgoingPacketDrop: *outgoingPacketDrop,
	})

	waitShuckleFor := 10 * time.Second
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	blockServices := eggs.WaitForShuckle(fmt.Sprintf("localhost:%v", shucklePort), hddBlockServices+flashBlockServices, waitShuckleFor)

	// Start shards
	numShards := 256
	for i := 0; i < numShards; i++ {
		shid := msgs.ShardId(i)
		shopts := eggs.ShardOpts{
			Exe:                shardExe,
			Dir:                path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			Verbose:            *verbose,
			Shid:               shid,
			Valgrind:           *valgrind,
			WaitForShuckle:     true,
			Perf:               *perf,
			IncomingPacketDrop: *incomingPacketDrop,
			OutgoingPacketDrop: *outgoingPacketDrop,
		}
		procs.StartShard(&shopts)
	}

	waitShardFor := 20 * time.Second
	fmt.Printf("waiting for shards for %v...\n", waitShardFor)
	for i := 0; i < numShards; i++ {
		eggs.WaitForShard(log, msgs.ShardId(i), waitShardFor)
	}

	fmt.Printf("operational 🤖\n")

	// start tests
	go func() { runTests(terminateChan, log, blockServices, *short, filterRe) }()

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		cleanupDbDir = false
		panic(err)
	}
}
