package main

import (
	"flag"
	"fmt"
	"os"
	"strconv"
	"sync"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/managedroutine"
	"xtx/eggsfs/msgs"
)

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	verbose := flag.Bool("verbose", false, "Enables debug logging.")
	singleIteration := flag.Bool("single-iteration", false, "Whether to run a single iteration of GC and terminate.")
	logFile := flag.String("log-file", "", "File to log to, stdout if not provided.")
	shuckleAddress := flag.String("shuckle", eggs.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
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
	log := eggs.NewLogger(*verbose, logOut)

	panicChan := make(chan error)
	finishedChan := make(chan struct{})
	var wait sync.WaitGroup
	wait.Add(len(shards))

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
			fmt.Sprintf("shard%v", shard),
			func() {
				for {
					eggs.CollectDirectories(log, *shuckleAddress, nil, shard)
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
