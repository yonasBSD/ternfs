package main

import (
	"flag"
	"fmt"
	"math/rand"
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
	collectDirectories := flag.Bool("collect-directories", false, "")
	destructFiles := flag.Bool("destruct-files", false, "")
	zeroBlockServices := flag.Bool("zero-block-services", false, "")
	parallel := flag.Uint("parallel", 1, "Work will be split in N groups, so for example with -parallel 16 work will be done in groups of 16 shards.")
	metrics := flag.Bool("metrics", false, "Send metrics")
	countMetrics := flag.Bool("count-metrics", false, "Compute and send count metrics")
	flag.Parse()

	if !*destructFiles && !*collectDirectories && !*zeroBlockServices && !*countMetrics {
		fmt.Fprintf(os.Stderr, "Nothing to do!")
		os.Exit(2)
	}

	if *parallel < 1 {
		fmt.Fprintf(os.Stderr, "-parallel must be at least 1.")
		os.Exit(2)
	}

	shards := []msgs.ShardId{}
	var appInstance string

	if flag.NArg() < 1 { // all shards
		for i := 0; i < 256; i++ {
			shards = append(shards, msgs.ShardId(i))
		}
	} else {
		shardsMap := make(map[uint64]struct{})
		for _, shStr := range flag.Args() {
			shid, err := strconv.ParseUint(shStr, 0, 8)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Could not parse %q as shard: %v", shStr, err)
				os.Exit(2)
			}
			shardsMap[shid] = struct{}{}
		}
		for shid := range shardsMap {
			shards = append(shards, msgs.ShardId(shid))
		}
	}

	if int(*parallel) > len(shards) {
		fmt.Fprintf(os.Stderr, "-parallel can't be greater than %v (number of shards).\n", len(shards))
		os.Exit(2)
	}
	if len(shards)%int(*parallel) != 0 {
		fmt.Fprintf(os.Stderr, "-parallel does not divide %v (number of shards).\n", len(shards))
		os.Exit(2)
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

	shardsStrs := []string{}
	for _, shard := range shards {
		shardsStrs = append(shardsStrs, fmt.Sprintf("%03d", shard))
	}
	if flag.NArg() >= 1 { // only have instance when shards are provided (otherwise for all shards it's huge)
		appInstance = strings.Join(shardsStrs, ",")
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

	shardsPerGroup := len(shards) / int(*parallel)

	if *collectDirectories {
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
					if err := lib.CollectDirectories(log, client, dirInfoCache, &cdcMu, &stats, groupShards); err != nil {
						log.RaiseAlert("could not collect directories: %v", err)
					}
				}
			}()
		}
	}
	if *destructFiles {
		// files
		for group0 := 0; group0 < int(*parallel); group0++ {
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
	}
	if *zeroBlockServices {
		// zero block services
		for group0 := 0; group0 < int(*parallel); group0++ {
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
	}
	if *metrics && (*destructFiles || *collectDirectories || *zeroBlockServices) {
		// one thing just pushing the stats every minute
		go func() {
			metrics := lib.MetricsBuilder{}
			alert := log.NewNCAlert(10 * time.Second)
			for {
				log.Info("sending stats metrics")
				now := time.Now()
				metrics.Reset()
				metrics.Measurement("eggsfs_gc")
				if appInstance != "" {
					metrics.Tag("shards", appInstance)
				}
				if *destructFiles {
					metrics.FieldU64("visited_files", atomic.LoadUint64(&stats.VisitedFiles))
					metrics.FieldU64("destructed_files", atomic.LoadUint64(&stats.DestructedFiles))
					metrics.FieldU64("destructed_spans", atomic.LoadUint64(&stats.DestructedSpans))
					metrics.FieldU64("skipped_spans", atomic.LoadUint64(&stats.SkippedSpans))
					metrics.FieldU64("destructed_blocks", atomic.LoadUint64(&stats.DestructedBlocks))
					metrics.FieldU64("failed_files", atomic.LoadUint64(&stats.FailedFiles))
				}
				if *collectDirectories {
					metrics.FieldU64("visited_directories", atomic.LoadUint64(&stats.VisitedDirectories))
					metrics.FieldU64("visited_edges", atomic.LoadUint64(&stats.VisitedEdges))
					metrics.FieldU64("collected_edges", atomic.LoadUint64(&stats.CollectedEdges))
					metrics.FieldU64("destructed_directories", atomic.LoadUint64(&stats.DestructedDirectories))
				}
				if *zeroBlockServices {
					metrics.FieldU64("zero_block_service_files_removed", atomic.LoadUint64(&stats.ZeroBlockServiceFilesRemoved))
				}
				metrics.Timestamp(now)
				err := lib.SendMetrics(metrics.Payload())
				if err == nil {
					log.ClearNC(alert)
					sleepFor := time.Second * 30
					log.Info("gc metrics sent, sleeping for %v", sleepFor)
					time.Sleep(sleepFor)
				} else {
					log.RaiseNC(alert, "failed to send gc metrics, will try again in a second: %v", err)
					time.Sleep(time.Second)
				}
			}
		}()
	}
	if *countMetrics {
		// counting transient files/files/directories
		var transientFiles [256]uint64
		go func() {
			for {
				log.Info("starting to count transient files")
				for i := 0; i < 256; i++ {
					shid := msgs.ShardId(i)
					req := msgs.VisitTransientFilesReq{}
					resp := msgs.VisitTransientFilesResp{}
					files := uint64(0)
					var err error
					for {
						if err = client.ShardRequest(log, shid, &req, &resp); err != nil {
							log.RaiseAlert("could not get transient files for shard %v: %v", shid, err)
							break
						}
						files += uint64(len(resp.Files))
						req.BeginId = resp.NextId
						if req.BeginId == 0 {
							break
						}
					}
					if err == nil {
						transientFiles[i] = files
					}
				}
			}
		}()
		var files [256]uint64
		go func() {
			for {
				log.Info("starting to count files")
				for i := 0; i < 256; i++ {
					shid := msgs.ShardId(i)
					req := msgs.VisitFilesReq{}
					resp := msgs.VisitFilesResp{}
					count := uint64(0)
					var err error
					for {
						if err = client.ShardRequest(log, shid, &req, &resp); err != nil {
							log.RaiseAlert("could not get files for shard %v: %v", shid, err)
							break
						}
						count += uint64(len(resp.Ids))
						req.BeginId = resp.NextId
						if req.BeginId == 0 {
							break
						}
					}
					if err == nil {
						files[i] = count
					}
				}
			}
		}()
		var directories [256]uint64
		go func() {
			for {
				log.Info("starting to count directories")
				for i := 0; i < 256; i++ {
					shid := msgs.ShardId(i)
					req := msgs.VisitDirectoriesReq{}
					resp := msgs.VisitDirectoriesResp{}
					count := uint64(0)
					var err error
					for {
						if err = client.ShardRequest(log, shid, &req, &resp); err != nil {
							log.RaiseAlert("could not get directories for shard %v: %v", shid, err)
							break
						}
						count += uint64(len(resp.Ids))
						req.BeginId = resp.NextId
						if req.BeginId == 0 {
							break
						}
					}
					if err == nil {
						directories[i] = count
					}
				}
			}
		}()
		go func() {
			metrics := lib.MetricsBuilder{}
			alert := log.NewNCAlert(10 * time.Second)
			rand := wyhash.New(rand.Uint64())
			for {
				log.Info("sending files/transient files/dirs metrics")
				now := time.Now()
				metrics.Reset()
				for i := 0; i < 256; i++ {
					if transientFiles[i] != 0 {
						metrics.Measurement("eggsfs_transient_files")
						metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
						metrics.FieldU64("count", transientFiles[i])
						metrics.Timestamp(now)
					}
					if files[i] != 0 {
						metrics.Measurement("eggsfs_files")
						metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
						metrics.FieldU64("count", files[i])
						metrics.Timestamp(now)
					}
					if directories[i] != 0 {
						metrics.Measurement("eggsfs_directories")
						metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
						metrics.FieldU64("count", directories[i])
						metrics.Timestamp(now)
					}
				}
				err := lib.SendMetrics(metrics.Payload())
				if err == nil {
					log.ClearNC(alert)
					sleepFor := time.Minute + time.Duration(rand.Uint64() & ^(uint64(1)<<63))%time.Minute
					log.Info("count metrics sent, sleeping for %v", sleepFor)
					time.Sleep(sleepFor)
				} else {
					log.RaiseNC(alert, "failed to send count metrics, will try again in a second: %v", err)
					time.Sleep(time.Second)
				}
			}
		}()
	}

	mbErr := <-terminateChan
	log.Info("got error, winding down: %v", mbErr)
	panic(mbErr)
}
