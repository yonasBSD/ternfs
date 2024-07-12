package cleanup

import (
	"sync/atomic"
	"time"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type scratchFile struct {
	id     msgs.InodeId
	cookie [8]byte
	size   uint64
}

func ensureScratchFile(log *lib.Logger, c *client.Client, shard msgs.ShardId, file *scratchFile, note string) error {
	if msgs.InodeId(atomic.LoadUint64((*uint64)(&file.id))) != msgs.NULL_INODE_ID {
		return nil
	}
	resp := msgs.ConstructFileResp{}
	err := c.ShardRequest(
		log,
		shard,
		&msgs.ConstructFileReq{
			Type: msgs.FILE,
			Note: note,
		},
		&resp,
	)
	if err != nil {
		return err
	}
	log.Debug("created scratch file %v", resp.Id)
	file.cookie = resp.Cookie
	file.size = 0
	atomic.StoreUint64((*uint64)(&file.id), uint64(resp.Id))
	return nil
}

func (f *scratchFile) clear() {
	atomic.StoreUint64((*uint64)(&f.id), uint64(msgs.NULL_INODE_ID))
}

type keepScratchFileAlive struct {
	ticker *time.Ticker
	stopC  chan struct{}
}

func startToKeepScratchFileAlive(
	log *lib.Logger,
	c *client.Client,
	scratchFile *scratchFile,
) keepScratchFileAlive {
	ticker := time.NewTicker(10 * time.Second)
	stopC := make(chan struct{})
	go func() {
		for {
			if msgs.InodeId(atomic.LoadUint64((*uint64)(&scratchFile.id))) != msgs.NULL_INODE_ID {
				// bump the deadline, makes sure the file stays alive for
				// the duration of this function
				log.Debug("bumping deadline for scratch file %v", scratchFile.id)
				req := msgs.AddInlineSpanReq{
					FileId:       scratchFile.id,
					Cookie:       scratchFile.cookie,
					StorageClass: msgs.EMPTY_STORAGE,
				}
				if err := c.ShardRequest(log, scratchFile.id.Shard(), &req, &msgs.AddInlineSpanResp{}); err != nil {
					log.RaiseAlert("could not bump scratch file deadline when migrating blocks: %v", err)
				}
			}
			select {
			case <-ticker.C:
				break
			case _, ok := <-stopC:
				if !ok {
					return
				}
			}
		}
	}()
	return keepScratchFileAlive{ticker, stopC}
}

func (k *keepScratchFileAlive) stop() {
	k.ticker.Stop()
	close(k.stopC)
}
