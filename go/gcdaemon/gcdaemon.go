package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"runtime/debug"
	"strconv"
	"strings"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func loop(log eggs.LogLevels, panicChan chan error, body func()) {
	defer func() {
		if err := recover(); err != nil {
			env.RaiseAlert(fmt.Errorf("PANIC %v", err))
			env.logMutex.Lock()
			env.Logger.Printf("%s[%d]: PANIC %v. Stacktrace:", env.Role, env.Shid, err)
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				env.Logger.Printf("%s[%d]: %s", env.Role, env.Shid, line)
			}
			env.logMutex.Unlock()
			panicChan <- fmt.Errorf("%s[%v]: PANIC %v", env.Role, env.Shid, err)
		}
	}()
	for {
		body()
	}
}

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	shard := flag.Int("shard", -1, "Which shard to collect on.")
	singleIteration := flag.Bool("single-iteration", false, "Whether to run a single iteration of GC and terminate.")
	logFile := flag.String("log-file", "", "File to log to, stdout if not provided.")
	flag.Parse()

	logger := eggs.NewLogger()
	ll := eggs.LogLogger

	logger := log.New(os.Stderr, "", log.Lshortfile)

	shardsToCollect := make(map[msgs.ShardId]bool)
	if *shards != "" {
		for _, shardStr := range strings.Split(*shards, ",") {
			shardInt, err := strconv.Atoi(shardStr)
			if err != nil || shardInt < 0 || shardInt > 255 {
				logger.Fatal(fmt.Errorf("invalid shard string `%s'", shardStr))
			}
			shid := msgs.ShardId(shardInt)
			shardsToCollect[shid] = true
		}
	} else {
		for shid := 0; shid < 256; shid++ {
			shardsToCollect[msgs.ShardId(shid)] = true
		}
	}

	panicChan := make(chan error, 1)
	env := eggs.DaemonEnv{
		Logger:  logger,
		Timeout: 10 * time.Second,
		CDCKey:  eggs.CDCKey(),
		Verbose: *verbose,
	}
	logger.Printf("starting one collector and one destructor (running on %d shards).", len(shardsToCollect))
	for shid := 0; shid < 256; shid++ {
		if !shardsToCollect[msgs.ShardId(shid)] {
			continue
		}
		destructor := env
		destructor.Role = "destructor"
		destructor.Shid = msgs.ShardId(shid)
		go destructor.Loop(panicChan, (*eggs.DaemonEnv).DestructForever)
		collector := env
		collector.Role = "collector"
		collector.Shid = msgs.ShardId(shid)
		go collector.Loop(panicChan, (*eggs.DaemonEnv).CollectForever)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
