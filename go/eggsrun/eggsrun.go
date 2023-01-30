// Utility to quickly bring up a full eggsfs, including all its components,
// while hopefully not leaking processes left and right when this process dies.
package main

import (
	"flag"
	"fmt"
	"os"
	"path"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

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
	hddBlockServices := flag.Uint("hdd-block-services", 10, "Number of HDD block services (default 10).")
	flashBlockServices := flag.Uint("flash-block-services", 5, "Number of HDD block services (default 5).")
	flag.Parse()
	noRunawayArgs()

	if *verbose && *buildType != "debug" {
		fmt.Printf("We're building with build type %v, which is not \"debug\", and you also passed in -verbose.\nBe aware that you won't get debug messages for C++ binaries.\n\n", *buildType)
	}

	if *dataDir == "" {
		dir, err := os.MkdirTemp("", "eggsrun.")
		if err != nil {
			panic(fmt.Errorf("could not create tmp data dir: %w", err))
		}
		*dataDir = dir
		fmt.Printf("running with temp data dir %v\n", *dataDir)
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
	log := &eggs.LogLogger{
		Verbose: *verbose,
		Logger:  eggs.NewLogger(logOut),
	}

	fmt.Printf("building shard/cdc/blockservice/shuckle\n")

	cppExes := eggs.BuildCppExes(log, *buildType)
	shuckleExe := eggs.BuildShuckleExe(log)
	blockServiceExe := eggs.BuildBlockServiceExe(log)

	terminateChan := make(chan any, 1)

	procs := eggs.NewManagedProcesses(terminateChan)
	defer procs.Close()

	fmt.Printf("starting components\n")

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

	// Start block services
	for i := uint(0); i < *hddBlockServices+*flashBlockServices; i++ {
		storageClass := msgs.HDD_STORAGE
		if i >= *hddBlockServices {
			storageClass = msgs.FLASH_STORAGE
		}
		procs.StartBlockService(log, &eggs.BlockServiceOpts{
			Exe:            blockServiceExe,
			Path:           path.Join(*dataDir, fmt.Sprintf("bs_%d", i)),
			StorageClass:   storageClass,
			FailureDomain:  fmt.Sprintf("%d", i),
			Verbose:        *verbose,
			ShuckleAddress: fmt.Sprintf("localhost:%d", shucklePort),
			OwnIp:          "127.0.0.1",
		})
	}

	// Start CDC
	procs.StartCDC(log, &eggs.CDCOpts{
		Exe:            cppExes.CDCExe,
		Dir:            path.Join(*dataDir, "cdc"),
		Verbose:        *verbose && *buildType == "debug",
		Valgrind:       *buildType == "valgrind",
		ShuckleAddress: shuckleAddress,
		OwnIp:          "127.0.0.1",
	})

	// Start shards
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		procs.StartShard(log, &eggs.ShardOpts{
			Exe:            cppExes.ShardExe,
			Dir:            path.Join(*dataDir, fmt.Sprintf("shard_%03d", i)),
			Verbose:        *verbose && *buildType == "debug",
			Shid:           shid,
			Valgrind:       *buildType == "valgrind",
			ShuckleAddress: shuckleAddress,
			OwnIp:          "127.0.0.1",
		})
	}

	waitShuckleFor := 5 * time.Second
	if *buildType == "valgrind" {
		waitShuckleFor = 30 * time.Second
	}
	fmt.Printf("waiting for shuckle for %v...\n", waitShuckleFor)
	eggs.WaitForShuckle(log, fmt.Sprintf("localhost:%v", shucklePort), int(*hddBlockServices+*flashBlockServices), waitShuckleFor)

	fmt.Printf("operational 🤖\n")

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
