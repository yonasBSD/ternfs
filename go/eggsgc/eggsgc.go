package main

import (
	"database/sql"
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
	_, err := db.Exec(`CREATE TABLE IF NOT EXISTS count_stats(
		shid INT NOT NULL PRIMARY KEY,
		files INT NOT NULL,
		directories INT NOT NULL,
		transient_files INT NOT NULL
	)`)
	if err != nil {
		panic(err)
	}
	for i := 0; i < 256; i++ {
		_, err = db.Exec("INSERT OR IGNORE INTO count_stats (shid, files, directories, transient_files) VALUES (?, 0, 0, 0)", i)
		if err != nil {
			panic(err)
		}
	}
	return nil
}

func updateCountStat(db *sql.DB, mu *sync.Mutex, shid msgs.ShardId, which string, increase uint64) error {
	mu.Lock()
	defer mu.Unlock()

	_, err := db.Exec(fmt.Sprintf("UPDATE count_stats SET %s = ? WHERE shid = ?", which), increase, int(shid))
	return err
}

type countStats struct {
	files           uint64
	directories     uint64
	transient_files uint64
}

func getCountStats(db *sql.DB) (*[256]countStats, error) {
	rows, err := db.Query("SELECT files, directories, transient_files FROM count_stats")
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	s := &[256]countStats{}
	i := 0
	for rows.Next() {
		var files, directories, transient_files uint64
		err = rows.Scan(&files, &directories, &transient_files)
		if err != nil {
			return nil, err
		}
		s[i].files = files
		s[i].directories = directories
		s[i].transient_files = transient_files
		i++
	}

	return s, nil
}

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
	destructFilesParallel := flag.Uint("destruct-files-parallel", 0, "If non-zero, it'll override -parallel just for files destruction")
	zeroBlockServices := flag.Bool("zero-block-services", false, "")
	parallel := flag.Uint("parallel", 1, "Work will be split in N groups, so for example with -parallel 16 work will be done in groups of 16 shards.")
	metrics := flag.Bool("metrics", false, "Send metrics")
	countMetrics := flag.Bool("count-metrics", false, "Compute and send count metrics")
	dataDir := flag.String("data-dir", "", "Where to store the GC files. This is currently non-critical data (files/directories/transient files count, migrations)")
	flag.Parse()

	if *dataDir == "" {
		fmt.Fprintf(os.Stderr, "You need to specify a -data-dir\n")
		os.Exit(2)
	}

	if !*destructFiles && !*collectDirectories && !*zeroBlockServices && !*countMetrics {
		fmt.Fprintf(os.Stderr, "Nothing to do!\n")
		os.Exit(2)
	}

	if *parallel < 1 {
		fmt.Fprintf(os.Stderr, "-parallel must be at least 1.\n")
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
				fmt.Fprintf(os.Stderr, "Could not parse %q as shard: %v\n", shStr, err)
				os.Exit(2)
			}
			shardsMap[shid] = struct{}{}
		}
		for shid := range shardsMap {
			shards = append(shards, msgs.ShardId(shid))
		}
	}

	if int(*parallel) > len(shards) || int(*destructFilesParallel) > len(shards) {
		fmt.Fprintf(os.Stderr, "-parallel/-destruct-files-parallel can't be greater than %v (number of shards).\n", len(shards))
		os.Exit(2)
	}
	if len(shards)%int(*parallel) != 0 || (*destructFilesParallel > 0 && len(shards)%int(*destructFilesParallel) != 0) {
		fmt.Fprintf(os.Stderr, "-parallel/-destruct-files-parallel does not divide %v (number of shards).\n", len(shards))
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
		shardsPerGroup := len(shards) / int(*parallel)
		if *destructFilesParallel > 0 {
			shardsPerGroup = len(shards) / int(*destructFilesParallel)
		}
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
		var mu sync.Mutex
		// counting transient files/files/directories
		go func() {
			for {
				log.Info("starting to count files")
				updateAlert := log.NewNCAlert(10 * time.Second)
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
						for {
							err := updateCountStat(db, &mu, shid, "files", count)
							if err == nil {
								break
							}
							log.RaiseNC(updateAlert, "could not update file count, will wait one second: %v", err)
							time.Sleep(time.Second)
						}
					}
				}
			}
		}()
		go func() {
			for {
				log.Info("starting to count directories")
				updateAlert := log.NewNCAlert(10 * time.Second)
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
					for {
						err := updateCountStat(db, &mu, shid, "directories", count)
						if err == nil {
							break
						}
						log.RaiseNC(updateAlert, "could not update directory count, will wait one second: %v", err)
						time.Sleep(time.Second)
					}
				}
			}
		}()
		go func() {
			for {
				log.Info("starting to count transient files")
				updateAlert := log.NewNCAlert(10 * time.Second)
				for i := 0; i < 256; i++ {
					shid := msgs.ShardId(i)
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
					for {
						err := updateCountStat(db, &mu, shid, "transient_files", count)
						if err == nil {
							break
						}
						log.RaiseNC(updateAlert, "could not update transient files count, will wait one second: %v", err)
						time.Sleep(time.Second)
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
				stats, err := getCountStats(db)
				if err != nil {
					log.RaiseNC(alert, "failed to get count stats, will try again in a second: %v", err)
					time.Sleep(time.Second)
					continue
				}
				log.ClearNC(alert)
				for i := 0; i < 256; i++ {
					metrics.Measurement("eggsfs_transient_files")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", stats[i].transient_files)
					metrics.Timestamp(now)
					metrics.Measurement("eggsfs_files")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", stats[i].files)
					metrics.Timestamp(now)
					metrics.Measurement("eggsfs_directories")
					metrics.Tag("shard", fmt.Sprintf("%v", msgs.ShardId(i)))
					metrics.FieldU64("count", stats[i].directories)
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
