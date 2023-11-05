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
) (*bytes.Buffer, error) {
	blockService := &blockServices[block.BlockServiceIx]
	data, err := client.FetchBlock(log, blockService, block.BlockId, 0, blockSize)
	if err != nil {
		log.Info("couldn't fetch block %v in block service %v: %v", block.BlockId, blockService, err)
		return nil, err
	}
	if data.Len() != int(blockSize) {
		panic(fmt.Errorf("data.Len() %v != blockSize %v", data.Len(), int(blockSize)))
	}
	readCrc := msgs.Crc(crc32c.Sum(0, data.Bytes()))
	if block.Crc != readCrc {
		client.PutFetchedBlock(data)
		return nil, fmt.Errorf("read %v CRC instead of %v", readCrc, block.Crc)
	}
	return data, nil
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
) (msgs.BlockId, msgs.BlockServiceId, error) {
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
			return 0, 0, err
		}
		dstBlock := &initiateSpanResp.Resp.Blocks[0]
		var writeProof [8]byte
		writeProof, err = client.WriteBlock(log, dstBlock, newContents, blockSize, block.Crc)
		certifySpanResp := msgs.AddSpanCertifyResp{}
		if err != nil {
			log.Info("could not write to block service, might retry: %v", err)
			goto FailedAttempt
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
			return 0, 0, err
		}
		return dstBlock.BlockId, dstBlock.BlockServiceId, nil

	FailedAttempt:
		if attempt >= maxAttempts {
			return 0, 0, err
		}
		err = nil
		// create temp file, move the bad span there, then we can restart
		constructResp := &msgs.ConstructFileResp{}
		if err := client.ShardRequest(log, scratch.id.Shard(), &msgs.ConstructFileReq{Type: msgs.FILE, Note: "bad_write_block_attempt"}, constructResp); err != nil {
			return 0, 0, err
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
			return 0, 0, err
		}
	}
}

// the bool is whether we found an error that we can retry
func copyBlock(
	log *Logger,
	client *Client,
	scratch *scratchFile,
	file msgs.InodeId,
	blockServices []msgs.BlockService,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
) (msgs.BlockId, msgs.BlockServiceId, bool, error) {
	data, err := fetchBlock(log, client, blockServices, blockSize, block)
	if err != nil {
		return 0, 0, true, nil // might find other block services
	}
	blockId, blockServiceId, err := writeBlock(log, client, scratch, file, blacklist, blockSize, storageClass, block, data)
	client.PutFetchedBlock(data)
	return blockId, blockServiceId, false, err
}

func reconstructBlock(
	log *Logger,
	client *Client,
	bufPool *BufPool,
	fileId msgs.InodeId,
	scratchFile *scratchFile,
	blockServices []msgs.BlockService,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	parity rs.Parity,
	blocks []msgs.FetchedBlock,
	blockToMigrateIx uint8,
	blocksToMigrateIxs []uint8, // the other blocks to migrate
) (msgs.BlockId, msgs.BlockServiceId, error) {
	if err := ensureScratchFile(log, client, fileId.Shard(), scratchFile); err != nil {
		return 0, 0, err
	}
	D := parity.DataBlocks()
	haveBlocks := [][]byte{}
	haveBlocksIxs := []uint8{}
	for blockIx := range blocks {
		block := &blocks[blockIx]
		blockService := blockServices[block.BlockServiceIx]
		if !blockService.Flags.CanRead() {
			continue
		}
		isToBeMigrated := false
		for _, otherBlockIx := range blocksToMigrateIxs {
			if blockIx == int(otherBlockIx) {
				isToBeMigrated = true
				break
			}
		}
		if isToBeMigrated {
			continue
		}
		// try to fetch
		data, err := fetchBlock(log, client, blockServices, blockSize, block)
		if err != nil {
			log.Info("could not fetch block %v: %v, might try other ones", block.BlockId, err)
			continue
		}
		defer client.PutFetchedBlock(data)
		// we managed to fetch, good
		haveBlocks = append(haveBlocks, data.Bytes())
		haveBlocksIxs = append(haveBlocksIxs, uint8(blockIx))
		if len(haveBlocks) >= D {
			break
		}
	}
	if len(haveBlocks) < D {
		blocksToMigrate := make([]msgs.BlockId, len(blocksToMigrateIxs))
		for i, ix := range blocksToMigrateIxs {
			blocksToMigrate[i] = blocks[ix].BlockId
		}
		return 0, 0, fmt.Errorf("could not migrate blocks %+v, ixs %+v, in file %v, we don't have enough suitable data blocks (%v needed, have %v)", blocksToMigrate, blocksToMigrateIxs, fileId, D, len(haveBlocks))
	}
	// we got everything we need
	rs := rs.Get(parity)
	wantBytes := bufPool.Get(int(blockSize))
	defer bufPool.Put(wantBytes)
	rs.RecoverInto(haveBlocksIxs, haveBlocks, blockToMigrateIx, *wantBytes)
	blockId, blockServiceId, err := writeBlock(log, client, scratchFile, fileId, blacklist, blockSize, storageClass, &blocks[blockToMigrateIx], bytes.NewReader(*wantBytes))
	if err != nil {
		return 0, 0, err
	}
	return blockId, blockServiceId, nil
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

func printStatsLastReport(log *Logger, what string, client *Client, stats *MigrateStats, timeStats *timeStats, lastReport int64, now int64) {
	timeSinceLastReport := time.Duration(now - lastReport)
	timeSinceStart := time.Duration(now - atomic.LoadInt64(&timeStats.startedAt))
	overallMB := float64(stats.MigratedBytes) / 1e6
	overallMBs := 1000.0 * overallMB / float64(timeSinceStart.Milliseconds())
	recentMB := float64(stats.MigratedBytes-timeStats.lastReportBytes) / 1e6
	recentMBs := 1000.0 * recentMB / float64(timeSinceLastReport.Milliseconds())
	log.Info("%s %0.2fMiB in %v blocks in %v files, at %.2fMiB/s (recent), %0.2fMiB/s (overall)", what, overallMB, stats.MigratedBlocks, stats.MigratedFiles, recentMBs, overallMBs)
	timeStats.lastReportAt = now
	timeStats.lastReportBytes = stats.MigratedBytes
}

func printMigrateStats(log *Logger, what string, client *Client, stats *MigrateStats, timeStats *timeStats) {
	printStatsLastReport(log, what, client, stats, timeStats, atomic.LoadInt64(&timeStats.lastReportAt), time.Now().UnixNano())
}

// We reuse this functionality for scrubbing, they're basically doing the same
// thing.
func migrateBlocksInFileGeneric(
	log *Logger,
	client *Client,
	bufPool *BufPool,
	stats *MigrateStats,
	timeStats *timeStats,
	what string,
	badBlock func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error),
	scratchFile *scratchFile,
	fileId msgs.InodeId,
) error {
	if timeStats != nil {
		defer func() {
			lastReportAt := atomic.LoadInt64(&timeStats.lastReportAt)
			now := time.Now().UnixNano()
			if (now - lastReportAt) > time.Minute.Nanoseconds() {
				if atomic.CompareAndSwapInt64(&timeStats.lastReportAt, lastReportAt, now) {
					printStatsLastReport(log, what, client, stats, timeStats, lastReportAt, now)
				}
			}
		}()
	}
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
			blocksToMigrateIxs := []uint8{} // indices
			for blockIx := range body.Blocks {
				block := &body.Blocks[blockIx]
				blockService := &fileSpansResp.BlockServices[block.BlockServiceIx]
				isBadBlock, err := badBlock(blockService, body.CellSize*uint32(body.Stripes), block)
				if err != nil {
					return err
				}
				if isBadBlock {
					blocksToMigrateIxs = append(blocksToMigrateIxs, uint8(blockIx))
				}
			}
			if len(blocksToMigrateIxs) == 0 {
				continue
			}
			D := body.Parity.DataBlocks()
			P := body.Parity.ParityBlocks()
			B := body.Parity.Blocks()
			// we keep going until we're out of bad blocks. in the overwhelming majority
			// of cases it'll only be once.
			blacklist := make([]msgs.BlockServiceId, B)
			for blockIx, block := range body.Blocks {
				blacklist[blockIx] = fileSpansResp.BlockServices[block.BlockServiceIx].Id
			}
			for _, blockToMigrateIx := range blocksToMigrateIxs {
				blockToMigrateId := body.Blocks[blockToMigrateIx].BlockId
				log.Debug("will migrate block %v in file %v", blockToMigrateId, fileId)
				newBlock := msgs.BlockId(0)
				scratchOffset := scratchFile.size
				if P == 0 {
					return fmt.Errorf("could not migrate block %v in file %v, because there are no parity blocks", blockToMigrateId, fileId)
				} else if D == 1 {
					// For mirroring, this is pretty easy, we just get the first non-stale
					// block. Otherwise, we need to recover from the others.
					replacementFound := false
					for blockIx := range body.Blocks {
						block := &body.Blocks[blockIx]
						blockService := fileSpansResp.BlockServices[block.BlockServiceIx]
						if !blockService.Flags.CanRead() {
							log.Debug("skipping block ix %v because of its flags %v", blockIx, blockService.Flags)
							continue
						}
						goodToCopyFrom := true
						for _, otherIx := range blocksToMigrateIxs {
							if otherIx == uint8(blockIx) {
								log.Debug("skipping block ix %v because it's one of the blocks to migrate", blockIx)
								goodToCopyFrom = false
								break
							}
						}
						if !goodToCopyFrom {
							continue
						}
						if err := ensureScratchFile(log, client, fileId.Shard(), scratchFile); err != nil {
							return err
						}
						var err error
						var canRetry bool
						var newBlockServiceId msgs.BlockServiceId
						newBlock, newBlockServiceId, canRetry, err = copyBlock(log, client, scratchFile, fileId, fileSpansResp.BlockServices, blacklist, body.CellSize*uint32(body.Stripes), span.Header.StorageClass, block)
						if err != nil && !canRetry {
							return err
						}
						if err == nil {
							replacementFound = true
							blacklist = append(blacklist, newBlockServiceId)
							break
						}
					}
					if !replacementFound {
						return fmt.Errorf("could not migrate block %v in file %v, because a suitable replacement block was not found", blockToMigrateId, fileId)
					}
				} else {
					var err error
					var newBlockServiceId msgs.BlockServiceId
					newBlock, newBlockServiceId, err = reconstructBlock(
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
						blocksToMigrateIxs,
					)
					if err != nil {
						return err
					}
					blacklist = append(blacklist, newBlockServiceId)
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
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		return blockService.Id == blockServiceId, nil
	}
	return migrateBlocksInFileGeneric(log, client, NewBufPool(), stats, newTimeStats(), "migrated", badBlock, &scratchFile, fileId)
}

// Tries to migrate as many blocks as possible from that block service in a certain
// shard.
func migrateBlocksInternal(
	log *Logger,
	client *Client,
	bufPool *BufPool,
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
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		return blockService.Id == blockServiceId, nil
	}
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
			if err := migrateBlocksInFileGeneric(log, client, bufPool, stats, timeStats, "migrated", badBlock, &scratchFile, file); err != nil {
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
	bufPool := NewBufPool()
	if err := migrateBlocksInternal(log, client, bufPool, stats, timeStats, shid, blockServiceId); err != nil {
		return err
	}
	printMigrateStats(log, "migrated", client, stats, timeStats)
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
	bufPool := NewBufPool()
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
	printMigrateStats(log, "migrated", client, stats, timeStats)
	log.Info("finished migrating blocks out of %v in all shards, stats: %+v", blockServiceId, stats)
	if atomic.LoadInt32(&failed) == 1 {
		return fmt.Errorf("some shards failed to migrate, check logs")
	}
	return nil
}
