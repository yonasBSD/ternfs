// Utility to quickly bring up a full eggsfs, including all its components,
// while hopefully not leaking processes left and right when this process dies.
package main

import (
	"flag"
	"fmt"
	"path"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func main() {
	dataDir := flag.String("dir", "", "Directory where to store all the databases. Must be provided.")
	valgrind := flag.Bool("valgrind", false, "Whether to build/run with valgrind.")
	sanitize := flag.Bool("sanitize", false, "Whether to build with sanitize.")
	debug := flag.Bool("debug", false, "Whether to build without optimizations.")
	verbose := flag.Bool("verbose", false, "Whether to run the tools as verbose. Note that all the logs will be written to files anyway.")
	hddBlockServices := flag.Uint("hdd-block-services", 10, "Number of HDD block services (default 10).")
	flashBlockServices := flag.Uint("flash-block-services", 5, "Number of HDD block services (default 5).")
	flag.Parse()

	if *dataDir == "" {
		panic("-dir must be provided.")
	}

	if *verbose && !*debug {
		panic("You asked me to build without -debug, and with -verbose. This is almost certainly wrong.")
	}

	shardExe := eggs.BuildShardExe(&eggs.LogToStdout{}, &eggs.BuildShardOpts{
		Valgrind: *valgrind,
		Sanitize: *sanitize,
		Debug:    *debug,
	})
	shuckleExe := eggs.BuildShuckleExe(&eggs.LogToStdout{})

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
	procs.StartCDC(path.Join(*dataDir, "cdc"), *verbose)

	fmt.Printf("waiting for shuckle for 10 seconds...\n")
	eggs.WaitForShuckle(fmt.Sprintf("localhost:%v", shucklePort), int(*hddBlockServices+*flashBlockServices), 10*time.Second)

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

	fmt.Printf("waiting for shards for 10 seconds...\n")
	for i := 0; i < 256; i++ {
		eggs.WaitForShard(msgs.ShardId(i), 10*time.Second)
	}

	fmt.Printf("operational 🤖\n")

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
