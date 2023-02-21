// Utility to quickly bring up a full eggsfs, including all its components,
// while hopefully not leaking processes left and right when this process dies.
package main

func main() {

}

/*
import (
	"flag"
	"fmt"
	"os"
	"path"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	buildType := flag.String("build-type", "alpine", "C++ build type, one of alpine/release/debug/sanitized/valgrind.")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	dataDir := flag.String("data-dir", "", "Directory where to store the EggsFS data. If not present a temporary directory will be used.")
	hddBlockServices := flag.Uint("hdd-block-services", 20, "Number of HDD block services.")
	flashBlockServices := flag.Uint("flash-block-services", 20, "Number of HDD block services.")
	profile := flag.Bool("profile", false, "Whether to run code (both Go and C++) with profiling.")
	ownIp := flag.String("own-ip", "127.0.0.1", "What IP to advertise to shuckle for these services.")
	shuckleBincodePort := flag.Uint("shuckle-bincode-port", 10001, "")
	shuckleHttpPort := flag.Uint("shuckle-http-port", 10000, "")
	startingPort := flag.Uint("start-port", 10002, "The services will be assigned port in this order, CDC, shard_000, ..., shard_255, bs_0, ..., bs_n. Otherwise ports will be chosen randomly.")
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

	if *dataDir == "" {
		dir, err := os.MkdirTemp("", "eggsrun.")
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
	log := lib.NewLogger(level, logOut)

	fmt.Printf("building shard/cdc/blockservice/shuckle\n")

	cppExes := lib.BuildCppExes(log, *buildType)
	shuckleExe := lib.BuildShuckleExe(log)
	blockServiceExe := lib.BuildBlockServiceExe(log)
	eggsFuseExe := lib.BuildEggsFuseExe(log)

	terminateChan := make(chan any, 1)

	procs := lib.NewManagedProcesses(terminateChan)
	defer procs.Close()

	fmt.Printf("starting components\n")

	// Start shuckle
	shuckleAddress := fmt.Sprintf("127.0.0.1:%v", *shuckleBincodePort)
	procs.StartShuckle(log, &lib.ShuckleOpts{
		Exe:         shuckleExe,
		BincodePort: uint16(*shuckleBincodePort),
		HttpPort:    uint16(*shuckleHttpPort),
		Verbose:     *verbose,
		Dir:         path.Join(*dataDir, "shuckle"),
	})

	// Start block services
	for i := uint(0); i < *hddBlockServices+*flashBlockServices; i++ {
		storageClass := msgs.HDD_STORAGE
		if i >= *hddBlockServices {
			storageClass = msgs.FLASH_STORAGE
		}
		opts := lib.BlockServiceOpts{
			Exe:            blockServiceExe,
			Path:           path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
			StorageClass:   storageClass,
			FailureDomain:  fmt.Sprintf("%d", i),
			Verbose:        *verbose,
			ShuckleAddress: shuckleAddress,
			OwnIp1:         *ownIp,
			Profile:        *profile,
		}
		if *startingPort != 0 {
			opts.Port1 = uint16(*startingPort) + 257 + uint16(i)
		}
		procs.StartBlockService(log, &opts)
	}

	// Start CDC
	{
		opts := lib.CDCOpts{
			Exe:            cppExes.CDCExe,
			Dir:            path.Join(*dataDir, "cdc"),
			Verbose:        *verbose,
			Valgrind:       *buildType == "valgrind",
			ShuckleAddress: shuckleAddress,
			OwnIp:          *ownIp,
			Perf:           *profile,
		}
		if *startingPort != 0 {
			opts.Port = uint16(*startingPort)
		}
		procs.StartCDC(log, &opts)
	}

	// Start shards
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		opts := lib.ShardOpts{
			Exe:            cppExes.ShardExe,
			Dir:            path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			Verbose:        *verbose,
			Shid:           shid,
			Valgrind:       *buildType == "valgrind",
			ShuckleAddress: shuckleAddress,
			OwnIp:          *ownIp,
			Perf:           *profile,
		}
		if *startingPort != 0 {
			opts.Port = uint16(*startingPort) + 1 + uint16(i)
		}
		procs.StartShard(log, &opts)
	}

	waitShuckleFor := 5 * time.Second
	if *buildType == "valgrind" || *profile {
		waitShuckleFor = 30 * time.Second
	}
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	lib.WaitForShuckle(log, shuckleAddress, int(*hddBlockServices+*flashBlockServices), waitShuckleFor)

	procs.StartFuse(log, &lib.FuseOpts{
		Exe:            eggsFuseExe,
		Path:           path.Join(*dataDir, "eggsfuse"),
		Verbose:        *verbose,
		Wait:           true,
		ShuckleAddress: shuckleAddress,
		Profile:        *profile,
	})

	fmt.Printf("operational 🤖\n")

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
*/
