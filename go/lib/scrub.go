package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type ScrubState struct {
	Migrate              MigrateStats
	CheckedBlocks        uint64
	CheckedBytes         uint64
	WorkersQueuesSize    [256]uint64
	CheckQueuesSize      [256]uint64
	DecommissionedBlocks uint64
	Cursors              [256]msgs.InodeId
}

type ScrubOptions struct {
	MaximumCheckAttempts  uint // 0 = infinite
	NumWorkers            int  // how many goroutienes should be sending check request to the block services
	WorkersQueueSize      int
	CheckerQueueSize      int
	QuietPeriod           time.Duration
	RateLimitErasedBlocks *RateLimitOpts
}

func scrubFileInternal(
	log *Logger,
	client *Client,
	bufPool *BufPool,
	stats *ScrubState,
	timeStats *timeStats,
	scratchFile *scratchFile,
	file msgs.InodeId,
) error {
	badBlock := func(blockService *msgs.BlockService, blockSize uint32, block *msgs.FetchedBlock) (bool, error) {
		err := client.CheckBlock(log, blockService, block.BlockId, blockSize, block.Crc)
		if err == msgs.BAD_BLOCK_CRC || err == msgs.BLOCK_NOT_FOUND || err == msgs.BLOCK_PARTIAL_IO_ERROR {
			log.RaiseAlert("found bad block, block service %v, block %v: %v", blockService.Id, block.BlockId, err)
			return true, nil
		}
		return false, err
	}
	return migrateBlocksInFileGeneric(log, client, bufPool, &stats.Migrate, timeStats, "scrubbed", badBlock, scratchFile, file)
}

type scrubCheckInfo struct {
	file           msgs.InodeId
	alert          *XmonNCAlert
	blockService   msgs.BlockService
	block          msgs.BlockId
	size           uint32
	crc            msgs.Crc
	canIgnoreError bool
	attempts       uint
}

func scrubChecker(
	log *Logger,
	client *Client,
	opts *ScrubOptions,
	rateLimit *RateLimit,
	stats *ScrubState,
	shid msgs.ShardId,
	scratchFiles *scratchFile,
	checkerChan chan *BlockCompletion,
	terminateChan chan any,
) {
	bufPool := NewBufPool()
	var scrubbingMu sync.Mutex
	terminal := false
	outstandingScrubbings := 0
	outstandingScrubbingsChan := make(chan struct{}, 10)

	for {
		select {
		case <-outstandingScrubbingsChan:
			outstandingScrubbings--
			log.Debug("scrubbing done, outstanding scrubbings: %v", outstandingScrubbings)
		case completion := <-checkerChan:
			if completion == nil {
				terminal = true
				log.Debug("checker terminating, waiting for outstanding scrubbings")
			} else {
				if rateLimit != nil {
					rateLimit.Acquire()
				}
				atomic.StoreUint64(&stats.CheckQueuesSize[shid], uint64(len(checkerChan)))
				err := completion.Error
				info := completion.Extra.(*scrubCheckInfo)
				if err == msgs.BAD_BLOCK_CRC || err == msgs.BLOCK_NOT_FOUND || err == msgs.BLOCK_PARTIAL_IO_ERROR {
					atomic.AddUint64(&stats.CheckedBlocks, 1)
					if err == msgs.BAD_BLOCK_CRC {
						atomic.AddUint64(&stats.CheckedBytes, uint64(info.size))
					}
					// Scrub in separate thread to avoid delaying queue processing
					outstandingScrubbings++
					go func() {
						defer func() {
							log.Debug("finished scrubbing file %v", info.file)
							outstandingScrubbingsChan <- struct{}{}
						}()
						log.Debug("starting to scrub file %v", info.file)
						scrubbingMu.Lock() // scrubbings don't work in parallel
						defer scrubbingMu.Unlock()
						log.Info("got bad block error for file %v (block %v, block service %v), starting to migrate: %v", info.file, info.block, info.blockService.Id, err)
						if err := scrubFileInternal(log, client, bufPool, stats, nil, scratchFiles, info.file); err != nil {
							log.Info("could not scrub file %v, will terminate: %v", info.file, err)
							select {
							case terminateChan <- err:
							default:
							}
							return
						}
						log.Info("migration finished for file %v", info.file)
					}()
				} else if err == msgs.BLOCK_IO_ERROR {
					// This is almost certainly a broken server. There isn't that much we can do.
					// The block service will alert.
					log.Info("got IO error for file %v (block %v, block service %v), ignoring: %v", info.file, info.block, info.blockService.Id, err)
				} else if err != nil {
					if info.canIgnoreError {
						log.Debug("could not check block %v in file %v block service %v, but can ignore error (block service is probably decommissioned): %v", info.block, info.file, info.blockService.Id, err)
					} else {
						info.attempts++
						if info.attempts >= opts.MaximumCheckAttempts {
							log.Info("could not check block %v in file %v, terminating: %v", info.block, info.file, err)
							select {
							case terminateChan <- err:
							default:
							}
							return
						} else {
							outstandingScrubbings++
							go func() {
								defer func() {
									log.Debug("finished checking block %v for file %v again", info.block, info.file)
									outstandingScrubbingsChan <- struct{}{}
								}()
								log.Debug("starting to check block %v for file %v again", info.block, info.file)
								if info.alert == nil {
									info.alert = log.NewNCAlert(0)
								}
								log.RaiseNC(info.alert, "could not check block %v in file %v, will wait one second and retry: %v", info.block, info.file, err)
								time.Sleep(time.Second)
								for {
									err := client.StartCheckBlock(log, &info.blockService, info.block, info.size, info.crc, info, checkerChan)
									if err == nil {
										break
									}
									log.RaiseNC(info.alert, "could not start checking block %v in file %v, will retry after a second: %v", info.block, info.file, err)
									time.Sleep(time.Second)
								}
							}()
						}
					}
				} else {
					log.Debug("block %v is good", info.block)
					atomic.AddUint64(&stats.CheckedBlocks, 1)
					atomic.AddUint64(&stats.CheckedBytes, uint64(info.size))
					if info.alert != nil {
						log.ClearNC(info.alert)
					}
				}
			}
		}
		if terminal && outstandingScrubbings == 0 {
			log.Debug("checker terminal, and no scrubbings outstanding, terminating")
			return
		}
	}
}

type scrubRequest struct {
	file         msgs.InodeId
	blockService msgs.BlockService
	block        msgs.BlockId
	size         uint32
	crc          msgs.Crc
}

func scrubWorker(
	log *Logger,
	client *Client,
	stats *ScrubState,
	shid msgs.ShardId,
	workerChan chan *scrubRequest,
	checkerChan chan *BlockCompletion,
	terminateChan chan any,
) {
	alert := log.NewNCAlert(10 * time.Second)
	for {
		req := <-workerChan
		if req == nil {
			log.Debug("worker terminating")
			workerChan <- nil // terminate the other workers, too
			log.Debug("worker terminated")
			return
		}
		atomic.StoreUint64(&stats.WorkersQueuesSize[shid], uint64(len(workerChan)))
		canIgnoreError := req.blockService.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_STALE | msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED | msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE)
		if req.blockService.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) {
			atomic.AddUint64(&stats.DecommissionedBlocks, 1)
		}
		info := &scrubCheckInfo{
			file:           req.file,
			blockService:   req.blockService,
			block:          req.block,
			size:           req.size,
			crc:            req.crc,
			canIgnoreError: canIgnoreError,
		}
		err := client.StartCheckBlock(log, &req.blockService, req.block, req.size, req.crc, info, checkerChan)
		if err == nil {
			log.ClearNC(alert)
		} else {
			log.Info("could not start checking block %v in file %v, terminating: %v", req.block, req.file, err)
			select {
			case terminateChan <- err:
			default:
			}
			return
		}
	}
}

func scrubScraper(
	log *Logger,
	client *Client,
	stats *ScrubState,
	shid msgs.ShardId,
	terminateChan chan any,
	workerChan chan *scrubRequest,
) {
	fileReq := &msgs.VisitFilesReq{BeginId: stats.Cursors[shid]}
	fileResp := &msgs.VisitFilesResp{}
	for {
		if err := client.ShardRequest(log, msgs.ShardId(shid), fileReq, fileResp); err != nil {
			log.Info("could not get files: %v", err)
			select {
			case terminateChan <- err:
			default:
			}
			return
		}
		log.Debug("will migrate %d files", len(fileResp.Ids))
		for _, file := range fileResp.Ids {
			spansReq := msgs.FileSpansReq{FileId: file}
			spansResp := msgs.FileSpansResp{}
			for {
				if err := client.ShardRequest(log, file.Shard(), &spansReq, &spansResp); err != nil {
					log.Info("could not get spans: %v", err)
					select {
					case terminateChan <- err:
					default:
					}
					return
				}
				for _, span := range spansResp.Spans {
					if span.Header.StorageClass == msgs.INLINE_STORAGE {
						continue
					}
					body := span.Body.(*msgs.FetchedBlocksSpan)
					for _, block := range body.Blocks {
						size := body.CellSize * uint32(body.Stripes)
						blockService := spansResp.BlockServices[block.BlockServiceIx]
						workerChan <- &scrubRequest{
							file:         file,
							blockService: blockService,
							block:        block.BlockId,
							size:         size,
							crc:          block.Crc,
						}
					}
				}
				spansReq.ByteOffset = spansResp.NextOffset
				if spansReq.ByteOffset == 0 {
					break
				}
			}
		}
		stats.Cursors[shid] = fileResp.NextId
		fileReq.BeginId = fileResp.NextId
		if fileReq.BeginId == 0 {
			// this will terminate all the workers
			log.Debug("file scraping done, terminating workers")
			workerChan <- nil
			return
		}
	}
}

func ScrubFile(
	log *Logger,
	client *Client,
	stats *ScrubState,
	file msgs.InodeId,
) error {
	bufPool := NewBufPool()
	scratchFile := scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, client, &scratchFile)
	defer keepAlive.stop()
	return scrubFileInternal(log, client, bufPool, stats, nil, &scratchFile, file)
}

func ScrubFiles(
	log *Logger,
	client *Client,
	opts *ScrubOptions,
	rateLimit *RateLimit,
	stats *ScrubState,
	shid msgs.ShardId,
) error {
	if opts.NumWorkers <= 0 {
		panic(fmt.Errorf("the number of senders should be positive, got %v", opts.NumWorkers))
	}
	log.Info("starting to scrub files")
	terminateChan := make(chan any, 1)
	sendChan := make(chan *scrubRequest, opts.WorkersQueueSize)
	checkChan := make(chan *BlockCompletion, opts.CheckerQueueSize)
	scratchFile := &scratchFile{}
	keepAlive := startToKeepScratchFileAlive(log, client, scratchFile)
	defer func() {
		keepAlive.stop()
	}()

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		scrubScraper(log, client, stats, shid, terminateChan, sendChan)
	}()

	var workersWg sync.WaitGroup
	workersWg.Add(opts.NumWorkers)
	for i := 0; i < opts.NumWorkers; i++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			scrubWorker(log, client, stats, shid, sendChan, checkChan, terminateChan)
			workersWg.Done()
		}()
	}
	go func() {
		workersWg.Wait()
		log.Info("all workers terminated, terminating checker")
		checkChan <- nil
	}()

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		scrubChecker(log, client, opts, rateLimit, stats, shid, scratchFile, checkChan, terminateChan)
		log.Info("checker terminated")
		select {
		case terminateChan <- nil:
		default:
		}
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not scrub files: %v", err)
		return err.(error)
	}
}

func ScrubFilesInAllShards(
	log *Logger,
	client *Client,
	opts *ScrubOptions,
	state *ScrubState,
) error {
	terminateChan := make(chan any, 1)
	var rateLimit *RateLimit
	if opts.RateLimitErasedBlocks != nil {
		rateLimit = NewRateLimit(opts.RateLimitErasedBlocks)
		defer rateLimit.Close()
	}

	var wg sync.WaitGroup
	wg.Add(256)
	for i := 0; i < 256; i++ {
		shid := msgs.ShardId(i)
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				if err := ScrubFiles(log, client, opts, rateLimit, state, shid); err != nil {
					panic(err)
				}
				if opts.QuietPeriod < 0 {
					break
				} else {
					log.Info("waiting for %v before starting to scrub files again in shard %v", opts.QuietPeriod, shid)
					time.Sleep(opts.QuietPeriod)
				}
			}
			wg.Done()
		}()
	}
	go func() {
		wg.Wait()
		log.Info("scrubbing terminated in all shards")
		terminateChan <- nil
	}()

	err := <-terminateChan
	if err == nil {
		return nil
	} else {
		log.Info("could not scrub files: %v", err)
		return err.(error)
	}
}
