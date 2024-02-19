package main

import (
	"fmt"
	"sync"
	"time"
	"xtx/eggsfs/client"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

func deleteDir(log *lib.Logger, client *client.Client, ownerId msgs.InodeId, name string, creationTime msgs.EggsTime, dirId msgs.InodeId) {
	readDirReq := msgs.ReadDirReq{
		DirId: dirId,
	}
	readDirResp := msgs.ReadDirResp{}
	for {
		if err := client.ShardRequest(log, dirId.Shard(), &readDirReq, &readDirResp); err != nil {
			panic(err)
		}
		for _, res := range readDirResp.Results {
			if res.TargetId.Type() == msgs.DIRECTORY {
				deleteDir(log, client, dirId, res.Name, res.CreationTime, res.TargetId)
			} else {
				if err := client.ShardRequest(
					log,
					dirId.Shard(),
					&msgs.SoftUnlinkFileReq{OwnerId: dirId, FileId: res.TargetId, Name: res.Name, CreationTime: res.CreationTime},
					&msgs.SoftUnlinkFileResp{},
				); err != nil {
					panic(err)
				}
			}
		}
		if readDirResp.NextHash == 0 {
			break
		}
	}
	if ownerId != msgs.NULL_INODE_ID {
		if err := client.CDCRequest(
			log, &msgs.SoftUnlinkDirectoryReq{OwnerId: ownerId, TargetId: dirId, Name: name, CreationTime: creationTime}, &msgs.SoftUnlinkDirectoryResp{},
		); err != nil {
			panic(err)
		}
	}
}

func cleanupAfterTest(
	log *lib.Logger,
	shuckleAddress string,
	counters *client.ClientCounters,
	pauseBlockServiceKiller *sync.Mutex,
) {
	c, err := client.NewClient(log, nil, shuckleAddress)
	if err != nil {
		panic(err)
	}
	c.SetCounters(counters)
	defer c.Close()
	// Delete all current things
	deleteDir(log, c, msgs.NULL_INODE_ID, "", 0, msgs.ROOT_DIR_INODE_ID)
	// Collect everything, making sure that all the deadlines will have passed
	dirInfoCache := client.NewDirInfoCache()
	{
		state := &client.CollectDirectoriesState{}
		if err := client.CollectDirectoriesInAllShards(log, c, dirInfoCache, nil, &client.CollectDirectoriesOpts{NumWorkersPerShard: 2, WorkersQueueSize: 100}, state); err != nil {
			panic(err)
		}
	}
	if err := client.CollectZeroBlockServiceFiles(log, c, &client.ZeroBlockServiceFilesStats{}); err != nil {
		panic(err)
	}
	log.Info("waiting for transient deadlines to have passed")
	time.Sleep(testTransientDeadlineInterval)
	log.Info("deadlines passed, collecting")
	{
		state := &client.DestructFilesState{}
		if err := client.DestructFilesInAllShards(log, c, &client.DestructFilesOptions{NumWorkersPerShard: 10, WorkersQueueSize: 100}, state); err != nil {
			panic(err)
		}
	}
	// Make sure nothing is left after collection
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		// No dirs
		visitDirsResp := msgs.VisitDirectoriesResp{}
		if err := c.ShardRequest(log, shid, &msgs.VisitDirectoriesReq{}, &visitDirsResp); err != nil {
			panic(err)
		}
		if len(visitDirsResp.Ids) > 0 && !(len(visitDirsResp.Ids) == 1 && visitDirsResp.Ids[0] == msgs.ROOT_DIR_INODE_ID) {
			panic(err)
		}
		// No files
		visitFilesResp := msgs.VisitFilesResp{}
		if err := c.ShardRequest(log, shid, &msgs.VisitFilesReq{}, &visitFilesResp); err != nil {
			panic(err)
		}
		if len(visitFilesResp.Ids) > 0 {
			panic(fmt.Errorf("%v: unexpected files (%v) after cleanup", shid, len(visitFilesResp.Ids)))
		}
		// No transient files. We might have some transient files
		// left due to repeated calls to construct file because
		// of packet drops. We check that at least they're empty.
		visitTransientFilesReq := msgs.VisitTransientFilesReq{}
		visitTransientFilesResp := msgs.VisitTransientFilesResp{}
		for {
			if err := c.ShardRequest(log, shid, &visitTransientFilesReq, &visitTransientFilesResp); err != nil {
				panic(err)
			}
			for _, file := range visitTransientFilesResp.Files {
				statResp := msgs.StatTransientFileResp{}
				if err := c.ShardRequest(log, shid, &msgs.StatTransientFileReq{Id: file.Id}, &statResp); err != nil {
					panic(err)
				}
				if statResp.Size > 0 {
					panic(fmt.Errorf("unexpected non-empty transient file %+v, %+v after cleanup", file, statResp))
				}
			}
			if visitTransientFilesResp.NextId == 0 {
				break
			}
			visitTransientFilesReq.BeginId = visitTransientFilesResp.NextId
		}
	}
	// Nothing in root dir
	fullReadDirResp := msgs.FullReadDirResp{}
	if err := c.ShardRequest(log, msgs.ROOT_DIR_INODE_ID.Shard(), &msgs.FullReadDirReq{DirId: msgs.ROOT_DIR_INODE_ID}, &fullReadDirResp); err != nil {
		panic(err)
	}
	if len(fullReadDirResp.Results) != 0 {
		panic(fmt.Errorf("unexpected stuff in root directory"))
	}
}
