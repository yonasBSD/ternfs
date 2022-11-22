package eggs

import (
	"crypto/cipher"
	"fmt"
	"log"
	"net"
	"runtime/debug"
	"strings"
	"sync"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

// This is a type we use for long-running processes which do stuff
// to the filesystem -- mostly migration and gc stuff.
type DaemonEnv struct {
	Logger *log.Logger
	// we use a lock on top of the logger lock to have stack traces to be on their own
	logMutex     *sync.Mutex
	Shid         msgs.ShardId
	Role         string
	ShardSocket  *net.UDPConn
	CDCSocket    *net.UDPConn
	Timeout      time.Duration
	Verbose      bool
	Dry          bool
	CDCKey       cipher.Block
	DirInfoCache *DirInfoCache
}

type DaemonEnvArgs struct {
	logger      *log.Logger
	Shid        msgs.ShardId
	Role        string
	ShardSocket *net.UDPConn
	CDCSocket   *net.UDPConn
	Timeout     time.Duration
	Verbose     bool
	Dry         bool
	CDCKey      cipher.Block
}

func NewDaemonEnv(args *DaemonEnvArgs) *DaemonEnv {
	if args.Timeout == time.Duration(0) {
		panic("zero duration")
	}
	return &DaemonEnv{
		Logger:       args.logger,
		Shid:         args.Shid,
		Role:         args.Role,
		ShardSocket:  args.ShardSocket,
		CDCSocket:    args.CDCSocket,
		Timeout:      args.Timeout,
		Verbose:      args.Verbose,
		Dry:          args.Dry,
		CDCKey:       args.CDCKey,
		logMutex:     new(sync.Mutex),
		DirInfoCache: NewDirInfoCache(),
	}
}

func (env *DaemonEnv) RaiseAlert(err error) {
	env.logMutex.Lock()
	env.Logger.Printf("%s[%d]: ALERT: %v\n", env.Role, env.Shid, err)
	env.logMutex.Unlock()
}

func (env *DaemonEnv) Info(format string, v0 ...any) {
	v := make([]any, 2+len(v0))
	v[0] = env.Role
	v[1] = env.Shid
	copy(v[2:], v0)
	env.logMutex.Lock()
	env.Logger.Printf("%s[%d]: "+format+"\n", v...)
	env.logMutex.Unlock()
}

func (env *DaemonEnv) Debug(format string, v0 ...any) {
	if env.Verbose {
		v := make([]any, 2+len(v0))
		v[0] = env.Role
		v[1] = env.Shid
		copy(v[2:], v0)
		env.logMutex.Lock()
		env.Logger.Printf("%s[%d]: "+format+"\n", v...)
		env.logMutex.Unlock()
	}
}

func (env *DaemonEnv) Log(debug bool, format string, v ...any) {
	if debug {
		env.Debug(format, v...)
	} else {
		env.Info(format, v...)
	}
}

func (env *DaemonEnv) ShardRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return ShardRequestSocket(env, env.CDCKey, env.ShardSocket, env.Timeout, req, resp)
}

func (env *DaemonEnv) CDCRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return CDCRequestSocket(env, env.CDCSocket, env.Timeout, req, resp)
}

func (env *DaemonEnv) Loop(panicChan chan error, body func(gc *DaemonEnv)) {
	defer func() {
		if err := recover(); err != nil {
			env.RaiseAlert(fmt.Errorf("PANIC %v", err))
			env.logMutex.Lock()
			env.Logger.Printf("%s[%d]: PANIC %v. Stacktrace:", env.Role, env.Shid, err)
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				env.Logger.Printf("%s[%d]: %s", env.Role, env.Shid, line)
			}
			env.logMutex.Unlock()
			panicChan <- fmt.Errorf("%s[%v]: PANIC %v", env.Role, env.Shid, err)
		}
	}()
	for {
		body(env)
	}
}
