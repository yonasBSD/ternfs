package main

import (
	"flag"
	"fmt"
	"math/rand"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	trace := flag.Bool("trace", false, "Enables debug logging.")
	singleIteration := flag.Bool("single-iteration", false, "Whether to run a single iteration of GC and terminate.")
	logFile := flag.String("log-file", "", "File to log to, stdout if not provided.")
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	syslog := flag.Bool("syslog", false, "")
	mtu := flag.Uint64("mtu", 0, "")
	retryOnDestructFailure := flag.Bool("retry-on-destruct-failure", false, "")
	parallel := flag.Uint("parallel", 0, "Collect all shards in parallel. If 0, no parallelism. If n > 0, the shards will be split in n groups and done in parallel.")
	flag.Parse()

	if *parallel > 0 && *singleIteration {
		fmt.Fprintf(os.Stderr, "-single-iteration is not compatible with -parallel for now. Pick one.")
		os.Exit(2)
	}

	shards := []msgs.ShardId{}
	var appInstance string

	if flag.NArg() < 1 { // all shards
		for i := 0; i < 256; i++ {
			shards = append(shards, msgs.ShardId(i))
		}
	}

	if int(*parallel) > len(shards) {
		fmt.Fprintf(os.Stderr, "-parallel can't be greater than %v (number of shards).", len(shards))
		os.Exit(2)
	}
	if len(shards)%int(*parallel) != 0 {
		fmt.Fprintf(os.Stderr, "-parallel does not divide %v (number of shards).", len(shards))
		os.Exit(2)
	}

	for _, shardStr := range flag.Args() {
		shardI, err := strconv.Atoi(shardStr)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Invalid shard %v: %v", shardStr, err)
			os.Exit(2)
		}
		if shardI < 0 || shardI > 255 {
			fmt.Fprintf(os.Stderr, "Invalid shard %v", shardStr)
			os.Exit(2)
		}
		shards = append(shards, msgs.ShardId(shardI))
	}

	shardsStrs := []string{}
	for _, shard := range shards {
		shardsStrs = append(shardsStrs, fmt.Sprintf("%03d", shard))
	}
	if flag.NArg() >= 1 { // only have instance when shards are provided (otherwise for all shards it's huge)
		appInstance = strings.Join(shardsStrs, ",")
	}

	logOut := os.Stdout
	if *logFile != "" {
		var err error
		logOut, err = os.OpenFile(*logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not open file %v: %v", *logFile, err)
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

	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppName: "gc", AppType: "restech.daytime", AppInstance: appInstance})

	if *mtu != 0 {
		lib.SetMTU(*mtu)
	}

	log.Info("Will run GC in shards %v", strings.Join(shardsStrs, ", "))

	counters := lib.NewClientCounters()

	// print out stats when sent USR1
	{
		statsChan := make(chan os.Signal, 1)
		signal.Notify(statsChan, syscall.SIGUSR1)
		go func() {
			for {
				<-statsChan
				counters.Log(log)
			}
		}()
	}

	// Keep trying forever, we'll alert anyway and it's useful when we restart everything
	//
	// The timeout is _extremely_ lax because we run GC with a lot of parallelism in prod,
	// and we want GC to have low priority.
	options := &lib.GCOptions{
		ShuckleTimeouts:        lib.NewReqTimeouts(lib.DefaultShuckleTimeout.Initial, lib.DefaultShuckleTimeout.Max, 0, lib.DefaultShuckleTimeout.Growth, lib.DefaultShuckleTimeout.Jitter),
		ShardTimeouts:          lib.NewReqTimeouts(5*time.Second, 60*time.Second, 0, lib.DefaultShardTimeout.Growth, lib.DefaultShardTimeout.Jitter),
		CDCTimeouts:            lib.NewReqTimeouts(5*time.Second, 60*time.Second, 0, lib.DefaultCDCTimeout.Growth, lib.DefaultCDCTimeout.Jitter),
		RetryOnDestructFailure: *retryOnDestructFailure,
	}

	dirInfoCache := lib.NewDirInfoCache()
	client, err := lib.GCClient(log, *shuckleAddress, options)
	if err != nil {
		panic(err)
	}
	var cdcMu sync.Mutex
	if *parallel > 0 {
		shardsPerGroup := len(shards) / int(*parallel)
		terminateChan := make(chan any)
		// directories
		for group0 := 0; group0 < int(*parallel); group0++ {
			group := group0
			rand := wyhash.New(uint64(group))
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				for {
					groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
					waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
					log.Info("waiting %v before collecting directories in %+v", waitFor, groupShards)
					time.Sleep(waitFor)
					if err := lib.CollectDirectories(log, client, dirInfoCache, &cdcMu, groupShards); err != nil {
						log.RaiseAlert("could not collect directories: %v", err)
					}
				}
			}()
		}
		// files
		for group0 := 0; group0 < int(*parallel); group0++ {
			group := group0
			rand := wyhash.New(uint64(group))
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				for {
					groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
					waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
					log.Info("waiting %v before destructing files in %v", waitFor, groupShards)
					time.Sleep(waitFor)
					if err := lib.DestructFiles(log, options, client, groupShards); err != nil {
						log.RaiseAlert("could not destruct files: %v", err)
					}
				}
			}()
		}
		err := <-terminateChan
		log.Info("got error, winding down: %v", err)
		panic(err)
	} else {
		rand := wyhash.New(rand.Uint64())
		for {
			if err := lib.CollectDirectories(log, client, dirInfoCache, &cdcMu, shards); err != nil {
				log.RaiseAlert("could not collect directories: %v", err)
			}
			if err := lib.DestructFiles(log, options, client, shards); err != nil {
				log.RaiseAlert("could not destruct files: %v", err)
			}
			if *singleIteration {
				goto Finish
			}
			waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
			log.Info("waiting %v before collecting again", waitFor)
			time.Sleep(waitFor)
		}
	Finish:
	}
}
