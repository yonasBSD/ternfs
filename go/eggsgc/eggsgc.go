package main

import (
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"math/rand"
	"net"
	"os"
	"path"
	"sync/atomic"
	"time"
	"xtx/eggsfs/cleanup"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	"net/http"
	_ "net/http/pprof"

	_ "github.com/mattn/go-sqlite3"
)

// Right now the DB just stores the counting stats
func initDb(db *sql.DB) error {
	var err error
	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS state(
		what TEXT NOT NULL PRIMARY KEY,
		state BLOB NOT NULL
	)`)
	if err == nil {
		return err
	}
	return nil
}

func storeState(db *sql.Tx, what string, state any) error {
	bytes, err := json.Marshal(state)
	if err != nil {
		return err
	}
	if _, err := db.Exec("INSERT OR REPLACE INTO state (what, state) VALUES (?, ?)", what, bytes); err != nil {
		return err
	}
	return nil
}

func loadState(db *sql.DB, what string, state any) error {
	var bytes []byte
	err := db.QueryRow("SELECT state FROM state WHERE what = ?", what).Scan(&bytes)
	if err == sql.ErrNoRows {
		return nil
	}
	if err != nil {
		return err
	}
	if err := json.Unmarshal(bytes, state); err != nil {
		return err
	}
	return nil
}

type CountStateSection struct {
	Shard  msgs.ShardId
	Counts [256]uint64
}

type CountState struct {
	Files          CountStateSection
	Directories    CountStateSection
	TransientFiles CountStateSection
}

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	appInstance := flag.String("app-instance", "eggsgc", "")
	trace := flag.Bool("trace", false, "Enables debug logging.")
	logFile := flag.String("log-file", "", "File to log to, stdout if not provided.")
	shuckleAddress := flag.String("shuckle", "", "Shuckle address (host:port).")
	syslog := flag.Bool("syslog", false, "")
	mtu := flag.Uint64("mtu", 0, "")
	collectDirectories := flag.Bool("collect-directories", false, "")
	collectDirectoriesWorkersPerShard := flag.Int("collect-directories-workers-per-shard", 10, "")
	collectDirectoriesWorkersQueueSize := flag.Int("collect-directories-workers-queue-size", 50, "")
	destructFiles := flag.Bool("destruct-files", false, "")
	destructFilesWorkersPerShard := flag.Int("destruct-files-workers-per-shard", 10, "")
	destructFilesWorkersQueueSize := flag.Int("destruct-files-workers-queue-size", 50, "")
	defrag := flag.Bool("defrag", false, "")
	defragWorkersPerShard := flag.Int("defrag-workers-per-shard", 5, "")
	defragMinSpanSize := flag.Uint("defrag-min-span-size", 0, "")
	defragStorageClass := flag.String("defrag-storage-class", "", "If present, will only defrag spans in this storage class.")
	zeroBlockServices := flag.Bool("zero-block-services", false, "")
	metrics := flag.Bool("metrics", false, "Send metrics")
	countMetrics := flag.Bool("count-metrics", false, "Compute and send count metrics")
	migrate := flag.Bool("migrate", false, "migrate")
	scrub := flag.Bool("scrub", false, "scrub")
	scrubWorkersPerShard := flag.Int("scrub-workers-per-shard", 10, "")
	scrubWorkersQueueSize := flag.Int("scrub-workers-queue-size", 50, "")
	dataDir := flag.String("data-dir", "", "Where to store the GC files. This is currently non-critical data (files/directories/transient files count, migrations)")
	pprofHttpPort := flag.Int("pprof-http-port", -1, "Port on which to run the pprof HTTP server")
	flag.Parse()

	if *shuckleAddress == "" {
		fmt.Fprintf(os.Stderr, "You need to specify -shuckle.\n")
		os.Exit(2)
	}

	if *dataDir == "" {
		fmt.Fprintf(os.Stderr, "You need to specify a -data-dir\n")
		os.Exit(2)
	}

	if !*destructFiles && !*collectDirectories && !*zeroBlockServices && !*countMetrics && !*scrub && !*migrate && !*defrag {
		fmt.Fprintf(os.Stderr, "Nothing to do!\n")
		os.Exit(2)
	}

	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
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

	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppType: lib.XMON_DAYTIME, AppInstance: *appInstance})

	if *mtu != 0 {
		client.SetMTU(*mtu)
	}

	log.Info("Will run GC in all shards")

	if err := os.Mkdir(*dataDir, 0777); err != nil && !os.IsExist(err) {
		panic(err)
	}
	dbFile := path.Join(*dataDir, "gc.db")
	db, err := sql.Open("sqlite3", fmt.Sprintf("file:%s?_journal=WAL", dbFile))
	if err != nil {
		panic(err)
	}
	defer db.Close()

	if err := initDb(db); err != nil {
		panic(err)
	}

	// Keep trying forever, we'll alert anyway and it's useful when we restart everything
	shuckleTimeouts := &client.DefaultShuckleTimeout
	shuckleTimeouts.Overall = 0
	shardTimeouts := &client.DefaultShardTimeout
	shardTimeouts.Initial = 500 * time.Millisecond
	shardTimeouts.Overall = 0
	cdcTimeouts := &client.DefaultCDCTimeout
	cdcTimeouts.Initial = 2 * time.Second
	cdcTimeouts.Overall = 0
	blockTimeouts := &client.DefaultBlockTimeout
	// The reason why this is non-infinite is that sometimes block services go down
	// for a considerable amount of time, and in that case we don't want to be
	// stuck forever. Rather, we just skip over block services.
	blockTimeouts.Overall = 10 * time.Minute

	dirInfoCache := client.NewDirInfoCache()
	c, err := client.NewClient(log, shuckleTimeouts, *shuckleAddress)
	if err != nil {
		panic(err)
	}
	c.SetShardTimeouts(shardTimeouts)
	c.SetCDCTimeouts(cdcTimeouts)
	c.SetBlockTimeout(blockTimeouts)
	counters := client.NewClientCounters()
	c.SetCounters(counters)
	terminateChan := make(chan any)

	collectDirectoriesState := &cleanup.CollectDirectoriesState{}
	if err := loadState(db, "collect_directories", collectDirectoriesState); err != nil {
		panic(err)
	}

	destructFilesState := &cleanup.DestructFilesState{}
	if err := loadState(db, "destruct_files", destructFilesState); err != nil {
		panic(err)
	}

	scrubState := &cleanup.ScrubState{}
	if err := loadState(db, "scrub", scrubState); err != nil {
		panic(err)
	}

	zeroBlockServiceFilesStats := &cleanup.ZeroBlockServiceFilesStats{}

	countState := &CountState{}
	if err := loadState(db, "count", countState); err != nil {
		panic(err)
	}

	migrateState := &cleanup.MigrateState{}

	// store the state
	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		for {
			tx, err := db.Begin()
			if err != nil {
				panic(err)
			}
			if *collectDirectories {
				storeState(tx, "collect_directories", collectDirectoriesState)
			}
			if *destructFiles {
				storeState(tx, "destruct_files", destructFilesState)
			}
			if *scrub {
				storeState(tx, "scrub", scrubState)
			}
			if *countMetrics {
				storeState(tx, "count", countState)
			}
			if err := tx.Commit(); err != nil {
				panic(err)
			}
			log.Info("stored state, waiting one minute")
			time.Sleep(time.Minute)
		}
	}()

	if *pprofHttpPort >= 0 {
		httpListener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%v", *pprofHttpPort))
		if err != nil {
			panic(err)
		}
		defer httpListener.Close()
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			terminateChan <- http.Serve(httpListener, nil)
		}()
		log.Info("http pprof listener started on port %v", httpListener.Addr().(*net.TCPAddr).Port)
	}

	if *collectDirectories {
		opts := &cleanup.CollectDirectoriesOpts{
			NumWorkersPerShard: *collectDirectoriesWorkersPerShard,
			WorkersQueueSize:   *collectDirectoriesWorkersQueueSize,
		}
		for i := 0; i < 256; i++ {
			shid := msgs.ShardId(i)
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				for {
					if err := cleanup.CollectDirectories(log, c, dirInfoCache, nil, opts, collectDirectoriesState, shid); err != nil {
						panic(fmt.Errorf("could not collect directories in shard %v: %v", shid, err))
					}
				}
			}()
		}
	}
	if *destructFiles {
		opts := &cleanup.DestructFilesOptions{
			NumWorkersPerShard: *destructFilesWorkersPerShard,
			WorkersQueueSize:   *destructFilesWorkersQueueSize,
		}
		for i := 0; i < 256; i++ {
			shid := msgs.ShardId(i)
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				for {
					if err := cleanup.DestructFiles(log, c, opts, destructFilesState, shid); err != nil {
						panic(fmt.Errorf("could not destruct files: %v", err))
					}
					log.Info("finished destructing in shard %v, sleeping for one hour", shid)
					time.Sleep(time.Hour)
				}
			}()
		}
	}
	if *zeroBlockServices {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				// just do that once an hour, we don't need this often.
				waitFor := time.Second * time.Duration(rand.Uint64()%(60*60))
				log.Info("waiting %v before collecting zero block service files", waitFor)
				time.Sleep(waitFor)
				if err := cleanup.CollectZeroBlockServiceFiles(log, c, zeroBlockServiceFilesStats); err != nil {
					log.RaiseAlert("could not collecting zero block service files: %v", err)
				}
				log.Info("finished zero block services cycle, will restart")
			}
		}()
	}
	if *scrub {
		rateLimit := lib.NewRateLimit(&lib.RateLimitOpts{
			RefillInterval: time.Second,
			Refill:         50000, // 50k blocks per second scrubs in ~1 month right now (100 billion blocks)
			BucketSize:     50000 * 100,
		})
		defer rateLimit.Close()
		// retry forever
		opts := &cleanup.ScrubOptions{
			NumWorkersPerShard: *scrubWorkersPerShard,
			WorkersQueueSize:   *scrubWorkersQueueSize,
		}
		for i := 0; i < 256; i++ {
			shid := msgs.ShardId(i)
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				for {
					if err := cleanup.ScrubFiles(log, c, opts, rateLimit, scrubState, shid); err != nil {
						log.RaiseAlert("could not scrub files: %v", err)
					}
				}
			}()
		}
	}
	if *migrate {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				log.Info("requesting block services")
				blockServicesResp, err := client.ShuckleRequest(log, nil, *shuckleAddress, &msgs.AllBlockServicesReq{})
				if err != nil {
					terminateChan <- err
					return
				}
				blockServices := blockServicesResp.(*msgs.AllBlockServicesResp)
				blockServicesToMigrate := make(map[string]*[]msgs.BlockServiceId) // by failure domain
				for _, bs := range blockServices.BlockServices {
					if bs.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) && bs.HasFiles {
						bss := blockServicesToMigrate[bs.FailureDomain.String()]
						if bss == nil {
							bss = &[]msgs.BlockServiceId{}
							blockServicesToMigrate[bs.FailureDomain.String()] = bss
						}
						*bss = append(*bss, bs.Id)
					}
				}

				progressReportAlert := log.NewNCAlert(10 * time.Second)
				progressReportAlert.SetAppType(lib.XMON_NEVER)
				for failureDomain, bss := range blockServicesToMigrate {
					for _, blockServiceId := range *bss {
						log.RaiseNC(progressReportAlert, "migrating block service %v, %v", blockServiceId, failureDomain)
						if err := cleanup.MigrateBlocksInAllShards(log, c, &migrateState.Stats, progressReportAlert, blockServiceId); err != nil {
							log.RaiseAlert("could not migrate blocks out of block service %v, stats so far %+v: %v", blockServiceId, migrateState.Stats, err)
						} else {
							log.Info("finished migrating blocks away from block service %v, stats so far: %+v", blockServiceId, migrateState.Stats)
						}
						log.ClearNC(progressReportAlert)
					}
				}
				if migrateState.Stats.MigratedBlocks > 0 {
					log.Info("finished migrating away from all block services, stats: %+v", migrateState.Stats)
				}
				time.Sleep(time.Minute)
			}
		}()
	}

	defragStats := &cleanup.DefragStats{}
	if *defrag {
		var storageClass msgs.StorageClass
		if *defragStorageClass != "" {
			storageClass = msgs.StorageClassFromString(*defragStorageClass)
		}
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				log.Info("starting to defrag")
				defragStats = &cleanup.DefragStats{}
				bufPool := lib.NewBufPool()
				progressReportAlert := log.NewNCAlert(0)
				progressReportAlert.SetAppType(lib.XMON_NEVER)
				options := cleanup.DefragOptions{
					WorkersPerShard: *defragWorkersPerShard,
					MinSpanSize:     uint32(*defragMinSpanSize),
					StorageClass:    storageClass,
				}
				cleanup.DefragFiles(log, c, bufPool, dirInfoCache, defragStats, progressReportAlert, &options, "/")
				log.RaiseAlertAppType(lib.XMON_DAYTIME, "finished one cycle of defragging, will start again")
			}
		}()
	}

	if *metrics && (*destructFiles || *collectDirectories || *zeroBlockServices || *scrub || *defrag) {
		// one thing just pushing the stats every minute
		go func() {
			metrics := lib.MetricsBuilder{}
			alert := log.NewNCAlert(10 * time.Second)
			for {
				log.Info("sending stats metrics")
				now := time.Now()
				metrics.Reset()
				// generic GC metrics
				metrics.Measurement("eggsfs_gc")
				if *destructFiles {
					metrics.FieldU64("visited_files", atomic.LoadUint64(&destructFilesState.Stats.VisitedFiles))
					metrics.FieldU64("destructed_files", atomic.LoadUint64(&destructFilesState.Stats.DestructedFiles))
					metrics.FieldU64("destructed_spans", atomic.LoadUint64(&destructFilesState.Stats.DestructedSpans))
					metrics.FieldU64("skipped_spans", atomic.LoadUint64(&destructFilesState.Stats.SkippedSpans))
					metrics.FieldU64("skipped_files", atomic.LoadUint64(&destructFilesState.Stats.SkippedFiles))
					metrics.FieldU64("destructed_blocks", atomic.LoadUint64(&destructFilesState.Stats.DestructedBlocks))
					metrics.FieldU64("failed_files", atomic.LoadUint64(&destructFilesState.Stats.FailedFiles))
				}
				if *collectDirectories {
					metrics.FieldU64("visited_directories", atomic.LoadUint64(&collectDirectoriesState.Stats.VisitedDirectories))
					metrics.FieldU64("visited_edges", atomic.LoadUint64(&collectDirectoriesState.Stats.VisitedEdges))
					metrics.FieldU64("collected_edges", atomic.LoadUint64(&collectDirectoriesState.Stats.CollectedEdges))
					metrics.FieldU64("destructed_directories", atomic.LoadUint64(&collectDirectoriesState.Stats.DestructedDirectories))
				}
				if *zeroBlockServices {
					metrics.FieldU64("zero_block_service_files_removed", atomic.LoadUint64(&zeroBlockServiceFilesStats.ZeroBlockServiceFilesRemoved))
				}
				if *scrub {
					metrics.FieldU64("checked_bytes", atomic.LoadUint64(&scrubState.CheckedBytes))
					metrics.FieldU64("checked_blocks", atomic.LoadUint64(&scrubState.CheckedBlocks))
					metrics.FieldU64("scrubbed_files", atomic.LoadUint64(&scrubState.Migrate.MigratedFiles))
					metrics.FieldU64("scrubbed_blocks", atomic.LoadUint64(&scrubState.Migrate.MigratedBlocks))
					metrics.FieldU64("scrubbed_bytes", atomic.LoadUint64(&scrubState.Migrate.MigratedBytes))
					metrics.FieldU64("decommissioned_blocks", atomic.LoadUint64(&scrubState.DecommissionedBlocks))
				}
				if *defrag {
					metrics.FieldU64("defrag_analyzed_files", atomic.LoadUint64(&defragStats.AnalyzedFiles))
					metrics.FieldU64("defrag_analyzed_logical_size", atomic.LoadUint64(&defragStats.AnalyzedLogicalSize))
					metrics.FieldU64("defrag_analyzed_blocks", atomic.LoadUint64(&defragStats.AnalyzedBlocks))
					metrics.FieldU64("defragged_spans", atomic.LoadUint64(&defragStats.DefraggedSpans))
					metrics.FieldU64("defragged_logical_bytes", atomic.LoadUint64(&defragStats.DefraggedLogicalBytes))
					metrics.FieldU64("defragged_blocks_before", atomic.LoadUint64(&defragStats.DefraggedBlocksBefore))
					metrics.FieldU64("defragged_physical_bytes_before", atomic.LoadUint64(&defragStats.DefraggedPhysicalBytesBefore))
					metrics.FieldU64("defragged_blocks_after", atomic.LoadUint64(&defragStats.DefraggedBlocksAfter))
					metrics.FieldU64("defragged_physical_bytes_after", atomic.LoadUint64(&defragStats.DefraggedPhysicalBytesAfter))
				}
				metrics.Timestamp(now)
				// per shard gc metrics
				if *destructFiles || *collectDirectories || *scrub {
					for i := 0; i < 256; i++ {
						metrics.Measurement("eggsfs_gc")
						metrics.Tag("shard", fmt.Sprintf("%v", i))
						if *destructFiles {
							metrics.FieldU64("destruct_files_worker_queue_size", atomic.LoadUint64(&destructFilesState.WorkersQueuesSize[i]))
							metrics.FieldU32("destruct_files_cycles", atomic.LoadUint32(&destructFilesState.Stats.Cycles[i]))
						}
						if *collectDirectories {
							metrics.FieldU64("collect_directories_worker_queue_size", atomic.LoadUint64(&collectDirectoriesState.WorkersQueuesSize[i]))
							metrics.FieldU32("collect_directories_cycles", atomic.LoadUint32(&collectDirectoriesState.Stats.Cycles[i]))
						}
						if *scrub {
							metrics.FieldU64("scrub_worker_queue_size", atomic.LoadUint64(&scrubState.WorkersQueuesSize[i]))
							metrics.FieldU64("scrub_check_queue_size", atomic.LoadUint64(&scrubState.CheckQueuesSize[i]))
							metrics.FieldU32("scrub_cycles", atomic.LoadUint32(&scrubState.Cycles[i]))
						}
						metrics.Timestamp(now)
					}
				}
				// shard requests
				for _, kind := range msgs.AllShardMessageKind {
					for i := 0; i < 256; i++ {
						counter := &counters.Shard[uint8(kind)][i]
						metrics.Measurement("eggsfs_gc_shard_reqs")
						if *appInstance != "" {
							metrics.Tag("instance", *appInstance)
						}
						metrics.Tag("shard", fmt.Sprintf("%d", i))
						metrics.Tag("kind", kind.String())
						metrics.FieldU64("attempts", counter.Attempts)
						metrics.FieldU64("completed", counter.Timings.Count())
						metrics.Timestamp(now)
					}
				}
				// CDC requests
				for _, kind := range msgs.AllCDCMessageKind {
					counter := counters.CDC[uint8(kind)]
					metrics.Measurement("eggsfs_gc_cdc_reqs")
					if *appInstance != "" {
						metrics.Tag("instance", *appInstance)
					}
					metrics.Tag("kind", kind.String())
					metrics.FieldU64("attempts", counter.Attempts)
					metrics.FieldU64("completed", counter.Timings.Count())
					metrics.Timestamp(now)
				}
				// dir info cache
				{
					metrics.Measurement("eggsfs_gc_dir_info_cache")
					if *appInstance != "" {
						metrics.Tag("instance", *appInstance)
					}
					metrics.FieldU64("hits", dirInfoCache.Hits())
					metrics.FieldU64("lookups", dirInfoCache.Lookups())
					metrics.Timestamp(now)
				}
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
		go func() {
			for {
				log.Info("starting to count files")
				for i := int(countState.Files.Shard); i < 256; i++ {
					shid := msgs.ShardId(i)
					countState.Files.Shard = shid
					req := msgs.VisitFilesReq{}
					resp := msgs.VisitFilesResp{}
					count := uint64(0)
					var err error
					for {
						if err = c.ShardRequest(log, shid, &req, &resp); err != nil {
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
						countState.Files.Counts[shid] = count
					}
				}
				countState.Files.Shard = 0
			}
		}()
		go func() {
			for {
				log.Info("starting to count directories")
				for i := int(countState.Directories.Shard); i < 256; i++ {
					shid := msgs.ShardId(i)
					countState.Directories.Shard = shid
					req := msgs.VisitDirectoriesReq{}
					resp := msgs.VisitDirectoriesResp{}
					count := uint64(0)
					var err error
					for {
						if err = c.ShardRequest(log, shid, &req, &resp); err != nil {
							log.RaiseAlert("could not get directories for shard %v: %v", shid, err)
							break
						}
						count += uint64(len(resp.Ids))
						req.BeginId = resp.NextId
						if req.BeginId == 0 {
							break
						}
					}
					countState.Directories.Counts[shid] = count
				}
				countState.Directories.Shard = 0
			}
		}()
		go func() {
			for {
				log.Info("starting to count transient files")
				for i := int(countState.TransientFiles.Shard); i < 256; i++ {
					shid := msgs.ShardId(i)
					countState.TransientFiles.Shard = shid
					req := msgs.VisitTransientFilesReq{}
					resp := msgs.VisitTransientFilesResp{}
					count := uint64(0)
					var err error
					for {
						if err = c.ShardRequest(log, shid, &req, &resp); err != nil {
							log.RaiseAlert("could not get transient files for shard %v: %v", shid, err)
							break
						}
						count += uint64(len(resp.Files))
						req.BeginId = resp.NextId
						if req.BeginId == 0 {
							break
						}
					}
					countState.TransientFiles.Counts[shid] = count
				}
				countState.TransientFiles.Shard = 0
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
				log.ClearNC(alert)
				for i := 0; i < 256; i++ {
					metrics.Measurement("eggsfs_transient_files")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", countState.TransientFiles.Counts[i])
					metrics.Timestamp(now)
					metrics.Measurement("eggsfs_files")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", countState.Files.Counts[i])
					metrics.Timestamp(now)
					metrics.Measurement("eggsfs_directories")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", countState.Directories.Counts[i])
					metrics.Timestamp(now)
				}
				err = lib.SendMetrics(metrics.Payload())
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
