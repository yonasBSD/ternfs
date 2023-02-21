package lib

import (
	"fmt"
	"sync"
	"time"
	"xtx/eggsfs/msgs"
)

type dirInfoKey struct {
	id  msgs.InodeId
	tag msgs.DirectoryInfoTag
}

type cachedDirInfoEntry struct {
	entry         msgs.IsDirectoryInfoEntry
	inheritedFrom msgs.InodeId
	cachedAt      time.Time
}

type DirInfoCache struct {
	cache map[dirInfoKey]cachedDirInfoEntry
	mutex *sync.RWMutex
}

func NewDirInfoCache() *DirInfoCache {
	return &DirInfoCache{
		cache: map[dirInfoKey]cachedDirInfoEntry{},
		mutex: new(sync.RWMutex),
	}
}

// If it couldn't be found, will return NULL_INODE_ID.
// Otherwise that's where it was inherited from.
func (env *DirInfoCache) LookupCachedDirInfoEntry(
	dirId msgs.InodeId,
	entry msgs.IsDirectoryInfoEntry,
) msgs.InodeId {
	key := dirInfoKey{id: dirId, tag: entry.Tag()}
	now := time.Now()
	env.mutex.RLock()
	cached := env.cache[key]
	env.mutex.RUnlock()

	if now.Sub(cached.cachedAt) > time.Hour {
		env.mutex.Lock()
		delete(env.cache, key)
		env.mutex.Unlock()
		return msgs.NULL_INODE_ID
	}

	switch entry.Tag() {
	case msgs.SNAPSHOT_POLICY_TAG:
		*(entry.(*msgs.SnapshotPolicy)) = *(cached.entry.(*msgs.SnapshotPolicy))
	case msgs.BLOCK_POLICY_TAG:
		*(entry.(*msgs.BlockPolicy)) = *(cached.entry.(*msgs.BlockPolicy))
	case msgs.SPAN_POLICY_TAG:
		*(entry.(*msgs.SpanPolicy)) = *(cached.entry.(*msgs.SpanPolicy))
	case msgs.STRIPE_POLICY_TAG:
		*(entry.(*msgs.StripePolicy)) = *(cached.entry.(*msgs.StripePolicy))
	default:
		panic(fmt.Errorf("bad entry tag %v", entry.Tag()))
	}

	return cached.inheritedFrom
}

func (env *DirInfoCache) UpdateCachedDirInfo(dirId msgs.InodeId, entry msgs.IsDirectoryInfoEntry, inheritedFrom msgs.InodeId) {
	env.mutex.Lock()
	env.cache[dirInfoKey{id: dirId, tag: entry.Tag()}] = cachedDirInfoEntry{
		entry:         entry,
		inheritedFrom: inheritedFrom,
		cachedAt:      time.Now(),
	}
	env.mutex.Unlock()
}
