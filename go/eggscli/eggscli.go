package main

import (
	"encoding/binary"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"strings"
	"xtx/eggsfs/lib"
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
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")

	var log *lib.Logger

	commands = make(map[string]commandSpec)

	collectCmd := flag.NewFlagSet("collect", flag.ExitOnError)
	collectDirIdU64 := collectCmd.Uint64("dir", 0, "Directory inode id to GC. If not present, they'll all be collected.")
	collectRun := func() {
		if *collectDirIdU64 == 0 {
			if err := lib.CollectDirectoriesInAllShards(log, *shuckleAddress, nil); err != nil {
				panic(err)
			}
		} else {
			dirId := msgs.InodeId(*collectDirIdU64)
			if dirId.Type() != msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a directory", dirId))
			}
			shid := dirId.Shard()
			client, err := lib.NewClient(log, *shuckleAddress, &shid, nil, nil)
			if err != nil {
				panic(fmt.Errorf("could not create shard client: %v", err))
			}
			defer client.Close()
			stats := lib.CollectStats{}
			dirInfoCache := lib.NewDirInfoCache()
			if err := lib.CollectDirectory(log, client, dirInfoCache, &stats, dirId); err != nil {
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
			if err := lib.DestructFilesInAllShards(log, *shuckleAddress, nil); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*destructFileIdU64)
			if fileId.Type() == msgs.DIRECTORY {
				panic(fmt.Errorf("inode id %v is not a file/symlink", fileId))
			}
			shid := fileId.Shard()
			client, err := lib.NewClient(log, *shuckleAddress, &shid, nil, nil)
			if err != nil {
				panic(err)
			}
			defer client.Close()
			stats := lib.DestructionStats{}
			var destructFileCookie [8]byte
			binary.LittleEndian.PutUint64(destructFileCookie[:], *destructFileCookieU64)
			err = lib.DestructFile(log, client, &stats, fileId, 0, destructFileCookie)
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
		stats := lib.MigrateStats{}
		if *migrateFileIdU64 == 0 {
			client, err := lib.NewClient(log, *shuckleAddress, nil, nil, nil)
			if err != nil {
				panic(err)
			}
			if err := lib.MigrateBlocksInAllShards(log, client, &stats, blockServiceId); err != nil {
				panic(err)
			}
		} else {
			fileId := msgs.InodeId(*migrateFileIdU64)
			shid := fileId.Shard()
			client, err := lib.NewClient(log, *shuckleAddress, &shid, nil, nil)
			if err != nil {
				panic(fmt.Errorf("could not create shard socket: %v", err))
			}
			defer client.Close()
			stats := lib.MigrateStats{}
			if err := lib.MigrateBlocksInFile(log, client, &stats, blockServiceId, fileId); err != nil {
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
		client, err := lib.NewClient(log, *shuckleAddress, &shard, nil, nil)
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

	cdcReqCmd := flag.NewFlagSet("cdc-req", flag.ExitOnError)
	cdcReqKind := cdcReqCmd.String("kind", "", "")
	cdcReqReq := cdcReqCmd.String("req", "", "Request body, in JSON")
	cdcReqRun := func() {
		req, resp := msgs.MkCDCMessage(*cdcReqKind)
		if err := json.Unmarshal([]byte(*cdcReqReq), &req); err != nil {
			panic(fmt.Errorf("could not decode cdc req: %w", err))
		}
		client, err := lib.NewClient(log, *shuckleAddress, nil, nil, nil)
		if err != nil {
			panic(err)
		}
		defer client.Close()
		if err := client.CDCRequest(log, req, resp); err != nil {
			panic(err)
		}
		out, err := json.MarshalIndent(resp, "", "  ")
		if err != nil {
			panic(fmt.Errorf("could not encode response %+v to json: %w", resp, err))
		}
		os.Stdout.Write(out)
		fmt.Println()
	}
	commands["cdc-req"] = commandSpec{
		flags: cdcReqCmd,
		run:   cdcReqRun,
	}

	setDirInfoCmd := flag.NewFlagSet("set-dir-info", flag.ExitOnError)
	setDirInfoIdU64 := setDirInfoCmd.Uint64("id", 0, "InodeId for the directory to set the policy of.")
	setDirInfoIdTag := setDirInfoCmd.String("tag", "", "One of SNAPSHOT|SPAN|BLOCK")
	setDirInfoPolicy := setDirInfoCmd.String("body", "", "Policy, in JSON")
	setDirInfoRun := func() {
		entry := msgs.TagToDirInfoEntry(msgs.DirInfoTagFromName(*setDirInfoIdTag))
		if err := json.Unmarshal([]byte(*setDirInfoPolicy), entry); err != nil {
			panic(fmt.Errorf("could not decode directory info: %w", err))
		}
		id := msgs.InodeId(*setDirInfoIdU64)
		fmt.Printf("Will set dir info %v to directory %v:\n", entry.Tag(), id)
		fmt.Printf("%+v\n", entry)
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
		client, err := lib.NewClient(log, *shuckleAddress, &shid, nil, nil)
		if err != nil {
			panic(err)
		}
		if err := client.MergeDirectoryInfo(log, id, entry); err != nil {
			panic(err)
		}
	}
	commands["set-dir-info"] = commandSpec{
		flags: setDirInfoCmd,
		run:   setDirInfoRun,
	}

	removeDirInfoCmd := flag.NewFlagSet("remove-dir-info", flag.ExitOnError)
	removeDirInfoU64 := removeDirInfoCmd.Uint64("id", 0, "InodeId for the directory to unset the policy of.")
	removeDirInfoTag := removeDirInfoCmd.String("tag", "", "One of SNAPSHOT|SPAN|BLOCK")
	removeDirInfoRun := func() {
		id := msgs.InodeId(*removeDirInfoU64)
		shid := id.Shard()
		client, err := lib.NewClient(log, *shuckleAddress, &shid, nil, nil)
		if err != nil {
			panic(err)
		}
		if err := client.RemoveDirectoryInfoEntry(log, id, msgs.DirInfoTagFromName(*removeDirInfoTag)); err != nil {
			panic(err)
		}
	}
	commands["remove-dir-info"] = commandSpec{
		flags: removeDirInfoCmd,
		run:   removeDirInfoRun,
	}

	/*
		cpIntoCmd := flag.NewFlagSet("cp-into", flag.ExitOnError)
		cpIntoInput := cpIntoCmd.String("i", "", "What to copy, if empty stdin.")
		cpIntoOut := cpIntoCmd.String("o", "", "Where to write the file to in Eggs")
		cpIntoRun := func() {
			path := filepath.Clean("/" + *cpIntoOut)
			dirPath := filepath.Dir(path)
			fileName := filepath.Base(path)
			if fileName == dirPath {
				fmt.Fprintf(os.Stderr, "Bad output path '%v'.\n", *cpIntoOut)
				os.Exit(2)
			}
			client, err := lib.NewClient(log, *shuckleAddress, nil, nil, nil)
			if err != nil {
				panic(err)
			}
			dirId, err := client.ResolvePath(log, dirPath)
			if err != nil {
				panic(err)
			}
			dirInfoCache := lib.NewDirInfoCache()
			spanPolicies := msgs.SpanPolicy{}
			if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &spanPolicies); err != nil {
				panic(err)
			}
			blockPolicies := msgs.BlockPolicy{}
			if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &blockPolicies); err != nil {
				panic(err)
			}
			fileResp := msgs.ConstructFileResp{}
			if err := client.ShardRequest(log, dirId.Shard(), &msgs.ConstructFileReq{Type: msgs.FILE}, &fileResp); err != nil {
				panic(err)
			}
			fileId := fileResp.Id
			cookie := fileResp.Cookie
			var input io.Reader
			if *cpIntoInput == "" {
				input = os.Stdin
			} else {
				var err error
				input, err = os.Open(*cpIntoInput)
				if err != nil {
					panic(err)
				}
			}
			maxSpanSize := spanPolicies.Entries[len(spanPolicies.Entries)-1].MaxSize
			spanBuf := make([]byte, maxSpanSize+16) // 16 = max zero padding
			spanBuf = spanBuf[:maxSpanSize]
			offset := uint64(0)
			for {
				read, err := io.ReadFull(input, spanBuf)
				if err != nil && err != io.EOF && err != io.ErrUnexpectedEOF {
					panic(err)
				}
				if err == io.EOF {
					break
				}
				spanBuf, err = client.CreateSpan(log, []msgs.BlockServiceBlacklist{}, fileId, cookie, offset, &spanPolicies, &blockPolicies, offset, read, spanBuf[:read])
				if err := client.CreateSpan(log, []msgs.BlockServiceBlacklist{}, fileId, cookie, offset, &spanPolicies, &blockPolicies, spanBuf[:read]); err != nil {
					panic(err)
				}
				offset += uint64(read)
				if read < len(spanBuf) {
					break
				}
			}
			if err := client.ShardRequest(log, dirId.Shard(), &msgs.LinkFileReq{FileId: fileId, Cookie: cookie, OwnerId: dirId, Name: fileName}, &msgs.LinkFileResp{}); err != nil {
				panic(err)
			}
		}
		commands["cp-into"] = commandSpec{
			flags: cpIntoCmd,
			run:   cpIntoRun,
		}

		cpOutofCmd := flag.NewFlagSet("cp-outof", flag.ExitOnError)
		cpOutofInput := cpOutofCmd.String("i", "", "What to copy from lib.")
		cpOutofOut := cpOutofCmd.String("o", "", "Where to write the file to. Stdout if empty.")
		cpOutofExclude := cpOutofCmd.String("exclude", "", "What (if any) block services to exclude")
		cpOutofRun := func() {
			exclude := []msgs.BlockServiceBlacklist{}
			if *cpOutofExclude != "" {
				for _, s := range strings.Split(*cpOutofExclude, ",") {
					u, err := strconv.ParseUint(s, 0, 64)
					if err != nil {
						panic(err)
					}
					exclude = append(exclude, msgs.BlockServiceBlacklist{Id: msgs.BlockServiceId(u)})
				}
			}
			out := os.Stdout
			if *cpOutofOut != "" {
				var err error
				out, err = os.Create(*cpOutofOut)
				if err != nil {
					panic(err)
				}
			}
			client, err := lib.NewClient(log, *shuckleAddress, nil, nil, nil)
			if err != nil {
				panic(err)
			}
			id, err := client.ResolvePath(log, *cpOutofInput)
			if err != nil {
				panic(err)
			}
			offset := uint64(0)
			buf := make([]byte, 100<<20+16)
			for {
				var data []byte
				_, data, err = client.ReadSpan(log, exclude, id, offset, buf)
				if err == msgs.SPAN_NOT_FOUND {
					break
				}
				if err != nil {
					panic(err)
				}
				out.Write(data)
				offset += uint64(len(data))
			}
		}
		commands["cp-outof"] = commandSpec{
			flags: cpOutofCmd,
			run:   cpOutofRun,
		}
	*/

	flag.Parse()

	if flag.NArg() < 1 {
		fmt.Fprintf(os.Stderr, "No command provided.\n\n")
		flag.Usage()
		os.Exit(2)
	}

	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log = lib.NewLogger(level, os.Stdout)

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
