package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"time"
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
	collectDeleteAfterVersions := collectCmd.Int("deleteAfterVersions", 0, "delete snapshots beyond N versions")
	collectDeleteAfterTime := collectCmd.Duration("deleteAfterTime", time.Duration(0), "delete snapshot beyond time interval")

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructFileIdU64 := destructCmd.Uint64("file", 0, "transient file id to destruct")

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

		socket, err := request.ShardSocket(dirId.Shard())
		if err != nil {
			log.Fatalf("could not create shard socket: %v", err)
		}
		policy := gc.Policy{
			DeleteAfterVersions: *collectDeleteAfterVersions,
			DeleteAfterTime:     *collectDeleteAfterTime,
		}
		gcEnv := gc.GcEnv{
			Role:         "cli",
			Logger:       log.New(os.Stdout, "", log.Lshortfile),
			Shid:         dirId.Shard(),
			Buffer:       make([]byte, msgs.UDP_MTU),
			ShardTimeout: 10 * time.Second,
			Verbose:      true,
			ShardSocket:  socket,
			Policy:       policy,
		}
		stats := gc.CollectStats{}
		gcEnv.CollectInDirectory(&stats, *collectDry, dirId)
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
	default:
		badCommand()
	}
}
