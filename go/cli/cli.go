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
	"xtx/eggsfs/request"
)

func badCommand() {
	fmt.Printf("expected 'collect', 'destruct', or 'migrate' subcommand\n")
	os.Exit(2)
}

func main() {
	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "directory inode id to GC")
	collectDry := collectCmd.Bool("dry", false, "whether to execute the GC or not")

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructDry := destructCmd.Bool("dry", false, "whether to execute the destruction or not")
	destructFileIdU64 := destructCmd.Uint64("file", 0, "transient file id to destruct")
	destructFileCookie := destructCmd.Uint64("cookie", 0, "transient file cookie")

	migrateCmd := flag.NewFlagSet("migrate", flag.ExitOnError)
	migrateDry := migrateCmd.Bool("dry", false, "whether to execute the migration or not")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "file in which to migrate blocks")
	migrateBlockService := migrateCmd.Uint64("blockservice", 0, "block service to migrate from")

	if len(os.Args) < 2 {
		badCommand()
	}

	env := janitor.Env{
		Role:              "cli",
		Logger:            log.New(os.Stdout, "", log.Lshortfile),
		LogMutex:          new(sync.Mutex),
		Timeout:           10 * time.Second,
		CDCKey:            cdckey.CDCKey(),
		Verbose:           true,
		DirInfoCache:      map[msgs.InodeId]janitor.CachedDirInfo{},
		DirInfoCacheMutex: new(sync.RWMutex),
	}

	switch os.Args[1] {
	case "collect":
		collectCmd.Parse(os.Args[2:])
		env.Dry = *collectDry
		if *collectDirIdU64 == 0 {
			collectCmd.Usage()
			os.Exit(2)
		}
		dirId := msgs.InodeId(*collectDirIdU64)
		if dirId.Type() != msgs.DIRECTORY {
			log.Fatalf("inode id %v is not a directory", dirId)
		}
		shardSocket, err := request.ShardSocket(dirId.Shard())
		if err != nil {
			log.Fatalf("could not create shard socket: %v", err)
		}
		defer shardSocket.Close()
		cdcSocket, err := request.CDCSocket()
		if err != nil {
			log.Fatalf("could not create shard socket: %v", err)
		}
		defer cdcSocket.Close()
		env.ShardSocket = shardSocket
		env.CDCSocket = cdcSocket
		stats := janitor.CollectStats{}
		err = env.CollectDirectory(&stats, dirId)
		if err != nil {
			log.Fatalf("could not collect %v, stats: %+v, err: %v", dirId, stats, err)
		}
		env.Info("finished collecting %v, stats: %+v", dirId, stats)
	case "destruct":
		destructCmd.Parse(os.Args[2:])
		env.Dry = *destructDry
		if *destructFileIdU64 == 0 {
			destructCmd.Usage()
			os.Exit(2)
		}
		fileId := msgs.InodeId(*destructFileIdU64)
		if fileId.Type() == msgs.DIRECTORY {
			log.Fatalf("inode id %v is not a file/symlink", fileId)
		}
		socket, err := request.ShardSocket(fileId.Shard())
		if err != nil {
			log.Fatalf("could not create shard socket: %v", err)
		}
		env.ShardSocket = socket
		stats := janitor.DestructionStats{}
		err = env.DestructFile(&stats, fileId, 0, *destructFileCookie)
		if err != nil {
			log.Fatalf("could not destruct %v, stats: %+v, err: %v", fileId, stats, err)
		}
		env.Info("finished destructing %v, stats: %+v", fileId, stats)
	case "migrate":
		migrateCmd.Parse(os.Args[2:])
		env.Dry = *migrateDry
		if *migrateFileIdU64 == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		fileId := msgs.InodeId(*migrateFileIdU64)
		if *migrateBlockService == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		blockServiceId := msgs.BlockServiceId(*migrateBlockService)
		socket, err := request.ShardSocket(fileId.Shard())
		if err != nil {
			log.Fatalf("could not create shard socket: %v", err)
		}
		env.ShardSocket = socket
		migrated, err := env.MigrateBlocks(fileId, blockServiceId)
		if err != nil {
			log.Fatalf("error while migrating file %v away from block service %v after %v blocks: %v", fileId, blockServiceId, migrated, err)
		}
		env.Info("finished migrating %v away from block service %v, %v blocks migrated", fileId, blockServiceId, migrated)
	default:
		badCommand()
	}
}
