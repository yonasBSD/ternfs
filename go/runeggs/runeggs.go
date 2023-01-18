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
	dataDir := flag.String("dir", "", "Directory where to store all the databases. If not present a tmp dir will be used.")
	valgrind := flag.Bool("valgrind", false, "Whether to build/run with valgrind.")
	sanitize := flag.Bool("sanitize", false, "Whether to build with sanitize.")
	debug := flag.Bool("debug", false, "Whether to build without optimizations.")
	verbose := flag.Bool("verbose", false, "Whether to run the tools as verbose. Note that all the logs will be written to files anyway.")
	hddBlockServices := flag.Uint("hdd-block-services", 10, "Number of HDD block services (default 10).")
	flashBlockServices := flag.Uint("flash-block-services", 5, "Number of HDD block services (default 5).")
	flag.Parse()
	noRunawayArgs()

	if *verbose && !*debug {
		panic("You asked me to build without -debug, and with -verbose. This is almost certainly wrong.")
	}

	if *dataDir == "" {
		dir, err := os.MkdirTemp("", "runeggs.")
		if err != nil {
			panic(fmt.Errorf("could not create tmp data dir: %w", err))
		}
		*dataDir = dir
		fmt.Printf("running with temp data dir %v\n", *dataDir)
	}

	log := &eggs.LogToStdout{}

	cppBuildOpts := eggs.BuildCppOpts{
		Valgrind: *valgrind,
		Sanitize: *sanitize,
		Debug:    *debug,
	}
	shardExe := eggs.BuildShardExe(log, &cppBuildOpts)
	cdcExe := eggs.BuildCDCExe(log, &cppBuildOpts)
	shuckleExe := eggs.BuildShuckleExe(log)

	terminateChan := make(chan any, 1)

	procs := eggs.NewManagedProcesses(terminateChan)
	defer procs.Close()

	fmt.Printf("starting components\n")

	// Start shuckle
	shucklePort := uint16(39999)
	procs.StartShuckle(&eggs.ShuckleOpts{
		Exe:     shuckleExe,
		Port:    shucklePort,
		Verbose: *verbose,
		Dir:     path.Join(*dataDir, "shuckle"),
	})

	// Start block services
	for i := uint(0); i < *hddBlockServices+*flashBlockServices; i++ {
		storageClass := "HDD"
		if i >= *hddBlockServices {
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
	eggs.WaitForShuckle(fmt.Sprintf("localhost:%v", shucklePort), int(*hddBlockServices+*flashBlockServices), waitShuckleFor)

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
		eggs.WaitForShard(log, msgs.ShardId(i), waitShardFor)
	}

	fmt.Printf("operational 🤖\n")

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
