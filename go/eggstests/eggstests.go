package main

import (
	"bytes"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"os/exec"
	"path"
	"regexp"
	"runtime"
	"runtime/pprof"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/managedprocess"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
	"xtx/eggsfs/wyhash"
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

func formatCounters[K ~uint8](what string, counters map[uint8]*lib.ReqCounters) {
	fmt.Printf("    %s reqs count/attempts/avg/median/total:\n", what)
	for k, count := range counters {
		if count.Attempts == 0 {
			continue
		}
		fmt.Printf("      %-30v %10v %6.2f %7s %7s %7s\n", K(k), count.Timings.Count(), float64(count.Attempts)/float64(count.Timings.Count()), formatNanos(uint64(count.Timings.Mean().Nanoseconds())), formatNanos(uint64(count.Timings.Median().Nanoseconds())), formatNanos(uint64(count.Timings.TotalTime())))
	}
}

func totalRequests[K comparable](cs map[K]*lib.ReqCounters) uint64 {
	total := uint64(0)
	for _, c := range cs {
		total += c.Timings.Count()
	}
	return total
}

type RunTests struct {
	overrides               *cfgOverrides
	shuckleIp               string
	shucklePort             uint16
	mountPoint              string
	fuseMountPoint          string
	kmod                    bool
	short                   bool
	filter                  *regexp.Regexp
	pauseBlockServiceKiller *sync.Mutex
}

func (r *RunTests) shuckleAddress() string {
	return fmt.Sprintf("%s:%d", r.shuckleIp, r.shucklePort)
}

func (r *RunTests) test(
	log *lib.Logger,
	name string,
	extra string,
	run func(counters *lib.ClientCounters),
) {
	if !r.filter.Match([]byte(name)) {
		fmt.Printf("skipping test %s\n", name)
		return
	}

	counters := lib.NewClientCounters()

	fmt.Printf("running %s test, %s\n", name, extra)
	log.Info("running %s test, %s\n", name, extra) // also in log to track progress in CI more easily
	t0 := time.Now()
	run(counters)
	elapsed := time.Since(t0)

	totalShardRequests := totalRequests(counters.Shard)
	totalCDCRequests := totalRequests(counters.CDC)
	fmt.Printf("  ran test in %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters[msgs.ShardMessageKind]("shard", counters.Shard)
	}
	if totalCDCRequests > 0 {
		formatCounters[msgs.CDCMessageKind]("CDC", counters.CDC)
	}

	counters = lib.NewClientCounters()
	t0 = time.Now()
	cleanupAfterTest(log, r.shuckleAddress(), counters, r.pauseBlockServiceKiller)
	elapsed = time.Since(t0)
	totalShardRequests = totalRequests(counters.Shard)
	totalCDCRequests = totalRequests(counters.CDC)
	fmt.Printf("  cleanup took %v, %v shard requests performed, %v CDC requests performed\n", elapsed, totalShardRequests, totalCDCRequests)
	if totalShardRequests > 0 {
		formatCounters[msgs.ShardMessageKind]("shard", counters.Shard)
	}
	if totalCDCRequests > 0 {
		formatCounters[msgs.CDCMessageKind]("CDC", counters.CDC)
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

func (r *RunTests) run(
	terminateChan chan any,
	log *lib.Logger,
) {
	defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
	client, err := lib.NewClient(log, nil, r.shuckleAddress(), 1)
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
		fileHistoryOpts.targetFiles = 100
		fileHistoryOpts.lowFiles = 50
		fileHistoryOpts.steps = r.overrides.int("fileHistory.steps", 1000)
	}
	r.test(
		log,
		"file history",
		fmt.Sprintf("%v threads, %v steps", fileHistoryOpts.threads, fileHistoryOpts.steps),
		func(counters *lib.ClientCounters) {
			fileHistoryTest(log, r.shuckleAddress(), &fileHistoryOpts, counters)
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
		fsTestOpts.numDirs = r.overrides.int("fsTest.numDirs", 10)
		fsTestOpts.numFiles = r.overrides.int("fsTest.numFiles", 500)
		fsTestOpts.emptyFileProb = 0.1
		fsTestOpts.inlineFileProb = 0.3
	} else {
		fsTestOpts.numDirs = r.overrides.int("fsTest.numDirs", 400)
		fsTestOpts.numFiles = r.overrides.int("fsTest.numFiles", 10*400)
		fsTestOpts.emptyFileProb = 0.1
		fsTestOpts.inlineFileProb = 0.3
	}

	r.test(
		log,
		"direct fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, r.shuckleAddress(), &fsTestOpts, counters, "")
		},
	)

	r.test(
		log,
		"mounted fs",
		fmt.Sprintf("%v dirs, %v files, %v depth", fsTestOpts.numDirs, fsTestOpts.numFiles, fsTestOpts.depth),
		func(counters *lib.ClientCounters) {
			fsTest(log, r.shuckleAddress(), &fsTestOpts, counters, r.mountPoint)
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
	r.test(
		log,
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
	r.test(
		log,
		"rsync large",
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
	r.test(
		log,
		"rsync small",
		fmt.Sprintf("%v files, %v dirs, %vMB file size", rsyncOpts.numFiles, rsyncOpts.numDirs, float64(rsyncOpts.maxFileSize)/1e6),
		func(counters *lib.ClientCounters) {
			rsyncTest(log, &rsyncOpts, r.mountPoint)
		},
	)

	r.test(
		log,
		"cp",
		"",
		func(counters *lib.ClientCounters) {
			from := path.Join(r.mountPoint, "test-1")
			to := path.Join(r.mountPoint, "test-2")
			contents := []byte("foo")
			if err := ioutil.WriteFile(from, contents, 0644); err != nil {
				panic(err)
			}
			if err := exec.Command("cp", from, to).Run(); err != nil {
				panic(err)
			}
			contents1, err := os.ReadFile(from)
			if err != nil {
				panic(err)
			}
			if !bytes.Equal(contents, contents1) {
				panic(fmt.Errorf("expected %v, got %v", contents, contents1))
			}
			contents2, err := os.ReadFile(from)
			if err != nil {
				panic(err)
			}
			if !bytes.Equal(contents, contents2) {
				panic(fmt.Errorf("expected %v, got %v", contents, contents2))
			}
			/*
				// now try shell redirection
				f := path.Join(r.mountPoint, "test-3")
				if err := exec.Command("sh", "-c", fmt.Sprintf("echo foo > %s", f)); err != nil {
					panic(err)
				}
				contents3, err := os.ReadFile(from)
				if err != nil {
					panic(err)
				}
				if !bytes.Equal(contents, contents3) {
					panic(fmt.Errorf("expected %v, got %v", contents, contents3))
				}
			*/
		},
	)

	r.test(
		log,
		"utime",
		"",
		func(counters *lib.ClientCounters) {
			fn := path.Join(r.mountPoint, "test")
			if err := ioutil.WriteFile(fn, []byte{}, 0644); err != nil {
				panic(err)
			}
			time1 := time.Now()
			time2 := time.Now()
			if time1 == time2 {
				panic(fmt.Errorf("same times"))
			}
			if err := syscall.UtimesNano(fn, []syscall.Timespec{syscall.NsecToTimespec(time1.UnixNano()), syscall.NsecToTimespec(time2.UnixNano())}); err != nil {
				panic(err)
			}
			info, err := os.Stat(fn)
			if err != nil {
				panic(err)
			}
			stat_t := info.Sys().(*syscall.Stat_t)
			atime := time.Unix(int64(stat_t.Atim.Sec), int64(stat_t.Atim.Nsec))
			if atime.UnixNano() != time1.UnixNano() {
				panic(fmt.Errorf("expected atime %v, got %v", time1, atime))
			}
			mtime := time.Unix(int64(stat_t.Mtim.Sec), int64(stat_t.Mtim.Nsec))
			if mtime.UnixNano() != time2.UnixNano() {
				panic(fmt.Errorf("expected mtime %v, got %v", time2, mtime))
			}
			// atime is updated when opened
			time.Sleep(time.Millisecond)
			now := time.Now()
			time.Sleep(time.Millisecond)
			if _, err := ioutil.ReadFile(fn); err != nil {
				panic(err)
			}
			info, err = os.Stat(fn)
			if err != nil {
				panic(err)
			}
			stat_t = info.Sys().(*syscall.Stat_t)
			atime = time.Unix(int64(stat_t.Atim.Sec), int64(stat_t.Atim.Nsec))
			if now.UnixNano() > atime.UnixNano() {
				panic(fmt.Errorf("atime didn't update, %v > %v", now, atime))
			}
			// make sure O_NOATIME is respected
			file, err := os.OpenFile(fn, syscall.O_RDONLY|syscall.O_NOATIME, 0)
			if err != nil {
				panic(err)
			}
			info, err = os.Stat(fn)
			file.Close()
			if err != nil {
				panic(err)
			}
			stat_t = info.Sys().(*syscall.Stat_t)
			newAtime := time.Unix(int64(stat_t.Atim.Sec), int64(stat_t.Atim.Nsec))
			if atime != newAtime {
				panic(fmt.Errorf("expected atime to be still %v, but got %v", atime, newAtime))
			}
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
		FutureCutoff:   &testBlockFutureCutoff,
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
	pause *sync.Mutex,
	blocksExe string,
	shucklePort uint16,
	profile bool,
	procs *managedprocess.ManagedProcesses,
	bsProcs map[managedprocess.ManagedProcessId]blockServiceVictim,
	bsPorts map[msgs.FailureDomain]struct {
		_1 uint16
		_2 uint16
	},
) {
	// right now the kmod does not really digest multiple dead block services
	// at once when writing, because it only remembers the failures for the current
	// request. we should make it so that older failures are remembered too.
	// in any case, this means that for now we only kill one block service at a time.
	killDuration := time.Second * 10
	log.Info("will kill block service for %v", killDuration)
	rand := wyhash.New(uint64(time.Now().UnixNano()))
	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		for {
			// pick and kill the victim
			pause.Lock()
			var victim blockServiceVictim
			{
				ix := int(rand.Uint64()) % len(bsProcs)
				j := 0
				var procId managedprocess.ManagedProcessId
				for procId = range bsProcs {
					if j >= ix {
						victim = bsProcs[procId]
						delete(bsProcs, procId)
						log.Info("killing %v", victim.path)
						procs.Kill(procId, syscall.SIGKILL)
						break
					}
					j++
				}
			}
			pause.Unlock()
			// wait
			sleepChan := make(chan struct{}, 1)
			go func() {
				time.Sleep(killDuration)
				sleepChan <- struct{}{}
			}()
			select {
			case <-stopChan:
				stopChan <- struct{}{} // reply
				return
			case <-sleepChan:
			}
			// revive the victims -- important to preserve ports
			// so that shard blockservice cache data won't be
			// invalidated (we don't want to have to wait for it
			// to be refreshed for the tests to work)
			{
				log.Info("reviving %v", victim.path)
				var failureDomain msgs.FailureDomain
				copy(failureDomain.Name[:], victim.failureDomain)
				ports := bsPorts[failureDomain]
				procId := victim.start(
					log,
					blocksExe,
					shucklePort,
					ports._1,
					ports._2,
					profile,
					procs,
				)
				bsProcs[procId] = victim
			}
		}
	}()
}

// 0 interval won't do, because otherwise transient files will immediately be
// expired and not picked.
var testTransientDeadlineInterval = 10 * time.Second
var testBlockFutureCutoff = testTransientDeadlineInterval / 2

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
	short := flag.Bool("short", false, "Run a shorter version of the tests")
	repoDir := flag.String("repo-dir", "", "Used to build C++/Go binaries. If not provided, the path will be derived form the filename at build time (so will only work locally).")
	binariesDir := flag.String("binaries-dir", "", "If provided, nothing will be built, instead it'll be assumed that the binaries will be in the specified directory.")
	kmod := flag.Bool("kmod", false, "Whether to mount with the kernel module, rather than FUSE. Note that the tests will not attempt to run the kernel module and load it, they'll just mount with 'mount -t eggsfs'.")
	dropCachedSpansEvery := flag.Duration("drop-cached-spans-every", 0, "If set, will repeatedly drop the cached spans using 'sysctl fs.eggsfs.drop_cached_spans=1'")
	dropFetchBlockSocketsEvery := flag.Duration("drop-fetch-block-sockets-every", 0, "")
	changeBlockPolicyEvery := flag.Duration("change-block-policy-every", 0, "")
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

	// If we're running with kmod, set the timeout to be very high
	// (in qemu things can be delayed massively). TODO it would be
	// nice to understand the circumstances where the timeout
	// is so high, even inside a very stressed QEMU.
	if *kmod {
		cmd := exec.Command("sudo", "dmidecode", "-s", "system-manufacturer")
		dmiOut, err := cmd.Output()
		if err != nil {
			panic(err)
		}
		isQEMU := strings.TrimSpace(string(dmiOut)) == "QEMU"
		if isQEMU {
			log.Info("increasing metadata timeout since we're in QEMU")
			cmd = exec.Command("sudo", "/usr/sbin/sysctl", "fs.eggsfs.overall_shard_timeout_ms=60000")
			cmd.Stdout = io.Discard
			cmd.Stderr = io.Discard
			if err := cmd.Run(); err != nil {
				panic(err)
			}
			cmd = exec.Command("sudo", "/usr/sbin/sysctl", "fs.eggsfs.overall_cdc_timeout_ms=60000")
			cmd.Stdout = io.Discard
			cmd.Stderr = io.Discard
			if err := cmd.Run(); err != nil {
				panic(err)
			}
		}
	}

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

	// Start fetch block socket dropper
	if *dropFetchBlockSocketsEvery != time.Duration(0) {
		if !*kmod {
			panic(fmt.Errorf("you provided -drop-cached-spans-every but you did't provide -kmod"))
		}
		fmt.Printf("will drop fetch block sockets every %v\n", *dropFetchBlockSocketsEvery)
		go func() {
			for {
				cmd := exec.Command("sudo", "/usr/sbin/sysctl", "fs.eggsfs.drop_fetch_block_sockets=1")
				cmd.Stdout = io.Discard
				cmd.Stderr = io.Discard
				if err := cmd.Run(); err != nil {
					terminateChan <- err
					return
				}
				time.Sleep(*dropFetchBlockSocketsEvery)
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
		shuckleOpts.Stale = time.Hour * 1000 // never, so that we stimulate the clients ability to fallback
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

	waitShuckleFor := 10 * time.Second
	if *buildType == "valgrind" || *profile || *kmod { // kmod often runs in qemu, which is slower
		waitShuckleFor = 60 * time.Second
	}

	// wait for block services first, so we know that shards will immediately have all of them
	fmt.Printf("waiting for block services for %v...\n", waitShuckleFor)
	blockServices := lib.WaitForBlockServices(log, fmt.Sprintf("127.0.0.1:%v", shucklePort), failureDomains*(hddBlockServices+flashBlockServices), waitShuckleFor)
	blockServicesPorts := make(map[msgs.FailureDomain]struct {
		_1 uint16
		_2 uint16
	})
	for _, bs := range blockServices {
		blockServicesPorts[bs.FailureDomain] = struct {
			_1 uint16
			_2 uint16
		}{bs.Port1, bs.Port2}
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
			Exe:                       cppExes.ShardExe,
			Dir:                       path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			LogLevel:                  level,
			Shid:                      shid,
			Valgrind:                  *buildType == "valgrind",
			Perf:                      *profile,
			IncomingPacketDrop:        *incomingPacketDrop,
			OutgoingPacketDrop:        *outgoingPacketDrop,
			ShuckleAddress:            shuckleAddress,
			OwnIp1:                    "127.0.0.1",
			OwnIp2:                    "127.0.0.1",
			TransientDeadlineInterval: &testTransientDeadlineInterval,
		}
		procs.StartShard(log, *repoDir, &shopts)
	}

	// now wait for shards/cdc
	fmt.Printf("waiting for shards/cdc for %v...\n", waitShuckleFor)
	lib.WaitForClient(log, fmt.Sprintf("127.0.0.1:%v", shucklePort), waitShuckleFor)

	var stopBlockServiceKiller chan struct{}
	var pauseBlockServiceKiller sync.Mutex
	if *blockServiceKiller {
		fmt.Printf("will kill block services\n")
		stopBlockServiceKiller = make(chan struct{}, 1)
		killBlockServices(log, terminateChan, stopBlockServiceKiller, &pauseBlockServiceKiller, goExes.BlocksExe, shucklePort, *profile, procs, blockServicesProcs, blockServicesPorts)
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

	// Start block policy changer -- this is useful to test kmod/policy.c machinery
	if *changeBlockPolicyEvery != time.Duration(0) {
		fmt.Printf("will change block policy every %v\n", *dropFetchBlockSocketsEvery)
		client, err := lib.NewClient(log, nil, shuckleAddress, 1)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		blockPolicy := &msgs.BlockPolicy{}
		if _, err := client.ResolveDirectoryInfoEntry(log, lib.NewDirInfoCache(), msgs.ROOT_DIR_INODE_ID, blockPolicy); err != nil {
			panic(err)
		}
		if len(blockPolicy.Entries) != 2 || blockPolicy.Entries[0].MinSize != 0 {
			panic(fmt.Errorf("bad block policy %+v", blockPolicy))
		}
		go func() {
			i := 0
			// ./eggs/eggstests -kmod -binaries-dir eggs -filter 'direct' -short -change-block-policy-every 1s
			for {
				d := 1
				if (i & 1) != 0 {
					d = -1
				}
				i++
				blockPolicy.Entries[1].MinSize = uint32(int(blockPolicy.Entries[1].MinSize) + d) // flip flop between + and -
				if err := client.MergeDirectoryInfo(log, msgs.ROOT_DIR_INODE_ID, blockPolicy); err != nil {
					terminateChan <- err
					return
				}
				time.Sleep(*dropFetchBlockSocketsEvery)
			}
		}()
	}

	// start tests
	go func() {
		r := RunTests{
			overrides:               &overrides,
			shuckleIp:               "127.0.0.1",
			shucklePort:             shucklePort,
			mountPoint:              mountPoint,
			fuseMountPoint:          fuseMountPoint,
			kmod:                    *kmod,
			short:                   *short,
			filter:                  filterRe,
			pauseBlockServiceKiller: &pauseBlockServiceKiller,
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
