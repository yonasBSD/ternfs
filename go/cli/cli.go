package main

import (
	"encoding/binary"
	"flag"
	"fmt"
	"os"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func badCommand() {
	fmt.Fprintf(os.Stderr, "expected 'collect', 'destruct', or 'migrate' subcommand\n")
	os.Exit(2)
}

func noRunawayArgs() {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func main() {
	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC. If not present, they'll all be collected.")

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructFileIdU64 := destructCmd.Uint64("file", 0, "Transient file id to destruct. If not present, they'll all be destructed.")
	destructFileCookieU64 := destructCmd.Uint64("cookie", 0, "Transient file cookie. Must be present if file is specified.")

	migrateCmd := flag.NewFlagSet("migrate", flag.ExitOnError)
	migrateBlockService := migrateCmd.Uint64("blockservice", 0, "Block service to migrate from.")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "File in which to migrate blocks. If not present, all files will be migrated.")

	if len(os.Args) < 2 {
		badCommand()
	}

	log := &eggs.LogLogger{
		Logger:  eggs.NewLogger(os.Stdout),
		Verbose: true,
	}

	switch os.Args[1] {
	case "collect":
		collectCmd.Parse(os.Args[2:])
		noRunawayArgs()
		if *collectDirIdU64 == 0 {
			if err := eggs.CollectDirectoriesInAllShards(log, nil); err != nil {
				panic(err)
			}
		} else {
			dirId := msgs.InodeId(*collectDirIdU64)
			if dirId.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a directory", dirId))
			}
			shid := dirId.Shard()
			client, err := eggs.NewClient(&shid, nil, nil)
			if err != nil {
				panic(fmt.Errorf("could not create shard client: %v", err))
			}
			defer client.Close()
			stats := eggs.CollectStats{}
			dirInfoCache := eggs.NewDirInfoCache()
			if err := eggs.CollectDirectory(log, client, dirInfoCache, &stats, dirId); err != nil {
				panic(fmt.Errorf("could not collect %v, stats: %+v, err: %v", dirId, stats, err))
			}
			log.Info("finished collecting %v, stats: %+v", dirId, stats)
		}
	case "destruct":
		destructCmd.Parse(os.Args[2:])
		noRunawayArgs()
		if *destructFileIdU64 == 0 {
			if err := eggs.DestructFilesInAllShards(log, nil, nil); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a file/symlink", fileId))
			}
			shid := fileId.Shard()
			client, err := eggs.NewClient(&shid, nil, nil)
			if err != nil {
				panic(err)
			}
			defer client.Close()
			stats := eggs.DestructionStats{}
			var destructFileCookie [8]byte
			binary.LittleEndian.PutUint64(destructFileCookie[:], *destructFileCookieU64)
			err = eggs.DestructFile(log, client, nil, &stats, fileId, 0, destructFileCookie)
			if err != nil {
				panic(fmt.Errorf("could not destruct %v, stats: %+v, err: %v", fileId, stats, err))
			}
			log.Info("finished destructing %v, stats: %+v", fileId, stats)
		}
	case "migrate":
		migrateCmd.Parse(os.Args[2:])
		noRunawayArgs()
		if *migrateBlockService == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		blockServiceId := msgs.BlockServiceId(*migrateBlockService)
		if *migrateFileIdU64 == 0 {
			if err := eggs.MigrateBlocksInAllShards(log, blockServiceId); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*migrateFileIdU64)
			shid := fileId.Shard()
			client, err := eggs.NewClient(&shid, nil, nil)
			if err != nil {
				panic(fmt.Errorf("could not create shard socket: %v", err))
			}
			defer client.Close()
			stats := eggs.MigrateStats{}
			if err := eggs.MigrateBlocksInFile(log, client, &stats, blockServiceId, fileId); err != nil {
				panic(fmt.Errorf("error while migrating file %v away from block service %v: %v", fileId, blockServiceId, err))
			}
			log.Info("finished migrating %v away from block service %v, stats: %+v", fileId, blockServiceId, stats)
		}
	default:
		badCommand()
	}
}
