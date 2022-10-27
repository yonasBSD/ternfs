package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"time"
	"xtx/eggsfs/cdckey"
	"xtx/eggsfs/gc"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

func badCommand() {
	fmt.Printf("expected 'collect' or 'destruct' subcommand\n")
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

	if len(os.Args) < 2 {
		badCommand()
	}

	switch os.Args[1] {
	case "collect":
		collectCmd.Parse(os.Args[2:])
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
		gcEnv := gc.GcEnv{
			Role:        "cli",
			Logger:      log.New(os.Stdout, "", log.Lshortfile),
			Shid:        dirId.Shard(),
			Timeout:     10 * time.Second,
			Verbose:     true,
			ShardSocket: shardSocket,
			CDCSocket:   cdcSocket,
			Dry:         *collectDry,
			CDCKey:      cdckey.CDCKey(),
		}
		stats := gc.CollectStats{}
		err = gcEnv.CollectDirectory(&stats, dirId)
		if err != nil {
			log.Fatalf("could not collect %v, stats: %+v, err: %v", dirId, stats, err)
		}
		gcEnv.Info("finished collecting %v, stats: %+v", dirId, stats)
	case "destruct":
		destructCmd.Parse(os.Args[2:])
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
		gcEnv := gc.GcEnv{
			Role:        "cli",
			Logger:      log.New(os.Stdout, "", log.Lshortfile),
			Shid:        fileId.Shard(),
			Timeout:     10 * time.Second,
			Verbose:     true,
			ShardSocket: socket,
			Dry:         *destructDry,
			CDCKey:      cdckey.CDCKey(),
		}
		stats := gc.DestructionStats{}
		err = gcEnv.DestructFile(&stats, fileId, 0, *destructFileCookie)
		if err != nil {
			log.Fatalf("could not destruct %v, stats: %+v, err: %v", fileId, stats, err)
		}
		gcEnv.Info("finished destructing %v, stats: %+v", fileId, stats)
	default:
		badCommand()
	}
}
