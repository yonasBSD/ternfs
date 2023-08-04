package main

import (
	"flag"
	"fmt"
	"math/rand"
	"os"
	"os/signal"
	"strconv"
	"strings"
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
	flag.Parse()

	shards := []msgs.ShardId{}
	var appInstance string

	if flag.NArg() < 1 { // all shards
		for i := 0; i < 256; i++ {
			shards = append(shards, msgs.ShardId(i))
		}
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

	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon, AppName: "gc", AppInstance: appInstance})

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
	options := &lib.GCOptions{
		ShuckleTimeouts: lib.NewReqTimeouts(lib.DefaultShuckleTimeout.Initial, lib.DefaultShuckleTimeout.Max, 0, lib.DefaultShuckleTimeout.Growth, lib.DefaultShuckleTimeout.Jitter),
		ShardTimeouts:   lib.NewReqTimeouts(lib.DefaultShardTimeout.Initial, lib.DefaultShardTimeout.Max, 0, lib.DefaultShardTimeout.Growth, lib.DefaultShardTimeout.Jitter),
		CDCTimeouts:     lib.NewReqTimeouts(lib.DefaultCDCTimeout.Initial, lib.DefaultCDCTimeout.Max, 0, lib.DefaultCDCTimeout.Growth, lib.DefaultCDCTimeout.Jitter),
		ShuckleAddress:  *shuckleAddress,
	}

	dirInfoCache := lib.NewDirInfoCache()
	rand := wyhash.New(rand.Uint64())
	for {
		for _, shard := range shards {
			if err := lib.CollectDirectories(log, options, dirInfoCache, shard); err != nil {
				log.RaiseAlert("could not collect directories: %v", err)
			}
			if err := lib.DestructFiles(log, options, shard); err != nil {
				log.RaiseAlert("could not destruct files: %v", err)
			}
		}
		if *singleIteration {
			goto Finish
		}
		waitFor := time.Minute * time.Duration((rand.Uint32()%10)+1)
		log.Info("waiting %v before collecting again", waitFor)
		time.Sleep(waitFor)
	}
Finish:
}
