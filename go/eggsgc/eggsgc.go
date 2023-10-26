package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	trace := flag.Bool("trace", false, "Enables debug logging.")
	logFile := flag.String("log-file", "", "File to log to, stdout if not provided.")
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	syslog := flag.Bool("syslog", false, "")
	mtu := flag.Uint64("mtu", 0, "")
	retryOnDestructFailure := flag.Bool("retry-on-destruct-failure", false, "")
	dirsParallel := flag.Uint("directories-parallel", 1, "")
	filesParallel := flag.Uint("files-parallel", 1, "")
	metrics := flag.Bool("metrics", false, "")
	flag.Parse()

	if *dirsParallel < 1 || *filesParallel < 1 {
		fmt.Fprintf(os.Stderr, "-directories-parallel/-files-parallel must be at least 1.")
		os.Exit(2)
	}

	shards := []msgs.ShardId{}
	var appInstance string

	if flag.NArg() < 1 { // all shards
		for i := 0; i < 256; i++ {
			shards = append(shards, msgs.ShardId(i))
		}
	}

	if int(*filesParallel) > len(shards) || int(*dirsParallel) > len(shards) {
		fmt.Fprintf(os.Stderr, "-directories-parallel/-files-parallel can't be greater than %v (number of shards).", len(shards))
		os.Exit(2)
	}
	if len(shards)%int(*filesParallel) != 0 || len(shards)%int(*dirsParallel) != 0 {
		fmt.Fprintf(os.Stderr, "-directories-parallel/-files-parallel does not divide %v (number of shards).", len(shards))
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

	// Keep trying forever, we'll alert anyway and it's useful when we restart everything
	options := &lib.GCOptions{
		ShuckleTimeouts:        &lib.DefaultShuckleTimeout,
		ShardTimeouts:          &lib.DefaultShardTimeout,
		CDCTimeouts:            &lib.DefaultCDCTimeout,
		RetryOnDestructFailure: *retryOnDestructFailure,
	}
	options.ShuckleTimeouts.Overall = 0
	options.ShardTimeouts.Overall = 0
	options.CDCTimeouts.Overall = 0

	dirInfoCache := lib.NewDirInfoCache()
	client, err := lib.GCClient(log, *shuckleAddress, options)
	if err != nil {
		panic(err)
	}
	var cdcMu sync.Mutex
	terminateChan := make(chan any)
	var stats lib.GCStats

	// directories
	for group0 := 0; group0 < int(*dirsParallel); group0++ {
		shardsPerGroup := len(shards) / int(*dirsParallel)
		group := group0
		rand := wyhash.New(uint64(group))
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
				waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
				log.Info("waiting %v before collecting directories in %+v", waitFor, groupShards)
				time.Sleep(waitFor)
				if err := lib.CollectDirectories(log, client, dirInfoCache, &cdcMu, &stats, groupShards); err != nil {
					log.RaiseAlert("could not collect directories: %v", err)
				}
			}
		}()
	}
	// files
	for group0 := 0; group0 < int(*filesParallel); group0++ {
		shardsPerGroup := len(shards) / int(*filesParallel)
		group := group0
		rand := wyhash.New(uint64(group))
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
				waitFor := time.Millisecond * time.Duration(rand.Uint64()%(30_000))
				log.Info("waiting %v before destructing files in %v", waitFor, groupShards)
				time.Sleep(waitFor)
				if err := lib.DestructFiles(log, options, client, &stats, groupShards); err != nil {
					log.RaiseAlert("could not destruct files: %v", err)
				}
			}
		}()
	}
	// zero block services
	for group0 := 0; group0 < int(*dirsParallel); group0++ {
		shardsPerGroup := len(shards) / int(*dirsParallel)
		group := group0
		rand := wyhash.New(uint64(group))
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
				// just do that once an hour, we don't need this often.
				waitFor := time.Second * time.Duration(rand.Uint64()%(60*60))
				log.Info("waiting %v before collecting zero block service files in %v", waitFor, groupShards)
				time.Sleep(waitFor)
				if err := lib.CollectZeroBlockServiceFiles(log, client, &stats, groupShards); err != nil {
					log.RaiseAlert("could not collecting zero block service files: %v", err)
				}
			}
		}()
	}
	if *metrics {
		// one thing just pushing the stats every minute
		go func() {
			metrics := lib.MetricsBuilder{}
			alert := log.NewNCAlert(10 * time.Second)
			for {
				log.Info("sending stats metrics")
				now := time.Now()
				metrics.Measurement("eggsfs_gc")
				metrics.FieldU64("visited_files", atomic.LoadUint64(&stats.VisitedFiles))
				metrics.FieldU64("destructed_files", atomic.LoadUint64(&stats.DestructedFiles))
				metrics.FieldU64("destructed_spans", atomic.LoadUint64(&stats.DestructedSpans))
				metrics.FieldU64("skipped_spans", atomic.LoadUint64(&stats.SkippedSpans))
				metrics.FieldU64("destructed_blocks", atomic.LoadUint64(&stats.DestructedBlocks))
				metrics.FieldU64("failed_files", atomic.LoadUint64(&stats.FailedFiles))
				metrics.FieldU64("visited_directories", atomic.LoadUint64(&stats.VisitedDirectories))
				metrics.FieldU64("visited_edges", atomic.LoadUint64(&stats.VisitedEdges))
				metrics.FieldU64("collected_edges", atomic.LoadUint64(&stats.CollectedEdges))
				metrics.FieldU64("destructed_directories", atomic.LoadUint64(&stats.DestructedDirectories))
				metrics.FieldU64("zero_block_service_files_removed", atomic.LoadUint64(&stats.ZeroBlockServiceFilesRemoved))
				metrics.Timestamp(now)
				err := lib.SendMetrics(metrics.Payload())
				if err == nil {
					log.ClearNC(alert)
					sleepFor := time.Second * 30
					log.Info("metrics sent, sleeping for %v", sleepFor)
					time.Sleep(sleepFor)
				} else {
					log.RaiseNC(alert, "failed to send metrics, will try again in a second: %v", err)
					time.Sleep(time.Second)
				}
			}
		}()
	}
	mbErr := <-terminateChan
	log.Info("got error, winding down: %v", mbErr)
	panic(mbErr)
}
