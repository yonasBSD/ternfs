package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"sync"
	"time"
	"xtx/eggsfs/cdckey"
	"xtx/eggsfs/janitor"
	"xtx/eggsfs/msgs"
)

func main() {
	verbose := flag.Bool("verbose", false, "enables debug logging")
	flag.Parse()

	logger := log.New(os.Stderr, "", log.Lshortfile)
	panicChan := make(chan error, 1)
	env := janitor.Env{
		Logger:            logger,
		LogMutex:          new(sync.Mutex),
		Timeout:           10 * time.Second,
		CDCKey:            cdckey.CDCKey(),
		Verbose:           *verbose,
		DirInfoCache:      map[msgs.InodeId]janitor.CachedDirInfo{},
		DirInfoCacheMutex: new(sync.RWMutex),
	}
	logger.Printf("starting one collector and one destructor per shard.")
	for shid := 0; shid < 256; shid++ {
		destructor := env
		destructor.Role = "destructor"
		destructor.Shid = msgs.ShardId(shid)
		go destructor.Loop(panicChan, (*janitor.Env).DestructForever)
		collector := env
		collector.Role = "collector"
		collector.Shid = msgs.ShardId(shid)
		go collector.Loop(panicChan, (*janitor.Env).CollectForever)
	}
	err := <-panicChan
	logger.Fatal(fmt.Errorf("got fatal error, tearing down: %w", err))
}
