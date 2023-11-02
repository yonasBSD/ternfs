package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	crand "crypto/rand"
	"crypto/sha1"
	"encoding/binary"
	"errors"
	"flag"
	"fmt"
	"io"
	"math/rand"
	mrand "math/rand"
	"net"
	"os"
	"os/signal"
	"path"
	"path/filepath"
	"runtime/pprof"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	"golang.org/x/sys/unix"
)

type blockServiceStats struct {
	blocksWritten uint64
	bytesWritten  uint64
	blocksErased  uint64
	blocksFetched uint64
	bytesFetched  uint64
}

type env struct {
	bufPool  *lib.BufPool
	stats    map[msgs.BlockServiceId]*blockServiceStats
	counters map[msgs.BlocksMessageKind]*lib.Timings
}

func BlockWriteProof(blockServiceId msgs.BlockServiceId, blockId msgs.BlockId, key cipher.Block) [8]byte {
	buf := bytes.NewBuffer([]byte{})
	// struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
	binary.Write(buf, binary.LittleEndian, uint64(blockServiceId))
	buf.Write([]byte{'W'})
	binary.Write(buf, binary.LittleEndian, uint64(blockId))
	return lib.CBCMAC(key, buf.Bytes())
}

func BlockEraseProof(blockServiceId msgs.BlockServiceId, blockId msgs.BlockId, key cipher.Block) [8]byte {
	buf := bytes.NewBuffer([]byte{})
	// struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'E', block['block_id'])
	binary.Write(buf, binary.LittleEndian, uint64(blockServiceId))
	buf.Write([]byte{'E'})
	binary.Write(buf, binary.LittleEndian, uint64(blockId))

	return lib.CBCMAC(key, buf.Bytes())
}

func blockServiceIdFromKey(secretKey [16]byte) msgs.BlockServiceId {
	// we don't really care about leaking part or all of the key -- the whole key business is
	// to defend against bugs, not malicious agents.
	//
	// That said, we prefer to encode the knowledge of how to generate block service ids once,
	// in the block service.
	//
	// Also, we remove the highest bit for the sake of SQLite, at least for now
	return msgs.BlockServiceId(binary.LittleEndian.Uint64(secretKey[:8]) & uint64(0x7FFFFFFFFFFFFFFF))
}

func countBlocks(basePath string) (uint64, error) {
	blocks := uint64(0)
	for i := 0; i < 256; i++ {
		blockDir := fmt.Sprintf("%02x", i)
		d, err := os.Open(path.Join(basePath, blockDir))
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			return 0, err
		}
		defer d.Close()
		for {
			entries, err := d.Readdirnames(1000)
			if err == io.EOF {
				break
			}
			if err != nil {
				return 0, err
			}
			blocks += uint64(len(entries))
		}
	}
	return blocks, nil
}

// either updates `blockService`, or returns an error.
func updateBlockServiceInfoOrError(
	log *lib.Logger,
	blockService *blockService,
) error {
	t := time.Now()
	log.Info("starting to update block services info for %v", blockService.cachedInfo.Id)
	var statfs unix.Statfs_t
	if err := unix.Statfs(path.Join(blockService.path, "secret.key"), &statfs); err != nil {
		return err
	}
	capacityBytes := statfs.Blocks * uint64(statfs.Bsize)
	availableBytes := statfs.Bavail * uint64(statfs.Bsize)
	var err error
	blocks, err := countBlocks(blockService.path)
	if err != nil {
		return err
	}
	blockService.cachedInfo.CapacityBytes = capacityBytes
	blockService.cachedInfo.AvailableBytes = availableBytes
	blockService.cachedInfo.Blocks = blocks
	log.Info("done updating block service info for %v in %v", blockService.cachedInfo.Id, time.Since(t))
	return nil
}

func updateBlockServiceInfo(
	log *lib.Logger,
	blockService *blockService,
) {
	if err := updateBlockServiceInfoOrError(log, blockService); err != nil {
		blockService.couldNotUpdateInfo = true
		log.RaiseNC(&blockService.couldNotUpdateInfoAlert, "could not update block service info for block service %v: %v", blockService.cachedInfo.Id, err)
	} else {
		blockService.couldNotUpdateInfo = false
		log.ClearNC(&blockService.couldNotUpdateInfoAlert)
	}
}

func initBlockServicesInfo(
	log *lib.Logger,
	ip1 [4]byte,
	port1 uint16,
	ip2 [4]byte,
	port2 uint16,
	failureDomain [16]byte,
	blockServices map[msgs.BlockServiceId]*blockService,
) error {
	log.Info("initializing block services info")
	var wg sync.WaitGroup
	wg.Add(len(blockServices))
	alert := log.NewNCAlert(0)
	log.RaiseNC(alert, "getting info for %v block services", len(blockServices))
	for id, bs := range blockServices {
		bs.cachedInfo.Id = id
		bs.cachedInfo.Ip1 = ip1
		bs.cachedInfo.Port1 = port1
		bs.cachedInfo.Ip2 = ip2
		bs.cachedInfo.Port2 = port2
		bs.cachedInfo.SecretKey = bs.key
		bs.cachedInfo.StorageClass = bs.storageClass
		bs.cachedInfo.FailureDomain.Name = failureDomain
		bs.cachedInfo.Path = bs.path
		closureBs := bs
		go func() {
			updateBlockServiceInfo(log, closureBs)
			wg.Done()
		}()
	}
	wg.Wait()
	log.ClearNC(alert)
	return nil
}

func registerPeriodically(
	log *lib.Logger,
	blockServices map[msgs.BlockServiceId]*blockService,
	shuckleAddress string,
) {
	req := msgs.RegisterBlockServicesReq{}
	alert := log.NewNCAlert(10 * time.Second)
	for {
		req.BlockServices = req.BlockServices[:0]
		for _, bs := range blockServices {
			if bs.couldNotUpdateInfo {
				continue
			}
			req.BlockServices = append(req.BlockServices, bs.cachedInfo)
		}
		log.Trace("registering with %+v", req)
		_, err := lib.ShuckleRequest(log, nil, shuckleAddress, &req)
		if err != nil {
			log.RaiseNC(alert, "could not register block services with %+v: %v", shuckleAddress, err)
			time.Sleep(100 * time.Millisecond)
			continue
		}
		log.ClearNC(alert)
		waitForRange := time.Minute * 2
		waitFor := time.Duration(mrand.Uint64() % uint64(waitForRange.Nanoseconds()))
		log.Info("registered with %v, waiting %v", shuckleAddress, waitFor)
		time.Sleep(waitFor)
	}
}

func updateBlockServicesInfoForever(
	log *lib.Logger,
	blockServices map[msgs.BlockServiceId]*blockService,
) {
	for {
		for _, bs := range blockServices {
			updateBlockServiceInfo(log, bs)
		}
		time.Sleep(time.Minute) // so that we won't busy loop in tests etc
	}
}

func checkEraseCertificate(log *lib.Logger, blockServiceId msgs.BlockServiceId, cipher cipher.Block, req *msgs.EraseBlockReq) error {
	expectedMac, good := lib.CheckBlockEraseCertificate(blockServiceId, cipher, req)
	if !good {
		log.RaiseAlert("bad MAC, got %v, expected %v", req.Certificate, expectedMac)
		return msgs.BAD_CERTIFICATE
	}
	return nil
}

func blockIdToPath(basePath string, blockId msgs.BlockId) string {
	hex := fmt.Sprintf("%016x", uint64(blockId))
	// We want to split the blocks in dirs to avoid trouble with high number of
	// files in single directory (e.g. birthday paradox stuff). However the block
	// id is very much _not_ uniformly distributed (it's the time). So we use
	// the first byte of the SHA1 of the filename of the block id.
	h := sha1.New()
	h.Write([]byte(hex))
	dir := fmt.Sprintf("%02x", h.Sum(nil)[0])
	return path.Join(path.Join(basePath, dir), hex)
}

func eraseBlock(log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId) error {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("deleting block %v at path %v", blockId, blockPath)
	if err := os.Remove(blockPath); err != nil {
		if os.IsNotExist(err) {
			log.Info("could not find block to erase at path %v", blockPath)
			// we allow block to not exist here, because it could be that
			// a process that was in the process of destructing a span managed to erase the
			// block but not certify the deletion in the shard.
			return nil
		}
		log.RaiseAlert("internal error deleting block at path %v: %v", blockPath, err)
		return msgs.INTERNAL_ERROR
	}
	atomic.AddUint64(&env.stats[blockServiceId].blocksErased, 1)
	return nil
}

func sendFetchBlock(log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId, offset uint32, count uint32, conn *net.TCPConn) error {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("fetching block id %v at path %v", blockId, blockPath)
	f, err := os.Open(blockPath)
	if os.IsNotExist(err) {
		log.RaiseAlert("could not find block to fetch at path %v", blockPath)
		return msgs.BLOCK_NOT_FOUND
	}
	if err != nil {
		return err
	}
	defer f.Close()
	fi, err := f.Stat()
	if err != nil {
		return err
	}
	remainingSize := int(fi.Size()) - int(offset)
	if int(count) > remainingSize {
		log.RaiseAlert("was requested %v bytes, but only got %v", count, remainingSize)
		lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	if remainingSize < 0 {
		log.RaiseAlert("trying to read beyond EOF")
		lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	if _, err := f.Seek(int64(offset), 0); err != nil {
		return err
	}
	if err := lib.WriteBlocksResponse(log, conn, &msgs.FetchBlockResp{}); err != nil {
		return err
	}
	lf := io.LimitedReader{
		R: f,
		N: int64(count),
	}
	if _, err := conn.ReadFrom(&lf); err != nil {
		return err
	}
	s := env.stats[blockServiceId]
	atomic.AddUint64(&s.blocksFetched, 1)
	atomic.AddUint64(&s.bytesFetched, uint64(count))
	return nil
}

func checkWriteCertificate(log *lib.Logger, cipher cipher.Block, blockServiceId msgs.BlockServiceId, req *msgs.WriteBlockReq) error {
	expectedMac, good := lib.CheckBlockWriteCertificate(cipher, blockServiceId, req)
	if !good {
		log.Debug("mac computed for %v %v %v %v", blockServiceId, req.BlockId, req.Crc, req.Size)
		log.RaiseAlert("bad MAC, got %v, expected %v", req.Certificate, expectedMac)
		return msgs.BAD_CERTIFICATE
	}
	return nil
}

func writeToTemp(
	log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, size uint64, conn *net.TCPConn,
) (tmpName string, crc uint32, err error) {
	var f *os.File
	f, err = os.CreateTemp(basePath, "tmp.")
	if err != nil {
		return tmpName, crc, err
	}
	tmpName = f.Name()
	defer func() {
		f.Close()
		if err != nil {
			os.Remove(tmpName)
		}
	}()
	bufPtr := env.bufPool.Get(1 << 20)
	defer env.bufPool.Put(bufPtr)
	readSoFar := uint64(0)
	for {
		log.Debug("size=%v readSoFar=%v", size, readSoFar)
		buf := *bufPtr
		if uint64(len(buf)) > size-readSoFar {
			buf = buf[:int(size-readSoFar)]
		}
		var read int
		read, err = conn.Read(buf)
		if err != nil {
			return tmpName, crc, err
		}
		readSoFar += uint64(read)
		crc = crc32c.Sum(crc, buf[:read])
		if _, err = f.Write(buf[:read]); err != nil {
			return tmpName, crc, err
		}
		if readSoFar == size {
			break
		}
	}
	if err = f.Sync(); err != nil {
		return tmpName, crc, err
	}
	atomic.AddUint64(&env.stats[blockServiceId].bytesWritten, size)
	return tmpName, crc, err
}

func writeBlock(
	log *lib.Logger,
	env *env,
	blockServiceId msgs.BlockServiceId, cipher cipher.Block, basePath string,
	blockId msgs.BlockId, expectedCrc msgs.Crc, size uint32, conn *net.TCPConn,
) error {
	filePath := blockIdToPath(basePath, blockId)
	log.Debug("writing block %v at path %v", blockId, basePath)
	if err := os.Mkdir(path.Dir(filePath), 0777); err != nil && !os.IsExist(err) {
		return err
	}
	tmpName, crc, err := writeToTemp(log, env, blockServiceId, basePath, uint64(size), conn)
	if err != nil {
		return err
	}
	if msgs.Crc(crc) != expectedCrc {
		os.Remove(tmpName)
		log.RaiseAlert("bad crc for block %v, got %v in req, computed %v", blockId, msgs.Crc(crc), expectedCrc)
		lib.WriteBlocksResponseError(log, conn, msgs.BAD_BLOCK_CRC)
		return nil
	}
	if err := os.Rename(tmpName, filePath); err != nil {
		os.Remove(tmpName)
		return err
	}
	defer os.Remove(tmpName)
	// fsync directory too to ensure the rename is durable -- we have had blocks disappear
	// in the past, so this is probably needed. It would be good to coalesce these two
	// syncs (one for the file write, one for the rename), and it might be possible to
	// do only one relying on some XFS internal, but let's be safe for now.
	dir, err := os.Open(filepath.Dir(filePath))
	if err != nil {
		return err
	}
	defer dir.Close()
	if err := dir.Sync(); err != nil {
		return err
	}
	log.Debug("writing proof")
	if err := lib.WriteBlocksResponse(log, conn, &msgs.WriteBlockResp{Proof: BlockWriteProof(blockServiceId, blockId, cipher)}); err != nil {
		return err
	}
	atomic.AddUint64(&env.stats[blockServiceId].blocksWritten, 1)
	return nil
}

func testWrite(
	log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, size uint64, conn *net.TCPConn,
) error {
	tmpName, _, err := writeToTemp(log, env, blockServiceId, basePath, size, conn)
	if err != nil {
		return err
	}
	os.Remove(tmpName)
	if err := lib.WriteBlocksResponse(log, conn, &msgs.TestWriteResp{}); err != nil {
		return err
	}
	return nil
}

const PAST_CUTOFF time.Duration = 22 * time.Hour
const DEFAULT_FUTURE_CUTOFF time.Duration = 1 * time.Hour

const MAX_OBJECT_SIZE uint32 = 100 << 20

// The bool is whether we should keep going
func handleRequestError(
	log *lib.Logger,
	conn *net.TCPConn,
	lastError *error,
	req msgs.BlocksMessageKind,
	err error,
) bool {
	defer func() {
		*lastError = err
	}()

	if err == io.EOF {
		log.Debug("got EOF, terminating")
		return false
	}

	rootErr := err
	for {
		unwrapped := errors.Unwrap(rootErr)
		if unwrapped == nil {
			break
		}
		rootErr = unwrapped
	}

	if netErr, ok := rootErr.(net.Error); ok && netErr.Timeout() {
		log.Info("got timeout from %v, terminating", conn.RemoteAddr())
		return false
	}
	if sysErr, ok := rootErr.(syscall.Errno); ok {
		if sysErr == syscall.EPIPE {
			log.Info("got broken pipe error from %v, terminating", conn.RemoteAddr())
			return false
		}
		if sysErr == syscall.ECONNRESET {
			log.Info("got connection reset error from %v, terminating", conn.RemoteAddr())
			return false
		}
	}

	// we always raise an alert since this is almost always bad news in the block service
	log.RaiseAlertStack(1, "got unexpected error %v from %v for req kind %v, previous error %v", err, conn.RemoteAddr(), req, *lastError)

	if eggsErr, isEggsErr := err.(msgs.ErrCode); isEggsErr {
		lib.WriteBlocksResponseError(log, conn, eggsErr)
		// normal error, we can keep the connection alive
		return true
	} else {
		// attempt to say goodbye, ignore errors
		lib.WriteBlocksResponseError(log, conn, msgs.INTERNAL_ERROR)
		return false
	}
}

type deadBlockService struct {
	cipher cipher.Block
}

// The bool tells us whether we should keep going
func handleSingleRequest(
	log *lib.Logger,
	env *env,
	terminateChan chan any,
	lastError *error,
	blockServices map[msgs.BlockServiceId]*blockService,
	deadBlockServices map[msgs.BlockServiceId]deadBlockService,
	conn *net.TCPConn,
	futureCutoff time.Duration,
	connectionTimeout time.Duration,
) bool {
	if connectionTimeout != 0 {
		conn.SetReadDeadline(time.Now().Add(connectionTimeout))
	}
	blockServiceId, req, err := lib.ReadBlocksRequest(log, conn)
	if err != nil {
		return handleRequestError(log, conn, lastError, 0, err)
	}
	kind := req.BlocksRequestKind()
	t := time.Now()
	defer func() {
		env.counters[kind].Add(time.Since(t))
	}()
	log.Debug("servicing request of type %T from %v", req, conn.RemoteAddr())
	log.Trace("req %+v", req)
	defer log.Debug("serviced request of type %T from %v", req, conn.RemoteAddr())
	if connectionTimeout != 0 {
		// Reset timeout, with default settings this will give
		// the request a minute to complete, given that the max
		// block size is 10MiB, that is ~0.17MiB/s, so it should
		// be plenty of time unless something is wrong.
		//
		// If we didn't reset this (or just remove the timeout)
		// the previous timeout might very well trip the request
		// because it might have been almost expired.
		conn.SetDeadline(time.Now().Add(connectionTimeout))
	}
	blockService, found := blockServices[blockServiceId]
	if !found {
		// Special case: we're erasing a block in a dead block service. Always
		// succeeds.
		if whichReq, isErase := req.(*msgs.EraseBlockReq); isErase {
			if deadBlockService, isDead := deadBlockServices[blockServiceId]; isDead {
				log.Debug("servicing erase block request for dead block service from %v", conn.RemoteAddr())
				resp := msgs.EraseBlockResp{
					Proof: BlockEraseProof(blockServiceId, whichReq.BlockId, deadBlockService.cipher),
				}
				if err := lib.WriteBlocksResponse(log, conn, &resp); err != nil {
					log.Info("could not send blocks response to %v: %v", conn.RemoteAddr(), err)
					return handleRequestError(log, conn, lastError, kind, err)
				}
				atomic.AddUint64(&env.stats[blockServiceId].blocksErased, 1)
				return true
			}
		}
		// In general, refuse to service requests for block services that we
		// don't have.
		log.RaiseAlert("received unknown block service id %v", blockServiceId)
		return handleRequestError(log, conn, lastError, kind, msgs.BLOCK_SERVICE_NOT_FOUND)
	}
	switch whichReq := req.(type) {
	case *msgs.EraseBlockReq:
		if err := checkEraseCertificate(log, blockServiceId, blockService.cipher, whichReq); err != nil {
			return handleRequestError(log, conn, lastError, kind, err)
		}
		cutoffTime := msgs.EggsTime(uint64(whichReq.BlockId)).Time().Add(futureCutoff)
		now := time.Now()
		if now.Before(cutoffTime) {
			log.RaiseAlert("block %v is too recent to be deleted (now=%v, cutoffTime=%v)", whichReq.BlockId, now, cutoffTime)
			return handleRequestError(log, conn, lastError, kind, msgs.BLOCK_TOO_RECENT_FOR_DELETION)
		}
		if err := eraseBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId); err != nil {
			return handleRequestError(log, conn, lastError, kind, err)
		}

		resp := msgs.EraseBlockResp{
			Proof: BlockEraseProof(blockServiceId, whichReq.BlockId, blockService.cipher),
		}
		if err := lib.WriteBlocksResponse(log, conn, &resp); err != nil {
			log.Info("could not send blocks response to %v: %v", conn.RemoteAddr(), err)
			return handleRequestError(log, conn, lastError, kind, err)
		}
	case *msgs.FetchBlockReq:
		if err := sendFetchBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId, whichReq.Offset, whichReq.Count, conn); err != nil {
			log.Info("could not send block response to %v: %v", conn.RemoteAddr(), err)
			return handleRequestError(log, conn, lastError, kind, err)
		}
	case *msgs.WriteBlockReq:
		pastCutoffTime := msgs.EggsTime(uint64(whichReq.BlockId)).Time().Add(-PAST_CUTOFF)
		futureCutoffTime := msgs.EggsTime(uint64(whichReq.BlockId)).Time().Add(futureCutoff)
		now := time.Now()
		if now.Before(pastCutoffTime) {
			panic(fmt.Errorf("block %v is in the future! (now=%v, pastCutoffTime=%v)", whichReq.BlockId, now, pastCutoffTime))
		}
		if now.After(futureCutoffTime) {
			log.RaiseAlert("block %v is too old to be written (now=%v, futureCutoffTime=%v)", whichReq.BlockId, now, futureCutoffTime)
			return handleRequestError(log, conn, lastError, kind, msgs.BLOCK_TOO_OLD_FOR_WRITE)
		}
		if err := checkWriteCertificate(log, blockService.cipher, blockServiceId, whichReq); err != nil {
			return handleRequestError(log, conn, lastError, kind, err)
		}
		if whichReq.Size > MAX_OBJECT_SIZE {
			log.RaiseAlert("block %v exceeds max object size: %v > %v", whichReq.BlockId, whichReq.Size, MAX_OBJECT_SIZE)
			return handleRequestError(log, conn, lastError, kind, msgs.BLOCK_TOO_BIG)
		}
		if err := writeBlock(log, env, blockServiceId, blockService.cipher, blockService.path, whichReq.BlockId, whichReq.Crc, whichReq.Size, conn); err != nil {
			log.Info("could not write block: %v", err)
			return handleRequestError(log, conn, lastError, kind, err)
		}
	case *msgs.TestWriteReq:
		if err := testWrite(log, env, blockServiceId, blockService.path, whichReq.Size, conn); err != nil {
			log.Info("could not perform test write: %v", err)
			return handleRequestError(log, conn, lastError, kind, err)
		}
	default:
		return handleRequestError(log, conn, lastError, kind, fmt.Errorf("bad request type %T", req))
	}
	return true
}

func handleRequest(
	log *lib.Logger,
	env *env,
	terminateChan chan any,
	blockServices map[msgs.BlockServiceId]*blockService,
	deadBlockServices map[msgs.BlockServiceId]deadBlockService,
	conn *net.TCPConn,
	futureCutoff time.Duration,
	connectionTimeout time.Duration,
) {
	defer conn.Close()

	var lastError error

	for {
		keepGoing := handleSingleRequest(log, env, terminateChan, &lastError, blockServices, deadBlockServices, conn, futureCutoff, connectionTimeout)
		if !keepGoing {
			return
		}
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, "Usage: %v DIRECTORY STORAGE_CLASS...\n\n", os.Args[0])
	description := `
For each directory/storage class pair specified we'll have one block
service. The block service id for each will be automatically generated
when running for the first time. The failure domain will be the same.

The intention is that a single blockservice process will service
a storage node.

Options:`
	description = strings.TrimSpace(description)
	fmt.Fprintln(os.Stderr, description)
	flag.PrintDefaults()
}

func retrieveOrCreateKey(log *lib.Logger, dir string) [16]byte {
	var err error
	var keyFile *os.File
	keyFilePath := path.Join(dir, "secret.key")
	keyFile, err = os.OpenFile(keyFilePath, os.O_APPEND|os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		fmt.Fprintf(os.Stderr, "could not open key file %v: %v\n", keyFilePath, err)
		os.Exit(1)
	}
	keyFile.Seek(0, 0)
	if err := syscall.Flock(int(keyFile.Fd()), syscall.LOCK_EX|syscall.LOCK_NB); err != nil {
		fmt.Fprintf(os.Stderr, "could not lock key file %v: %v\n", keyFilePath, err)
		os.Exit(1)
	}
	var key [16]byte
	var read int
	read, err = keyFile.Read(key[:])
	if err != nil && err != io.EOF {
		fmt.Fprintf(os.Stderr, "could not read key file %v: %v\n", keyFilePath, err)
		os.Exit(1)
	}
	if err == io.EOF {
		log.Info("creating new secret key")
		if _, err := crand.Read(key[:]); err != nil {
			panic(err)
		}
		if _, err := keyFile.Write(key[:]); err != nil {
			panic(err)
		}
	} else if read != 16 {
		panic(fmt.Errorf("short secret key (%v rather than 16 bytes)", read))
	}
	return key
}

func sendMetrics(log *lib.Logger, env *env, blockServices map[msgs.BlockServiceId]*blockService, failureDomain string) {
	metrics := lib.MetricsBuilder{}
	rand := wyhash.New(rand.Uint64())
	alert := log.NewNCAlert(10 * time.Second)
	for {
		log.Info("sending metrics")
		metrics.Reset()
		now := time.Now()
		for bsId, bsStats := range env.stats {
			metrics.Measurement("eggsfs_blocks_write")
			metrics.Tag("blockservice", bsId.String())
			metrics.Tag("failuredomain", failureDomain)
			metrics.FieldU64("bytes", bsStats.bytesWritten)
			metrics.FieldU64("blocks", bsStats.blocksWritten)
			metrics.Timestamp(now)

			metrics.Measurement("eggsfs_blocks_read")
			metrics.Tag("blockservice", bsId.String())
			metrics.Tag("failuredomain", failureDomain)
			metrics.FieldU64("bytes", bsStats.bytesFetched)
			metrics.FieldU64("blocks", bsStats.blocksFetched)
			metrics.Timestamp(now)

			metrics.Measurement("eggsfs_blocks_erase")
			metrics.Tag("blockservice", bsId.String())
			metrics.Tag("failuredomain", failureDomain)
			metrics.FieldU64("blocks", bsStats.blocksErased)
			metrics.Timestamp(now)
		}
		for bsId, bsInfo := range blockServices {
			metrics.Measurement("eggsfs_blocks_storage")
			metrics.Tag("blockservice", bsId.String())
			metrics.Tag("failuredomain", failureDomain)
			metrics.Tag("storageclass", bsInfo.storageClass.String())
			metrics.FieldU64("capacity", bsInfo.cachedInfo.CapacityBytes)
			metrics.FieldU64("available", bsInfo.cachedInfo.AvailableBytes)
			metrics.FieldU64("blocks", bsInfo.cachedInfo.Blocks)
			metrics.Timestamp(now)
		}
		err := lib.SendMetrics(metrics.Payload())
		if err == nil {
			log.ClearNC(alert)
			sleepFor := time.Minute + time.Duration(rand.Uint64() & ^(uint64(1)<<63))%time.Minute
			log.Info("metrics sent, sleeping for %v", sleepFor)
			time.Sleep(sleepFor)
		} else {
			log.RaiseNC(alert, "failed to send metrics, will try again in a second: %v", err)
			time.Sleep(time.Second)
		}
	}
}

type blockService struct {
	path                    string
	key                     [16]byte
	cipher                  cipher.Block
	storageClass            msgs.StorageClass
	cachedInfo              msgs.BlockServiceInfo
	couldNotUpdateInfo      bool
	couldNotUpdateInfoAlert lib.XmonNCAlert
}

func main() {
	flag.Usage = usage
	failureDomainStr := flag.String("failure-domain", "", "Failure domain")
	futureCutoff := flag.Duration("future-cutoff", DEFAULT_FUTURE_CUTOFF, "")
	ownIp1Str := flag.String("own-ip-1", "", "First IP that we'll bind to, and that we'll advertise to shuckle.")
	port1 := flag.Uint("port-1", 0, "First port on which to run on. By default it will be picked automatically.")
	ownIp2Str := flag.String("own-ip-2", "", "Second IP that we'll advertise to shuckle. If it is not provided, we will only bind to the first IP.")
	port2 := flag.Uint("port-2", 0, "Port on which to run on. By default it will be picked automatically.")
	verbose := flag.Bool("verbose", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	trace := flag.Bool("trace", false, "")
	logFile := flag.String("log-file", "", "If empty, stdout")
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	profileFile := flag.String("profile-file", "", "")
	syslog := flag.Bool("syslog", false, "")
	connectionTimeout := flag.Duration("connection-timeout", time.Minute, "")
	metrics := flag.Bool("metrics", false, "")
	flag.Parse()
	if flag.NArg()%2 != 0 {
		fmt.Fprintf(os.Stderr, "Malformed directory/storage class pairs.\n\n")
		usage()
		os.Exit(2)
	}
	if flag.NArg() < 2 {
		fmt.Fprintf(os.Stderr, "Expected at least one block service.\n\n")
		usage()
		os.Exit(2)
	}

	if *ownIp1Str == "" {
		fmt.Fprintf(os.Stderr, "-own-ip-1 must be provided.\n\n")
		usage()
		os.Exit(2)
	}

	if *ownIp2Str == "" && *port2 != 0 {
		fmt.Fprintf(os.Stderr, "You've provided -port-2, but no -own-ip-2. If you don't need the second route, provide neither. If you do, provide both.\n\n")
		usage()
		os.Exit(2)
	}

	parseIp := func(ipStr string) [4]byte {
		parsedOwnIp := net.ParseIP(ipStr)
		if parsedOwnIp == nil || parsedOwnIp.To4() == nil {
			fmt.Fprintf(os.Stderr, "IP %v is not a valid ipv4 address. %v\n\n", ipStr, parsedOwnIp)
			usage()
			os.Exit(2)
		}
		var ownIp [4]byte
		copy(ownIp[:], parsedOwnIp.To4())
		return ownIp
	}
	ownIp1 := parseIp(*ownIp1Str)
	var ownIp2 [4]byte
	if *ownIp2Str != "" {
		ownIp2 = parseIp(*ownIp2Str)
	}

	var failureDomain [16]byte
	if copy(failureDomain[:], []byte(*failureDomainStr)) != len(*failureDomainStr) {
		fmt.Fprintf(os.Stderr, "Failure domain too long -- must be at most 16 characters: %v\n\n", *failureDomainStr)
		usage()
		os.Exit(2)
	}

	// create all directories first, we might need them for the log output
	for i := 0; i < flag.NArg(); i += 2 {
		dir := flag.Args()[i]
		if err := os.Mkdir(dir, 0777); err != nil && !os.IsExist(err) {
			panic(fmt.Errorf("could not create data dir %v", dir))
		}
	}

	logOut := os.Stdout
	if *logFile != "" {
		var err error
		logOut, err = os.OpenFile(*logFile, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
		if err != nil {
			fmt.Fprintf(os.Stderr, "could not open log file %v: %v\n", *logFile, err)
			os.Exit(1)
		}
		defer logOut.Close()
	}
	level := lib.INFO
	if *verbose {
		level = lib.DEBUG
	}
	if *trace {
		level = lib.TRACE
	}
	log := lib.NewLogger(logOut, &lib.LoggerOptions{
		Level:            level,
		Syslog:           *syslog,
		Xmon:             *xmon,
		AppName:          "blockservice",
		AppType:          "restech.daytime",
		PrintQuietAlerts: true,
	})

	if *profileFile != "" {
		f, err := os.Create(*profileFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Could not open profile file %v", *profileFile)
			os.Exit(1)
		}
		pprof.StartCPUProfile(f)
		stopCpuProfile := func() {
			log.Info("stopping cpu profile")
			pprof.StopCPUProfile()
		}
		defer stopCpuProfile()
		// Save CPU profile if we get killed by a signal
		signalChan := make(chan os.Signal, 1)
		signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
		go func() {
			sig := <-signalChan
			signal.Stop(signalChan)
			stopCpuProfile()
			syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
		}()
	}

	log.Info("Running block service with options:")
	log.Info("  failureDomain = %v", *failureDomainStr)
	log.Info("  futureCutoff = %v", *futureCutoff)
	log.Info("  ownIp1 = '%v'", *ownIp1Str)
	log.Info("  port1 = %v", *port1)
	log.Info("  ownIp2 = %v", *ownIp2Str)
	log.Info("  port2 = %v", *port2)
	log.Info("  logLevel = %v", level)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  shuckleAddress = '%v'", *shuckleAddress)
	log.Info("  connectionTimeout = %v", *connectionTimeout)

	blockServices := make(map[msgs.BlockServiceId]*blockService)
	for i := 0; i < flag.NArg(); i += 2 {
		dir := flag.Args()[i]
		storageClass := msgs.StorageClassFromString(flag.Args()[i+1])
		if storageClass == msgs.EMPTY_STORAGE || storageClass == msgs.INLINE_STORAGE {
			fmt.Fprintf(os.Stderr, "Storage class cannot be EMPTY/INLINE")
			os.Exit(2)
		}
		key := retrieveOrCreateKey(log, dir)
		id := blockServiceIdFromKey(key)
		cipher, err := aes.NewCipher(key[:])
		if err != nil {
			panic(fmt.Errorf("could not create AES-128 key: %w", err))
		}
		blockServices[id] = &blockService{
			path:                    dir,
			key:                     key,
			cipher:                  cipher,
			storageClass:            storageClass,
			couldNotUpdateInfoAlert: *log.NewNCAlert(time.Second),
		}
	}
	for id, blockService := range blockServices {
		log.Info("block service %v at %v, storage class %v", id, blockService.path, blockService.storageClass)
	}

	if len(blockServices) != flag.NArg()/2 {
		panic(fmt.Errorf("duplicate block services"))
	}

	// Now ask shuckle for block services we _had_ before. We need to know this to honor
	// erase block requests for old block services safely.
	deadBlockServices := make(map[msgs.BlockServiceId]deadBlockService)
	{
		timeouts := lib.NewReqTimeouts(lib.DefaultShuckleTimeout.Initial, lib.DefaultShuckleTimeout.Max, 0, lib.DefaultShuckleTimeout.Growth, lib.DefaultShuckleTimeout.Jitter)
		resp, err := lib.ShuckleRequest(log, timeouts, *shuckleAddress, &msgs.AllBlockServicesReq{})
		if err != nil {
			panic(fmt.Errorf("could not request block services from shuckle: %v", err))
		}
		shuckleBlockServices := resp.(*msgs.AllBlockServicesResp).BlockServices
		for i := range shuckleBlockServices {
			bs := &shuckleBlockServices[i]
			ourBs, weHaveBs := blockServices[bs.Id]
			sameFailureDomain := bs.FailureDomain.Name == failureDomain
			isDecommissioned := (bs.Flags & msgs.EGGSFS_BLOCK_SERVICE_DECOMMISSIONED) != 0
			// No disagreement on failure domain with shuckle (otherwise we could end up with
			// a split brain scenario where two eggsblocks processes assume control of two dead
			// block services)
			if weHaveBs && !sameFailureDomain {
				panic(fmt.Errorf("We have block service %v, and we're failure domain %v, but shuckle thinks it should be failure domain %v. If you've moved this block service, change the failure domain on shuckle.", bs.Id, failureDomain, bs.FailureDomain))
			}
			// block services in the same failure domain, which we do not have, must be
			// decommissioned
			if !weHaveBs && sameFailureDomain {
				if !isDecommissioned {
					panic(fmt.Errorf("Shuckle has block service %v for our failure domain %v, but we don't have this block service, and it is not decommissioned. If the block service is dead, mark it as decommissioned.", bs.Id, failureDomain))
				}
				cipher, err := aes.NewCipher(bs.SecretKey[:])
				if err != nil {
					panic(fmt.Errorf("could not create AES-128 key: %w", err))
				}
				log.Info("will service erase block requests for decommissioned block service %v", bs.Id)
				deadBlockServices[bs.Id] = deadBlockService{
					cipher: cipher,
				}
			}
			// we can't have a decommissioned block service
			if weHaveBs && isDecommissioned {
				log.RaiseAlert("We have block service %v, which is decommissioned according to shuckle. We will treat it as if it doesn't exist.", bs.Id)
				delete(blockServices, bs.Id)
				deadBlockServices[bs.Id] = deadBlockService{
					cipher: ourBs.cipher,
				}
			}
		}
	}

	listener1, err := net.Listen("tcp4", fmt.Sprintf("%v:%v", net.IP(ownIp1[:]), *port1))
	if err != nil {
		panic(err)
	}
	defer listener1.Close()

	log.Info("running 1 on %v", listener1.Addr())
	actualPort1 := uint16(listener1.Addr().(*net.TCPAddr).Port)

	var listener2 net.Listener
	var actualPort2 uint16
	if *ownIp2Str != "" {
		listener2, err = net.Listen("tcp4", fmt.Sprintf("%v:%v", net.IP(ownIp2[:]), *port2))
		if err != nil {
			panic(err)
		}
		defer listener2.Close()

		log.Info("running 2 on %v", listener2.Addr())
		actualPort2 = uint16(listener2.Addr().(*net.TCPAddr).Port)
	}

	initBlockServicesInfo(log, ownIp1, actualPort1, ownIp2, actualPort2, failureDomain, blockServices)
	log.Info("finished updating block service info, will now start")

	terminateChan := make(chan any)

	bufPool := lib.NewBufPool()

	env := &env{
		bufPool: bufPool,
		stats:   make(map[msgs.BlockServiceId]*blockServiceStats),
	}
	for bsId := range blockServices {
		env.stats[bsId] = &blockServiceStats{}
	}
	for bsId := range deadBlockServices {
		env.stats[bsId] = &blockServiceStats{}
	}
	env.counters = make(map[msgs.BlocksMessageKind]*lib.Timings)
	for _, k := range msgs.AllBlocksMessageKind {
		env.counters[k] = lib.NewTimings(40, 100*time.Microsecond, 1.5)
	}

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		registerPeriodically(log, blockServices, *shuckleAddress)
	}()

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		updateBlockServicesInfoForever(log, blockServices)
	}()

	if *metrics {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			sendMetrics(log, env, blockServices, *failureDomainStr)
		}()
	}

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		for {
			conn, err := listener1.Accept()
			log.Trace("new conn %+v", conn)
			if err != nil {
				terminateChan <- err
				return
			}
			go func() {
				defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
				handleRequest(log, env, terminateChan, blockServices, deadBlockServices, conn.(*net.TCPConn), *futureCutoff, *connectionTimeout)
			}()
		}
	}()
	if listener2 != nil {
		go func() {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				conn, err := listener2.Accept()
				log.Trace("new conn %+v", conn)
				if err != nil {
					terminateChan <- err
					return
				}
				go func() {
					defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
					handleRequest(log, env, terminateChan, blockServices, deadBlockServices, conn.(*net.TCPConn), *futureCutoff, *connectionTimeout)
				}()
			}
		}()
	}

	{
		err := <-terminateChan
		if err != nil {
			panic(err)
		}
	}
}
