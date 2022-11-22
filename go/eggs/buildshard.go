package eggs

import (
	"fmt"
	"os"
	"os/exec"
	"path"
	"runtime"
	"strings"
)

type BuildShardOpts struct {
	Valgrind bool
	Sanitize bool
	Debug    bool
}

func BuildShardExe(ll LogLevels, opts *BuildShardOpts) string {
	buildArgs := []string{"-j"}
	if opts.Valgrind {
		buildArgs = append(buildArgs, "valgrind=yes")
	}
	if opts.Sanitize {
		buildArgs = append(buildArgs, "sanitize=yes")
	}
	if opts.Debug {
		buildArgs = append(buildArgs, "debug=yes")
	}
	buildArgs = append(buildArgs, "eggs-shard")
	buildCmd := exec.Command("make", buildArgs...)
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	cppDir := path.Join(path.Dir(path.Dir(path.Dir(filename))), "cpp")
	buildCmd.Dir = cppDir
	ll.Info("building eggs-shard with `make %s'", strings.Join(buildArgs, " "))
	if out, err := buildCmd.CombinedOutput(); err != nil {
		fmt.Printf("build output:\n")
		os.Stdout.Write(out)
		panic(fmt.Errorf("could not build shard: %w", err))
	}
	return path.Join(cppDir, "eggs-shard")
}
