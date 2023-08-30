// TODO right now we only use one scratch file for everything, which is obviously not
// great -- in general this below is more a proof of concept than anything, to test
// the right shard code paths.
//
// It's especially not great because currently all the data will go in a single block
// service. We should really have it so we change scratch file every 1TiB or whatever.
//
// TODO the other problem is that we don't preserve the property that files only have
// one set of blocks when swapping blocks in. We should use some "whitelist" thing
// to enforce that. Edit: I actually don't think it's true, since we migrate spans
// left to right, which means that the first span will be migrated first, and the shard
// currently uses the first span to pick up block services. So things should actually
// work out most of the times.
package lib

import (
	"bytes"
	"fmt"
	"io"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
)

func fetchBlock(
	log *Logger,
	client *Client,
	blockServices []msgs.BlockService,
	blockSize uint32,
	block *msgs.FetchedBlock,
	buf *bytes.Buffer,
) error {
	blockService := &blockServices[block.BlockServiceIx]
	var srcConn BlocksConn
	srcConn, err := client.GetReadBlocksConn(log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2)
	if err != nil {
		return err
	}
	defer srcConn.Close()
	defer func() {
		if err == nil {
			srcConn.Put()
		}
	}()
	if err = FetchBlock(log, srcConn, blockService, block.BlockId, 0, blockSize); err != nil {
		log.Info("couldn't fetch block %v in block service %v: %v", block.BlockId, blockService, err)
		return err
	}
	var readBytes int64
	readBytes, err = buf.ReadFrom(io.LimitReader(srcConn, int64(blockSize)))
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
	scratch *scratchFile,
	file msgs.InodeId,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
	newContents io.Reader,
) (msgs.BlockId, error) {
	blacklistEntries := make([]msgs.BlacklistEntry, len(blacklist))
	for i := 0; i < len(blacklistEntries); i++ {
		blacklistEntries[i].BlockService = blacklist[i]
	}

	initiateSpanReq := msgs.AddSpanInitiateWithReferenceReq{
		Req: msgs.AddSpanInitiateReq{
			FileId:       scratch.id,
			Cookie:       scratch.cookie,
			ByteOffset:   scratch.size,
			Size:         blockSize,
			Crc:          block.Crc,
			StorageClass: storageClass,
			Blacklist:    blacklistEntries,
			Parity:       rs.MkParity(1, 0),
			Stripes:      1,
			CellSize:     blockSize,
			Crcs:         []msgs.Crc{block.Crc},
		},
		Reference: file,
	}

	maxAttempts := 4 // 4 = block services we currently kill in testing
	for attempt := 0; ; attempt++ {
		var err error
		initiateSpanResp := msgs.AddSpanInitiateWithReferenceResp{}
		if err := client.ShardRequest(log, scratch.id.Shard(), &initiateSpanReq, &initiateSpanResp); err != nil {
			return 0, err
		}
		dstBlock := &initiateSpanResp.Resp.Blocks[0]
		var dstConn BlocksConn
		certifySpanResp := msgs.AddSpanCertifyResp{}
		var writeProof [8]byte
		dstConn, err = client.GetWriteBlocksConn(log, dstBlock.BlockServiceId, dstBlock.BlockServiceIp1, dstBlock.BlockServicePort1, dstBlock.BlockServiceIp2, dstBlock.BlockServicePort2)
		if err != nil {
			log.Info("could not reach block service, might retry")
			goto FailedAttempt
		}
		defer dstConn.Close()
		writeProof, err = WriteBlock(log, dstConn, dstBlock, newContents, blockSize, block.Crc)
		if err != nil {
			dstConn.Close()
			log.Info("could not write to block service, might retry")
			goto FailedAttempt
		} else {
			dstConn.Put()
		}
		err = client.ShardRequest(
			log,
			scratch.id.Shard(),
			&msgs.AddSpanCertifyReq{
				FileId:     scratch.id,
				Cookie:     scratch.cookie,
				ByteOffset: scratch.size,
				Proofs:     []msgs.BlockProof{{BlockId: dstBlock.BlockId, Proof: writeProof}},
			},
			&certifySpanResp,
		)
		scratch.size += uint64(blockSize)
		if err != nil {
			return 0, err
		}
		return dstBlock.BlockId, nil

	FailedAttempt:
		if attempt >= maxAttempts {
			return 0, err
		}
		err = nil
		// create temp file, move the bad span there, then we can restart
		constructResp := &msgs.ConstructFileResp{}
		if err := client.ShardRequest(log, scratch.id.Shard(), &msgs.ConstructFileReq{Type: msgs.FILE, Note: "bad_write_block_attempt"}, constructResp); err != nil {
			return 0, err
		}
		moveSpanReq := &msgs.MoveSpanReq{
			FileId1:     scratch.id,
			ByteOffset1: initiateSpanReq.Req.ByteOffset,
			Cookie1:     scratch.cookie,
			FileId2:     constructResp.Id,
			ByteOffset2: 0,
			Cookie2:     constructResp.Cookie,
			SpanSize:    blockSize,
		}
		if err := client.ShardRequest(log, scratch.id.Shard(), moveSpanReq, &msgs.MoveSpanResp{}); err != nil {
			return 0, err
		}

		return 0, nil
	}
}

// the bool is whether we found an error that we can retry
func copyBlock(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	scratch *scratchFile,
	file msgs.InodeId,
	blockServices []msgs.BlockService,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
) (msgs.BlockId, bool, error) {
	buf := bufPool.Get().(*bytes.Buffer)
	buf.Reset()
	defer bufPool.Put(buf)
	if err := fetchBlock(log, client, blockServices, blockSize, block, buf); err != nil {
		return 0, true, nil // might find other block services
	}
	blockId, err := writeBlock(log, client, scratch, file, blacklist, blockSize, storageClass, block, buf)
	return blockId, false, err
}

func reconstructBlock(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	fileId msgs.InodeId,
	scratchFile *scratchFile,
	blockServices []msgs.BlockService,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	parity rs.Parity,
	blocks []msgs.FetchedBlock,
	blockToMigrateIx uint8,
) (msgs.BlockId, error) {
	if err := ensureScratchFile(log, client, fileId.Shard(), scratchFile); err != nil {
		return 0, err
	}
	D := parity.DataBlocks()
	haveBlocks := [][]byte{}
	haveBlocksIxs := []uint8{}
	blockToMigrate := blocks[blockToMigrateIx]
	blockServiceId := blockServices[blockToMigrate.BlockServiceIx].Id
	for blockIx := range blocks {
		block := &blocks[blockIx]
		blockService := blockServices[block.BlockServiceIx]
		if blockIx == int(blockToMigrateIx) || blockService.Id == blockServices[blocks[blockToMigrateIx].BlockServiceIx].Id || !blockService.Flags.CanRead() {
			continue
		}
		// try to fetch
		buf := bufPool.Get().(*bytes.Buffer)
		buf.Reset()
		defer bufPool.Put(buf)
		if err := fetchBlock(log, client, blockServices, blockSize, block, buf); err != nil {
			log.Info("could not fetch block %v: %v, might try other ones", block.BlockId, err)
			continue
		}
		// we managed to fetch, good
		haveBlocks = append(haveBlocks, buf.Bytes())
		haveBlocksIxs = append(haveBlocksIxs, uint8(blockIx))
		if len(haveBlocks) >= D {
			break
		}
	}
	if len(haveBlocks) < D {
		return 0, fmt.Errorf("could not migrate block %v in file %v out of block service %v, we don't have enough data blocks (%v needed, have %v)", blockToMigrate.BlockId, fileId, blockServiceId, D, len(haveBlocks))
	}
	// we got everything we need
	rs := rs.Get(parity)
	wantBuf := bufPool.Get().(*bytes.Buffer)
	defer bufPool.Put(wantBuf)
	wantBuf.Reset()
	wantBuf.Grow(int(blockSize))
	wantBytes := wantBuf.Bytes()[:blockSize]
	rs.RecoverInto(haveBlocksIxs, haveBlocks, uint8(blockToMigrateIx), wantBytes)
	return writeBlock(log, client, scratchFile, fileId, blacklist, blockSize, storageClass, &blocks[blockToMigrateIx], bytes.NewReader(wantBytes))
}

type timeStats struct {
	startedAt       int64 // unix nanos
	lastReportAt    int64 // unix nanos
	lastReportBytes uint64
}

func newTimeStats() *timeStats {
	now := time.Now().UnixNano()
	return &timeStats{startedAt: now, lastReportAt: now}
}

func printStatsLastReport(log *Logger, client *Client, stats *MigrateStats, timeStats *timeStats, lastReport int64, now int64) {
	timeSinceLastReport := time.Duration(now - lastReport)
	timeSinceStart := time.Duration(now - atomic.LoadInt64(&timeStats.startedAt))
	overallMiB := float64(stats.MigratedBytes) / float64(uint64(1)<<20)
	overallMiBs := 1000.0 * overallMiB / float64(timeSinceStart.Milliseconds())
	recentMiB := float64(stats.MigratedBytes-timeStats.lastReportBytes) / float64(uint64(1)<<20)
	recentMiBs := 1000.0 * recentMiB / float64(timeSinceLastReport.Milliseconds())
	log.Info("migrated %0.2fMiB in %v blocks in %v files, at %.2fMiB/s (recent), %0.2fMiB/s (overall)", overallMiB, stats.MigratedBlocks, stats.MigratedFiles, recentMiBs, overallMiBs)
	timeStats.lastReportAt = now
	timeStats.lastReportBytes = stats.MigratedBytes
}

func printStats(log *Logger, client *Client, stats *MigrateStats, timeStats *timeStats) {
	printStatsLastReport(log, client, stats, timeStats, atomic.LoadInt64(&timeStats.lastReportAt), time.Now().UnixNano())
}

func migrateBlocksInFileInternal(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	stats *MigrateStats,
	timeStats *timeStats,
	blockServiceId msgs.BlockServiceId,
	scratchFile *scratchFile,
	fileId msgs.InodeId,
) error {
	defer func() {
		lastReportAt := atomic.LoadInt64(&timeStats.lastReportAt)
		now := time.Now().UnixNano()
		if (now - lastReportAt) > time.Minute.Nanoseconds() {
			if atomic.CompareAndSwapInt64(&timeStats.lastReportAt, lastReportAt, now) {
				printStatsLastReport(log, client, stats, timeStats, lastReportAt, now)
			}
		}
	}()
	// do not migrate transient files -- they might have spans not fully written yet
	{
		err := client.ShardRequest(log, fileId.Shard(), &msgs.StatFileReq{Id: fileId}, &msgs.StatFileResp{})
		if err == msgs.FILE_NOT_FOUND {
			if err := client.ShardRequest(log, fileId.Shard(), &msgs.StatTransientFileReq{Id: fileId}, &msgs.StatTransientFileResp{}); err != nil {
				return nil
			}
			log.Debug("skipping transient file %v", fileId)
			return nil
		}
		if err != nil {
			return err
		}
	}
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
			B := body.Parity.Blocks()
			blacklist := make([]msgs.BlockServiceId, B)
			for blockIx, block := range body.Blocks {
				blacklist[blockIx] = fileSpansResp.BlockServices[block.BlockServiceIx].Id
			}
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
					if blockIx != blockToMigrateIx && blockService.Id != blockServiceId && blockService.Flags.CanRead() {
						if err := ensureScratchFile(log, client, fileId.Shard(), scratchFile); err != nil {
							return err
						}
						var err error
						var canRetry bool
						newBlock, canRetry, err = copyBlock(log, client, bufPool, scratchFile, fileId, fileSpansResp.BlockServices, blacklist, body.CellSize*uint32(body.Stripes), span.Header.StorageClass, block)
						if err != nil && !canRetry {
							return err
						}
						if err == nil {
							replacementFound = true
							break
						}
					}
				}
				if !replacementFound {
					return fmt.Errorf("could not migrate block %v in file %v out of block service %v, because a suitable replacement block was not found", blockToMigrateId, fileId, blockServiceId)
				}
			} else {
				var err error
				newBlock, err = reconstructBlock(
					log,
					client,
					bufPool,
					fileId,
					scratchFile,
					fileSpansResp.BlockServices,
					blacklist,
					body.CellSize*uint32(body.Stripes),
					span.Header.StorageClass,
					body.Parity,
					body.Blocks,
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
				atomic.AddUint64(&stats.MigratedBlocks, 1)
				atomic.AddUint64(&stats.MigratedBytes, uint64(D*int(body.CellSize)))
			}
		}
		if fileSpansResp.NextOffset == 0 {
			break
		}
		fileSpansReq.ByteOffset = fileSpansResp.NextOffset
	}
	atomic.AddUint64(&stats.MigratedFiles, 1)
	log.Debug("finished migrating file %v, %v files migrated so far", fileId, stats.MigratedFiles)
	return nil
}

type MigrateStats struct {
	MigratedFiles  uint64
	MigratedBlocks uint64
	MigratedBytes  uint64
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
	return migrateBlocksInFileInternal(log, client, newBufPool(), stats, newTimeStats(), blockServiceId, &scratchFile, fileId)
}

// Tries to migrate as many blocks as possible from that block service in a certain
// shard.
func migrateBlocksInternal(
	log *Logger,
	client *Client,
	bufPool *sync.Pool,
	stats *MigrateStats,
	timeStats *timeStats,
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
			if err := migrateBlocksInFileInternal(log, client, bufPool, stats, timeStats, blockServiceId, &scratchFile, file); err != nil {
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
	timeStats := newTimeStats()
	bufPool := newBufPool()
	if err := migrateBlocksInternal(log, client, bufPool, stats, timeStats, shid, blockServiceId); err != nil {
		return err
	}
	printStats(log, client, stats, timeStats)
	log.Info("finished migrating blocks out of %v in shard %v, stats: %+v", blockServiceId, shid, stats)
	return nil
}

func MigrateBlocksInAllShards(
	log *Logger,
	client *Client,
	stats *MigrateStats,
	blockServiceId msgs.BlockServiceId,
) error {
	timeStats := newTimeStats()
	bufPool := newBufPool()
	var wg sync.WaitGroup
	wg.Add(256)
	failed := int32(0)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			if err := migrateBlocksInternal(log, client, bufPool, stats, timeStats, shid, blockServiceId); err != nil {
				log.Info("could not migrate block service %v in shard %v: %v", blockServiceId, shid, err)
				atomic.StoreInt32(&failed, 1)
			}
			log.Info("finished migrating blocks out of shard %v", shid)
			wg.Done()
		}()
	}
	wg.Wait()
	printStats(log, client, stats, timeStats)
	log.Info("finished migrating blocks out of %v in all shards, stats: %+v", blockServiceId, stats)
	if atomic.LoadInt32(&failed) == 1 {
		return fmt.Errorf("some shards failed to migrate, check logs")
	}
	return nil
}
