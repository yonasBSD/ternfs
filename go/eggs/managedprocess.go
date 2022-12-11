// Useful when we have an application which spawns some process and wants to tear down all
// of them when it goes down, and also wants to go down itself if any of the processes
// terminates.
package eggs

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"runtime"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/msgs"
)

type managedProcess struct {
	cmd        *exec.Cmd
	name       string
	exitedChan chan struct{}
}

type ManagedProcesses struct {
	terminateChan  chan any
	processes      []managedProcess
	closeInitiated uint32
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

func (procs *ManagedProcesses) Start(args *ManagedProcessArgs) {
	exitedChan := make(chan struct{}, 1)

	procs.processes = append(procs.processes, managedProcess{
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
				select {
				case procs.terminateChan <- fmt.Errorf("%s died", proc.name):
				default:
				}
			} else {
				fmt.Printf("%s crashed: %v\n", args.Name, err)
				fmt.Printf("cmdline: %s %s\n", args.Exe, strings.Join(args.Args, " "))
				printOut("stdout", os.Stdout, proc.cmd.Stdout)
				printOut("stderr", os.Stderr, proc.cmd.Stderr)
				fmt.Println()
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
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	pythonDir := path.Join(path.Dir(path.Dir(path.Dir(filename))), "python")
	mpArgs.Name = name
	mpArgs.Exe = "python"
	mpArgs.Args = append([]string{script}, args...)
	mpArgs.Dir = pythonDir
	procs.Start(mpArgs)
}

type BlockServiceOpts struct {
	Path          string
	Port          uint16
	StorageClass  string
	FailureDomain string
	NoTimeCheck   bool
	Verbose       bool
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
	mpArgs := ManagedProcessArgs{
		StdoutFile: path.Join(opts.Path, "stdout"),
		StderrFile: path.Join(opts.Path, "stderr"),
	}
	procs.StartPythonScript(
		fmt.Sprintf("block service (port %d)", opts.Port),
		"block_service.py",
		args,
		&mpArgs,
	)
}

func (procs *ManagedProcesses) StartShuckle(dir string, verbose bool) {
	createDataDir(dir)
	args := []string{
		"--port", "39999",
		"--log_file", path.Join(dir, "log"),
		dir,
	}
	if verbose {
		args = append(args, "--verbose")
	}
	procs.StartPythonScript("shuckle", "shuckle.py", args, &ManagedProcessArgs{})
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
	procs.StartPythonScript("cdc", "cdc.py", args, &ManagedProcessArgs{})
}

type ShardOpts struct {
	Exe      string
	Dir      string
	Verbose  bool
	Shid     msgs.ShardId
	Valgrind bool
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
	mpArgs := ManagedProcessArgs{
		Name:       fmt.Sprintf("shard %v", opts.Shid),
		Exe:        opts.Exe,
		Args:       args,
		StdoutFile: path.Join(opts.Dir, "stdout"),
		StderrFile: path.Join(opts.Dir, "stderr"),
	}
	if opts.Valgrind {
		_, filename, _, ok := runtime.Caller(0)
		if !ok {
			panic("no caller information")
		}
		cppDir := path.Join(path.Dir(path.Dir(path.Dir(filename))), "cpp")
		mpArgs.Name = fmt.Sprintf("%s (valgrind)", mpArgs.Name)
		mpArgs.Exe = "valgrind"
		mpArgs.Args = append(
			[]string{
				"-q",
				fmt.Sprintf("--suppressions=%s/valgrind-suppressions", cppDir),
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
