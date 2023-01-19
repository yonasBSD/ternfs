// TODO right now we only use one scratch file for everything, which is obviously not
// great -- in general this below is more a proof of concept than anything, to test
// the right shard code paths.
package eggs

import (
	"crypto/cipher"
	"fmt"
	"time"
	"xtx/eggsfs/msgs"
)

type scratchFile struct {
	id     msgs.InodeId
	cookie [8]byte
	size   uint64
}

func ensureScratchFile(log LogLevels, client *Client, migratingIn msgs.InodeId, file *scratchFile) error {
	if file.id != msgs.NULL_INODE_ID {
		return nil
	}
	resp := msgs.ConstructFileResp{}
	err := client.ShardRequest(
		log,
		migratingIn.Shard(),
		&msgs.ConstructFileReq{
			Type: msgs.FILE,
			Note: fmt.Sprintf("migrate (file %v)", migratingIn),
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

func copyBlock(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	file *scratchFile,
	blockServices []msgs.BlockService,
	blockSize uint64,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
) (msgs.BlockId, error) {
	blockService := blockServices[block.BlockServiceIx]
	initiateSpanReq := msgs.AddSpanInitiateReq{
		FileId:       file.id,
		Cookie:       file.cookie,
		ByteOffset:   file.size,
		StorageClass: storageClass,
		Blacklist:    []msgs.BlockServiceBlacklist{{Id: blockService.Id}},
		Parity:       msgs.MkParity(1, 0),
		Crc32:        block.Crc32,
		Size:         blockSize,
		BlockSize:    blockSize,
		BodyBlocks:   []msgs.NewBlockInfo{{Crc32: block.Crc32}},
	}
	initiateSpanResp := msgs.AddSpanInitiateResp{}
	if err := client.ShardRequest(log, file.id.Shard(), &initiateSpanReq, &initiateSpanResp); err != nil {
		return 0, err
	}
	dstBlock := &initiateSpanResp.Blocks[0]
	var proof [8]byte
	var err error
	if blockServicesKeys == nil {
		proof, err = CopyBlock(log, blockServices, blockSize, block, dstBlock)
		if err != nil {
			return 0, err
		}
	} else {
		key, wasPresent := blockServicesKeys[dstBlock.BlockServiceId]
		if !wasPresent {
			panic(fmt.Errorf("could not find key for block service %v", dstBlock.BlockServiceId))
		}
		proof = BlockWriteProof(dstBlock.BlockServiceId, dstBlock.BlockId, key)
	}
	certifySpanResp := msgs.AddSpanCertifyResp{}
	err = client.ShardRequest(
		log,
		file.id.Shard(),
		&msgs.AddSpanCertifyReq{
			FileId:     file.id,
			Cookie:     file.cookie,
			ByteOffset: file.size,
			Proofs:     []msgs.BlockProof{{BlockId: dstBlock.BlockId, Proof: proof}},
		},
		&certifySpanResp,
	)
	file.size += blockSize
	if err != nil {
		return 0, err
	}
	return dstBlock.BlockId, nil
}

type keepScratchFileAlive struct {
	stopHeartbeat    chan struct{}
	heartbeatStopped chan struct{}
}

func startToKeepScratchFileAlive(
	log LogLevels,
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
				req := msgs.AddSpanInitiateReq{
					FileId:       scratchFile.id,
					Cookie:       scratchFile.cookie,
					StorageClass: msgs.EMPTY_STORAGE,
				}
				if err := client.ShardRequest(log, scratchFile.id.Shard(), &req, &msgs.AddSpanInitiateResp{}); err != nil {
					log.RaiseAlert(fmt.Errorf("could not bump scratch file deadline when migrating blocks: %w", err))
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
				// allow GC to immediately collect this, this is mostly useful for
				// integration tests right now, since they check that everything
				// has been cleaned up.
				if scratchFile.id != msgs.NULL_INODE_ID {
					log.Info("expiring scratch file %v", scratchFile.id)
					req := msgs.ExpireTransientFileReq{Id: scratchFile.id}
					if err := client.ShardRequest(log, scratchFile.id.Shard(), &req, &msgs.ExpireTransientFileResp{}); err != nil {
						log.RaiseAlert(fmt.Errorf("could not expire transient file %v: %w", scratchFile.id, err))
					}
				}
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

func migrateBlocksInFileInternal(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
	scratchFile *scratchFile,
	fileId msgs.InodeId,
) error {
	fileSpansReq := msgs.FileSpansReq{
		FileId:     fileId,
		ByteOffset: 0,
	}
	fileSpansResp := msgs.FileSpansResp{}
	for {
		if err := client.ShardRequest(log, fileId.Shard(), &fileSpansReq, &fileSpansResp); err != nil {
			return err
		}
		for _, span := range fileSpansResp.Spans {
			if span.Parity.DataBlocks() != 1 {
				panic(fmt.Errorf("non-mirroring not supported for now"))
			}
			var blockToMigrate msgs.BlockId
			for _, block := range span.BodyBlocks {
				blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
				if blockService.Id == blockServiceId {
					blockToMigrate = block.BlockId
					break
				}
			}
			if blockToMigrate == 0 {
				continue
			}
			log.Debug("will migrate block %v in file %v out of block service %v", blockToMigrate, fileId, blockServiceId)
			// Right now we only support mirroring, so this is pretty easy --
			// we just get the first non-stale block.
			replacementFound := false
			for _, block := range span.BodyBlocks {
				blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
				// TODO actually decide how flags work
				if blockService.Flags == 0 {
					replacementFound = true
					if err := ensureScratchFile(log, client, fileId, scratchFile); err != nil {
						return err
					}
					blockOffset := scratchFile.size
					newBlock, err := copyBlock(log, client, blockServicesKeys, scratchFile, fileSpansResp.BlockServices, span.BlockSize, span.StorageClass, &block)
					if err != nil {
						return err
					}
					swapReq := msgs.SwapBlocksReq{
						FileId1:     fileId,
						ByteOffset1: span.ByteOffset,
						BlockId1:    blockToMigrate,
						FileId2:     scratchFile.id,
						ByteOffset2: blockOffset,
						BlockId2:    newBlock,
					}
					if err := client.ShardRequest(log, fileId.Shard(), &swapReq, &msgs.SwapBlocksResp{}); err != nil {
						return err
					}
					stats.MigratedBlocks++
					break
				}
			}
			if !replacementFound {
				return fmt.Errorf("could not migrate block %v in file %v out of block service %v, because a suitable replacement block was not found", blockToMigrate, fileId, blockServiceId)
			}
		}
		if fileSpansResp.NextOffset == 0 {
			break
		}
	}
	stats.MigratedFiles++
	log.Debug("finished migrating file %v, %v files migrated so far", fileId, stats.MigratedFiles)
	return nil
}

type MigrateStats struct {
	MigratedFiles  uint64
	MigratedBlocks uint64
}

// Migrates the blocks in that block service, in that file.
//
// If the source block service it's still healthy, it'll just copy the block over, otherwise
// it'll be recovered from the other. If possible, anyway.
func MigrateBlocksInFile(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
	fileId msgs.InodeId,
) error {
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, client, &scratchFile)
	defer keepAlive.stop()
	return migrateBlocksInFileInternal(log, client, blockServicesKeys, stats, blockServiceId, &scratchFile, fileId)
}

// Tries to migrate as many blocks as possible from that block service in a certain
// shard.
func migrateBlocksInternal(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	stats *MigrateStats,
	shid msgs.ShardId,
	blockServiceId msgs.BlockServiceId,
) error {
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, client, &scratchFile)
	defer keepAlive.stop()
	filesReq := msgs.BlockServiceFilesReq{BlockServiceId: blockServiceId}
	filesResp := msgs.BlockServiceFilesResp{}
	for {
		if err := client.ShardRequest(log, shid, &filesReq, &filesResp); err != nil {
			return fmt.Errorf("error while trying to get files for block service %v: %w", blockServiceId, err)
		}
		if len(filesResp.FileIds) == 0 {
			log.Debug("could not find any file for block service %v, terminating", blockServiceId)
			return nil
		}
		log.Debug("will migrate %d files", len(filesResp.FileIds))
		for _, file := range filesResp.FileIds {
			if file == scratchFile.id {
				continue
			}
			if err := migrateBlocksInFileInternal(log, client, blockServicesKeys, stats, blockServiceId, &scratchFile, file); err != nil {
				return err
			}
		}
		filesReq.StartFrom = filesResp.FileIds[len(filesResp.FileIds)-1] + 1
	}
}

func MigrateBlocks(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	stats *MigrateStats,
	shid msgs.ShardId,
	blockServiceId msgs.BlockServiceId,
) error {
	if err := migrateBlocksInternal(log, client, blockServicesKeys, stats, shid, blockServiceId); err != nil {
		return err
	}
	log.Info("finished migrating blocks out of %v in shard %v, stats: %+v", blockServiceId, shid, stats)
	return nil
}

func MigrateBlocksInAllShards(
	log LogLevels,
	client *Client,
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
) error {
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		log.Info("migrating blocks in shard %v", shid)
		if err := migrateBlocksInternal(log, client, blockServicesKeys, stats, shid, blockServiceId); err != nil {
			return err
		}
	}
	log.Info("finished migrating blocks out of %v in all shards, stats: %+v", blockServiceId, stats)
	return nil
}
