package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

type commandSpec struct {
	flags *flag.FlagSet
	run   func()
}

var commands map[string]commandSpec

func noRunawayArgs(flag *flag.FlagSet) {
	if flag.NArg() > 0 {
		fmt.Fprintf(os.Stderr, "Unexpected extra arguments %v\n", flag.Args())
		os.Exit(2)
	}
}

func usage() {
	commandsStrs := []string{}
	for c := range commands {
		commandsStrs = append(commandsStrs, c)
	}
	fmt.Fprintf(os.Stderr, "Usage: %v %v\n\n", os.Args[0], strings.Join(commandsStrs, "|"))
	fmt.Fprintf(os.Stderr, "Global options:\n\n")
	flag.PrintDefaults()
}

func main() {
	flag.Usage = usage
	shuckleAddress := flag.String("shuckle", eggs.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	verbose := flag.Bool("verbose", false, "")

	var log *eggs.Logger

	commands = make(map[string]commandSpec)

	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC. If not present, they'll all be collected.")
	collectRun := func() {
		if *collectDirIdU64 == 0 {
			if err := eggs.CollectDirectoriesInAllShards(log, *shuckleAddress, nil); err != nil {
				panic(err)
			}
		} else {
			dirId := msgs.InodeId(*collectDirIdU64)
			if dirId.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a directory", dirId))
			}
			shid := dirId.Shard()
			client, err := eggs.NewClient(log, *shuckleAddress, &shid, nil, nil)
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
	}
	commands["collect"] = commandSpec{
		flags: collectCmd,
		run:   collectRun,
	}

	destructCmd := flag.NewFlagSet("destruct", flag.ExitOnError)
	destructFileIdU64 := destructCmd.Uint64("file", 0, "Transient file id to destruct. If not present, they'll all be destructed.")
	destructFileCookieU64 := destructCmd.Uint64("cookie", 0, "Transient file cookie. Must be present if file is specified.")
	destructRun := func() {
		if *destructFileIdU64 == 0 {
			if err := eggs.DestructFilesInAllShards(log, *shuckleAddress, nil, nil); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a file/symlink", fileId))
			}
			shid := fileId.Shard()
			client, err := eggs.NewClient(log, *shuckleAddress, &shid, nil, nil)
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
	}
	commands["destruct"] = commandSpec{
		flags: destructCmd,
		run:   destructRun,
	}

	migrateCmd := flag.NewFlagSet("migrate", flag.ExitOnError)
	migrateBlockService := migrateCmd.Uint64("blockservice", 0, "Block service to migrate from.")
	migrateFileIdU64 := migrateCmd.Uint64("file", 0, "File in which to migrate blocks. If not present, all files will be migrated.")
	migrateRun := func() {
		if *migrateBlockService == 0 {
			migrateCmd.Usage()
			os.Exit(2)
		}
		blockServiceId := msgs.BlockServiceId(*migrateBlockService)
		stats := eggs.MigrateStats{}
		if *migrateFileIdU64 == 0 {
			client, err := eggs.NewClient(log, *shuckleAddress, nil, nil, nil)
			if err != nil {
				panic(err)
			}
			if err := eggs.MigrateBlocksInAllShards(log, client, nil, &stats, blockServiceId); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*migrateFileIdU64)
			shid := fileId.Shard()
			client, err := eggs.NewClient(log, *shuckleAddress, &shid, nil, nil)
			if err != nil {
				panic(fmt.Errorf("could not create shard socket: %v", err))
			}
			defer client.Close()
			stats := eggs.MigrateStats{}
			if err := eggs.MigrateBlocksInFile(log, client, nil, &stats, blockServiceId, fileId); err != nil {
				panic(fmt.Errorf("error while migrating file %v away from block service %v: %v", fileId, blockServiceId, err))
			}
			log.Info("finished migrating %v away from block service %v, stats: %+v", fileId, blockServiceId, stats)
		}
	}
	commands["migrate"] = commandSpec{
		flags: migrateCmd,
		run:   migrateRun,
	}

	shardReqCmd := flag.NewFlagSet("shard-req", flag.ExitOnError)
	shardReqShard := shardReqCmd.Uint("shard", 0, "Shard to send the req too")
	shardReqKind := shardReqCmd.String("kind", "", "")
	shardReqReq := shardReqCmd.String("req", "", "Request body, in JSON")
	shardReqRun := func() {
		req, resp := msgs.MkShardMessage(*shardReqKind)
		if err := json.Unmarshal([]byte(*shardReqReq), &req); err != nil {
			panic(fmt.Errorf("could not decode shard req: %w", err))
		}
		shard := msgs.ShardId(*shardReqShard)
		client, err := eggs.NewClient(log, *shuckleAddress, &shard, nil, nil)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		if err := client.ShardRequest(log, shard, req, resp); err != nil {
			panic(err)
		}
		out, err := json.MarshalIndent(resp, "", "  ")
		if err != nil {
			panic(fmt.Errorf("could not encode response %+v to json: %w", resp, err))
		}
		os.Stdout.Write(out)
		fmt.Println()
	}
	commands["shard-req"] = commandSpec{
		flags: shardReqCmd,
		run:   shardReqRun,
	}

	setPolicyCmd := flag.NewFlagSet("set-policy", flag.ExitOnError)
	setPolicyIdU64 := setPolicyCmd.Uint64("id", 0, "InodeId for the directory to set the policy of.")
	setPolicyPolicy := setPolicyCmd.String("policy", "", "Policy, in JSON")
	setPolicyRun := func() {
		info := msgs.DirectoryInfoBody{}
		if err := json.Unmarshal([]byte(*setPolicyPolicy), &info); err != nil {
			panic(fmt.Errorf("could not decode directory info: %w", err))
		}
		id := msgs.InodeId(*setPolicyIdU64)
		fmt.Printf("Will set following policy to directory %v:\n", id)
		fmt.Printf("%+v\n", info)
		for {
			var action string
			fmt.Printf("Proceed? y/n ")
			fmt.Scanln(&action)
			if action == "y" {
				break
			}
			if action == "n" {
				fmt.Printf("BYE\n")
				os.Exit(0)
			}
		}
		shid := id.Shard()
		client, err := eggs.NewClient(log, *shuckleAddress, &shid, nil, nil)
		if err != nil {
			panic(err)
		}
		req := msgs.SetDirectoryInfoReq{
			Id: id,
			Info: msgs.SetDirectoryInfo{
				Inherited: false,
				Body:      bincode.Pack(&info),
			},
		}
		if err := client.ShardRequest(log, shid, &req, &msgs.SetDirectoryInfoResp{}); err != nil {
			panic(err)
		}
	}
	commands["set-policy"] = commandSpec{
		flags: setPolicyCmd,
		run:   setPolicyRun,
	}

	unsetPolicyCmd := flag.NewFlagSet("unset-policy", flag.ExitOnError)
	unsetPolicyIdU64 := unsetPolicyCmd.Uint64("id", 0, "InodeId for the directory to unset the policy of.")
	unsetPolicyRun := func() {
		id := msgs.InodeId(*unsetPolicyIdU64)
		shid := id.Shard()
		client, err := eggs.NewClient(log, *shuckleAddress, &shid, nil, nil)
		if err != nil {
			panic(err)
		}
		req := msgs.SetDirectoryInfoReq{
			Id: id,
			Info: msgs.SetDirectoryInfo{
				Inherited: true,
			},
		}
		if err := client.ShardRequest(log, shid, &req, &msgs.SetDirectoryInfoResp{}); err != nil {
			panic(err)
		}
	}
	commands["unset-policy"] = commandSpec{
		flags: unsetPolicyCmd,
		run:   unsetPolicyRun,
	}

	flag.Parse()

	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "No command provided.\n\n")
		flag.Usage()
		os.Exit(2)
	}

	log = eggs.NewLogger(*verbose, os.Stdout)

	spec, found := commands[flag.Args()[0]]
	if !found {
		fmt.Fprintf(os.Stderr, "Bad subcommand %v provided.\n\n", flag.Args()[0])
		flag.Usage()
		os.Exit(2)
	}
	spec.flags.Parse(flag.Args()[1:])
	noRunawayArgs(spec.flags)
	spec.run()
}
