// Utility to quickly bring up a full eggsfs, including all its components,
// while hopefully not leaking processes left and right when this process dies.
package main

import (
	"flag"
	"fmt"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func main() {
	dataDir := flag.String("dir", "", "Directory where to store all the databases. Must be provided.")
	build := flag.Bool("build", false, "Whether to build the shard executable ourselves. Otherwise -shard-exe must be provided.")
	valgrind := flag.Bool("valgrind", false, "Whether to build/run with valgrind. Has no effect if specified without -build.")
	sanitize := flag.Bool("sanitize", false, "Whether to build with sanitize. Has no effect if specified without -build.")
	debug := flag.Bool("debug", false, "Whether to build without optimizations. Has no effect if specified without -build.")
	shardExe := flag.String("shard-exe", "", "The shard executable. If not present, -build must be set.")
	verbose := flag.Bool("verbose", false, "Whether to run the tools as verbose. Note that all the logs will be written to files anyway.")
	hddBlockServices := flag.Uint("hdd-block-services", 10, "Number of HDD block services (default 10).")
	flashBlockServices := flag.Uint("flash-block-services", 5, "Number of HDD block services (default 5).")
	flag.Parse()

	if *dataDir == "" {
		panic("-dir must be provided.")
	}

	if (*build && *shardExe != "") || (!*build && *shardExe == "") {
		panic("Exactly one out of -build and -shard-exe must be provided.")
	}

	if *sanitize && *valgrind {
		panic("Cannot use -sanitize and -valgrind at the same time.")
	}

	if *build && *verbose && !*debug {
		panic("You asked me to build without -debug, and with -verbose. This is almost certainly wrong.")
	}

	if *build {
		buildOpts := eggs.BuildShardOpts{
			Valgrind: *valgrind,
			Sanitize: *sanitize,
			Debug:    *debug,
		}
		*shardExe = eggs.BuildShardExe(&eggs.LogToStdout{}, &buildOpts)
	}

	terminateChan := make(chan any, 1)

	procs := eggs.NewManagedProcesses(terminateChan)
	defer procs.Close()

	fmt.Printf("starting components\n")

	// Start block services
	for i := uint(0); i < *hddBlockServices+*flashBlockServices; i++ {
		storageClass := "HDD"
		if i >= *hddBlockServices {
			storageClass = "FLASH"
		}
		procs.StartBlockService(&eggs.BlockServiceOpts{
			Path:          fmt.Sprintf("%s/bs_%d", *dataDir, i),
			Port:          40000 + uint16(i),
			StorageClass:  storageClass,
			FailureDomain: fmt.Sprintf("%d", i),
			Verbose:       *verbose,
		})
	}

	// Start shuckle
	// procs = append(procs, startShuckle(terminateChan, fmt.Sprintf("%s/shuckle", *dataDir), *verbose))

	// Start CDC
	procs.StartCDC(fmt.Sprintf("%s/cdc", *dataDir), *verbose)

	// Start shards
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		procs.StartShard(&eggs.ShardOpts{
			Exe:     *shardExe,
			Dir:     fmt.Sprintf("%s/shard_%03d", *dataDir, i),
			Verbose: *verbose,
			Shid:    shid,
		})
	}

	fmt.Printf("operational 🤖\n")

	err := <-terminateChan
	if err != nil {
		panic(err)
	}
}
