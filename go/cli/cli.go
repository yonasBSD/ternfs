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
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC")
	collectDry := collectCmd.Bool("dry", false, "Whether to execute the GC or not")

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructDry := destructCmd.Bool("dry", false, "Whether to execute the destruction or not")
	destructFileIdU64 := destructCmd.Uint64("file", 0, "Transient file id to destruct. If not present, they'll all be destructed.")
	destructFileCookie := destructCmd.Uint64("cookie", 0, "Transient file cookie. Must be present if file is specified.")

	migrateCmd := flag.NewFlagSet("migrate", flag.ExitOnError)
	migrateDry := migrateCmd.Bool("dry", false, "Whether to execute the migration or not.")
	migrateBlockService := migrateCmd.Uint64("blockservice", 0, "Block service to migrate from.")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "File in which to migrate blocks. If not present, all files will be migrated.")

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
		env.Shid = dirId.Shard()
		shardSocket, err := request.ShardSocket(env.Shid)
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
			stats := janitor.DestructionStats{}
			for shid := 0; shid < 256; shid++ {
				env.Shid = msgs.ShardId(shid)
				socket, err := request.ShardSocket(env.Shid)
				if err != nil {
					log.Fatalf("could not create shard socket: %v", err)
				}
				defer socket.Close()
				env.ShardSocket = socket
				err = env.DestructFiles(&stats)
				if err != nil {
					log.Fatalf("could not destruct in shard %v, stats: %+v, err: %v", shid, stats, err)
				}
			}
			env.Info("finished destructing files, stats: %+v", stats)
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				log.Fatalf("inode id %v is not a file/symlink", fileId)
			}
			env.Shid = fileId.Shard()
			shardSocket, err := request.ShardSocket(env.Shid)
			if err != nil {
				log.Fatalf("could not create shard socket: %v", err)
			}
			defer shardSocket.Close()
			env.ShardSocket = shardSocket
			stats := janitor.DestructionStats{}
			err = env.DestructFile(&stats, fileId, 0, *destructFileCookie)
			if err != nil {
				log.Fatalf("could not destruct %v, stats: %+v, err: %v", fileId, stats, err)
			}
			env.Info("finished destructing %v, stats: %+v", fileId, stats)
		}
	case "migrate":
		migrateCmd.Parse(os.Args[2:])
		env.Dry = *migrateDry
		if *migrateBlockService == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		blockServiceId := msgs.BlockServiceId(*migrateBlockService)
		if *migrateFileIdU64 == 0 {
			stats := janitor.MigrateStats{}
			for shid := 0; shid < 256; shid++ {
				env.Shid = msgs.ShardId(shid)
				socket, err := request.ShardSocket(env.Shid)
				if err != nil {
					log.Fatalf("could not create shard socket: %v", err)
				}
				defer socket.Close()
				env.ShardSocket = socket
				if err := env.MigrateBlocks(&stats, blockServiceId); err != nil {
					log.Fatalf("could not destruct in shard %v, stats: %+v, err: %v", shid, stats, err)
				}
			}
			env.Info("finished collecting, stats: %+v", stats)
		} else {
			fileId := msgs.InodeId(*migrateFileIdU64)
			env.Shid = fileId.Shard()
			socket, err := request.ShardSocket(env.Shid)
			if err != nil {
				log.Fatalf("could not create shard socket: %v", err)
			}
			defer socket.Close()
			env.ShardSocket = socket
			stats := janitor.MigrateStats{}
			if err := env.MigrateBlocksInFile(&stats, blockServiceId, fileId); err != nil {
				log.Fatalf("error while migrating file %v away from block service %v: %v", fileId, blockServiceId, err)
			}
			env.Info("finished migrating %v away from block service %v, stats: %+v", fileId, blockServiceId, stats)
		}
	default:
		badCommand()
	}
}
