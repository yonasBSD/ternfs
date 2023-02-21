// TODO right now we only use one scratch file for everything, which is obviously not
// great -- in general this below is more a proof of concept than anything, to test
// the right shard code paths.
//
// It's especially not great because currently all the data will go in a single block
// service. We should really have it so we change scratch file every 1TiB or whatever.
package lib

import (
	"bytes"
	"fmt"
	"io"
	"sync"
	"time"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
)

type scratchFile struct {
	id     msgs.InodeId
	cookie [8]byte
	size   uint64
}

func ensureScratchFile(log *Logger, client *Client, migratingIn msgs.InodeId, file *scratchFile) error {
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

func fetchBlock(
	log *Logger,
	client *Client,
	blockServices []msgs.BlockService,
	blockSize uint32,
	block *msgs.FetchedBlock,
	buf *bytes.Buffer,
) error {
	blockService := &blockServices[block.BlockServiceIx]
	var err error
	srcConn, err := client.GetBlockServiceConnection(log, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
	if err != nil {
		return err
	}
	defer srcConn.Close()
	if err := FetchBlock(log, srcConn, blockService, block.BlockId, block.Crc, 0, blockSize); err != nil {
		return err
	}
	readBytes, err := buf.ReadFrom(io.LimitReader(srcConn, int64(blockSize)))
	if err != nil {
		return err
	}
	if readBytes != int64(blockSize) {
		return fmt.Errorf("read %v bytes instead of %v", readBytes, blockSize)
	}
	readCrc := msgs.Crc(crc32c.Sum(0, buf.Bytes()))
	if block.Crc != readCrc {
		return fmt.Errorf("read %v CRC instead of %v", readCrc, block.Crc)
	}
	return nil
}

func writeBlock(
	log *Logger,
	client *Client,
	file *scratchFile,
	blockServices []msgs.BlockService,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
	newContents io.Reader,
) (msgs.BlockId, error) {
	blockService := &blockServices[block.BlockServiceIx]
	initiateSpanReq := msgs.AddSpanInitiateReq{
		FileId:       file.id,
		Cookie:       file.cookie,
		ByteOffset:   file.size,
		Size:         blockSize,
		Crc:          block.Crc,
		StorageClass: storageClass,
		Blacklist:    []msgs.BlockServiceBlacklist{{Id: blockService.Id}},
		Parity:       rs.MkParity(1, 0),
		Stripes:      1,
		CellSize:     blockSize,
		Crcs:         []msgs.Crc{block.Crc},
	}
	initiateSpanResp := msgs.AddSpanInitiateResp{}
	if err := client.ShardRequest(log, file.id.Shard(), &initiateSpanReq, &initiateSpanResp); err != nil {
		return 0, err
	}
	dstBlock := &initiateSpanResp.Blocks[0]
	dstConn, err := client.GetBlockServiceConnection(log, dstBlock.BlockServiceIp1, dstBlock.BlockServicePort1, dstBlock.BlockServiceIp2, dstBlock.BlockServicePort2)
	if err != nil {
		return 0, err
	}
	defer dstConn.Close()
	writeProof, err := WriteBlock(log, dstConn, dstBlock, newContents, blockSize, block.Crc)
	if err != nil {
		return 0, err
	}
	dstConn.Close()
	certifySpanResp := msgs.AddSpanCertifyResp{}
	err = client.ShardRequest(
		log,
		file.id.Shard(),
		&msgs.AddSpanCertifyReq{
			FileId:     file.id,
			Cookie:     file.cookie,
			ByteOffset: file.size,
			Proofs:     []msgs.BlockProof{{BlockId: dstBlock.BlockId, Proof: writeProof}},
		},
		&certifySpanResp,
	)
	file.size += uint64(blockSize)
	if err != nil {
		return 0, err
	}
	return dstBlock.BlockId, nil
}

func copyBlock(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	file *scratchFile,
	blockServices []msgs.BlockService,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
) (msgs.BlockId, error) {
	buf := bufPool.Get().(*bytes.Buffer)
	buf.Reset()
	defer bufPool.Put(buf)
	if err := fetchBlock(log, client, blockServices, blockSize, block, buf); err != nil {
		return 0, nil
	}
	return writeBlock(log, client, file, blockServices, blockSize, storageClass, block, buf)
}

func reconstructBlock(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	file *scratchFile,
	blockServices []msgs.BlockService,
	blockSize uint32,
	storageClass msgs.StorageClass,
	parity rs.Parity,
	blocks []msgs.FetchedBlock,
	haveBlocksIx []uint8,
	wantBlockIx uint8,
) (msgs.BlockId, error) {
	haveBlocks := make([][]byte, len(haveBlocksIx))
	for d, haveBlockIx := range haveBlocksIx {
		buf := bufPool.Get().(*bytes.Buffer)
		buf.Reset()
		defer bufPool.Put(buf)
		if err := fetchBlock(log, client, blockServices, blockSize, &blocks[haveBlockIx], buf); err != nil {
			return 0, err
		}
		haveBlocks[d] = buf.Bytes()
	}
	rs := rs.Get(parity)
	wantBuf := bufPool.Get().(*bytes.Buffer)
	defer bufPool.Put(wantBuf)
	wantBuf.Reset()
	wantBuf.Grow(int(blockSize))
	wantBytes := wantBuf.Bytes()[:blockSize]
	rs.RecoverInto(haveBlocksIx, haveBlocks, wantBlockIx, wantBytes)
	return writeBlock(log, client, file, blockServices, blockSize, storageClass, &blocks[wantBlockIx], bytes.NewReader(wantBytes))
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
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
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
		for spanIx := range fileSpansResp.Spans {
			span := &fileSpansResp.Spans[spanIx]
			if span.Header.StorageClass == msgs.INLINE_STORAGE {
				continue
			}
			body := span.Body.(*msgs.FetchedBlocksSpan)
			blockToMigrateIx := -1
			for blockIx, block := range body.Blocks {
				blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
				if blockService.Id == blockServiceId {
					blockToMigrateIx = blockIx
					break
				}
			}
			if blockToMigrateIx == -1 {
				continue
			}
			blockToMigrateId := body.Blocks[blockToMigrateIx].BlockId
			log.Debug("will migrate block %v in file %v out of block service %v", blockToMigrateId, fileId, blockServiceId)
			D := body.Parity.DataBlocks()
			P := body.Parity.ParityBlocks()
			newBlock := msgs.BlockId(0)
			scratchOffset := scratchFile.size
			if P == 0 {
				return fmt.Errorf("could not migrate block %v in file %v out of block service %v, because there are no parity blocks", blockToMigrateId, fileId, blockServiceId)
			} else if D == 1 {
				// For mirroring, this is pretty easy, we just get the first non-stale
				// block. Otherwise, we need to recover from the others.
				replacementFound := false
				for blockIx := range body.Blocks {
					block := &body.Blocks[blockIx]
					blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
					// TODO actually decide how flags work
					if blockIx != blockToMigrateIx && blockService.Id != blockServiceId && blockService.Flags == 0 {
						replacementFound = true
						if err := ensureScratchFile(log, client, fileId, scratchFile); err != nil {
							return err
						}
						var err error
						newBlock, err = copyBlock(log, client, bufPool, scratchFile, fileSpansResp.BlockServices, body.CellSize*uint32(body.Stripes), span.Header.StorageClass, block)
						if err != nil {
							return err
						}
						break
					}
				}
				if !replacementFound {
					return fmt.Errorf("could not migrate block %v in file %v out of block service %v, because a suitable replacement block was not found", blockToMigrateId, fileId, blockServiceId)
				}
			} else {
				haveBlocks := []uint8{}
				for blockIx := range body.Blocks {
					block := &body.Blocks[blockIx]
					blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
					// TODO actually decide how flags work
					if blockIx != blockToMigrateIx && blockService.Id != blockServiceId && blockService.Flags == 0 {
						if err := ensureScratchFile(log, client, fileId, scratchFile); err != nil {
							return err
						}
						haveBlocks = append(haveBlocks, uint8(blockIx))
						if len(haveBlocks) >= D {
							break
						}
					}
				}
				if len(haveBlocks) < D {
					return fmt.Errorf("could not migrate block %v in file %v out of block service %v, we don't have enough data blocks (%v needed, have %v)", blockToMigrateId, fileId, blockServiceId, D, len(haveBlocks))
				}
				var err error
				newBlock, err = reconstructBlock(
					log,
					client,
					bufPool,
					scratchFile,
					fileSpansResp.BlockServices,
					body.CellSize*uint32(body.Stripes),
					span.Header.StorageClass,
					body.Parity,
					body.Blocks,
					haveBlocks,
					uint8(blockToMigrateIx),
				)
				if err != nil {
					return err
				}
			}
			if newBlock != 0 {
				swapReq := msgs.SwapBlocksReq{
					FileId1:     fileId,
					ByteOffset1: span.Header.ByteOffset,
					BlockId1:    blockToMigrateId,
					FileId2:     scratchFile.id,
					ByteOffset2: scratchOffset,
					BlockId2:    newBlock,
				}
				if err := client.ShardRequest(log, fileId.Shard(), &swapReq, &msgs.SwapBlocksResp{}); err != nil {
					return err
				}
				stats.MigratedBlocks++
				if stats.MigratedBlocks%uint64(100) == 0 {
					log.Info("migrated %v blocks", stats.MigratedBlocks)
				}
			}
		}
		if fileSpansResp.NextOffset == 0 {
			break
		}
		fileSpansReq.ByteOffset = fileSpansResp.NextOffset
	}
	stats.MigratedFiles++
	log.Debug("finished migrating file %v, %v files migrated so far", fileId, stats.MigratedFiles)
	return nil
}

type MigrateStats struct {
	MigratedFiles  uint64
	MigratedBlocks uint64
}

func newBufPool() *sync.Pool {
	pool := sync.Pool{
		New: func() any {
			var buf bytes.Buffer
			return &buf
		},
	}
	return &pool
}

// Migrates the blocks in that block service, in that file.
//
// If the source block service it's still healthy, it'll just copy the block over, otherwise
// it'll be recovered from the other. If possible, anyway.
func MigrateBlocksInFile(
	log *Logger,
	client *Client,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
	fileId msgs.InodeId,
) error {
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, client, &scratchFile)
	defer keepAlive.stop()
	return migrateBlocksInFileInternal(log, client, newBufPool(), stats, blockServiceId, &scratchFile, fileId)
}

// Tries to migrate as many blocks as possible from that block service in a certain
// shard.
func migrateBlocksInternal(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
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
			if err := migrateBlocksInFileInternal(log, client, bufPool, stats, blockServiceId, &scratchFile, file); err != nil {
				return err
			}
		}
		filesReq.StartFrom = filesResp.FileIds[len(filesResp.FileIds)-1] + 1
	}
}

func MigrateBlocks(
	log *Logger,
	client *Client,
	stats *MigrateStats,
	shid msgs.ShardId,
	blockServiceId msgs.BlockServiceId,
) error {
	bufPool := newBufPool()
	if err := migrateBlocksInternal(log, client, bufPool, stats, shid, blockServiceId); err != nil {
		return err
	}
	log.Info("finished migrating blocks out of %v in shard %v, stats: %+v", blockServiceId, shid, stats)
	return nil
}

func MigrateBlocksInAllShards(
	log *Logger,
	client *Client,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
) error {
	bufPool := newBufPool()
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		log.Info("migrating blocks in shard %v", shid)
		if err := migrateBlocksInternal(log, client, bufPool, stats, shid, blockServiceId); err != nil {
			return err
		}
	}
	log.Info("finished migrating blocks out of %v in all shards, stats: %+v", blockServiceId, stats)
	return nil
}
