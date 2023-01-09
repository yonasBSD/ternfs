package main

import (
	"fmt"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

func deleteDir(log eggs.LogLevels, client eggs.Client, ownerId msgs.InodeId, name string, dirId msgs.InodeId) {
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
				deleteDir(log, client, dirId, res.Name, res.TargetId)
			} else {
				if err := client.ShardRequest(
					log, res.TargetId.Shard(), &msgs.SoftUnlinkFileReq{OwnerId: dirId, FileId: res.TargetId, Name: res.Name}, &msgs.SoftUnlinkFileResp{},
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
			log, &msgs.SoftUnlinkDirectoryReq{OwnerId: ownerId, TargetId: dirId, Name: name}, &msgs.SoftUnlinkDirectoryResp{},
		); err != nil {
			panic(err)
		}
	}
}

func cleanupAfterTest(
	blockServicesKeys map[msgs.BlockServiceId][16]byte,
) {
	log := &eggs.LogToStdout{}
	client, err := eggs.NewAllShardsClient()
	if err != nil {
		panic(err)
	}
	defer client.Close()
	// Delete all current things
	deleteDir(log, client, msgs.NULL_INODE_ID, "", msgs.ROOT_DIR_INODE_ID)
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
	if err := eggs.CollectDirectoriesInAllShards(log); err != nil {
		panic(err)
	}
	if err := eggs.DestructFilesInAllShards(log, blockServicesKeys); err != nil {
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
		// No transient files
		visitTransientFilesResp := msgs.VisitTransientFilesResp{}
		if err := client.ShardRequest(log, shid, &msgs.VisitTransientFilesReq{}, &visitTransientFilesResp); err != nil {
			panic(fmt.Errorf("%v: unexpected transient files (%v) after cleanup", shid, len(visitTransientFilesResp.Files)))
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
