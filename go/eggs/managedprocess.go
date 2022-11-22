// Useful when we have an application which spawns some process and wants to tear down all
// of them when it goes down, and also wants to go down itself if any of the processes
// terminates abruptly.
package eggs

import (
	"bytes"
	"errors"
	"fmt"
	"os"
	"os/exec"
	"os/signal"
	"path"
	"runtime"
	"strings"
	"sync"
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
	terminateChan chan any
	processes     []managedProcess
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
	exe  string
	args []string
	name string
	dir  string
}

type ManagedProcessTerminated struct{}

func (procs *ManagedProcesses) Start(args *ManagedProcessArgs) {
	exitedChan := make(chan struct{}, 1)

	procs.processes = append(procs.processes, managedProcess{
		cmd:        exec.Command(args.exe, args.args...),
		name:       args.name,
		exitedChan: exitedChan,
	})
	proc := &procs.processes[len(procs.processes)-1]

	proc.cmd.Dir = args.dir
	var out bytes.Buffer
	proc.cmd.Stdout = &out
	proc.cmd.Stderr = &out
	proc.cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid: true, // do not propagate SIGINT, we do our own signal handling
	}
	if err := proc.cmd.Start(); err != nil {
		panic(fmt.Errorf("could not start shard: %w", err))
	}

	go func() {
		if err := proc.cmd.Wait(); err != nil {
			// We use SIGTERM to kill them, so ignore _those_
			if err.Error() != "signal: terminated" {
				fmt.Printf("%s crashed: %v\n", args.name, err)
				fmt.Printf("cmdline: %s %s\n", args.exe, strings.Join(args.args, " "))
				fmt.Printf("stderr+stdout:\n")
				os.Stdout.Write(out.Bytes())
				fmt.Println()
				procs.terminateChan <- fmt.Errorf("%s crashed: %w", args.name, err)
			}
		}
		exitedChan <- struct{}{}
	}()
}

func (procs *ManagedProcesses) Close() {
	var wait sync.WaitGroup
	wait.Add(len(procs.processes))
	for i := range procs.processes {
		proc := &procs.processes[i]
		go func() {
			proc.cmd.Process.Signal(syscall.SIGTERM)
			// wait at most 5 seconds for process to come down
			go func() {
				time.Sleep(5 * time.Second)
				fmt.Printf("process %s not terminating, killing it\n", proc.cmd)
				proc.cmd.Process.Kill() // ignoring error on purpose, there isn't much to do by now
			}()
			<-proc.exitedChan
			wait.Done()
		}()
	}
	wait.Wait()
}

func (procs *ManagedProcesses) installSignalHandlers() {
	// Cleanup if we get killed with a singal. Obviously we can't do much
	// in the case of SIGKILL or SIGQUIT.
	signalChan := make(chan os.Signal, 1)
	signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
	go func() {
		sig := <-signalChan
		signal.Stop(signalChan)
		procs.Close()
		syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
	}()
}

func (procs *ManagedProcesses) StartPythonScript(name string, script string, args []string) {
	_, filename, _, ok := runtime.Caller(0)
	if !ok {
		panic("no caller information")
	}
	pythonDir := path.Join(path.Dir(path.Dir(path.Dir(filename))), "python")
	procs.Start(
		&ManagedProcessArgs{
			name: name,
			exe:  "python",
			args: append([]string{script}, args...),
			dir:  pythonDir,
		},
	)
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
	args := []string{
		"--storage_class", opts.StorageClass,
		"--failure_domain", opts.FailureDomain,
		"--port", fmt.Sprintf("%d", opts.Port),
		"--log_file", fmt.Sprintf("%s/log", opts.Path),
		opts.Path,
	}
	if opts.NoTimeCheck {
		args = append(args, "--no_time_check")
	}
	if opts.Verbose {
		args = append(args, "--verbose")
	}
	procs.StartPythonScript(
		fmt.Sprintf("block service (port %d)", opts.Port),
		"block_service.py",
		args,
	)
}

func (procs *ManagedProcesses) StartShuckle(dir string, verbose bool) {
	createDataDir(dir)
	args := []string{
		"--port", "39999",
		"--log_file", fmt.Sprintf("%s/log", dir),
		dir,
	}
	if verbose {
		args = append(args, "--verbose")
	}
	procs.StartPythonScript("shuckle", "shuckle.py", args)
}

func (procs *ManagedProcesses) StartCDC(dir string, verbose bool) {
	createDataDir(dir)
	args := []string{
		"--log_file", fmt.Sprintf("%s/log", dir),
		dir,
	}
	if verbose {
		args = append(args, "--verbose")
	}
	procs.StartPythonScript("cdc", "cdc.py", args)
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
		"--log-file", fmt.Sprintf("%s/log", opts.Dir),
		opts.Dir,
		fmt.Sprintf("%d", int(opts.Shid)),
	}
	if opts.Verbose {
		args = append(args, "--verbose")
	}
	if opts.Valgrind {
		_, filename, _, ok := runtime.Caller(0)
		if !ok {
			panic("no caller information")
		}
		cppDir := path.Join(path.Dir(path.Dir(path.Dir(filename))), "cpp")
		procs.Start(
			&ManagedProcessArgs{
				name: fmt.Sprintf("shard %v (valgrind)", opts.Shid),
				exe:  "valgrind",
				args: append(
					[]string{
						"-q",
						fmt.Sprintf("--suppressions=%s/valgrind-suppressions", cppDir),
						"--error-exitcode=1",
						opts.Exe,
					},
					args...,
				),
			},
		)
	} else {
		procs.Start(
			&ManagedProcessArgs{
				name: fmt.Sprintf("shard %v", opts.Shid),
				exe:  opts.Exe,
				args: args,
			},
		)
	}
}
