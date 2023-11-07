package lib

import (
	"fmt"
	"sync/atomic"
	"xtx/eggsfs/msgs"
)

type ZeroBlockServiceFilesStats struct {
	// zero block service stuff
	ZeroBlockServiceFilesRemoved uint64
}

func CollectZeroBlockServiceFiles(log *Logger, client *Client, stats *ZeroBlockServiceFilesStats, shards []msgs.ShardId) error {
	log.Info("starting to collect block services with zero files in shards %+v", shards)
	reqs := make([]msgs.RemoveZeroBlockServiceFilesReq, len(shards))
	resps := make([]msgs.RemoveZeroBlockServiceFilesResp, len(shards))
	for i := 0; ; i++ {
		allDone := true
		for j, shid := range shards {
			req := &reqs[j]
			resp := &resps[j]
			if i > 0 && req.StartBlockService == 0 && req.StartFile == msgs.NULL_INODE_ID {
				continue
			}
			allDone = false
			err := client.ShardRequest(log, shid, req, resp)
			if err != nil {
				return fmt.Errorf("could not remove zero block services: %w", err)
			}
			req.StartBlockService = resp.NextBlockService
			req.StartFile = resp.NextFile
			atomic.AddUint64(&stats.ZeroBlockServiceFilesRemoved, resp.Removed)
		}
		if allDone {
			break
		}
	}
	log.Info("finished collecting zero block service files in shards %+v: %+v", shards, stats)
	return nil
}

func CollectZeroBlockServiceFilesInAllShards(log *Logger, client *Client) error {
	var stats ZeroBlockServiceFilesStats
	shards := make([]msgs.ShardId, 256)
	for i := 0; i < 256; i++ {
		shards[i] = msgs.ShardId(i)
	}
	return CollectZeroBlockServiceFiles(log, client, &stats, shards)
}
