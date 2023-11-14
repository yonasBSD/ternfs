// Useful when we have an application which spawns some process and wants to tear down all
// of them when it goes down, and also wants to go down itself if any of the processes
// terminates.
package managedprocess

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

func goDir(repoDir string) string {
	return path.Join(repoDir, "go")
}

func cppDir(repoDir string) string {
	return path.Join(repoDir, "cpp")
}

type managedProcess struct {
	cmd             *exec.Cmd
	name            string
	terminateOnExit bool
	exitedChan      chan struct{}
	closeInitiated  uint32
}

// Returns no error because `terminateChan` is used to propagate errors
// upwards anyway.
func (proc *managedProcess) Wait() {
	<-proc.exitedChan
	proc.exitedChan <- struct{}{} // continue other waiters
}

type ManagedProcesses struct {
	terminateChan chan any
	mu            sync.Mutex
	processes     map[uint64]*managedProcess
	procId        uint64
}

// This function will take over signals in a possibly surprising way!
// See installSignalHandlers().
func New(terminateChan chan any) *ManagedProcesses {
	procs := ManagedProcesses{
		terminateChan: terminateChan,
		processes:     make(map[uint64]*managedProcess),
	}
	procs.installSignalHandlers()
	return &procs
}

type ManagedProcessArgs struct {
	Exe        string
	Args       []string
	Name       string
	Dir        string
	StdoutFile string
	StderrFile string
	Env        []string
	// If `TerminateOnExit` is true, then `terminateChan` will be
	// filled in with an error if the process terminates without
	// an error. Useful for background process that are supposed
	// to be up the whole time.
	TerminateOnExit bool
}

type ManagedProcessTerminated struct{}

func initOut(filename string) io.Writer {
	if filename != "" {
		f, err := os.Create(filename)
		if err != nil {
			panic(fmt.Errorf("could not open file %v: %w", filename, err))
		}
		return f
	} else {
		return &bytes.Buffer{}
	}
}

func printOut(what string, out io.Writer, w io.Writer) {
	fmt.Printf("%s:", what)
	switch f := w.(type) {
	case *os.File:
		f.Sync()
		fmt.Printf(" %s\n", f.Name())
		bytes, err := os.ReadFile(f.Name())
		if err != nil {
			panic(err)
		}
		out.Write(bytes)
	case *bytes.Buffer:
		fmt.Println()
		out.Write(f.Bytes())
	default:
		panic("bad out")
	}
}

func closeOut(out io.Writer) {
	switch f := out.(type) {
	case *os.File:
		f.Close()
	}
}

type ManagedProcessId uint64

func (procs *ManagedProcesses) Start(ll *lib.Logger, args *ManagedProcessArgs) ManagedProcessId {
	exitedChan := make(chan struct{}, 1)

	procs.mu.Lock()
	id := procs.procId
	procs.procId++
	proc := &managedProcess{
		cmd:        exec.Command(args.Exe, args.Args...),
		name:       args.Name,
		exitedChan: exitedChan,
	}
	procs.processes[id] = proc
	procs.mu.Unlock()

	proc.cmd.Dir = args.Dir
	proc.cmd.Stdout = initOut(args.StdoutFile)
	proc.cmd.Stderr = initOut(args.StderrFile)

	proc.cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid: true, // do not propagate SIGINT, we do our own signal handling
	}

	proc.terminateOnExit = args.TerminateOnExit

	if args.Env != nil {
		proc.cmd.Env = args.Env
	}

	ll.Debug("starting %v", proc.cmd)
	if err := proc.cmd.Start(); err != nil {
		procs.mu.Lock()
		delete(procs.processes, id)
		procs.mu.Unlock()
		panic(fmt.Errorf("could not start process %s: %w", proc.name, err))
	}

	go func() {
		err := proc.cmd.Wait()

		if atomic.LoadUint32(&proc.closeInitiated) == 0 {
			if err == nil {
				if proc.terminateOnExit {
					select {
					case procs.terminateChan <- fmt.Errorf("%s died", proc.name):
					default:
					}
				}
			} else {
				procs.mu.Lock()
				fmt.Printf("%s crashed: %v\n", args.Name, err)
				fmt.Printf("cmdline: %s %s\n", args.Exe, strings.Join(args.Args, " "))
				if len(args.Env) > 0 {
					fmt.Printf("env: %v\n", args.Env)
				}
				printOut("stdout", os.Stdout, proc.cmd.Stdout)
				printOut("stderr", os.Stderr, proc.cmd.Stderr)
				fmt.Println()
				procs.mu.Unlock()
				select {
				case procs.terminateChan <- fmt.Errorf("%s crashed: %w", proc.name, err):
				default:
				}
			}
		}

		closeOut(proc.cmd.Stdout)
		closeOut(proc.cmd.Stderr)

		exitedChan <- struct{}{}
	}()

	return ManagedProcessId(id)
}

// Waits for the process to exit before terminating.
func (procs *ManagedProcesses) Kill(id ManagedProcessId, sig syscall.Signal) {
	procs.mu.Lock()
	proc, present := procs.processes[uint64(id)]
	if !present {
		procs.mu.Unlock()
		// process is already gone
		return
	}
	delete(procs.processes, uint64(id))
	procs.mu.Unlock()

	atomic.StoreUint32(&proc.closeInitiated, 1)
	if proc.cmd == nil || proc.cmd.Process == nil {
		return
	}
	proc.cmd.Process.Signal(sig)
	terminated := uint64(0)
	// wait at most 20 seconds for process to come down
	if sig != syscall.SIGKILL {
		go func() {
			time.Sleep(20 * time.Second)
			if atomic.LoadUint64(&terminated) == 0 {
				fmt.Printf("process %s not terminating, killing it\n", proc.name)
				proc.cmd.Process.Kill() // ignoring error on purpose, there isn't much to do by now
			}
		}()
	}
	<-proc.exitedChan
	atomic.StoreUint64(&terminated, 1)
	proc.exitedChan <- struct{}{}
}

func (procs *ManagedProcesses) Close() {
	// we don't try to be thread safe here...
	fmt.Printf("terminating %v managed processes\n", len(procs.processes))
	var wait sync.WaitGroup
	wait.Add(len(procs.processes))
	ids := make([]ManagedProcessId, len(procs.processes))
	ids = ids[:0]
	for id := range procs.processes {
		ids = append(ids, ManagedProcessId(id))
	}
	for i := range ids {
		id := ids[i]
		go func() {
			procs.Kill(id, syscall.SIGTERM)
			wait.Done()
		}()
	}
	wait.Wait()
}

func (procs *ManagedProcesses) installSignalHandlers() {
	// Cleanup if we get killed with a signal. Obviously we can't do much
	// in the case of SIGKILL or SIGQUIT.
	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		fmt.Printf("got signal `%v', will terminate managed processes\n", sig)
		signal.Stop(signalChan)
		procs.Close()
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()
}

type BlockServiceOpts struct {
	Exe            string
	Path           string
	OwnIp1         string
	Port1          uint16
	OwnIp2         string
	Port2          uint16
	StorageClasses []msgs.StorageClass
	FailureDomain  string
	FutureCutoff   *time.Duration
	LogLevel       lib.LogLevel
	ShuckleAddress string
	Profile        bool
	Xmon           string
}

func createDataDir(dir string) {
	if err := os.Mkdir(dir, 0777); err != nil {
		if !errors.Is(err, os.ErrExist) {
			panic(fmt.Errorf("could not create dir %s: %w", dir, err))
		}
	}
}

func (procs *ManagedProcesses) StartBlockService(ll *lib.Logger, opts *BlockServiceOpts) ManagedProcessId {
	createDataDir(opts.Path)
	args := []string{
		"-failure-domain", opts.FailureDomain,
		"-own-ip-1", opts.OwnIp1,
		"-port-1", fmt.Sprintf("%d", opts.Port1),
		"-own-ip-2", opts.OwnIp2,
		"-port-2", fmt.Sprintf("%d", opts.Port2),
		"-log-file", path.Join(opts.Path, "log"),
	}
	if opts.FutureCutoff != nil {
		args = append(args, "-future-cutoff", opts.FutureCutoff.String())
	}
	if opts.LogLevel == lib.DEBUG {
		args = append(args, "-verbose")
	}
	if opts.LogLevel == lib.TRACE {
		args = append(args, "-trace")
	}
	if opts.ShuckleAddress != "" {
		args = append(args, "-shuckle", opts.ShuckleAddress)
	}
	if opts.Profile {
		args = append(args, "-profile-file", path.Join(opts.Path, "pprof"))
	}
	if opts.Xmon != "" {
		args = append(args, "-xmon", opts.Xmon)
	}
	for i, storageClass := range opts.StorageClasses {
		args = append(args, path.Join(opts.Path, fmt.Sprintf("%d", i)), storageClass.String())
	}
	return procs.Start(ll, &ManagedProcessArgs{
		Name:            fmt.Sprintf("block service (%v:%d & %v:%d)", opts.OwnIp1, opts.Port1, opts.OwnIp2, opts.Port2),
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Path, "stdout"),
		StderrFile:      path.Join(opts.Path, "stderr"),
		TerminateOnExit: true,
	})
}

type FuseOpts struct {
	Exe                 string
	Path                string
	LogLevel            lib.LogLevel
	Wait                bool
	ShuckleAddress      string
	Profile             bool
	InitialShardTimeout time.Duration
	InitialCDCTimeout   time.Duration
}

func (procs *ManagedProcesses) StartFuse(ll *lib.Logger, opts *FuseOpts) string {
	createDataDir(opts.Path)
	mountPoint := path.Join(opts.Path, "mnt")
	createDataDir(mountPoint)
	args := []string{
		"-log-file", path.Join(opts.Path, "log"),
		"-shuckle", opts.ShuckleAddress,
	}
	var signalChan chan os.Signal
	if opts.Wait {
		args = append(args, "-signal-parent")
		signalChan = make(chan os.Signal, 1)
		signal.Notify(signalChan, syscall.SIGUSR1)
	}
	if opts.LogLevel == lib.DEBUG {
		args = append(args, "-verbose")
	}
	if opts.LogLevel == lib.TRACE {
		args = append(args, "-trace")
	}
	if opts.Profile {
		args = append(args, "-profile-file", path.Join(opts.Path, "pprof"))
	}
	if opts.InitialCDCTimeout != 0 {
		args = append(args, "-initial-cdc-timeout", opts.InitialCDCTimeout.String())
	}
	if opts.InitialShardTimeout != 0 {
		args = append(args, "-initial-shard-timeout", opts.InitialShardTimeout.String())
	}
	args = append(args, mountPoint)
	procs.Start(ll, &ManagedProcessArgs{
		Name:            "eggsfuse",
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Path, "stdout"),
		StderrFile:      path.Join(opts.Path, "stderr"),
		TerminateOnExit: true,
	})
	if opts.Wait {
		ll.Info("waiting for eggsfuse")
		<-signalChan
		signal.Stop(signalChan)
	}
	return mountPoint
}

type ShuckleOpts struct {
	Exe                  string
	Dir                  string
	LogLevel             lib.LogLevel
	BincodePort          uint16
	HttpPort             uint16
	BlockserviceMinBytes uint64
	Stale                time.Duration
	Xmon                 string
	ScriptsJs            string
	OwnIp1               string
	OwnIp2               string
}

func (procs *ManagedProcesses) StartShuckle(ll *lib.Logger, opts *ShuckleOpts) {
	createDataDir(opts.Dir)
	args := []string{
		"-bincode-port", fmt.Sprintf("%d", opts.BincodePort),
		"-http-port", fmt.Sprintf("%d", opts.HttpPort),
		"-log-file", path.Join(opts.Dir, "log"),
		"-bs-min-bytes", fmt.Sprintf("%d", opts.BlockserviceMinBytes),
		"-data-dir", opts.Dir,
		"-own-ip-1", opts.OwnIp1,
	}
	if opts.LogLevel == lib.DEBUG {
		args = append(args, "-verbose")
	}
	if opts.LogLevel == lib.TRACE {
		args = append(args, "-trace")
	}
	if opts.Stale != 0 {
		args = append(args, "-stale", opts.Stale.String())
	}
	if opts.Xmon != "" {
		args = append(args, "-xmon", opts.Xmon)
	}
	if opts.ScriptsJs != "" {
		args = append(args, "-scripts-js", opts.ScriptsJs)
	}
	if opts.OwnIp2 != "" {
		args = append(args, "-own-ip-2", opts.OwnIp2)
	}
	procs.Start(ll, &ManagedProcessArgs{
		Name:            "shuckle",
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Dir, "stdout"),
		StderrFile:      path.Join(opts.Dir, "stderr"),
		TerminateOnExit: true,
	})
}

type GoExes struct {
	ShuckleExe string
	BlocksExe  string
	FuseExe    string
}

func BuildGoExes(ll *lib.Logger, repoDir string, race bool) *GoExes {
	args := []string{"eggsshuckle", "eggsblocks", "eggsfuse"}
	if race {
		args = append(args, "--race")
	}
	buildCmd := exec.Command("./build.py", args...)
	buildCmd.Dir = goDir(repoDir)
	ll.Info("building shuckle/blocks/fuse")
	if out, err := buildCmd.CombinedOutput(); err != nil {
		fmt.Printf("build output:\n")
		os.Stdout.Write(out)
		panic(fmt.Errorf("could not build shucke/blocks/fuse: %w", err))
	}
	return &GoExes{
		ShuckleExe: path.Join(goDir(repoDir), "eggsshuckle", "eggsshuckle"),
		BlocksExe:  path.Join(goDir(repoDir), "eggsblocks", "eggsblocks"),
		FuseExe:    path.Join(goDir(repoDir), "eggsfuse", "eggsfuse"),
	}
}

type ShardOpts struct {
	Exe                       string
	Dir                       string
	LogLevel                  lib.LogLevel
	Shid                      msgs.ShardId
	Valgrind                  bool
	Perf                      bool
	IncomingPacketDrop        float64
	OutgoingPacketDrop        float64
	ShuckleAddress            string
	OwnIp1                    string
	Port1                     uint16
	OwnIp2                    string
	Port2                     uint16
	TransientDeadlineInterval *time.Duration
	Xmon                      string
}

func (procs *ManagedProcesses) StartShard(ll *lib.Logger, repoDir string, opts *ShardOpts) {
	if opts.Valgrind && opts.Perf {
		panic(fmt.Errorf("cannot do valgrind and perf together"))
	}
	createDataDir(opts.Dir)
	args := []string{
		"-log-file", path.Join(opts.Dir, "log"),
		"-incoming-packet-drop", fmt.Sprintf("%g", opts.IncomingPacketDrop),
		"-outgoing-packet-drop", fmt.Sprintf("%g", opts.OutgoingPacketDrop),
		"-shuckle", opts.ShuckleAddress,
		"-own-ip-1", opts.OwnIp1,
		"-port-1", fmt.Sprintf("%v", opts.Port1),
		"-port-2", fmt.Sprintf("%v", opts.Port2),
	}
	if opts.OwnIp2 != "" {
		args = append(args, "-own-ip-2", opts.OwnIp2)
	}
	if opts.TransientDeadlineInterval != nil {
		args = append(args, "-transient-deadline-interval", fmt.Sprintf("%dns", opts.TransientDeadlineInterval.Nanoseconds()))
	}
	if opts.Xmon != "" {
		args = append(args, "-xmon", opts.Xmon)
	}
	switch opts.LogLevel {
	case lib.TRACE:
		args = append(args, "-log-level", "trace")
	case lib.DEBUG:
		args = append(args, "-log-level", "debug")
	case lib.INFO:
		args = append(args, "-log-level", "info")
	case lib.ERROR:
		args = append(args, "-log-level", "error")
	}
	args = append(args,
		opts.Dir,
		fmt.Sprintf("%d", int(opts.Shid)),
	)
	cppDir := cppDir(repoDir)
	mpArgs := ManagedProcessArgs{
		Name:            fmt.Sprintf("shard %v", opts.Shid),
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Dir, "stdout"),
		StderrFile:      path.Join(opts.Dir, "stderr"),
		TerminateOnExit: true,
		Env:             []string{"UBSAN_OPTIONS=print_stacktrace=1"},
	}
	if opts.Valgrind {
		mpArgs.Name = fmt.Sprintf("%s (valgrind)", mpArgs.Name)
		mpArgs.Exe = "valgrind"
		mpArgs.Args = append(
			[]string{
				"--exit-on-first-error=yes",
				"-q",
				fmt.Sprintf("--suppressions=%s", path.Join(cppDir, "valgrind-suppressions")),
				"--error-exitcode=1",
				opts.Exe,
			},
			mpArgs.Args...,
		)
	}
	if opts.Perf {
		mpArgs.Name = fmt.Sprintf("%s (perf)", mpArgs.Name)
		mpArgs.Exe = "perf"
		mpArgs.Args = append(
			[]string{
				"record",
				"-g",
				fmt.Sprintf("--output=%s", path.Join(opts.Dir, "perf.data")),
				opts.Exe,
			},
			mpArgs.Args...,
		)
	}
	procs.Start(ll, &mpArgs)
}

type CDCOpts struct {
	Exe            string
	Dir            string
	LogLevel       lib.LogLevel
	Valgrind       bool
	Perf           bool
	ShuckleAddress string
	OwnIp1         string
	Port1          uint16
	OwnIp2         string
	Port2          uint16
	ShardTimeout   time.Duration
	Xmon           string
}

func (procs *ManagedProcesses) StartCDC(ll *lib.Logger, repoDir string, opts *CDCOpts) {
	if opts.Valgrind && opts.Perf {
		panic(fmt.Errorf("cannot do valgrind and perf together"))
	}
	createDataDir(opts.Dir)
	args := []string{
		"-log-file", path.Join(opts.Dir, "log"),
		"-shuckle", opts.ShuckleAddress,
		"-own-ip-1", opts.OwnIp1,
		"-port-1", fmt.Sprintf("%v", opts.Port1),
		"-port-2", fmt.Sprintf("%v", opts.Port2),
	}
	if opts.OwnIp2 != "" {
		args = append(args, "-own-ip-2", opts.OwnIp2)
	}
	if opts.ShardTimeout != 0 {
		args = append(args, "-shard-timeout-ms", fmt.Sprintf("%v", opts.ShardTimeout.Milliseconds()))
	}
	if opts.Xmon != "" {
		args = append(args, "-xmon", opts.Xmon)
	}
	switch opts.LogLevel {
	case lib.TRACE:
		args = append(args, "-log-level", "trace")
	case lib.DEBUG:
		args = append(args, "-log-level", "debug")
	case lib.INFO:
		args = append(args, "-log-level", "info")
	case lib.ERROR:
		args = append(args, "-log-level", "error")
	}
	args = append(args, opts.Dir)
	cppDir := cppDir(repoDir)
	mpArgs := ManagedProcessArgs{
		Name:            "cdc",
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Dir, "stdout"),
		StderrFile:      path.Join(opts.Dir, "stderr"),
		TerminateOnExit: true,
		Env:             []string{"UBSAN_OPTIONS=print_stacktrace=1"},
	}
	if opts.Valgrind {
		mpArgs.Name = fmt.Sprintf("%s (valgrind)", mpArgs.Name)
		mpArgs.Exe = "valgrind"
		mpArgs.Args = append(
			[]string{
				"--exit-on-first-error=yes",
				"-q",
				fmt.Sprintf("--suppressions=%s", path.Join(cppDir, "valgrind-suppressions")),
				"--error-exitcode=1",
				opts.Exe,
			},
			mpArgs.Args...,
		)
	}
	if opts.Perf {
		mpArgs.Name = fmt.Sprintf("%s (perf)", mpArgs.Name)
		mpArgs.Exe = "perf"
		mpArgs.Args = append(
			[]string{
				"record",
				fmt.Sprintf("--output=%s", path.Join(opts.Dir, "perf.data")),
				opts.Exe,
			},
			mpArgs.Args...,
		)
	}
	procs.Start(ll, &mpArgs)
}

type BuildCppOpts struct {
	Valgrind bool
	Sanitize bool
	Debug    bool
	Coverage bool
}

// Returns build dir
func buildCpp(ll *lib.Logger, repoDir string, buildType string, targets []string) string {
	cppDir := cppDir(repoDir)
	buildArgs := append([]string{buildType}, targets...)
	buildCmd := exec.Command("./build.py", buildArgs...)
	buildCmd.Dir = cppDir
	ll.Info("building cpp with `./build.py %s'", strings.Join(buildArgs, " "))
	if out, err := buildCmd.CombinedOutput(); err != nil {
		fmt.Printf("build output:\n")
		os.Stdout.Write(out)
		panic(fmt.Errorf("could not build %s: %w", targets, err))
	}
	return path.Join(cppDir, "build", buildType)
}

type CppExes struct {
	ShardExe string
	CDCExe   string
}

func BuildCppExes(ll *lib.Logger, repoDir string, buildType string) *CppExes {
	buildDir := buildCpp(ll, repoDir, buildType, []string{"shard/eggsshard", "cdc/eggscdc"})
	return &CppExes{
		ShardExe: path.Join(buildDir, "shard/eggsshard"),
		CDCExe:   path.Join(buildDir, "cdc/eggscdc"),
	}
}
