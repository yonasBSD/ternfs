package eggs

import (
	"sync"
	"time"
	"xtx/eggsfs/msgs"
)

type cachedDirInfo struct {
	info     msgs.DirectoryInfoBody
	cachedAt time.Time
}

type DirInfoCache struct {
	cache map[msgs.InodeId]cachedDirInfo
	mutex *sync.RWMutex
}

func NewDirInfoCache() *DirInfoCache {
	return &DirInfoCache{
		cache: map[msgs.InodeId]cachedDirInfo{},
		mutex: new(sync.RWMutex),
	}
}

func (env *DirInfoCache) LookupCachedDirInfo(dirId msgs.InodeId) *msgs.DirectoryInfoBody {
	now := time.Now()
	env.mutex.RLock()
	cached := env.cache[dirId]
	env.mutex.RUnlock()

	if now.Sub(cached.cachedAt) > time.Hour {
		env.mutex.Lock()
		delete(env.cache, dirId)
		env.mutex.Unlock()
		return nil
	}

	return &cached.info
}

func (env *DirInfoCache) UpdateCachedDirInfo(dirId msgs.InodeId, dirInfo *msgs.DirectoryInfoBody) {
	env.mutex.Lock()
	env.cache[dirId] = cachedDirInfo{
		info:     *dirInfo,
		cachedAt: time.Now(),
	}
	env.mutex.Unlock()
}
