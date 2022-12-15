// Useful when we have an application which spawns some process and wants to tear down all
// of them when it goes down, and also wants to go down itself if any of the processes
// terminates.
package eggs

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/msgs"
)

func pythonDir() string {
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	return path.Join(path.Dir(path.Dir(path.Dir(filename))), "python")
}

func goDir() string {
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	return path.Dir(path.Dir(filename))
}

func cppDir() string {
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	return path.Join(path.Dir(path.Dir(path.Dir(filename))), "cpp")
}

type ManagedProcess struct {
	cmd             *exec.Cmd
	name            string
	terminateOnExit bool
	exitedChan      chan struct{}
}

// Returns no error because `terminateChan` is used to propagate errors
// upwards anyway.
func (proc *ManagedProcess) Wait() {
	<-proc.exitedChan
	proc.exitedChan <- struct{}{} // continue other waiters
}

type ManagedProcesses struct {
	terminateChan  chan any
	processes      []ManagedProcess
	closeInitiated uint32
	printLock      sync.Mutex
}

// This function will take over signals in a possibly surprising way!
// See installSignalHandlers().
func NewManagedProcesses(terminateChan chan any) *ManagedProcesses {
	procs := ManagedProcesses{
		terminateChan: terminateChan,
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

func (procs *ManagedProcesses) Start(args *ManagedProcessArgs) *ManagedProcess {
	exitedChan := make(chan struct{}, 1)

	procs.processes = append(procs.processes, ManagedProcess{
		cmd:        exec.Command(args.Exe, args.Args...),
		name:       args.Name,
		exitedChan: exitedChan,
	})
	proc := &procs.processes[len(procs.processes)-1]

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

	if err := proc.cmd.Start(); err != nil {
		panic(fmt.Errorf("could not start process %s: %w", proc.name, err))
	}

	go func() {
		err := proc.cmd.Wait()

		if atomic.LoadUint32(&procs.closeInitiated) == 0 {
			if err == nil {
				if proc.terminateOnExit {
					select {
					case procs.terminateChan <- fmt.Errorf("%s died", proc.name):
					default:
					}
				}
			} else {
				procs.printLock.Lock()
				fmt.Printf("%s crashed: %v\n", args.Name, err)
				fmt.Printf("cmdline: %s %s\n", args.Exe, strings.Join(args.Args, " "))
				printOut("stdout", os.Stdout, proc.cmd.Stdout)
				printOut("stderr", os.Stderr, proc.cmd.Stderr)
				fmt.Println()
				procs.printLock.Unlock()
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

	return proc
}

func (procs *ManagedProcesses) Close() {
	atomic.StoreUint32(&procs.closeInitiated, 1)
	var wait sync.WaitGroup
	wait.Add(len(procs.processes))
	for i := range procs.processes {
		proc := &procs.processes[i]
		go func() {
			if proc.cmd.Process == nil {
				return
			}
			proc.cmd.Process.Signal(syscall.SIGTERM)
			// wait at most 5 seconds for process to come down
			go func() {
				time.Sleep(5 * time.Second)
				fmt.Printf("process %s not terminating, killing it\n", proc.name)
				proc.cmd.Process.Kill() // ignoring error on purpose, there isn't much to do by now
			}()
			<-proc.exitedChan
			proc.exitedChan <- struct{}{}
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
		fmt.Printf("got signal `%v', terminating %v managed processes\n", sig, len(procs.processes))
		signal.Stop(signalChan)
		procs.Close()
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()
}

func (procs *ManagedProcesses) StartPythonScript(
	name string, script string, args []string, mpArgs *ManagedProcessArgs,
) {
	mpArgs.Name = name
	mpArgs.Exe = "python"
	mpArgs.Args = append([]string{script}, args...)
	mpArgs.Dir = pythonDir()
	procs.Start(mpArgs)
}

type BlockServiceOpts struct {
	Path          string
	Port          uint16
	StorageClass  string
	FailureDomain string
	NoTimeCheck   bool
	Verbose       bool
	ShuckleHost   string
}

func createDataDir(dir string) {
	if err := os.Mkdir(dir, 0777); err != nil {
		if !errors.Is(err, os.ErrExist) {
			panic(fmt.Errorf("could not create dir %s: %w", dir, err))
		}
	}
}

func (procs *ManagedProcesses) StartBlockService(opts *BlockServiceOpts) {
	createDataDir(opts.Path)
	args := []string{
		"--storage_class", opts.StorageClass,
		"--failure_domain", opts.FailureDomain,
		"--port", fmt.Sprintf("%d", opts.Port),
		"--log_file", path.Join(opts.Path, "log"),
		opts.Path,
	}
	if opts.NoTimeCheck {
		args = append(args, "--no_time_check")
	}
	if opts.Verbose {
		args = append(args, "--verbose")
	}
	if opts.ShuckleHost != "" {
		args = append(args, "--shuckle_host", opts.ShuckleHost)
	}
	mpArgs := ManagedProcessArgs{
		TerminateOnExit: true,
		StdoutFile:      path.Join(opts.Path, "stdout"),
		StderrFile:      path.Join(opts.Path, "stderr"),
	}
	procs.StartPythonScript(
		fmt.Sprintf("block service (port %d)", opts.Port),
		"block_service.py",
		args,
		&mpArgs,
	)
}

func (procs *ManagedProcesses) StartCDC(dir string, verbose bool) {
	createDataDir(dir)
	args := []string{
		"--log_file", path.Join(dir, "log"),
		dir,
	}
	if verbose {
		args = append(args, "--verbose")
	}
	procs.StartPythonScript("cdc", "cdc.py", args, &ManagedProcessArgs{TerminateOnExit: true})
}

type ShuckleOpts struct {
	Exe     string
	Dir     string
	Verbose bool
	Port    uint16
}

func (procs *ManagedProcesses) StartShuckle(opts *ShuckleOpts) {
	createDataDir(opts.Dir)
	args := []string{
		"-port", fmt.Sprintf("%d", opts.Port),
		"-log-file", path.Join(opts.Dir, "log"),
	}
	if opts.Verbose {
		args = append(args, "-verbose")
	}
	procs.Start(&ManagedProcessArgs{
		Name:            "shuckle",
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Dir, "stdout"),
		StderrFile:      path.Join(opts.Dir, "stderr"),
		TerminateOnExit: true,
	})
}

func BuildShuckleExe(ll LogLevels) string {
	buildCmd := exec.Command("go", "build", ".")
	buildCmd.Dir = path.Join(goDir(), "shuckle")
	ll.Info("building shuckle")
	if out, err := buildCmd.CombinedOutput(); err != nil {
		fmt.Printf("build output:\n")
		os.Stdout.Write(out)
		panic(fmt.Errorf("could not build shard: %w", err))
	}
	return path.Join(buildCmd.Dir, "shuckle")
}

func WaitForShuckle(shuckleHost string, expectedBlockServices int, timeout time.Duration) []BlockService {
	t0 := time.Now()
	var err error
	var bss []BlockService
	for {
		t := time.Now()
		if t.Sub(t0) > timeout {
			panic(fmt.Errorf("giving up waiting for shuckle, last error: %w", err))
		}
		bss, err = GetAllBlockServices(shuckleHost)
		if err == nil && len(bss) == expectedBlockServices {
			return bss
		}
		if err == nil && len(bss) > expectedBlockServices {
			panic(fmt.Errorf("got more block services than expected (%v > %v)", len(bss), expectedBlockServices))
		}
		if err == nil {
			err = fmt.Errorf("expecting %v block services, got %v", expectedBlockServices, len(bss))
		}
		time.Sleep(10 * time.Millisecond)
	}
}

type ShardOpts struct {
	Exe            string
	Dir            string
	Verbose        bool
	Shid           msgs.ShardId
	Valgrind       bool
	WaitForShuckle bool
}

func (procs *ManagedProcesses) StartShard(opts *ShardOpts) {
	createDataDir(opts.Dir)
	args := []string{
		"--log-file", path.Join(opts.Dir, "log"),
		opts.Dir,
		fmt.Sprintf("%d", int(opts.Shid)),
	}
	if opts.Verbose {
		args = append(args, "--verbose")
	}
	if opts.WaitForShuckle {
		args = append(args, "--wait-for-shuckle")
	}
	mpArgs := ManagedProcessArgs{
		Name:            fmt.Sprintf("shard %v", opts.Shid),
		Exe:             opts.Exe,
		Args:            args,
		StdoutFile:      path.Join(opts.Dir, "stdout"),
		StderrFile:      path.Join(opts.Dir, "stderr"),
		TerminateOnExit: true,
	}
	if opts.Valgrind {
		mpArgs.Name = fmt.Sprintf("%s (valgrind)", mpArgs.Name)
		mpArgs.Exe = "valgrind"
		mpArgs.Args = append(
			[]string{
				"-q",
				fmt.Sprintf("--suppressions=%s", path.Join(cppDir(), "valgrind-suppressions")),
				"--error-exitcode=1",
				opts.Exe,
			},
			mpArgs.Args...,
		)
		procs.Start(&mpArgs)
	} else {
		procs.Start(&mpArgs)
	}
}

func WaitForShard(shid msgs.ShardId, timeout time.Duration) {
	t0 := time.Now()
	var err error
	var sock *net.UDPConn
	for {
		t := time.Now()
		if t.Sub(t0) > timeout {
			panic(fmt.Errorf("giving up waiting for shard %v, last error: %w", shid, err))
		}
		sock, err = ShardSocket(shid)
		if err != nil {
			sock.Close()
			time.Sleep(10 * time.Millisecond)
			continue
		}
		err = ShardRequestSocket(
			LogBlackHole{},
			nil,
			sock,
			time.Second,
			&msgs.StatDirectoryReq{Id: msgs.ROOT_DIR_INODE_ID},
			&msgs.StatDirectoryResp{},
		)
		sock.Close()
		if err != nil {
			time.Sleep(10 * time.Millisecond)
			continue
		}
		break
	}
}

type BuildShardOpts struct {
	Valgrind bool
	Sanitize bool
	Debug    bool
	Coverage bool
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
	if opts.Coverage {
		buildArgs = append(buildArgs, "coverage=yes")
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
	// Nicer for stuff like coverage files
	exe, err := filepath.EvalSymlinks(path.Join(cppDir, "eggs-shard"))
	if err != nil {
		panic(fmt.Errorf("could not resolve symlink: %w", err))
	}
	return exe
}
