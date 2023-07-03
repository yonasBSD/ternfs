package main

import (
	"flag"
	"fmt"
	"os"
	"os/signal"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/managedroutine"
	"xtx/eggsfs/msgs"
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

	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "Please specify at least one shard to run with using positional arguments.")
		os.Exit(2)
	}

	shards := []msgs.ShardId{}
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
	log := lib.NewLogger(logOut, &lib.LoggerOptions{Level: level, Syslog: *syslog, Xmon: *xmon})

	if *mtu != 0 {
		lib.SetMTU(*mtu)
	}

	{
		shardsStrs := []string{}
		for _, shard := range shards {
			shardsStrs = append(shardsStrs, fmt.Sprintf("%v", shard))
		}
		log.Info("Will run GC in shards %v", strings.Join(shardsStrs, ", "))
	}

	panicChan := make(chan error)
	finishedChan := make(chan struct{})
	var wait sync.WaitGroup
	wait.Add(len(shards))

	counters := &lib.ClientCounters{}

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

	managedRoutines := managedroutine.New(panicChan)
	managedRoutines.Start(
		"waiter",
		func() {
			wait.Wait()
			finishedChan <- struct{}{}
		},
	)
	for _, shard := range shards {
		managedRoutines.Start(
			fmt.Sprintf("GC %v", shard),
			func() {
				for {
					if err := lib.CollectDirectories(log, *shuckleAddress, counters, shard); err != nil {
						log.RaiseAlert(fmt.Errorf("could not collect directories: %v", err))
					}
					if err := lib.DestructFiles(log, *shuckleAddress, counters, shard); err != nil {
						log.RaiseAlert(fmt.Errorf("could not destruct files: %v", err))
					}
					log.Info("waiting 1 minute before collecting again")
					time.Sleep(time.Minute)
					if *singleIteration {
						break
					}
				}
				wait.Done()
			},
		)
	}

	select {
	case err := <-panicChan:
		panic(err)
	case <-finishedChan:
		log.Info("finished collecting on all shards, terminating")
	}
}
