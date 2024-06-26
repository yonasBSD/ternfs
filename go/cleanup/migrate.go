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
package cleanup

import (
	"bytes"
	"container/heap"
	"fmt"
	"io"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/client"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"
)

type MigrateStats struct {
	MigratedFiles  uint64
	MigratedBlocks uint64
	MigratedBytes  uint64
	FilesToMigrate uint64
}

type MigrateState struct {
	Stats MigrateStats
}

func fetchBlock(
	log *lib.Logger,
	c *client.Client,
	fileId msgs.InodeId,
	blockServices []msgs.BlockService,
	blockSize uint32,
	block *msgs.FetchedBlock,
) (*bytes.Buffer, error) {
	blockService := &blockServices[block.BlockServiceIx]
	// fail immediately to other block services
	data, err := c.FetchBlock(log, &lib.NoTimeouts, blockService, block.BlockId, 0, blockSize)
	if err != nil {
		log.Info("couldn't fetch block %v in file %v in block service %v: %v", block.BlockId, fileId, blockService, err)
		return nil, err
	}
	if data.Len() != int(blockSize) {
		panic(fmt.Errorf("data.Len() %v != blockSize %v", data.Len(), int(blockSize)))
	}
	readCrc := msgs.Crc(crc32c.Sum(0, data.Bytes()))
	if block.Crc != readCrc {
		c.PutFetchedBlock(data)
		return nil, fmt.Errorf("read %v CRC instead of %v", readCrc, block.Crc)
	}
	return data, nil
}

func writeBlock(
	log *lib.Logger,
	c *client.Client,
	scratch *scratchFile,
	file msgs.InodeId,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
	newContents io.ReadSeeker,
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

	var err error
	initiateSpanResp := msgs.AddSpanInitiateWithReferenceResp{}
	if err := c.ShardRequest(log, scratch.id.Shard(), &initiateSpanReq, &initiateSpanResp); err != nil {
		return 0, 0, err
	}
	dstBlock := &initiateSpanResp.Resp.Blocks[0]
	var writeProof [8]byte
	writeProof, err = c.WriteBlock(log, nil, dstBlock, newContents, blockSize, block.Crc)
	certifySpanResp := msgs.AddSpanCertifyResp{}
	if err != nil {
		return 0, 0, err
	}
	err = c.ShardRequest(
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
}

// the bool is whether we found an error that we can retry
func copyBlock(
	log *lib.Logger,
	c *client.Client,
	scratch *scratchFile,
	file msgs.InodeId,
	blockServices []msgs.BlockService,
	blacklist []msgs.BlockServiceId,
	blockSize uint32,
	storageClass msgs.StorageClass,
	block *msgs.FetchedBlock,
) (msgs.BlockId, msgs.BlockServiceId, bool, error) {
	data, err := fetchBlock(log, c, file, blockServices, blockSize, block)
	if err != nil {
		return 0, 0, true, err // might find other block services
	}
	blockId, blockServiceId, err := writeBlock(log, c, scratch, file, blacklist, blockSize, storageClass, block, bytes.NewReader(data.Bytes()))
	c.PutFetchedBlock(data)
	return blockId, blockServiceId, false, err
}

func reconstructBlock(
	log *lib.Logger,
	c *client.Client,
	bufPool *lib.BufPool,
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
	if err := ensureScratchFile(log, c, fileId.Shard(), scratchFile, fmt.Sprintf("reconstruct %v", fileId)); err != nil {
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
		data, err := fetchBlock(log, c, fileId, blockServices, blockSize, block)
		if err != nil {
			log.Info("could not fetch block %v: %v, might try other ones", block.BlockId, err)
			continue
		}
		defer c.PutFetchedBlock(data)
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
	blockId, blockServiceId, err := writeBlock(log, c, scratchFile, fileId, blacklist, blockSize, storageClass, &blocks[blockToMigrateIx], bytes.NewReader(*wantBytes))
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

func printStatsLastReport(log *lib.Logger, what string, c *client.Client, stats *MigrateStats, timeStats *timeStats, progressReportAlert *lib.XmonNCAlert, lastReport int64, now int64) {
	timeSinceLastReport := time.Duration(now - lastReport)
	timeSinceStart := time.Duration(now - atomic.LoadInt64(&timeStats.startedAt))
	overallMB := float64(stats.MigratedBytes) / 1e6
	overallMBs := 1000.0 * overallMB / float64(timeSinceStart.Milliseconds())
	recentMB := float64(stats.MigratedBytes-timeStats.lastReportBytes) / 1e6
	recentMBs := 1000.0 * recentMB / float64(timeSinceLastReport.Milliseconds())
	log.RaiseNC(progressReportAlert, "%s %0.2fMB in %v blocks in %v files, at %.2fMB/s (recent), %0.2fMB/s (overall)", what, overallMB, stats.MigratedBlocks, stats.MigratedFiles, recentMBs, overallMBs)
	timeStats.lastReportAt = now
	timeStats.lastReportBytes = stats.MigratedBytes
}

func printMigrateStats(log *lib.Logger, what string, c *client.Client, stats *MigrateStats, timeStats *timeStats, progressReportAlert *lib.XmonNCAlert) {
	printStatsLastReport(log, what, c, stats, timeStats, progressReportAlert, atomic.LoadInt64(&timeStats.lastReportAt), time.Now().UnixNano())
}

// We reuse this functionality for scrubbing, they're basically doing the same
// thing.
func migrateBlocksInFileGeneric(
	log *lib.Logger,
	c *client.Client,
	bufPool *lib.BufPool,
	stats *MigrateStats,
	timeStats *timeStats,
	progressReportAlert *lib.XmonNCAlert,
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
					printStatsLastReport(log, what, c, stats, timeStats, progressReportAlert, lastReportAt, now)
				}
			}
		}()
	}
	// do not migrate transient files -- they might have spans not fully written yet
	{
		err := c.ShardRequest(log, fileId.Shard(), &msgs.StatFileReq{Id: fileId}, &msgs.StatFileResp{})
		if err == msgs.FILE_NOT_FOUND {
			if err := c.ShardRequest(log, fileId.Shard(), &msgs.StatTransientFileReq{Id: fileId}, &msgs.StatTransientFileResp{}); err != nil {
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
		if err := c.ShardRequest(log, fileId.Shard(), &fileSpansReq, &fileSpansResp); err != nil {
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
						if err := ensureScratchFile(log, c, fileId.Shard(), scratchFile, fmt.Sprintf("reconstruct copy %v", fileId)); err != nil {
							return err
						}
						log.Debug("trying block ix %v", blockIx)
						var err error
						var canRetry bool
						var newBlockServiceId msgs.BlockServiceId
						newBlock, newBlockServiceId, canRetry, err = copyBlock(log, c, scratchFile, fileId, fileSpansResp.BlockServices, blacklist, body.CellSize*uint32(body.Stripes), span.Header.StorageClass, block)
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
						c,
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
					if err := c.ShardRequest(log, fileId.Shard(), &swapReq, &msgs.SwapBlocksResp{}); err != nil {
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

// Migrates the blocks in that block service, in that file.
//
// If the source block service it's still healthy, it'll just copy the block over, otherwise
// it'll be recovered from the other. If possible, anyway.
func MigrateBlocksInFile(
	log *lib.Logger,
	c *client.Client,
	stats *MigrateStats,
	progressReportAlert *lib.XmonNCAlert,
	blockServiceId msgs.BlockServiceId,
	fileId msgs.InodeId,
) error {
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, c, &scratchFile)
	defer keepAlive.stop()
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		return blockService.Id == blockServiceId, nil
	}
	return migrateBlocksInFileGeneric(log, c, lib.NewBufPool(), stats, newTimeStats(), progressReportAlert, fmt.Sprintf("%v: migrated", blockServiceId), badBlock, &scratchFile, fileId)
}

// Tries to migrate as many blocks as possible from that block service in a certain
// shard.
func migrateBlocksInternal(
	log *lib.Logger,
	c *client.Client,
	bufPool *lib.BufPool,
	stats *MigrateStats,
	timeStats *timeStats,
	progressReportAlert *lib.XmonNCAlert,
	shid msgs.ShardId,
	blockServiceId msgs.BlockServiceId,
) error {
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, c, &scratchFile)
	defer keepAlive.stop()
	filesReq := msgs.BlockServiceFilesReq{BlockServiceId: blockServiceId}
	filesResp := msgs.BlockServiceFilesResp{}
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		return blockService.Id == blockServiceId, nil
	}
	blockNotFoundAlert := log.NewNCAlert(0)
	defer log.ClearNC(blockNotFoundAlert)
	for {
		if err := c.ShardRequest(log, shid, &filesReq, &filesResp); err != nil {
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
			for attempts := 1; ; attempts++ {
				if err := migrateBlocksInFileGeneric(log, c, bufPool, stats, timeStats, progressReportAlert, fmt.Sprintf("%v: migrated", blockServiceId), badBlock, &scratchFile, file); err != nil {
					if err == msgs.BLOCK_NOT_FOUND {
						log.RaiseNC(blockNotFoundAlert, "could not migrate blocks in file %v after %v attempts because a block was not found in it. this is probably due to conflicts with other migrations or scrubbing. will retry in one second.", file, attempts)
						time.Sleep(time.Second)
					} else {
						return err
					}
				} else {
					break
				}
			}
		}
		filesReq.StartFrom = filesResp.FileIds[len(filesResp.FileIds)-1] + 1
	}
}

func MigrateBlocks(
	log *lib.Logger,
	c *client.Client,
	stats *MigrateStats,
	progressReportAlert *lib.XmonNCAlert,
	shid msgs.ShardId,
	blockServiceId msgs.BlockServiceId,
) error {
	timeStats := newTimeStats()
	bufPool := lib.NewBufPool()
	if err := migrateBlocksInternal(log, c, bufPool, stats, timeStats, progressReportAlert, shid, blockServiceId); err != nil {
		return err
	}
	printMigrateStats(log, "migrated", c, stats, timeStats, progressReportAlert)
	log.Info("finished migrating blocks out of %v in shard %v, stats: %+v", blockServiceId, shid, stats)
	return nil
}

func MigrateBlocksInAllShards(
	log *lib.Logger,
	c *client.Client,
	stats *MigrateStats,
	progressReportAlert *lib.XmonNCAlert,
	blockServiceId msgs.BlockServiceId,
) error {
	timeStats := newTimeStats()
	bufPool := lib.NewBufPool()
	var wg sync.WaitGroup
	wg.Add(256)
	failed := int32(0)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			if err := migrateBlocksInternal(log, c, bufPool, stats, timeStats, progressReportAlert, shid, blockServiceId); err != nil {
				log.Info("could not migrate block service %v in shard %v: %v", blockServiceId, shid, err)
				atomic.StoreInt32(&failed, 1)
			}
			log.Info("finished migrating blocks out of shard %v", shid)
			wg.Done()
		}()
	}
	wg.Wait()
	printMigrateStats(log, "migrated", c, stats, timeStats, progressReportAlert)
	log.Info("finished migrating blocks out of %v in all shards, stats: %+v", blockServiceId, stats)
	if atomic.LoadInt32(&failed) == 1 {
		return fmt.Errorf("some shards failed to migrate, check logs")
	}
	return nil
}

type fileMigrationResult struct {
	id  msgs.InodeId
	err error
}

type migrator struct {
	shuckleAddress            string
	log                       *lib.Logger
	client                    *client.Client
	numMigrators              uint64
	migratorIdx               uint64
	numFilesPerShard          int
	stats                     MigrateStats
	blockServicesLock         *sync.RWMutex
	scheduledBlockServices    map[msgs.BlockServiceId]any
	fileFetchers              [256]chan msgs.BlockServiceId
	fileAggregatorNewFile     chan msgs.InodeId
	fileAggregatoFileFinished chan fileMigrationResult
	fileMigratorsNewFile      [256]chan msgs.InodeId
	statsC                    chan MigrateStats
	stopC                     chan bool
}

func Migrator(shuckleAddress string, log *lib.Logger, client *client.Client, numMigrators uint64, migratorIdx uint64, numFilesPerShard int) *migrator {
	res := migrator{
		shuckleAddress,
		log,
		client,
		numMigrators,
		migratorIdx,
		numFilesPerShard,
		MigrateStats{},
		&sync.RWMutex{},
		map[msgs.BlockServiceId]any{},
		[256]chan msgs.BlockServiceId{},
		make(chan msgs.InodeId, 10000),
		make(chan fileMigrationResult, 256*numFilesPerShard),
		[256]chan msgs.InodeId{},
		make(chan MigrateStats, 10),
		make(chan bool)}
	for i := 0; i < len(res.fileMigratorsNewFile); i++ {
		res.fileFetchers[i] = make(chan msgs.BlockServiceId, 500)
		res.fileMigratorsNewFile[i] = make(chan msgs.InodeId, res.numFilesPerShard)
	}
	return &res
}

func (m *migrator) Run() {
	m.log.Debug("migrator started")
	fetchersWaitGroup := sync.WaitGroup{}
	aggregatorWaitGroup := sync.WaitGroup{}
	migratorsWaitGroup := sync.WaitGroup{}
	m.runFileFetchers(&fetchersWaitGroup)
	m.runFileAggregator(&aggregatorWaitGroup)
	m.runFileMigrators(&migratorsWaitGroup)
	shuckleResponseAlert := m.log.NewNCAlert(5 * time.Minute)
	shuckleResponseAlert.SetAppType(lib.XMON_DAYTIME)
	ticker := time.NewTicker(1 * time.Minute)
	defer ticker.Stop()
OUT:
	for {
		select {
		case <-m.stopC:
			m.log.Debug("stop received")
			for _, c := range m.fileFetchers {
				close(c)
			}
			break OUT
		case <-ticker.C:
		}
		m.log.Debug("requesting block services")
		blockServicesResp, err := client.ShuckleRequest(m.log, nil, m.shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			m.log.RaiseNC(shuckleResponseAlert, "error getting block services from shuckle: %v", err)
		} else {
			m.log.ClearNC(shuckleResponseAlert)
			blockServices := blockServicesResp.(*msgs.AllBlockServicesResp)
			for _, bs := range blockServices.BlockServices {
				if bs.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) && bs.HasFiles {
					m.ScheduleBlockService(bs.Id)
				} else {
					delete(m.scheduledBlockServices, bs.Id)
				}
			}

		}
	}
	m.log.Debug("stop received waiting for fetchers to stop")
	fetchersWaitGroup.Wait()
	m.log.Debug("closing aggregator channel and waiting for aggregator")
	close(m.fileAggregatorNewFile)
	aggregatorWaitGroup.Wait()
	m.log.Debug("aggregator stopped, waiting for file migrators to stop")
	migratorsWaitGroup.Wait()
	m.log.Debug("migrator stopped")
}

func (m *migrator) ScheduleBlockService(bs msgs.BlockServiceId) {
	m.blockServicesLock.Lock()
	defer m.blockServicesLock.Unlock()
	if _, ok := m.scheduledBlockServices[bs]; !ok {
		m.log.Info("scheduling block service %v", bs)
		m.scheduledBlockServices[bs] = nil
		for _, c := range m.fileFetchers {
			c <- bs
		}
	}
}

func (m *migrator) Stop() {
	m.log.Debug("sending stop signal to migrator")
	close(m.stopC)
}

func (m *migrator) MigrationFinishedStats() <-chan MigrateStats {
	return m.statsC
}

func (m *migrator) runFileFetchers(wg *sync.WaitGroup) {
	wg.Add(len(m.fileFetchers))
	for idx, c := range m.fileFetchers {
		go func(shid msgs.ShardId, c <-chan msgs.BlockServiceId) {
			defer wg.Done()
			for {
				blockServiceId, ok := <-c
				if !ok {
					m.log.Debug("received stop signal in fileFetcher for shard %v", shid)
					break
				}
				m.log.Debug("fetching files for block service %v in shard %v", blockServiceId, shid)
				filesReq := msgs.BlockServiceFilesReq{BlockServiceId: blockServiceId, StartFrom: 0}
				filesResp := msgs.BlockServiceFilesResp{}
				shardResponseAlert := m.log.NewNCAlert(5 * time.Minute)
				filesScheduled := uint64(0)
				for {
					if err := m.client.ShardRequest(m.log, shid, &filesReq, &filesResp); err != nil {
						m.log.RaiseNC(shardResponseAlert, "error while trying to get files for block service %v: %w", blockServiceId, err)
						time.Sleep(1 * time.Minute)
						continue
					}
					m.log.ClearNC(shardResponseAlert)
					if len(filesResp.FileIds) == 0 {
						break
					}
					for _, file := range filesResp.FileIds {
						if (uint64(file)>>8)%m.numMigrators != m.migratorIdx {
							continue
						}
						filesScheduled++
						m.fileAggregatorNewFile <- file
					}
					filesReq.StartFrom = filesResp.FileIds[len(filesResp.FileIds)-1] + 1
				}
				m.log.Debug("finished fetching files for block service %v in shard %v, scheduled %d files", blockServiceId, shid, filesScheduled)
			}
		}(msgs.ShardId(idx), c)
	}
}

type fileInfo struct {
	id         msgs.InodeId
	errorCount int
}

type filePQ struct {
	pq         []fileInfo
	inodeToIdx map[msgs.InodeId]int
}

func (pq filePQ) Len() int { return len(pq.pq) }

func (pq filePQ) Less(i, j int) bool {
	if pq.pq[i].errorCount == pq.pq[j].errorCount {
		return pq.pq[i].id > pq.pq[j].id
	}
	return pq.pq[i].errorCount > pq.pq[j].errorCount
}

func (pq *filePQ) Swap(i, j int) {
	pq.pq[i], pq.pq[j] = pq.pq[j], pq.pq[i]
	pq.inodeToIdx[pq.pq[i].id] = i
	pq.inodeToIdx[pq.pq[j].id] = j
}

func (pq *filePQ) Push(x any) {
	n := len(pq.pq)
	item := x.(fileInfo)
	pq.inodeToIdx[item.id] = n
	pq.pq = append(pq.pq, item)
}

func (pq *filePQ) Pop() any {
	old := pq.pq
	n := len(old)
	item := old[n-1]
	delete(pq.inodeToIdx, item.id)
	pq.pq = old[0 : n-1]
	return item
}

func (m *migrator) runFileAggregator(wg *sync.WaitGroup) {
	wg.Add(1)
	go func() {
		defer wg.Done()
		defer m.closeMigrators()
		ticker := time.NewTicker(1 * time.Minute)
		defer ticker.Stop()
		timeStats := newTimeStats()
		totalInProgress := uint64(0)
		inProgressPerShard := [256]int{}
		queuePerShard := [256]filePQ{}
		for i := 0; i < len(queuePerShard); i++ {
			queuePerShard[i].inodeToIdx = make(map[msgs.InodeId]int)
			heap.Init(&queuePerShard[i])
		}
		pushMoreWork := func(shid msgs.ShardId) {
			for inProgressPerShard[shid] < m.numFilesPerShard && queuePerShard[shid].Len() > 0 {
				newFile := heap.Pop(&queuePerShard[shid]).(fileInfo)
				inProgressPerShard[shid]++
				totalInProgress++
				m.fileMigratorsNewFile[shid] <- newFile.id
			}
		}
		inProgressAlert := m.log.NewNCAlert(1 * time.Minute)
		inProgressAlert.SetAppType(lib.XMON_NEVER)
		for {
			select {
			case newFileId, ok := <-m.fileAggregatorNewFile:
				if !ok {
					m.log.Debug("received stop in fileAggregator")
					return
				}
				shid := newFileId.Shard()
				idx, ok := queuePerShard[shid].inodeToIdx[newFileId]
				if !ok {
					heap.Push(&queuePerShard[shid], fileInfo{newFileId, 1})
					m.stats.FilesToMigrate++
					pushMoreWork(shid)
				} else {
					queuePerShard[shid].pq[idx].errorCount++
					heap.Fix(&queuePerShard[shid], idx)
				}
			case fileResult, ok := <-m.fileAggregatoFileFinished:
				if !ok {
					return
				}
				shid := fileResult.id.Shard()
				inProgressPerShard[shid]--
				totalInProgress--
				m.stats.FilesToMigrate--
				pushMoreWork(shid)
				if fileResult.err != nil {
					m.fileAggregatorNewFile <- fileResult.id
				}
			case <-ticker.C:
				if m.stats.FilesToMigrate == 0 {
					if m.stats.MigratedFiles != 0 && len(m.statsC) < 10 {
						m.log.Debug("migration finished sending out stats")
						m.statsC <- m.stats
					} else {
						m.log.Debug("no migrations in progress")
					}
					m.log.ClearNC(inProgressAlert)
					m.stats = MigrateStats{}
					timeStats = newTimeStats()
				} else {
					printMigrateStats(m.log, fmt.Sprintf("migrating: files: %v/%v (in progress/remaining). ", totalInProgress, m.stats.FilesToMigrate), m.client, &m.stats, timeStats, inProgressAlert)
				}
			}
		}
	}()
}

func (m *migrator) closeMigrators() {
	m.log.Debug("stopping fileMigrators")
	for _, c := range m.fileMigratorsNewFile {
		close(c)
	}
}

func (m *migrator) runFileMigrators(wg *sync.WaitGroup) {
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		m.blockServicesLock.RLock()
		defer m.blockServicesLock.RUnlock()
		_, ok := m.scheduledBlockServices[blockService.Id]
		return ok, nil
	}
	bufPool := lib.NewBufPool()
	for i := 0; i < len(m.fileMigratorsNewFile); i++ {
		for j := 0; j < m.numFilesPerShard; j++ {
			wg.Add(1)
			go func(idx int, shid msgs.ShardId, c <-chan msgs.InodeId) {
				defer wg.Done()
				ticker := time.NewTicker(10 * time.Minute)
				defer ticker.Stop()
				tmpFile := scratchFile{}
				tmpFileActive := false
				var keepAlive keepScratchFileAlive
				blockNotFoundAlert := m.log.NewNCAlert(0)
				for {
					select {
					case file, ok := <-c:
						ticker.Reset(10 * time.Minute)
						if !ok {
							if tmpFileActive {
								keepAlive.stop()
							}
							m.log.Debug("recived stop signal in fileMigrator %v for shard %v", idx, shid)
							return
						}
						if !tmpFileActive {
							tmpFileActive = true
							keepAlive = startToKeepScratchFileAlive(m.log, m.client, &tmpFile)
						}
						err := error(nil)
						for {
							if err = migrateBlocksInFileGeneric(m.log, m.client, bufPool, &m.stats, nil, nil, "", badBlock, &tmpFile, file); err == nil {
								break
							}
							if err != msgs.BLOCK_NOT_FOUND {
								m.log.Info("could not migrate file in shard %v: %v", file, shid, err)
								break
							}
							m.log.RaiseNC(blockNotFoundAlert, "could not migrate blocks in file %v because a block was not found in it. this is probably due to conflicts with other migrations or scrubbing. will retry in one second.", file)
							time.Sleep(time.Second)
						}
						m.fileAggregatoFileFinished <- fileMigrationResult{file, err}
					case <-ticker.C:
						if tmpFileActive {
							tmpFileActive = false
							keepAlive.stop()
							tmpFile = scratchFile{}
						}
					}
				}
			}(j, msgs.ShardId(i), m.fileMigratorsNewFile[i])
		}
	}
}
