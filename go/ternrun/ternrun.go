// Utility to quickly bring up a full ternfs, including all its components,
// while hopefully not leaking processes left and right when this process dies.
package main

import (
	"flag"
	"fmt"
	"os"
	"path"
	"runtime"
	"time"
	"xtx/ternfs/client"
	"xtx/ternfs/lib"
	"xtx/ternfs/managedprocess"
	"xtx/ternfs/msgs"
)

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	buildType := flag.String("build-type", "release", "C++ build type")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	dataDir := flag.String("data-dir", "", "Directory where to store the TernFS data. If not present a temporary directory will be used.")
	failureDomains := flag.Uint("failure-domains", 16, "Number of failure domains.")
	hddBlockServices := flag.Uint("hdd-block-services", 2, "Number of HDD block services per failure domain.")
	flashBlockServices := flag.Uint("flash-block-services", 2, "Number of HDD block services per failure domain.")
	profile := flag.Bool("profile", false, "Whether to run code (both Go and C++) with profiling.")
	shuckleBincodePort := flag.Uint("shuckle-bincode-port", 10001, "")
	shuckleHttpPort := flag.Uint("shuckle-http-port", 10000, "")
	startingPort := flag.Uint("start-port", 10003, "The services will be assigned port in this order, CDC, shard_000, ..., shard_255, bs_0, ..., bs_n. If 0, ports will be chosen randomly.")
	repoDir := flag.String("repo-dir", "", "Used to build C++/Go binaries. If not provided, the path will be derived form the filename at build time (so will only work locally).")
	binariesDir := flag.String("binaries-dir", "", "If provided, nothing will be built, instead it'll be assumed that the binaries will be in the specified directory.")
	xmon := flag.String("xmon", "", "")
	shuckleScriptsJs := flag.String("shuckle-scripts-js", "", "")
	noFuse := flag.Bool("no-fuse", false, "")
	leaderOnly := flag.Bool("leader-only", false, "Run only LogsDB leader with LEADER_NO_FOLLOWERS")
	multiLocation := flag.Bool("multi-location", false, "Run 2 sets of shards/shuckle/cdc/storages to simulate multi data centre setup")
	flag.Parse()
	noRunawayArgs()

	validPort := func(port uint) {
		if port > uint(^uint16(0)) {
			fmt.Fprintf(os.Stderr, "Invalid port %v.\n", port)
			os.Exit(2)
		}
	}
	if *shuckleBincodePort == 0 {
		fmt.Fprintf(os.Stderr, "-shuckle-bincode-port can't be automatically picked.\n")
		os.Exit(2)
	}
	validPort(*shuckleBincodePort)
	validPort(*shuckleHttpPort)
	validPort(*startingPort)

	if *repoDir == "" {
		_, filename, _, ok := runtime.Caller(0)
		if !ok {
			panic("no caller information")
		}
		*repoDir = path.Dir(path.Dir(path.Dir(filename)))
	}

	if *dataDir == "" {
		dir, err := os.MkdirTemp("", "ternrun.")
		if err != nil {
			panic(fmt.Errorf("could not create tmp data dir: %w", err))
		}
		*dataDir = dir
		fmt.Printf("running with temp data dir %v\n", *dataDir)
	} else {
		if err := os.Mkdir(*dataDir, 0777); err != nil && !os.IsExist(err) {
			panic(fmt.Errorf("could not create data dir: %w", err))
		}
	}

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
	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: false, PrintQuietAlerts: true})

	var cppExes *managedprocess.CppExes
	var goExes *managedprocess.GoExes
	if *binariesDir != "" {
		cppExes = &managedprocess.CppExes{
			ShardExe: path.Join(*binariesDir, "ternshard"),
			CDCExe:   path.Join(*binariesDir, "terncdc"),
		}
		goExes = &managedprocess.GoExes{
			ShuckleExe: path.Join(*binariesDir, "ternshuckle"),
			BlocksExe:  path.Join(*binariesDir, "ternblocks"),
			FuseExe:    path.Join(*binariesDir, "ternfuse"),
			ShuckleProxyExe: path.Join(*binariesDir, "ternshuckleproxy"),
		}
	} else {
		fmt.Printf("building shard/cdc/blockservice/shuckle\n")
		cppExes = managedprocess.BuildCppExes(log, *repoDir, *buildType)
		goExes = managedprocess.BuildGoExes(log, *repoDir, false)
	}

	terminateChan := make(chan any, 1)

	procs := managedprocess.New(terminateChan)
	defer procs.Close()

	fmt.Printf("starting components\n")

	// Start shuckle
	shuckleAddress := fmt.Sprintf("127.0.0.1:%v", *shuckleBincodePort)
	procs.StartShuckle(log, &managedprocess.ShuckleOpts{
		Exe:       goExes.ShuckleExe,
		HttpPort:  uint16(*shuckleHttpPort),
		LogLevel:  level,
		Dir:       path.Join(*dataDir, "shuckle"),
		Xmon:      *xmon,
		ScriptsJs: *shuckleScriptsJs,
		Addr1:     shuckleAddress,
	})

	shuckleProxyPort := *shuckleBincodePort + 1
	shuckleProxyAddress := fmt.Sprintf("127.0.0.1:%v", shuckleProxyPort)

	numLocations := 1
	if *multiLocation {
		// Waiting for shuckle
		err := client.WaitForShuckle(log, shuckleAddress, 10*time.Second)
		if err != nil {
			panic(fmt.Errorf("failed to connect to shuckle %v", err))
		}
		_, err = client.ShuckleRequest(log, nil, shuckleAddress, &msgs.CreateLocationReq{1, "location1"})
		if err != nil {
			// it's possible location already exits, try renaming it
			_, err = client.ShuckleRequest(log, nil, shuckleAddress, &msgs.RenameLocationReq{1, "location1"})
			if err != nil {
				panic(fmt.Errorf("failed to create location %v", err))
			}
		}
		procs.StartShuckleProxy(
			log, &managedprocess.ShuckleProxyOpts{
				Exe:       goExes.ShuckleProxyExe,
				LogLevel:  level,
				Dir:       path.Join(*dataDir, "shuckleproxy"),
				Xmon:      *xmon,
				Addr1:     shuckleProxyAddress,
				ShuckleAddress: shuckleAddress,
				Location: 1,
			},
		)
		numLocations = 2
	}
	replicaCount := 5
	if *leaderOnly {
		replicaCount = 1
	}

	// Start block services
	storageClasses := make([]msgs.StorageClass, *hddBlockServices+*flashBlockServices)
	for i := range storageClasses {
		if i >= int(*hddBlockServices) {
			storageClasses[i] = msgs.HDD_STORAGE
		} else {
			storageClasses[i] = msgs.FLASH_STORAGE
		}
	}
	for loc := uint(0); loc < uint(numLocations); loc++ {
		shuckleAddressToUse := shuckleAddress
		if loc == 1{
			shuckleAddressToUse = shuckleProxyAddress
		}

		for i := uint(0); i < *failureDomains; i++ {
			dirName := fmt.Sprintf("bs_%d", i)
			if loc > 0 {
				dirName = fmt.Sprintf("%s_loc%d", dirName, loc)
			}
			opts := managedprocess.BlockServiceOpts{
				Exe:              goExes.BlocksExe,
				Path:             path.Join(*dataDir, dirName),
				StorageClasses:   storageClasses,
				FailureDomain:    fmt.Sprintf("%d_%d", i, loc),
				Location: 		  msgs.Location(loc),
				LogLevel:         level,
				ShuckleAddress:   shuckleAddressToUse,
				Profile:          *profile,
				Xmon:             *xmon,
				ReserverdStorage: 10 << 30, // 10GB
			}
			if *startingPort != 0 {
				opts.Addr1 = fmt.Sprintf("127.0.0.1:%v", uint16(*startingPort)+257*uint16(replicaCount)+uint16(loc**failureDomains)+uint16(i))
			} else {
				opts.Addr1 = "127.0.0.1:0"
				opts.Addr2 = "127.0.0.1:0"
			}
			procs.StartBlockService(log, &opts)
		}
	}

	waitShuckleFor := 10 * time.Second
	if *buildType == "valgrind" || *profile {
		waitShuckleFor = 60 * time.Second
	}
	fmt.Printf("waiting for block services for %v...\n", waitShuckleFor)
	client.WaitForBlockServices(log, shuckleAddress, int(*failureDomains**hddBlockServices**flashBlockServices*uint(numLocations)), true, waitShuckleFor)

	// Start CDC
	{
		for r := uint8(0); r < uint8(replicaCount); r++ {
			dir := path.Join(*dataDir, fmt.Sprintf("cdc_%d", r))
			if r == 0 {
				dir = path.Join(*dataDir, "cdc")
			}
			opts := managedprocess.CDCOpts{
				ReplicaId:      msgs.ReplicaId(r),
				Exe:            cppExes.CDCExe,
				Dir:            dir,
				LogLevel:       level,
				Valgrind:       *buildType == "valgrind",
				ShuckleAddress: shuckleAddress,
				Perf:           *profile,
				Xmon:           *xmon,
			}
			if r == 0 {
				if *leaderOnly {
					opts.LogsDBFlags = []string{"-logsdb-leader", "-logsdb-no-replication"}
				} else {
					opts.LogsDBFlags = []string{"-logsdb-leader"}
				}
			}
			if *startingPort != 0 {
				opts.Addr1 = fmt.Sprintf("127.0.0.1:%v", uint16(*startingPort)+uint16(r))
			} else {
				opts.Addr1 = "127.0.0.1:0"
			}
			procs.StartCDC(log, *repoDir, &opts)
		}
	}

	// Start shards
	for loc := 0; loc < numLocations; loc++ {
		shuckleAddressToUse := shuckleAddress
		if loc == 1{
			shuckleAddressToUse = shuckleProxyAddress
		}
		for i := 0; i < 256; i++ {
			for r := uint8(0); r < uint8(replicaCount); r++ {
				shrid := msgs.MakeShardReplicaId(msgs.ShardId(i), msgs.ReplicaId(r))
				dirName := fmt.Sprintf("shard_%03d_%d", i, r)
				if loc > 0 {
					dirName = fmt.Sprintf("%s_loc%d", dirName, loc)
				}
				opts := managedprocess.ShardOpts{
					Exe:            cppExes.ShardExe,
					Shrid:          shrid,
					Dir:            path.Join(*dataDir, dirName),
					LogLevel:       level,
					Valgrind:       *buildType == "valgrind",
					ShuckleAddress: shuckleAddressToUse,
					Perf:           *profile,
					Xmon:           *xmon,
					Location:       msgs.Location(loc),
					LogsDBFlags:    nil,
				}
				if r == 0 {
					if *leaderOnly {
						opts.LogsDBFlags = []string{"-logsdb-leader", "-logsdb-no-replication"}
					} else {
						opts.LogsDBFlags = []string{"-logsdb-leader"}
					}
				}
				if *startingPort != 0 {
					opts.Addr1 = fmt.Sprintf("127.0.0.1:%v", uint16(*startingPort)+5+uint16(replicaCount)*256*uint16(loc)+uint16(r)*256+uint16(i))
				} else {
					opts.Addr1 = "127.0.0.1:0"
				}
				procs.StartShard(log, *repoDir, &opts)
			}
		}
	}

	fmt.Printf("waiting for shards/cdc for %v...\n", waitShuckleFor)
	client.WaitForClient(log, shuckleAddress, waitShuckleFor)

	if !*noFuse {
		for loc :=0; loc < numLocations; loc++ {
			shuckleAddressToUse := shuckleAddress
			fuseDir := "fuse"
			if loc == 1 {
				shuckleAddressToUse = shuckleProxyAddress
				fuseDir = "fuse1"
			}
			fuseMountPoint := procs.StartFuse(log, &managedprocess.FuseOpts{
				Exe:               goExes.FuseExe,
				Path:              path.Join(*dataDir, fuseDir),
				LogLevel:          level,
				Wait:              true,
				ShuckleAddress:    shuckleAddressToUse,
				Profile:           *profile,
			})

			fmt.Printf("operational, mounted at %v\n", fuseMountPoint)
		}
	} else {
		fmt.Printf("operational\n")
	}

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
