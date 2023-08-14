package lib

import (
	"time"
	"xtx/eggsfs/msgs"
)

type scratchFile struct {
	id     msgs.InodeId
	cookie [8]byte
	size   uint64
}

func ensureScratchFile(log *Logger, client *Client, shard msgs.ShardId, file *scratchFile) error {
	if file.id != msgs.NULL_INODE_ID {
		return nil
	}
	resp := msgs.ConstructFileResp{}
	err := client.ShardRequest(
		log,
		shard,
		&msgs.ConstructFileReq{
			Type: msgs.FILE,
			Note: "migrate",
		},
		&resp,
	)
	if err != nil {
		return err
	}
	file.id = resp.Id
	file.cookie = resp.Cookie
	file.size = 0
	return nil
}

type keepScratchFileAlive struct {
	stopHeartbeat    chan struct{}
	heartbeatStopped chan struct{}
}

func startToKeepScratchFileAlive(
	log *Logger,
	client *Client,
	scratchFile *scratchFile,
) keepScratchFileAlive {
	stopHeartbeat := make(chan struct{})
	heartbeatStopped := make(chan struct{})
	timerExpired := make(chan struct{}, 1)
	go func() {
		for {
			if scratchFile.id != msgs.NULL_INODE_ID {
				// bump the deadline, makes sure the file stays alive for
				// the duration of this function
				log.Debug("bumping deadline for scratch file %v", scratchFile.id)
				req := msgs.AddInlineSpanReq{
					FileId:       scratchFile.id,
					Cookie:       scratchFile.cookie,
					StorageClass: msgs.EMPTY_STORAGE,
				}
				if err := client.ShardRequest(log, scratchFile.id.Shard(), &req, &msgs.AddInlineSpanResp{}); err != nil {
					log.RaiseAlert("could not bump scratch file deadline when migrating blocks: %w", err)
				}
			}
			go func() {
				time.Sleep(time.Minute)
				select {
				case timerExpired <- struct{}{}:
				default:
				}
			}()
			select {
			case <-stopHeartbeat:
				heartbeatStopped <- struct{}{}
				return
			case <-timerExpired:
			}
		}
	}()
	return keepScratchFileAlive{
		stopHeartbeat:    stopHeartbeat,
		heartbeatStopped: heartbeatStopped,
	}
}

func (k *keepScratchFileAlive) stop() {
	k.stopHeartbeat <- struct{}{}
	<-k.heartbeatStopped
}
