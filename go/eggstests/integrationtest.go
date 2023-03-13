package main

import (
	"flag"
	"fmt"
	"os"
	"path"
	"regexp"
	"runtime"
	"runtime/debug"
	"runtime/pprof"
	"strings"
	"sync"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/managedprocess"
	"xtx/eggsfs/msgs"

	"golang.org/x/exp/constraints"
)

func formatNanos(nanos uint64) string {
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

func handleRecover(log *lib.Logger, terminateChan chan any, err any) {
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

func formatCounters[K constraints.Integer](what string, counters []lib.ReqCounters) {
	fmt.Printf("    %s reqs count/attempts/avg/total:\n", what)
	for i := 0; i < len(counters); i++ {
		count := &counters[i]
		if count.Attempts == 0 {
			continue
		}
		fmt.Printf("      %-30v %10v %6.2f %7s %7s\n", K(i), count.Timings.TotalCount(), float64(count.Attempts)/float64(count.Timings.TotalCount()), formatNanos(uint64(count.Timings.TotalTime())/count.Timings.TotalCount()), formatNanos(uint64(count.Timings.TotalTime())))
	}
}

func totalRequests(c []lib.ReqCounters) uint64 {
	total := uint64(0)
	for i := 0; i < len(c); i++ {
		total += c[i].Timings.TotalCount()
	}
	return total
}

func runTest(
	log *lib.Logger,
	shuckleAddress string,
	filter *regexp.Regexp,
	name string,
	extra string,
	run func(counters *lib.ClientCounters),
) {
	if !filter.Match([]byte(name)) {
		fmt.Printf("skipping test %s\n", name)
		return
	}

	counters := &lib.ClientCounters{}

	fmt.Printf("running %s test, %s\n", name, extra)
	t0 := time.Now()
	run(counters)
	elapsed := time.Since(t0)

	totalShardRequests := totalRequests(counters.Shard[:])
	totalCDCRequests := totalRequests(counters.CDC[:])
	fmt.Printf("  ran test in %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters[msgs.ShardMessageKind]("shard", counters.Shard[:])
	}
	if totalCDCRequests > 0 {
		formatCounters[msgs.CDCMessageKind]("CDC", counters.CDC[:])
	}

	counters = &lib.ClientCounters{}
	t0 = time.Now()
	cleanupAfterTest(log, shuckleAddress, counters)
	elapsed = time.Since(t0)
	totalShardRequests = totalRequests(counters.Shard[:])
	totalCDCRequests = totalRequests(counters.CDC[:])
	fmt.Printf("  cleanup took %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters[msgs.ShardMessageKind]("shard", counters.Shard[:])
	}
	if totalCDCRequests > 0 {
		formatCounters[msgs.CDCMessageKind]("CDC", counters.CDC[:])
	}
}

func runTests(terminateChan chan any, log *lib.Logger, shuckleAddress string, fuseMountPoint string, short bool, filter *regexp.Regexp) {
	defer func() { handleRecover(log, terminateChan, recover()) }()

	// We want to immediately clean up everything when we run the GC manually
	{
		client, err := lib.NewClient(log, shuckleAddress, nil, nil, nil)
		if err != nil {
			panic(err)
		}
		snapshotPolicy := &msgs.SnapshotPolicy{
			DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(0),
		}
		if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, snapshotPolicy); err != nil {
			panic(err)
		}
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
		shuckleAddress,
		filter,
		"file history",
		fmt.Sprintf("%v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(counters *lib.ClientCounters) {
			fileHistoryTest(log, shuckleAddress, &fileHistoryOpts, counters)
		},
	)

	fsTestOpts := fsTestOpts{
		numDirs:  1 * 1000,  // we need at least 256 directories, to have at least one dir per shard
		numFiles: 20 * 1000, // around 20 files per dir
		depth:    4,
		// 0.03 * 20*1000 * 5MiB = ~3GiB of file data
		emptyFileProb:  0.8,
		inlineFileProb: 0.17,
		maxFileSize:    10 << 20, // 10MiB
		spanSize:       1 << 20,  // 1MiB
	}
	if short {
		fsTestOpts.numDirs = 200
		fsTestOpts.numFiles = 10 * 200
		fsTestOpts.emptyFileProb = 0.1
		fsTestOpts.inlineFileProb = 0.3
	}
	runTest(
		log,
		shuckleAddress,
		filter,
		"direct fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, shuckleAddress, &fsTestOpts, counters, "")
		},
	)

	runTest(
		log,
		shuckleAddress,
		filter,
		"fuse fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, shuckleAddress, &fsTestOpts, counters, fuseMountPoint)
		},
	)

	largeFileOpts := largeFileTestOpts{
		fileSize: 1 << 30, // 1GiB
	}
	runTest(
		log,
		shuckleAddress,
		filter,
		"large file",
		fmt.Sprintf("%vGB", float64(largeFileOpts.fileSize)/1e9),
		func(counters *lib.ClientCounters) {
			largeFileTest(log, &largeFileOpts, fuseMountPoint)
		},
	)

	rsyncOpts := rsyncTestOpts{
		maxFileSize: 200 << 20, // 200MiB
		numFiles:    100,       // 20GiB
		numDirs:     10,
	}
	runTest(
		log,
		shuckleAddress,
		filter,
		"rsync large files",
		fmt.Sprintf("%v files, %v dirs, %vMB file size", rsyncOpts.numFiles, rsyncOpts.numDirs, float64(rsyncOpts.maxFileSize)/1e6),
		func(counters *lib.ClientCounters) {
			rsyncTest(log, &rsyncOpts, fuseMountPoint)
		},
	)

	rsyncOpts = rsyncTestOpts{
		maxFileSize: 1 << 20, // 1Mib
		numFiles:    10000,   // 10GiB
		numDirs:     1000,
	}
	runTest(
		log,
		shuckleAddress,
		filter,
		"rsync small files",
		fmt.Sprintf("%v files, %v dirs, %vMB file size", rsyncOpts.numFiles, rsyncOpts.numDirs, float64(rsyncOpts.maxFileSize)/1e6),
		func(counters *lib.ClientCounters) {
			rsyncTest(log, &rsyncOpts, fuseMountPoint)
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
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	dataDir := flag.String("data-dir", "", "Directory where to store the EggsFS data. If not present a temporary directory will be used.")
	preserveDbDir := flag.Bool("preserve-data-dir", false, "Whether to preserve the temp data dir (if we're using a temp data dir).")
	filter := flag.String("filter", "", "Regex to match against test names -- only matching ones will be ran.")
	profileTest := flag.Bool("profile-test", false, "Run the test driver with profiling. Implies -preserve-data-dir")
	profile := flag.Bool("profile", false, "Run with profiling (this includes the C++ and Go binaries and the test driver). Implies -preserve-data-dir")
	incomingPacketDrop := flag.Float64("incoming-packet-drop", 0.0, "Simulate packet drop in shard (the argument is the probability that any packet will be dropped). This one will drop the requests on arrival.")
	outgoingPacketDrop := flag.Float64("outgoing-packet-drop", 0.0, "Simulate packet drop in shard (the argument is the probability that any packet will be dropped). This one will process the requests, but drop the responses.")
	short := flag.Bool("short", false, "Run a shorter version of the tests (useful with packet drop flags)")
	repoDir := flag.String("repo-dir", "", "Used to build C++/Go binaries. If not provided, the path will be derived form the filename at build time (so will only work locally).")
	binariesDir := flag.String("binaries-dir", "", "If provided, nothing will be built, instead it'll be assumed that the binaries will be in the specified directory.")
	flag.Parse()
	noRunawayArgs()

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

	if *profileTest {
		f, err := os.Create(path.Join(*dataDir, "test-profile"))
		if err != nil {
			panic(err)
		}
		pprof.StartCPUProfile(f)
		defer pprof.StopCPUProfile()
	}

	if *repoDir == "" {
		_, filename, _, ok := runtime.Caller(0)
		if !ok {
			panic("no caller information")
		}
		*repoDir = path.Dir(path.Dir(path.Dir(filename)))
	}

	logFile := path.Join(*dataDir, "test-log")
	var logOut *os.File
	{
		var err error
		logOut, err = os.OpenFile(logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not open file %v: %v\n", logFile, err)
			os.Exit(1)
		}
		defer logOut.Close()
	}
	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log := lib.NewLogger(level, logOut)

	var cppExes *managedprocess.CppExes
	var goExes *managedprocess.GoExes
	if *binariesDir != "" {
		cppExes = &managedprocess.CppExes{
			ShardExe: path.Join(*binariesDir, "eggsshard"),
			CDCExe:   path.Join(*binariesDir, "eggscdc"),
		}
		goExes = &managedprocess.GoExes{
			ShuckleExe: path.Join(*binariesDir, "eggsshuckle"),
			BlocksExe:  path.Join(*binariesDir, "eggsblocks"),
			FuseExe:    path.Join(*binariesDir, "eggsfuse"),
		}
	} else {
		fmt.Printf("building shard/cdc/blockservice/shuckle\n")
		cppExes = managedprocess.BuildCppExes(log, *repoDir, *buildType)
		goExes = managedprocess.BuildGoExes(log, *repoDir)
	}

	terminateChan := make(chan any, 1)

	procs := managedprocess.New(terminateChan)
	defer procs.Close()

	// Start shuckle
	shucklePort := uint16(55555)
	shuckleAddress := fmt.Sprintf("localhost:%v", shucklePort)
	procs.StartShuckle(log, &managedprocess.ShuckleOpts{
		Exe:         goExes.ShuckleExe,
		BincodePort: shucklePort,
		LogLevel:    level,
		Dir:         path.Join(*dataDir, "shuckle"),
	})

	// Start block services. 32 to cover RS(16,16).
	hddBlockServices := 32
	flashBlockServices := 32
	for i := 0; i < hddBlockServices+flashBlockServices; i++ {
		storageClass := msgs.HDD_STORAGE
		if i >= hddBlockServices {
			storageClass = msgs.FLASH_STORAGE
		}
		procs.StartBlockService(log, &managedprocess.BlockServiceOpts{
			Exe:            goExes.BlocksExe,
			Path:           path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
			StorageClass:   storageClass,
			FailureDomain:  fmt.Sprintf("%d", i),
			LogLevel:       level,
			ShuckleAddress: fmt.Sprintf("localhost:%d", shucklePort),
			NoTimeCheck:    true,
			OwnIp1:         "127.0.0.1",
			OwnIp2:         "127.0.0.1",
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
	procs.StartCDC(log, *repoDir, &managedprocess.CDCOpts{
		Exe:            cppExes.CDCExe,
		Dir:            path.Join(*dataDir, "cdc"),
		LogLevel:       level,
		Valgrind:       *buildType == "valgrind",
		Perf:           *profile,
		ShuckleAddress: shuckleAddress,
		OwnIp:          "127.0.0.1",
	})

	// Start shards
	numShards := 256
	for i := 0; i < numShards; i++ {
		shid := msgs.ShardId(i)
		shopts := managedprocess.ShardOpts{
			Exe:                cppExes.ShardExe,
			Dir:                path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			LogLevel:           level,
			Shid:               shid,
			Valgrind:           *buildType == "valgrind",
			Perf:               *profile,
			IncomingPacketDrop: *incomingPacketDrop,
			OutgoingPacketDrop: *outgoingPacketDrop,
			ShuckleAddress:     shuckleAddress,
			OwnIp:              "127.0.0.1",
		}
		procs.StartShard(log, *repoDir, &shopts)
	}

	waitShuckleFor := 5 * time.Second
	if *buildType == "valgrind" || *profile {
		waitShuckleFor = 30 * time.Second
	}
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	lib.WaitForShuckle(log, fmt.Sprintf("localhost:%v", shucklePort), hddBlockServices+flashBlockServices, waitShuckleFor)

	fuseMountPoint := procs.StartFuse(log, &managedprocess.FuseOpts{
		Exe:            goExes.FuseExe,
		Path:           path.Join(*dataDir, "fuse"),
		LogLevel:       level,
		Wait:           true,
		ShuckleAddress: shuckleAddress,
		Profile:        *profile,
	})

	fmt.Printf("operational 🤖\n")

	// start tests
	go func() {
		runTests(terminateChan, log, shuckleAddress, fuseMountPoint, *short, filterRe)
	}()

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		panic(err)
	}
	// we haven't panicked, allow to cleanup the db dir if appropriate
	cleanupDbDir = tmpDataDir && !*preserveDbDir && !*profile && !*profileTest
}
