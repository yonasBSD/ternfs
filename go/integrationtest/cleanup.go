package main

import (
	"crypto/cipher"
	"fmt"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func deleteDir(log *eggs.Logger, client *eggs.Client, ownerId msgs.InodeId, name string, creationTime msgs.EggsTime, dirId msgs.InodeId) {
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
	log *eggs.Logger,
	shuckleAddress string,
	counters *eggs.ClientCounters,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
) {
	client, err := eggs.NewClient(log, shuckleAddress, nil, counters, nil)
	if err != nil {
		panic(err)
	}
	defer client.Close()
	// Delete all current things
	deleteDir(log, client, msgs.NULL_INODE_ID, "", 0, msgs.ROOT_DIR_INODE_ID)
	// Make all historical stuff die immediately for all directories
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		visitDirsReq := msgs.VisitDirectoriesReq{}
		visitDirsResp := msgs.VisitDirectoriesResp{}
		for {
			if err := client.ShardRequest(log, shid, &visitDirsReq, &visitDirsResp); err != nil {
				panic(err)
			}
			for _, dirId := range visitDirsResp.Ids {
				dirInfo, err := eggs.GetDirectoryInfo(log, client, dirId)
				if err != nil {
					panic(err)
				}
				if dirInfo == nil { // inherited
					continue
				}
				dirInfo.DeleteAfterVersions = msgs.ActiveDeleteAfterVersions(0)
				if err := eggs.SetDirectoryInfo(log, client, dirId, false, dirInfo); err != nil {
					panic(fmt.Errorf("could not set directory info for dir %v, shard %v: %v", dirId, dirId.Shard(), err))
				}
			}
			if visitDirsResp.NextId == 0 {
				break
			}
		}
	}
	// Collect everything
	if err := eggs.CollectDirectoriesInAllShards(log, shuckleAddress, counters); err != nil {
		panic(err)
	}
	var mbs eggs.MockableBlockServices
	if blockServicesKeys == nil {
		mbs = client
	} else {
		mbs = &eggs.MockedBlockServices{Keys: blockServicesKeys}
	}
	if err := eggs.DestructFilesInAllShards(log, shuckleAddress, counters, mbs); err != nil {
		panic(err)
	}
	// Make sure nothing is left
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		// No dirs
		visitDirsResp := msgs.VisitDirectoriesResp{}
		if err := client.ShardRequest(log, shid, &msgs.VisitDirectoriesReq{}, &visitDirsResp); err != nil {
			panic(err)
		}
		if len(visitDirsResp.Ids) > 0 && !(len(visitDirsResp.Ids) == 1 && visitDirsResp.Ids[0] == msgs.ROOT_DIR_INODE_ID) {
			panic(err)
		}
		// No files
		visitFilesResp := msgs.VisitFilesResp{}
		if err := client.ShardRequest(log, shid, &msgs.VisitFilesReq{}, &visitFilesResp); err != nil {
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
			if err := client.ShardRequest(log, shid, &visitTransientFilesReq, &visitTransientFilesResp); err != nil {
				panic(err)
			}
			for _, file := range visitTransientFilesResp.Files {
				statResp := msgs.StatTransientFileResp{}
				if err := client.ShardRequest(log, shid, &msgs.StatTransientFileReq{Id: file.Id}, &statResp); err != nil {
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
	if err := client.ShardRequest(log, msgs.ROOT_DIR_INODE_ID.Shard(), &msgs.FullReadDirReq{DirId: msgs.ROOT_DIR_INODE_ID}, &fullReadDirResp); err != nil {
		panic(err)
	}
	if len(fullReadDirResp.Results) != 0 {
		panic(fmt.Errorf("unexpected stuff in root directory"))
	}
}
