package janitor

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
	"xtx/eggsfs/request"
)

type CachedDirInfo struct {
	info     msgs.DirectoryInfoBody
	cachedAt time.Time
}

// TODO make some of the fields private and initialize things here --
// so that we don't get out of sync sockets/shid etc.
type Env struct {
	Logger *log.Logger
	// we use a lock on top of the logger lock to have stack traces to be on ther own
	LogMutex          *sync.Mutex
	Shid              msgs.ShardId
	Role              string
	ShardSocket       *net.UDPConn
	CDCSocket         *net.UDPConn
	Timeout           time.Duration
	Verbose           bool
	Dry               bool
	CDCKey            cipher.Block
	DirInfoCache      map[msgs.InodeId]CachedDirInfo
	DirInfoCacheMutex *sync.RWMutex
}

func (env *Env) lookupCachedDirInfo(dirId msgs.InodeId) *msgs.DirectoryInfoBody {
	now := time.Now()
	env.DirInfoCacheMutex.RLock()
	cached := env.DirInfoCache[dirId]
	env.DirInfoCacheMutex.RUnlock()

	if now.Sub(cached.cachedAt) > time.Hour {
		env.DirInfoCacheMutex.Lock()
		delete(env.DirInfoCache, dirId)
		env.DirInfoCacheMutex.Unlock()
		return nil
	}

	return &cached.info
}

func (env *Env) updateCachedDirInfo(dirId msgs.InodeId, dirInfo *msgs.DirectoryInfoBody) {
	env.DirInfoCacheMutex.Lock()
	env.DirInfoCache[dirId] = CachedDirInfo{
		info:     *dirInfo,
		cachedAt: time.Now(),
	}
	env.DirInfoCacheMutex.Unlock()
}

func (env *Env) RaiseAlert(err error) {
	env.LogMutex.Lock()
	env.Logger.Printf("%s[%d]: ALERT: %v\n", env.Role, env.Shid, err)
	env.LogMutex.Unlock()
}

func (env *Env) Info(format string, v0 ...any) {
	v := make([]any, 2+len(v0))
	v[0] = env.Role
	v[1] = env.Shid
	copy(v[2:], v0)
	env.LogMutex.Lock()
	env.Logger.Printf("%s[%d]: "+format+"\n", v...)
	env.LogMutex.Unlock()
}

func (env *Env) Debug(format string, v0 ...any) {
	if env.Verbose {
		v := make([]any, 2+len(v0))
		v[0] = env.Role
		v[1] = env.Shid
		copy(v[2:], v0)
		env.LogMutex.Lock()
		env.Logger.Printf("%s[%d]: "+format+"\n", v...)
		env.LogMutex.Unlock()
	}
}

func (env *Env) Log(debug bool, format string, v ...any) {
	if debug {
		env.Debug(format, v...)
	} else {
		env.Info(format, v...)
	}
}

func (env *Env) ShardRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return request.ShardRequestSocket(env, env.CDCKey, env.ShardSocket, env.Timeout, req, resp)
}

func (env *Env) CDCRequest(req bincode.Packable, resp bincode.Unpackable) error {
	return request.CDCRequestSocket(env, env.CDCSocket, env.Timeout, req, resp)
}

func (env *Env) Loop(panicChan chan error, body func(gc *Env)) {
	defer func() {
		if err := recover(); err != nil {
			env.RaiseAlert(fmt.Errorf("PANIC %v", err))
			env.LogMutex.Lock()
			env.Logger.Printf("%s[%d]: PANIC %v. Stacktrace:", env.Role, env.Shid, err)
			for _, line := range strings.Split(string(debug.Stack()), "\n") {
				env.Logger.Printf("%s[%d]: %s", env.Role, env.Shid, line)
			}
			env.LogMutex.Unlock()
			panicChan <- fmt.Errorf("%s[%v]: PANIC %v", env.Role, env.Shid, err)
		}
	}()
	for {
		body(env)
	}
}
