// Goes through all the _current_ files, and checks that the span block allocation
// is coherent with the span policy. Very similar to migration, hence it shares
// some datatypes.
package cleanup

import (
	"fmt"
	"path"
	"sync/atomic"
	"time"
	"xtx/eggsfs/client"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

type DefragStats struct {
	AnalyzedFiles                uint64
	AnalyzedLogicalSize          uint64
	AnalyzedBlocks               uint64
	DefraggedSpans               uint64
	DefraggedLogicalBytes        uint64
	DefraggedBlocksBefore        uint64
	DefraggedPhysicalBytesBefore uint64
	DefraggedBlocksAfter         uint64
	DefraggedPhysicalBytesAfter  uint64
}

func defragPrintStatsLastReport(log *lib.Logger, c *client.Client, stats *DefragStats, timeStats *timeStats, progressReportAlert *lib.XmonNCAlert, lastReport int64, now int64) {
	timeSinceStart := time.Duration(now - atomic.LoadInt64(&timeStats.startedAt))
	physicalDeltaMB := (float64(stats.DefraggedPhysicalBytesAfter) - float64(stats.DefraggedPhysicalBytesBefore)) / 1e6
	physicalDeltaMBs := 1000.0 * physicalDeltaMB / float64(timeSinceStart.Milliseconds())
	log.RaiseNC(
		progressReportAlert,
		"looked at %v files %v blocks, logical size %0.2fTB, defragged %v spans, logical size %0.2fTB; %v blocks %0.2fTB -> %v blocks (%+d) %0.2fTB (%+0.2fTB), %+0.2fMB/s",
		stats.AnalyzedFiles, stats.AnalyzedBlocks, float64(stats.AnalyzedLogicalSize)/1e12,
		stats.DefraggedSpans, float64(stats.DefraggedLogicalBytes)/1e12,
		stats.DefraggedBlocksBefore, float64(stats.DefraggedPhysicalBytesBefore)/1e12,
		stats.DefraggedBlocksAfter, int64(stats.DefraggedBlocksAfter)-int64(stats.DefraggedBlocksBefore),
		float64(stats.DefraggedPhysicalBytesAfter)/1e12, physicalDeltaMB/1e6, physicalDeltaMBs,
	)
	timeStats.lastReportAt = now
}

func defragFileInternal(
	log *lib.Logger,
	c *client.Client,
	bufPool *lib.BufPool,
	dirInfoCache *client.DirInfoCache,
	stats *DefragStats,
	progressReportAlert *lib.XmonNCAlert,
	timeStats *timeStats,
	parent msgs.InodeId,
	fileId msgs.InodeId,
	filePath string,
) error {
	defer func() {
		lastReportAt := atomic.LoadInt64(&timeStats.lastReportAt)
		now := time.Now().UnixNano()
		if (now - lastReportAt) > time.Minute.Nanoseconds() {
			if atomic.CompareAndSwapInt64(&timeStats.lastReportAt, lastReportAt, now) {
				defragPrintStatsLastReport(log, c, stats, timeStats, progressReportAlert, lastReportAt, now)
			}
		}
	}()
	atomic.AddUint64(&stats.AnalyzedFiles, 1)
	var spanPolicy msgs.SpanPolicy
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, parent, &spanPolicy); err != nil {
		return err
	}
	var blockPolicy msgs.BlockPolicy
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, parent, &blockPolicy); err != nil {
		return err
	}
	var stripePolicy msgs.StripePolicy
	if _, err := c.ResolveDirectoryInfoEntry(log, dirInfoCache, parent, &stripePolicy); err != nil {
		return err
	}
	fileSpansReq := msgs.FileSpansReq{
		FileId:     fileId,
		ByteOffset: 0,
	}
	fileSpansResp := msgs.FileSpansResp{}
	sf := scratchFile{}
	for {
		if err := c.ShardRequest(log, fileId.Shard(), &fileSpansReq, &fileSpansResp); err != nil {
			return err
		}
		for spanIx := range fileSpansResp.Spans {
			span := &fileSpansResp.Spans[spanIx]
			atomic.AddUint64(&stats.AnalyzedLogicalSize, uint64(span.Header.Size))
			if span.Header.StorageClass == msgs.INLINE_STORAGE {
				continue
			}
			body := span.Body.(*msgs.FetchedBlocksSpan)
			atomic.AddUint64(&stats.AnalyzedBlocks, uint64(body.Parity.Blocks()))
			actualSpanParams := &client.SpanParameters{
				Parity:       body.Parity,
				Stripes:      body.Stripes,
				StorageClass: span.Header.StorageClass,
				CellSize:     body.CellSize,
			}
			desiredSpanParams := client.ComputeSpanParameters(&spanPolicy, &blockPolicy, &stripePolicy, span.Header.Size)
			if *actualSpanParams == *desiredSpanParams {
				continue
			}
			log.Debug("replacing path=%q file=%v offset=%v before=%v after=%v", filePath, fileId, span.Header.ByteOffset, actualSpanParams, desiredSpanParams)
			atomic.AddUint64(&stats.DefraggedSpans, 1)
			atomic.AddUint64(&stats.DefraggedLogicalBytes, uint64(span.Header.Size))
			atomic.AddUint64(&stats.DefraggedBlocksBefore, uint64(body.Parity.Blocks()))
			atomic.AddUint64(&stats.DefraggedBlocksAfter, uint64(desiredSpanParams.Parity.Blocks()))
			atomic.AddUint64(&stats.DefraggedPhysicalBytesBefore, uint64(body.Parity.Blocks())*uint64(body.Stripes)*uint64(body.CellSize))
			atomic.AddUint64(&stats.DefraggedPhysicalBytesAfter, uint64(desiredSpanParams.Parity.Blocks())*uint64(desiredSpanParams.Stripes)*uint64(desiredSpanParams.CellSize))
			// read span
			spanBuf, err := c.FetchSpan(log, bufPool, fileId, fileSpansResp.BlockServices, span)
			if err != nil {
				return err
			}
			defer bufPool.Put(spanBuf)
			crc := crc32c.Sum(0, *spanBuf)
			if msgs.Crc(crc) != span.Header.Crc {
				panic(fmt.Errorf("expected crc %v, got %v", span.Header.Crc, msgs.Crc(crc)))
			}
			// create scratch file
			if err := ensureScratchFile(log, c, fileId.Shard(), &sf, fmt.Sprintf("defrag %v", fileId)); err != nil {
				return err
			}
			// create new span in scratch file
			scratchOffset := sf.size
			createdBlocks, err := c.CreateSpan(log, []msgs.BlacklistEntry{}, &spanPolicy, &blockPolicy, &stripePolicy, sf.id, fileId, sf.cookie, sf.size, span.Header.Size, spanBuf)
			if err != nil {
				return err
			}
			sf.size += uint64(span.Header.Size)
			// swap them
			swapReq := msgs.SwapSpansReq{
				FileId1:     fileId,
				ByteOffset1: span.Header.ByteOffset,
				Blocks1:     []msgs.BlockId{},
				FileId2:     sf.id,
				ByteOffset2: scratchOffset,
				Blocks2:     createdBlocks,
			}
			for _, block := range body.Blocks {
				swapReq.Blocks1 = append(swapReq.Blocks1, block.BlockId)
			}
			if err := c.ShardRequest(log, fileId.Shard(), &swapReq, &msgs.SwapSpansResp{}); err != nil {
				return err
			}
		}
		if fileSpansResp.NextOffset == 0 {
			break
		}
		fileSpansReq.ByteOffset = fileSpansResp.NextOffset
	}
	log.Debug("finished migrating file %v, %v files migrated so far", fileId, stats.AnalyzedFiles)
	return nil
}

func DefragFile(
	log *lib.Logger,
	client *client.Client,
	bufPool *lib.BufPool,
	dirInfoCache *client.DirInfoCache,
	stats *DefragStats,
	progressReportAlert *lib.XmonNCAlert,
	parent msgs.InodeId,
	fileId msgs.InodeId,
	filePath string,
) error {
	timeStats := newTimeStats()
	return defragFileInternal(log, client, bufPool, dirInfoCache, stats, progressReportAlert, timeStats, parent, fileId, filePath)
}

func DefragFiles(
	log *lib.Logger,
	c *client.Client,
	bufPool *lib.BufPool,
	dirInfoCache *client.DirInfoCache,
	stats *DefragStats,
	progressReportAlert *lib.XmonNCAlert,
	root string,
	workersPerShard int,
	startFrom msgs.EggsTime,
) error {
	timeStats := newTimeStats()
	return client.Parwalk(
		log, c, &client.ParwalkOptions{WorkersPerShard: 5}, root,
		func(parent msgs.InodeId, parentPath string, name string, creationTime msgs.EggsTime, id msgs.InodeId, current bool, owned bool) error {
			if id.Type() == msgs.DIRECTORY {
				return nil
			}
			if creationTime < startFrom {
				return nil
			}
			return defragFileInternal(
				log, c, bufPool, dirInfoCache, stats, progressReportAlert, timeStats, parent, id, path.Join(parentPath, name),
			)
		},
	)
}
