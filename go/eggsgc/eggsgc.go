package main

import (
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"math/rand"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	_ "github.com/mattn/go-sqlite3"
)

// Right now the DB just stores the counting stats
func initDb(db *sql.DB) error {
	var err error
	for i := 0; i < 256; i++ {
		_, err = db.Exec("INSERT OR IGNORE INTO count_stats (shid, files, directories, transient_files) VALUES (?, 0, 0, 0)", i)
		if err != nil {
			return err
		}
	}
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
	appInstance := flag.String("app-instance", "", "")
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
	scrub := flag.Bool("scrub", false, "scrub")
	dataDir := flag.String("data-dir", "", "Where to store the GC files. This is currently non-critical data (files/directories/transient files count, migrations)")
	flag.Parse()

	if *dataDir == "" {
		fmt.Fprintf(os.Stderr, "You need to specify a -data-dir\n")
		os.Exit(2)
	}

	if !*destructFiles && !*collectDirectories && !*zeroBlockServices && !*countMetrics && !*scrub {
		fmt.Fprintf(os.Stderr, "Nothing to do!\n")
		os.Exit(2)
	}

	if *parallel < 1 {
		fmt.Fprintf(os.Stderr, "-parallel must be at least 1.\n")
		os.Exit(2)
	}

	shards := []msgs.ShardId{}

	if flag.NArg() < 1 { // all shards
		for i := 0; i < 256; i++ {
			shards = append(shards, msgs.ShardId(i))
		}
	} else {
		shardsMap := make(map[uint64]struct{})
		for _, shStr := range flag.Args() {
			shid, err := strconv.ParseUint(shStr, 0, 8)
			if err != nil {
				fmt.Fprintf(os.Stderr, "Could not parse %q as shard: %v\n", shStr, err)
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
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppName: "gc", AppType: "restech_eggsfs.daytime", AppInstance: *appInstance})

	if *mtu != 0 {
		lib.SetMTU(*mtu)
	}

	log.Info("Will run GC in shards %v", strings.Join(shardsStrs, ", "))

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
	counters := lib.NewClientCounters()
	client.SetCounters(counters)
	var cdcMu sync.Mutex
	terminateChan := make(chan any)

	collectDirectoriesState := &lib.CollectDirectoriesState{}
	if err := loadState(db, "collect_directories", collectDirectoriesState); err != nil {
		panic(err)
	}

	destructFilesState := &lib.DestructFilesState{}
	if err := loadState(db, "destruct_files", destructFilesState); err != nil {
		panic(err)
	}

	scrubState := &lib.ScrubState{}
	if err := loadState(db, "scrub", scrubState); err != nil {
		panic(err)
	}

	zeroBlockServiceFilesStats := &lib.ZeroBlockServiceFilesStats{}

	countState := &CountState{}
	if err := loadState(db, "count", countState); err != nil {
		panic(err)
	}

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

	shardsPerGroup := len(shards) / int(*parallel)

	if *collectDirectories {
		go func() {
			for {
				var wg sync.WaitGroup
				wg.Add(int(*parallel))
				for group0 := 0; group0 < int(*parallel); group0++ {
					group := group0
					rand := wyhash.New(uint64(group))
					go func() {
						defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
						groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
						waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
						log.Info("waiting %v before collecting directories in %+v", waitFor, groupShards)
						time.Sleep(waitFor)
						if err := lib.CollectDirectories(log, client, dirInfoCache, &cdcMu, collectDirectoriesState, groupShards); err != nil {
							log.RaiseAlert("could not collect directories: %v", err)
						}
						wg.Done()
					}()
				}
				wg.Wait()
				log.Info("finished collect directories cycle")
				*collectDirectoriesState = lib.CollectDirectoriesState{}
			}
		}()
	}
	if *destructFiles {
		go func() {
			for {
				var wg sync.WaitGroup
				wg.Add(int(*parallel))
				for group0 := 0; group0 < int(*parallel); group0++ {
					group := group0
					rand := wyhash.New(uint64(group))
					go func() {
						defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
						groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
						waitFor := time.Millisecond * time.Duration(rand.Uint64()%(30_000))
						log.Info("waiting %v before destructing files in %v", waitFor, groupShards)
						time.Sleep(waitFor)
						if err := lib.DestructFiles(log, options, client, destructFilesState, groupShards); err != nil {
							log.RaiseAlert("could not destruct files: %v", err)
						}
					}()
				}
				wg.Wait()
				log.Info("finished destruct files cycle")
				*destructFilesState = lib.DestructFilesState{}
			}
		}()
	}
	if *zeroBlockServices {
		go func() {
			var wg sync.WaitGroup
			wg.Add(int(*parallel))
			for group0 := 0; group0 < int(*parallel); group0++ {
				group := group0
				rand := wyhash.New(uint64(group))
				go func() {
					defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
					groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
					// just do that once an hour, we don't need this often.
					waitFor := time.Second * time.Duration(rand.Uint64()%(60*60))
					log.Info("waiting %v before collecting zero block service files in %v", waitFor, groupShards)
					time.Sleep(waitFor)
					if err := lib.CollectZeroBlockServiceFiles(log, client, zeroBlockServiceFilesStats, groupShards); err != nil {
						log.RaiseAlert("could not collecting zero block service files: %v", err)
					}
				}()
			}
			wg.Wait()
			log.Info("finished zero block services cycle")
			*zeroBlockServiceFilesStats = lib.ZeroBlockServiceFilesStats{}
		}()
	}
	if *scrub {
		go func() {
			var wg sync.WaitGroup
			wg.Add(int(*parallel))
			for group0 := 0; group0 < int(*parallel); group0++ {
				group := group0
				rand := wyhash.New(uint64(group))
				go func() {
					defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
					groupShards := shards[shardsPerGroup*group : shardsPerGroup*(group+1)]
					waitFor := time.Millisecond * time.Duration(rand.Uint64()%30_000)
					log.Info("waiting %v before scrubbing files in %+v", waitFor, groupShards)
					time.Sleep(waitFor)
					if err := lib.ScrubFiles(log, client, scrubState, true, groupShards); err != nil {
						log.RaiseAlert("could not scrub files: %v", err)
					}
				}()
			}
			wg.Wait()
			log.Info("finished scrubbing cycle")
			*scrubState = lib.ScrubState{}
		}()
	}
	if *metrics && (*destructFiles || *collectDirectories || *zeroBlockServices || *scrub) {
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
					metrics.FieldU64("scrub_send_queue_size", atomic.LoadUint64(&scrubState.SendQueueSize))
					metrics.FieldU64("scrub_check_queue_size", atomic.LoadUint64(&scrubState.CheckQueueSize))
				}
				metrics.Timestamp(now)
				// shard requests
				for _, kind := range msgs.AllShardMessageKind {
					counter := counters.Shard[uint8(kind)]
					metrics.Measurement("eggsfs_gc_shard_reqs")
					if *appInstance != "" {
						metrics.Tag("instance", *appInstance)
					}
					metrics.Tag("kind", kind.String())
					metrics.FieldU64("attempts", counter.Attempts)
					metrics.FieldU64("completed", counter.Timings.Count())
					metrics.Timestamp(now)
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
						if err = client.ShardRequest(log, shid, &req, &resp); err != nil {
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
