package main

import (
	"fmt"
	"os"
	"os/exec"
	"path"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/wyhash"
)

type rsyncTestOpts struct {
	numDirs     uint32
	numFiles    uint32
	maxFileSize uint64
}

func dirName(mountPoint string, i int) string {
	return path.Join(mountPoint, fmt.Sprintf("dir_%v", i))
}

func rsyncTest(
	log *lib.Logger,
	opts *rsyncTestOpts,
	mountPoint string,
) {
	tmpDir1, err := os.MkdirTemp("", "eggs-rsynctest.")
	if err != nil {
		panic(err)
	}
	defer func() {
		os.RemoveAll(tmpDir1)
	}()
	// create directories
	for i := 0; i < int(opts.numDirs); i++ {
		if err := os.Mkdir(dirName(tmpDir1, i), 0777); err != nil {
			panic(err)
		}
	}
	// create files
	rand := wyhash.New(0)
	buf := []byte{}
	for i := 0; i < int(opts.numFiles); i++ {
		dirIx := rand.Uint32() % (opts.numDirs + 1)
		var dir string
		if dirIx == 0 {
			dir = tmpDir1
		} else {
			dir = dirName(tmpDir1, int(dirIx-1))
		}
		size := rand.Uint64() % opts.maxFileSize
		buf = ensureLen(buf, int(size))
		rand.Read(buf)
		if err := os.WriteFile(path.Join(dir, fmt.Sprintf("file_%v", i)), buf, 0666); err != nil {
			panic(err)
		}
	}
	// rsync into mountpoint
	rsyncCmd := exec.Command("rsync", "-rv", tmpDir1+"/", mountPoint+"/")
	rsyncCmd.Stdout = log.Sink(lib.INFO)
	rsyncCmd.Stderr = log.Sink(lib.INFO)
	if err := rsyncCmd.Run(); err != nil {
		panic(err)
	}
	// diff directories
	diffCmd := exec.Command("diff", "-rq", tmpDir1, mountPoint)
	diffCmd.Stdout = log.Sink(lib.INFO)
	diffCmd.Stderr = log.Sink(lib.INFO)
	if err := diffCmd.Run(); err != nil {
		panic(err)
	}
}
