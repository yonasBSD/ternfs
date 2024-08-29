package scratch

import (
	"fmt"
	"sync"
	"time"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type ScratchFile interface {
	Close()
	Lock() (*lockedScratchFile, error)
	// does not ensure valid
	FileId() msgs.InodeId
}

func NewScratchFile(log *lib.Logger, c *client.Client, shard msgs.ShardId, note string) ScratchFile {
	scratch := &scratchFile{
		log:   log,
		c:     c,
		shard: shard,
		note:  note,

		clearOnUnlock: false,
		clearReason:   "",
		deadline:      msgs.MakeEggsTime(time.Now().Add(time.Hour)),
		done:          make(chan struct{}),

		id: msgs.NULL_INODE_ID,
	}
	go func() {
		ticker := time.NewTicker(time.Hour)
		defer ticker.Stop()
		for {
			select {
			case <-ticker.C:
				scratch.heartBeatScratchFile()
				break
			case _, ok := <-scratch.done:
				if !ok {
					return
				}
			}
		}
	}()
	return scratch
}

func (f *lockedScratchFile) Unlock() {
	f.locked = false
	if f.clearOnUnlock {
		f.log.Debug("closing scratch file %v, reason: %s", f.id, f.clearReason)
		f.clearOnUnlock = false
		f.clearReason = ""
		f.id = msgs.NULL_INODE_ID
		f.size = 0
		f.cookie = [8]byte{}
	}
	f.mu.Unlock()
}

func (f *lockedScratchFile) FileId() msgs.InodeId {
	if !f.locked {
		panic(fmt.Errorf("accessing id on unlocked scratch file with note %s", f.note))
	}
	return f.id
}

func (f *lockedScratchFile) Cookie() [8]byte {
	if !f.locked {
		panic(fmt.Errorf("accessing cookie on unlocked scratch file with note %s", f.note))
	}
	return f.cookie
}

func (f *lockedScratchFile) Note() string {
	if !f.locked {
		panic(fmt.Errorf("accessing note on unlocked scratch file with note %s", f.note))
	}
	return f.note
}

func (f *lockedScratchFile) Shard() msgs.ShardId {
	if !f.locked {
		panic(fmt.Errorf("accessing shard on unlocked scratch file with note %s", f.note))
	}
	return f.shard
}

func (f *lockedScratchFile) Size() uint64 {
	if !f.locked {
		panic(fmt.Errorf("accessing size on unlocked scratch file with note %s", f.note))
	}
	return f.size
}

func (f *lockedScratchFile) AddSize(size uint64) {
	if !f.locked {
		panic(fmt.Errorf("accessing size on unlocked scratch file with note %s", f.note))
	}
	f.size += size
}

func (f *lockedScratchFile) ClearOnUnlock(reason string) {
	if !f.locked {
		panic(fmt.Errorf("accessing size on unlocked scratch file with note %s", f.note))
	}
	f.clearOnUnlock = true
	f.clearReason = reason
}

type lockedScratchFile struct {
	*scratchFile
	locked bool
}

func (s *scratchFile) Lock() (*lockedScratchFile, error) {
	s.mu.Lock()
	select {
	case <-s.done:
		s.mu.Unlock()
		panic("locking closed scratch file")
	default:
	}

	s.deadline = msgs.MakeEggsTime(time.Now().Add(time.Hour))
	if s.id == msgs.NULL_INODE_ID {
		resp := msgs.ConstructFileResp{}
		err := s.c.ShardRequest(
			s.log,
			s.shard,
			&msgs.ConstructFileReq{
				Type: msgs.FILE,
				Note: s.note,
			},
			&resp,
		)
		if err != nil {
			s.mu.Unlock()
			return nil, err
		}
		s.log.Debug("created scratch file %v", resp.Id)
		s.id = resp.Id
		s.cookie = resp.Cookie
		s.size = 0
	}

	return &lockedScratchFile{s, true}, nil
}

func (s *scratchFile) _lock() *lockedScratchFile {
	s.mu.Lock()
	select {
	case <-s.done:
		s.mu.Unlock()
		panic("locking closed scratch file")
	default:
	}
	return &lockedScratchFile{s, true}
}

func (f *scratchFile) Close() {
	f.mu.Lock()
	defer f.mu.Unlock()
	close(f.done)
}

func (f *scratchFile) FileId() msgs.InodeId {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.id
}

type scratchFile struct {
	log   *lib.Logger
	c     *client.Client
	shard msgs.ShardId
	note  string

	clearOnUnlock bool
	clearReason   string
	deadline      msgs.EggsTime
	done          chan struct{}

	mu     sync.Mutex
	id     msgs.InodeId
	cookie [8]byte
	size   uint64
}

func (s *scratchFile) heartBeatScratchFile() {
	lockedScratchFile := s._lock()
	defer lockedScratchFile.Unlock()
	if s.id == msgs.NULL_INODE_ID {
		return
	}
	if msgs.Now() > s.deadline {
		lockedScratchFile.ClearOnUnlock(fmt.Sprintf("scratch file %v with note (%s) not accessed for an hour, stopping heartbeat", s.id, s.note))
		return
	}
	// bump the deadline, makes sure the file stays alive for
	// the duration of this function
	s.log.Debug("bumping deadline for scratch file %v", s.id)
	req := msgs.AddInlineSpanReq{
		FileId:       s.id,
		Cookie:       s.cookie,
		StorageClass: msgs.EMPTY_STORAGE,
	}
	if err := s.c.ShardRequest(s.log, s.shard, &req, &msgs.AddInlineSpanResp{}); err != nil {
		s.log.RaiseAlert("could not bump scratch file (%v) deadline when migrating blocks: %v", s.id, err)
	}
}
