package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path"
	"regexp"
	"runtime"
	"runtime/pprof"
	"strconv"
	"strings"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/managedprocess"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
	"xtx/eggsfs/wyhash"

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
	acceptGcFailures bool,
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
	log.Info("running %s test, %s\n", name, extra) // also in log to track progress in CI more easily
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
	cleanupAfterTest(log, shuckleAddress, counters, acceptGcFailures)
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

func getKmodDirRefreshTime() uint64 {
	bs, err := os.ReadFile("/proc/sys/fs/eggsfs/dir_refresh_time_ms")
	if err != nil {
		panic(err)
	}
	ms, err := strconv.ParseUint(strings.TrimSpace(string(bs)), 10, 64)
	if err != nil {
		panic(err)
	}
	return ms
}

func setKmodDirRefreshTime(ms uint64) {
	out, err := exec.Command("sudo", "sh", "-c", fmt.Sprintf("echo %v > /proc/sys/fs/eggsfs/dir_refresh_time_ms", ms)).CombinedOutput()
	if err != nil {
		panic(fmt.Errorf("could not set dir_refresh_time (%w): %s", err, out))
	}
}

func mountKmod(shucklePort uint16, mountPoint string) {
	out, err := exec.Command("sudo", "mount", "-t", "eggsfs", fmt.Sprintf("127.0.0.1:%v", shucklePort), mountPoint).CombinedOutput()
	if err != nil {
		panic(fmt.Errorf("could not mount filesystem (%w): %s", err, out))
	}
}

type cfgOverrides map[string]string

func (i *cfgOverrides) Set(value string) error {
	parts := strings.Split(value, "=")
	if len(parts) != 2 {
		return fmt.Errorf("invalid cfg override %q", value)
	}
	(*i)[parts[0]] = parts[1]
	return nil
}

func (i *cfgOverrides) String() string {
	pairs := []string{}
	for k, v := range *i {
		pairs = append(pairs, fmt.Sprintf("%s=%s", k, v))
	}
	return strings.Join(pairs, ",")
}

func (i *cfgOverrides) int(k string, def int) int {
	s, present := (*i)[k]
	if !present {
		return def
	}
	n, err := strconv.Atoi(s)
	if err != nil {
		panic(err)
	}
	return n
}

type RunTests struct {
	acceptGcFailures         bool
	overrides                *cfgOverrides
	shuckleIp                string
	shucklePort              uint16
	mountPoint               string
	fuseMountPoint           string
	kmod                     bool
	short                    bool
	filter                   *regexp.Regexp
	writeDirectInMountedTest bool
}

func (r *RunTests) run(
	terminateChan chan any,
	log *lib.Logger,
) {
	shuckleAddress := fmt.Sprintf("%s:%d", r.shuckleIp, r.shucklePort)
	defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
	client, err := lib.NewClient(log, shuckleAddress, 1)
	if err != nil {
		panic(err)
	}
	defer client.Close()

	defaultSpanPolicy := &msgs.SpanPolicy{}
	defaultStripePolicy := &msgs.StripePolicy{}
	{
		// We want to immediately clean up everything when we run the GC manually
		if err != nil {
			panic(err)
		}
		snapshotPolicy := &msgs.SnapshotPolicy{
			DeleteAfterVersions: msgs.ActiveDeleteAfterVersions(0),
		}
		if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, snapshotPolicy); err != nil {
			panic(err)
		}
		dirInfoCache := lib.NewDirInfoCache()
		_, err = client.ResolveDirectoryInfoEntry(log, dirInfoCache, msgs.ROOT_DIR_INODE_ID, defaultSpanPolicy)
		if err != nil {
			panic(err)
		}
		// We also want to stimulate many spans while not writing huge files.
		// This is the same as the default but the second and third policy are
		/// 500KiB and 1MiB
		spanPolicy := &msgs.SpanPolicy{
			Entries: []msgs.SpanPolicyEntry{
				{
					MaxSize: 64 << 10, // 64KiB
					Parity:  rs.MkParity(1, 4),
				},
				{
					MaxSize: 500 << 10, // 500KiB
					Parity:  rs.MkParity(4, 4),
				},
				{
					MaxSize: 1 << 20, // 1MiB
					Parity:  rs.MkParity(10, 4),
				},
			},
		}
		// Also reduce stripe size to get many stripes
		_, err = client.ResolveDirectoryInfoEntry(log, dirInfoCache, msgs.ROOT_DIR_INODE_ID, defaultStripePolicy)
		if err != nil {
			panic(err)
		}
		if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, spanPolicy); err != nil {
			panic(err)
		}
		stripePolicy := &msgs.StripePolicy{
			TargetStripeSize: 4096 * 5,
		}
		if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, stripePolicy); err != nil {
			panic(err)
		}
	}

	fileHistoryOpts := fileHistoryTestOpts{
		steps:           r.overrides.int("fileHistory.steps", 10*1000), // perform 10k actions
		checkpointEvery: 100,                                           // get times every 100 actions
		targetFiles:     1000,                                          // how many files we want
		lowFiles:        500,
		threads:         r.overrides.int("fileHistory.threads", 5),
	}
	if r.short {
		fileHistoryOpts.threads = r.overrides.int("fileHistory.threads", 2)
	}
	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"file history",
		fmt.Sprintf("%v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(counters *lib.ClientCounters) {
			fileHistoryTest(log, shuckleAddress, &fileHistoryOpts, counters)
		},
	)

	fsTestOpts := fsTestOpts{
		depth:        4,
		maxFileSize:  r.overrides.int("fsTest.maxFileSize", 10<<20), // 10MiB
		spanSize:     1 << 20,                                       // 1MiB
		checkThreads: r.overrides.int("fsTest.checkThreads", 5),
	}

	// 0.03 * 20*1000 * 5MiB = ~3GiB of file data
	if r.short {
		fsTestOpts.numDirs = r.overrides.int("fsTest.numDirs", 200)
		fsTestOpts.numFiles = r.overrides.int("fsTest.numFiles", 10*200)
		fsTestOpts.emptyFileProb = 0.1
		fsTestOpts.inlineFileProb = 0.3
	} else {
		fsTestOpts.numDirs = r.overrides.int("fsTest.numDirs", 1*1000)    // we need at least 256 directories, to have at least one dir per shard
		fsTestOpts.numFiles = r.overrides.int("fsTest.numFiles", 20*1000) // around 20 files per dir
		fsTestOpts.emptyFileProb = 0.8
		fsTestOpts.inlineFileProb = 0.17
	}

	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"direct fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, shuckleAddress, &fsTestOpts, counters, "", r.writeDirectInMountedTest)
		},
	)

	if r.kmod {
		preadddirOpts := preadddirOpts{
			numDirs:     1000,
			filesPerDir: 100,
			loops:       1000,
			threads:     10,
		}
		runTest(
			log,
			r.acceptGcFailures,
			shuckleAddress,
			r.filter,
			"parallel readdir",
			fmt.Sprintf("%v dirs, %v files per dir, %v loops, %v threads", preadddirOpts.numDirs, preadddirOpts.filesPerDir, preadddirOpts.loops, preadddirOpts.threads),
			func(counters *lib.ClientCounters) {
				preaddirTest(log, r.mountPoint, &preadddirOpts)
			},
		)
	}

	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"mounted fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, shuckleAddress, &fsTestOpts, counters, r.mountPoint, r.writeDirectInMountedTest)
		},
	)

	// Restore default values for policies, otherwise things will be unduly slow below
	if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, defaultSpanPolicy); err != nil {
		panic(err)
	}
	if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, defaultStripePolicy); err != nil {
		panic(err)
	}

	// remount kmod to ensure that policies are refreshed
	if r.kmod {
		out, err := exec.Command("sudo", "umount", r.mountPoint).CombinedOutput()
		if err != nil {
			panic(fmt.Errorf("could not umount fs (%w): %s", err, out))
		}
		mountKmod(r.shucklePort, r.mountPoint)
	}

	largeFileOpts := largeFileTestOpts{
		fileSize: 1 << 30, // 1GiB
	}
	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"large file",
		fmt.Sprintf("%vGB", float64(largeFileOpts.fileSize)/1e9),
		func(counters *lib.ClientCounters) {
			largeFileTest(log, &largeFileOpts, r.mountPoint)
		},
	)

	rsyncOpts := rsyncTestOpts{
		maxFileSize: 200 << 20, // 200MiB
		numFiles:    100,       // 20GiB
		numDirs:     10,
	}
	if r.short {
		rsyncOpts.numFiles = 10
		rsyncOpts.numDirs = 1
	}
	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"rsync large files",
		fmt.Sprintf("%v files, %v dirs, %vMB file size", rsyncOpts.numFiles, rsyncOpts.numDirs, float64(rsyncOpts.maxFileSize)/1e6),
		func(counters *lib.ClientCounters) {
			rsyncTest(log, &rsyncOpts, r.mountPoint)
		},
	)

	rsyncOpts = rsyncTestOpts{
		maxFileSize: 1 << 20, // 1Mib
		numFiles:    10000,   // 10GiB
		numDirs:     1000,
	}
	if r.short {
		rsyncOpts.numFiles /= 10
		rsyncOpts.numDirs /= 10
	}
	runTest(
		log,
		r.acceptGcFailures,
		shuckleAddress,
		r.filter,
		"rsync small files",
		fmt.Sprintf("%v files, %v dirs, %vMB file size", rsyncOpts.numFiles, rsyncOpts.numDirs, float64(rsyncOpts.maxFileSize)/1e6),
		func(counters *lib.ClientCounters) {
			rsyncTest(log, &rsyncOpts, r.mountPoint)
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

type blockServiceVictim struct {
	failureDomain  string
	path           string
	storageClasses []msgs.StorageClass
}

func (bsv *blockServiceVictim) start(
	log *lib.Logger,
	blocksExe string,
	shucklePort uint16,
	port1 uint16,
	port2 uint16,
	profile bool,
	procs *managedprocess.ManagedProcesses,
) managedprocess.ManagedProcessId {
	return procs.StartBlockService(log, &managedprocess.BlockServiceOpts{
		Exe:            blocksExe,
		Path:           bsv.path,
		StorageClasses: bsv.storageClasses,
		FailureDomain:  bsv.failureDomain,
		LogLevel:       log.Level(),
		ShuckleAddress: fmt.Sprintf("127.0.0.1:%d", shucklePort),
		NoTimeCheck:    true,
		OwnIp1:         "127.0.0.1",
		OwnIp2:         "127.0.0.1",
		Profile:        profile,
		Port1:          port1,
		Port2:          port2,
	})
}

func killBlockServices(
	log *lib.Logger,
	terminateChan chan any,
	stopChan chan struct{},
	blocksExe string,
	shucklePort uint16,
	profile bool,
	procs *managedprocess.ManagedProcesses,
	bsProcs map[managedprocess.ManagedProcessId]blockServiceVictim,
	bsPorts map[[16]byte]struct {
		_1 uint16
		_2 uint16
	},
) {
	// a few things to take into account here, e.g. how much it takes for bs to start,
	// and the shuckle metadata to update. we are able to do this because we never
	// mark block services as stale when the killer is active, so that basically we
	// give all responsibility to the clients, which is what we want to test.
	killAtOnce := 2
	killDuration := time.Second * 10
	gracePeriod := time.Second * 0
	log.Info("will kill %v block services for %v, with %v in between", killAtOnce, killDuration, gracePeriod)
	rand := wyhash.New(uint64(time.Now().Nanosecond()))
	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		for {
			for i := 0; i < killAtOnce; i++ {
				// pick a victim
				var bsv blockServiceVictim
				ix := int(rand.Uint64()) % len(bsProcs)
				i := 0
				var procId managedprocess.ManagedProcessId
				for procId = range bsProcs {
					if i >= ix {
						bsv = bsProcs[procId]
						delete(bsProcs, procId)
						break
					}
					i++
				}
				// kill the victim
				log.Info("killing %v", bsv.path)
				procs.Kill(procId, syscall.SIGKILL)
				// wait
				time.Sleep(killDuration)
				// revive the victim -- important to preserve ports
				// so that shard blockservice cache data won't be
				// invalidated (we don't want to have to wait for it
				// to be refreshed for the tests to work)
				log.Info("reviving %v", bsv.path)
				var failureDomain [16]byte
				copy(failureDomain[:], bsv.failureDomain)
				ports := bsPorts[failureDomain]
				procId = bsv.start(
					log,
					blocksExe,
					shucklePort,
					ports._1,
					ports._2,
					profile,
					procs,
				)
				bsProcs[procId] = bsv
			}
			sleepChan := make(chan struct{}, 1)
			go func() {
				time.Sleep(gracePeriod)
				sleepChan <- struct{}{}
			}()
			select {
			case <-sleepChan:
			case <-stopChan:
				stopChan <- struct{}{} // reply
				return
			}
		}
	}()
}

func main() {
	overrides := make(cfgOverrides)

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
	kmod := flag.Bool("kmod", false, "Whether to mount with the kernel module, rather than FUSE. Note that the tests will not attempt to run the kernel module and load it, they'll just mount with 'mount -t eggsfs'.")
	dropCachedSpansEvery := flag.Duration("drop-cached-spans-every", 0, "If set, will repeatedly drop the cached spans using 'sysctl fs.eggsfs.drop_cached_spans=1'")
	dirRefreshTime := flag.Duration("dir-refresh-time", 0, "If set, it will set the kmod /proc/sys/fs/eggsfs/dir_refresh_time_ms")
	mtu := flag.Uint64("mtu", 0, "If set, we'll use the given MTU for big requests.")
	tmpDir := flag.String("tmp-dir", "", "")
	shucklePortArg := flag.Uint("shuckle-port", 55555, "")
	blockServiceKiller := flag.Bool("block-service-killer", false, "Go around killing block services to stimulate paths recovering from that.")
	race := flag.Bool("race", false, "Go race detector")
	flag.Var(&overrides, "cfg", "Config overrides")
	flag.Parse()
	noRunawayArgs()

	filterRe := regexp.MustCompile(*filter)

	cleanupDbDir := false
	tmpDataDir := *dataDir == ""
	if tmpDataDir {
		dir, err := os.MkdirTemp(*tmpDir, "eggs-integrationtest.")
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
	if *dirRefreshTime != 0 {
		fmt.Printf("setting dir refresh time to %v\n", *dirRefreshTime)
		before := getKmodDirRefreshTime()
		setKmodDirRefreshTime(uint64(dirRefreshTime.Milliseconds()))
		defer setKmodDirRefreshTime(before)
	}

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
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: false})

	if *mtu > 0 {
		log.Info("Setting MTU to %v", *mtu)
		lib.SetMTU(*mtu)
	}

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
		goExes = managedprocess.BuildGoExes(log, *repoDir, *race)
	}

	terminateChan := make(chan any, 1)

	procs := managedprocess.New(terminateChan)
	defer procs.Close()

	// Start cached spans dropper
	if *dropCachedSpansEvery != time.Duration(0) {
		if !*kmod {
			panic(fmt.Errorf("you provided -drop-cached-spans-every but you did't provide -kmod"))
		}
		fmt.Printf("will drop cached spans every %v\n", *dropCachedSpansEvery)
		go func() {
			for {
				cmd := exec.Command("sudo", "/usr/sbin/sysctl", "fs.eggsfs.drop_cached_spans=1")
				cmd.Stdout = io.Discard
				cmd.Stderr = io.Discard
				if err := cmd.Run(); err != nil {
					terminateChan <- err
					return
				}
				time.Sleep(*dropCachedSpansEvery)
			}
		}()
	}

	// Start shuckle
	shucklePort := uint16(*shucklePortArg)
	shuckleAddress := fmt.Sprintf("127.0.0.1:%v", shucklePort)
	shuckleOpts := &managedprocess.ShuckleOpts{
		Exe:         goExes.ShuckleExe,
		BincodePort: shucklePort,
		LogLevel:    level,
		Dir:         path.Join(*dataDir, "shuckle"),
	}
	if *blockServiceKiller {
		shuckleOpts.Stale = time.Hour * 1000 // never
	}
	procs.StartShuckle(log, shuckleOpts)

	failureDomains := 14 + 4 // so that any 4 can fail and we can still do everything.
	hddBlockServices := 10
	flashBlockServices := 10
	blockServicesProcs := make(map[managedprocess.ManagedProcessId]blockServiceVictim)
	{
		storageClasses := make([]msgs.StorageClass, hddBlockServices+flashBlockServices)
		for i := 0; i < hddBlockServices+flashBlockServices; i++ {
			storageClasses[i] = msgs.HDD_STORAGE
			if i >= hddBlockServices {
				storageClasses[i] = msgs.FLASH_STORAGE
			}
		}
		for i := 0; i < failureDomains; i++ {
			bsv := blockServiceVictim{
				failureDomain:  fmt.Sprintf("%d", i),
				path:           path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
				storageClasses: storageClasses,
			}
			procId := bsv.start(
				log,
				goExes.BlocksExe,
				shucklePort,
				0, 0,
				*profile,
				procs,
			)
			blockServicesProcs[procId] = bsv
		}
	}

	if *outgoingPacketDrop > 0 {
		fmt.Printf("will drop %0.2f%% of packets after executing requests\n", *outgoingPacketDrop*100.0)
	}
	if *incomingPacketDrop > 0 {
		fmt.Printf("will drop %0.2f%% of packets before executing requests\n", *outgoingPacketDrop*100.0)
	}

	// Start CDC
	cdcOpts := &managedprocess.CDCOpts{
		Exe:            cppExes.CDCExe,
		Dir:            path.Join(*dataDir, "cdc"),
		LogLevel:       level,
		Valgrind:       *buildType == "valgrind",
		Perf:           *profile,
		ShuckleAddress: shuckleAddress,
		OwnIp1:         "127.0.0.1",
		OwnIp2:         "127.0.0.1",
	}
	if *buildType == "valgrind" {
		// apparently 100ms is too little when running with valgrind
		cdcOpts.ShardTimeout = time.Millisecond * 500
	}
	procs.StartCDC(log, *repoDir, cdcOpts)

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
			OwnIp1:             "127.0.0.1",
			OwnIp2:             "127.0.0.1",
		}
		procs.StartShard(log, *repoDir, &shopts)
	}

	waitShuckleFor := 10 * time.Second
	if *buildType == "valgrind" || *profile {
		waitShuckleFor = 60 * time.Second
	}
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	shuckleInfo := lib.WaitForShuckle(log, fmt.Sprintf("127.0.0.1:%v", shucklePort), failureDomains*(hddBlockServices+flashBlockServices), waitShuckleFor)
	blockServicesPorts := make(map[[16]byte]struct {
		_1 uint16
		_2 uint16
	})
	for _, bs := range shuckleInfo.BlockServices {
		blockServicesPorts[bs.FailureDomain] = struct {
			_1 uint16
			_2 uint16
		}{bs.Port1, bs.Port2}
	}

	var stopBlockServiceKiller chan struct{}
	if *blockServiceKiller {
		fmt.Printf("will kill block services\n")
		stopBlockServiceKiller = make(chan struct{}, 1)
		killBlockServices(log, terminateChan, stopBlockServiceKiller, goExes.BlocksExe, shucklePort, *profile, procs, blockServicesProcs, blockServicesPorts)
		// stop before trying to clean up data dir etc.
		defer func() {
			stopBlockServiceKiller <- struct{}{}
			<-stopBlockServiceKiller
		}()
	}

	fuseMountPoint := procs.StartFuse(log, &managedprocess.FuseOpts{
		Exe:            goExes.FuseExe,
		Path:           path.Join(*dataDir, "fuse"),
		LogLevel:       level,
		Wait:           true,
		ShuckleAddress: shuckleAddress,
		Profile:        *profile,
	})

	var mountPoint string
	if *kmod {
		mountPoint = path.Join(*dataDir, "kmod", "mnt")
		err := os.MkdirAll(mountPoint, 0777)
		if err != nil {
			panic(err)
		}
		mountKmod(shucklePort, mountPoint)
		defer func() {
			out, err := exec.Command("sudo", "umount", mountPoint).CombinedOutput()
			if err != nil {
				fmt.Printf("could not umount fs (%v): %s", err, out)
			}
		}()
	} else {
		mountPoint = fuseMountPoint
	}

	fmt.Printf("operational 🤖\n")

	// start tests
	go func() {
		// if the block service killer is on, we just can't reliably GC,
		// because some of the block services might be down.
		r := RunTests{
			acceptGcFailures: *blockServiceKiller,
			overrides:        &overrides,
			shuckleIp:        "127.0.0.1",
			shucklePort:      shucklePort,
			mountPoint:       mountPoint,
			fuseMountPoint:   fuseMountPoint,
			kmod:             *kmod,
			short:            *short,
			filter:           filterRe,
			// TODO write explanation for this
			writeDirectInMountedTest: *blockServiceKiller,
		}
		r.run(terminateChan, log)
	}()

	// wait for things to finish
	err := <-terminateChan
	if err != nil {
		panic(err)
	}
	// we haven't panicked, allow to cleanup the db dir if appropriate
	cleanupDbDir = tmpDataDir && !*preserveDbDir && !*profile && !*profileTest
}
