package lib

import (
	"fmt"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type ScrubState struct {
	Migrate        MigrateStats
	CheckedBlocks  uint64
	CheckedBytes   uint64
	SendQueueSize  uint64
	CheckQueueSize uint64
	Cursors        [256]msgs.InodeId
}

type ScrubOptions struct {
	MaximumCheckAttempts uint // 0 = infinite
	NumSenders           int  // how many goroutienes should be sending check request to the block services
	SendersQueueSize     int
	CheckerQueueSize     int
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
	stats *ScrubState,
	scratchFiles map[msgs.ShardId]*scratchFile,
	checkerChan chan *BlockCompletion,
	terminateChan chan any,
) {
	bufPool := NewBufPool()
	outstandingScrubbings := &sync.WaitGroup{}
	var scrubbingMu sync.Mutex

	for {

		completion := <-checkerChan
		if completion == nil {
			log.Debug("checker terminating, waiting for outstanding scrubbings")
			outstandingScrubbings.Wait()
			log.Debug("checker finished waiting for outstanding scrubbing, terminating")
			return
		}
		atomic.StoreUint64(&stats.CheckQueueSize, uint64(len(checkerChan)))
		err := completion.Error
		info := completion.Extra.(*scrubCheckInfo)
		if err == msgs.BAD_BLOCK_CRC || err == msgs.BLOCK_NOT_FOUND || err == msgs.BLOCK_PARTIAL_IO_ERROR {
			atomic.AddUint64(&stats.CheckedBlocks, 1)
			if err == msgs.BAD_BLOCK_CRC {
				atomic.AddUint64(&stats.CheckedBytes, uint64(info.size))
			}
			// Scrub in separate thread to avoid delaying queue processing
			outstandingScrubbings.Add(1)
			go func() {
				defer outstandingScrubbings.Done()
				scrubbingMu.Lock() // scrubbings don't work in parallel
				defer scrubbingMu.Unlock()
				log.Info("got bad block error for file %v (block %v, block service %v), starting to migrate: %v", info.file, info.block, info.blockService.Id, err)
				if err := scrubFileInternal(log, client, bufPool, stats, nil, scratchFiles[info.file.Shard()], info.file); err != nil {
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
					outstandingScrubbings.Add(1)
					go func() {
						defer outstandingScrubbings.Done()
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
			atomic.AddUint64(&stats.CheckedBlocks, 1)
			atomic.AddUint64(&stats.CheckedBytes, uint64(info.size))
			if info.alert != nil {
				log.ClearNC(info.alert)
			}
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

func scrubSender(
	log *Logger,
	client *Client,
	stats *ScrubState,
	sendChan chan *scrubRequest,
	checkerChan chan *BlockCompletion,
	terminateChan chan any,
) {
	alert := log.NewNCAlert(10 * time.Second)
	for {
		req := <-sendChan
		if req == nil {
			log.Debug("sender terminating")
			sendChan <- nil // terminate the other senders, too
			return
		}
		atomic.StoreUint64(&stats.SendQueueSize, uint64(len(sendChan)))
		canIgnoreError := req.blockService.Flags.HasAny(msgs.EGGSFS_BLOCK_SERVICE_STALE | msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED | msgs.EGGSFS_BLOCK_SERVICE_NO_WRITE)
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
	terminateChan chan any,
	sendChan chan *scrubRequest,
	shards []msgs.ShardId,
) {
	fileReqs := make([]msgs.VisitFilesReq, len(shards))
	for i := range fileReqs {
		fileReqs[i].BeginId = stats.Cursors[shards[i]]
	}
	fileResps := make([]msgs.VisitFilesResp, len(shards))
	for i := 0; ; i++ {
		allDone := true
		for j, shid := range shards {
			fileReq := &fileReqs[j]
			fileResp := &fileResps[j]
			if i > 0 && fileReq.BeginId == 0 {
				continue
			}
			allDone = false
			if err := client.ShardRequest(log, shid, fileReq, fileResp); err != nil {
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
							sendChan <- &scrubRequest{
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
		}
		if allDone {
			// this will terminate all the senders
			log.Debug("file scraping done, terminating senders")
			sendChan <- nil
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
	stats *ScrubState,
	shards []msgs.ShardId,
) error {
	if opts.NumSenders <= 0 {
		panic(fmt.Errorf("the number of senders should be positive, got %v", opts.NumSenders))
	}
	log.Info("starting to scrub files in shards %+v", shards)
	terminateChan := make(chan any, 1)
	numSenders := opts.NumSenders
	sendChan := make(chan *scrubRequest, opts.SendersQueueSize)
	checkChan := make(chan *BlockCompletion, opts.CheckerQueueSize)
	scratchFiles := make(map[msgs.ShardId]*scratchFile)
	keepAlives := make(map[msgs.ShardId]keepScratchFileAlive)
	for _, shid := range shards {
		scratchFiles[shid] = &scratchFile{}
		keepAlives[shid] = startToKeepScratchFileAlive(log, client, scratchFiles[shid])
	}
	defer func() {
		for _, keepAlive := range keepAlives {
			keepAlive.stop()
		}
	}()

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		scrubScraper(log, client, stats, terminateChan, sendChan, shards)
	}()

	var sendersWg sync.WaitGroup
	sendersWg.Add(numSenders)
	for i := 0; i < numSenders; i++ {
		go func() {
			defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
			scrubSender(log, client, stats, sendChan, checkChan, terminateChan)
			sendersWg.Done()
		}()
	}
	go func() {
		sendersWg.Wait()
		log.Info("all senders terminated, terminating checker")
		checkChan <- nil
	}()

	go func() {
		defer func() { HandleRecoverChan(log, terminateChan, recover()) }()
		scrubChecker(log, client, opts, stats, scratchFiles, checkChan, terminateChan)
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

func ScrubFilesInAllShards(log *Logger, client *Client, opts *ScrubOptions, stats *ScrubState) error {
	var shards [256]msgs.ShardId
	for i := range shards {
		shards[i] = msgs.ShardId(i)
	}
	return ScrubFiles(log, client, opts, stats, shards[:])
}
