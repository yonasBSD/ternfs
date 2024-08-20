package main

import (
	"bufio"
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	crand "crypto/rand"
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
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"unsafe"
	"xtx/eggsfs/certificate"
	"xtx/eggsfs/client"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"

	"golang.org/x/sys/unix"
)

// #include <unistd.h>
// #include <fcntl.h>
// #include <errno.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/syscall.h>
//
// struct linux_dirent {
//     unsigned long  d_ino;
//     off_t          d_off;
//     unsigned short d_reclen;
//     char           d_name[];
// };
//
// // If negative, it's an error code.
// ssize_t count_blocks(char* base_path) {
//     int base_fd = -1;
//     int dir_fd = -1;
//     ssize_t blocks = 0;
//     base_fd = open(base_path, O_RDONLY|O_DIRECTORY);
//     if (base_fd < 0) {
//         fprintf(stderr, "could not open %s: %d\n", base_path, errno);
//         blocks = -errno;
//         goto out;
//     }
//     char buf[1024];
//     for (int i = 0; i < 256; i++) {
//         char dir_path[3];
//         snprintf(dir_path, 3, "%02x", i);
//         if (dir_fd >= 0) { close(dir_fd); }
//         dir_fd = openat(base_fd, dir_path, O_RDONLY|O_DIRECTORY);
//         if (dir_fd < 0) {
//             if (errno == ENOENT) {
//                 continue;
//             }
//             fprintf(stderr, "could not open dir %s/%s: %d\n", base_path, dir_path, errno);
//             blocks = -errno;
//             goto out;
//         }
//         for (;;) {
//             long read = syscall(SYS_getdents, dir_fd, buf, sizeof(buf));
//             if (read < 0) {
//                 fprintf(stderr, "could not read direntries in %s/%s: %d\n", base_path, dir_path, errno);
//                 blocks = -errno;
//                 goto out;
//             }
//             if (read == 0) { break; }
//             for (long bpos = 0; bpos < read; blocks++) {
//                 bpos += ((struct linux_dirent*)(buf+bpos))->d_reclen;
//             }
//         }
//     }
// out:
//     if (dir_fd >= 0) { close(dir_fd); }
//     if (base_fd >= 0) { close(base_fd); }
//     return blocks;
// }
import "C"

type blockServiceStats struct {
	blocksWritten uint64
	bytesWritten  uint64
	blocksErased  uint64
	blocksFetched uint64
	bytesFetched  uint64
	blocksChecked uint64
	bytesChecked  uint64
}

type atomicRenameDeleteReq struct {
	oldPath    string
	tmpPath    string
	newPath    string
	delete     bool
	deleteDone chan<- error
}

type env struct {
	bufPool            *lib.BufPool
	stats              map[msgs.BlockServiceId]*blockServiceStats
	counters           map[msgs.BlocksMessageKind]*lib.Timings
	conversionChannels map[msgs.BlockServiceId]chan atomicRenameDeleteReq
}

func BlockWriteProof(blockServiceId msgs.BlockServiceId, blockId msgs.BlockId, key cipher.Block) [8]byte {
	buf := bytes.NewBuffer([]byte{})
	// struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
	binary.Write(buf, binary.LittleEndian, uint64(blockServiceId))
	buf.Write([]byte{'W'})
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

// Do this in one go in C to minimize syscall overhead in golang,
// also I think the busy loop with syscalls in them was starving
// the goroutines waiting on network stuff, which in turn caused
// xmon connections to be dropped.
//
// Note that `findRunnable` picks up network stuff last:
// <https://github.com/golang/go/blob/1d45a7ef560a76318ed59dfdb178cecd58caf948/src/runtime/proc.go#L3222-L3242>
func countBlocks(basePath string) (uint64, error) {
	cBasePath := C.CString(basePath)
	defer C.free(unsafe.Pointer(cBasePath))
	blocks := C.count_blocks(cBasePath)
	if blocks < 0 {
		return 0, syscall.Errno(-blocks)
	}
	return uint64(blocks), nil
}

func updateBlockServiceInfoCapacity(
	log *lib.Logger,
	blockService *blockService,
) error {
	var statfs unix.Statfs_t
	if err := unix.Statfs(path.Join(blockService.path, "secret.key"), &statfs); err != nil {
		return err
	}
	capacityBytes := statfs.Blocks * uint64(statfs.Bsize)
	availableBytes := statfs.Bavail * uint64(statfs.Bsize)
	blockService.cachedInfo.CapacityBytes = capacityBytes
	blockService.cachedInfo.AvailableBytes = availableBytes
	return nil
}

// either updates `blockService`, or returns an error.
func updateBlockServiceInfoBlocks(
	log *lib.Logger,
	blockService *blockService,
) error {
	t := time.Now()
	log.Info("starting to count blocks for %v", blockService.cachedInfo.Id)
	var totalBlocks uint64
	for _, subDir := range []string{"with_crc", "without_crc"} {
		blocks, err := countBlocks(path.Join(blockService.path, subDir))
		if err != nil {
			return err
		}
		totalBlocks += blocks
	}
	blockService.cachedInfo.Blocks = totalBlocks
	log.Info("done counting blocks for %v in %v", blockService.cachedInfo.Id, time.Since(t))
	return nil
}

func convertBlockServiceFolderStructureToCrcBased(
	log *lib.Logger,
	blockService *blockService,
) error {
	t := time.Now()
	log.Info("starting to convert folder structure for %v", blockService.cachedInfo.Id)
	if err := os.Mkdir(path.Join(blockService.path, "without_crc"), 0755); err != nil && !os.IsExist(err) {
		return fmt.Errorf("failed to create 'without_crc' folder: %w", err)
	}
	if err := os.Mkdir(path.Join(blockService.path, "with_crc"), 0755); err != nil && !os.IsExist(err) {
		return fmt.Errorf("failed to create 'with_crc' folder: %w", err)
	}
	for i := 0; i < 256; i++ {
		dirPath := path.Join(blockService.path, fmt.Sprintf("%02x", i))
		newDirPath := path.Join(blockService.path, "without_crc", fmt.Sprintf("%02x", i))
		{
			stat, err := os.Stat(dirPath)
			if err != nil {
				if os.IsNotExist(err) {
					continue
				}
				return err
			}
			if !stat.IsDir() {
				return fmt.Errorf("expected %v to be a directory", dirPath)
			}
		}
		if err := os.Rename(dirPath, newDirPath); err != nil {
			return fmt.Errorf("failed to rename %v to %v: %w", dirPath, newDirPath, err)
		}
	}
	log.Info("done converting folder structure for %v in %v", blockService.cachedInfo.Id, time.Since(t))
	return nil
}

func initBlockServicesInfo(
	log *lib.Logger,
	addrs msgs.AddrsInfo,
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
		bs.cachedInfo.Addrs = addrs
		bs.cachedInfo.SecretKey = bs.key
		bs.cachedInfo.StorageClass = bs.storageClass
		bs.cachedInfo.FailureDomain.Name = failureDomain
		bs.cachedInfo.Path = bs.path
		closureBs := bs
		go func() {
			// only update if it isn't filled it in already from shuckle
			if closureBs.cachedInfo.Blocks == 0 {
				if err := updateBlockServiceInfoCapacity(log, closureBs); err != nil {
					panic(err)
				}
				if err := updateBlockServiceInfoBlocks(log, closureBs); err != nil {
					panic(err)
				}
			}
			wg.Done()
		}()
	}
	wg.Wait()
	log.ClearNC(alert)
	return nil
}

var maximumRegisterInterval time.Duration = time.Minute * 2

func registerPeriodically(
	log *lib.Logger,
	blockServices map[msgs.BlockServiceId]*blockService,
	deadBlockServices map[msgs.BlockServiceId]deadBlockService,
	shuckleAddress string,
) {
	req := msgs.RegisterBlockServicesReq{}
	alert := log.NewNCAlert(10 * time.Second)
	for {
		req.BlockServices = req.BlockServices[:0]
		for _, bs := range blockServices {
			if bs.couldNotUpdateInfoBlocks || bs.couldNotUpdateInfoCapacity {
				continue
			}
			req.BlockServices = append(req.BlockServices, bs.cachedInfo)
		}
		for _, bs := range deadBlockServices {
			req.BlockServices = append(req.BlockServices, bs.info)
		}
		log.Trace("registering with %+v", req)
		_, err := client.ShuckleRequest(log, nil, shuckleAddress, &req)
		if err != nil {
			log.RaiseNC(alert, "could not register block services with %+v: %v", shuckleAddress, err)
			time.Sleep(100 * time.Millisecond)
			continue
		}
		log.ClearNC(alert)
		waitFor := time.Duration(mrand.Uint64() % uint64(maximumRegisterInterval.Nanoseconds()))
		log.Info("registered with %v (%v alive, %v dead), waiting %v", shuckleAddress, len(blockServices), len(deadBlockServices), waitFor)
		time.Sleep(waitFor)
	}
}

func updateBlockServiceInfoBlocksForever(
	log *lib.Logger,
	blockServices map[msgs.BlockServiceId]*blockService,
) {
	for {

		for _, bs := range blockServices {
			if err := updateBlockServiceInfoBlocks(log, bs); err != nil {
				bs.couldNotUpdateInfoBlocks = true
				log.RaiseNC(&bs.couldNotUpdateInfoBlocksAlert, "could not count blocks for block service %v: %v", bs.cachedInfo.Id, err)
			} else {
				bs.couldNotUpdateInfoBlocks = false
				log.ClearNC(&bs.couldNotUpdateInfoBlocksAlert)
			}
		}
		time.Sleep(time.Minute) // so that we won't busy loop in tests etc
	}
}

func updateBlockServiceInfoCapacityForever(
	log *lib.Logger,
	blockServices map[msgs.BlockServiceId]*blockService,
) {
	for {
		for _, bs := range blockServices {
			if err := updateBlockServiceInfoCapacity(log, bs); err != nil {
				bs.couldNotUpdateInfoCapacity = true
				log.RaiseNC(&bs.couldNotUpdateInfoCapacityAlert, "could not get capacity for block service %v: %v", bs.cachedInfo.Id, err)
			} else {
				bs.couldNotUpdateInfoCapacity = false
				log.ClearNC(&bs.couldNotUpdateInfoCapacityAlert)
			}
		}
		time.Sleep(time.Minute) // so that we won't busy loop in tests etc
	}
}

func checkEraseCertificate(log *lib.Logger, blockServiceId msgs.BlockServiceId, cipher cipher.Block, req *msgs.EraseBlockReq) error {
	expectedMac, good := certificate.CheckBlockEraseCertificate(blockServiceId, cipher, req)
	if !good {
		log.RaiseAlert("bad MAC, got %v, expected %v", req.Certificate, expectedMac)
		return msgs.BAD_CERTIFICATE
	}
	return nil
}

func eraseBlock(log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId) error {
	blockPathNoCrc := path.Join(basePath, "without_crc", blockId.Path())
	blockPathWithCrc := path.Join(basePath, "with_crc", blockId.Path())

	blockPath := path.Join(basePath, blockId.Path())
	log.Debug("deleting block %v at path %v", blockId, blockPath)
	deleteDone := make(chan error)
	env.conversionChannels[blockServiceId] <- atomicRenameDeleteReq{
		oldPath:    blockPathNoCrc,
		tmpPath:    "",
		newPath:    blockPathWithCrc,
		delete:     true,
		deleteDone: deleteDone,
	}
	err := <-deleteDone
	if err != nil {
		log.RaiseAlert("error deleting block at path %v: %v", blockPath, err)
		return err
	}
	atomic.AddUint64(&env.stats[blockServiceId].blocksErased, 1)
	return nil
}

func writeBlocksResponse(log *lib.Logger, w io.Writer, resp msgs.BlocksResponse) error {
	log.Trace("writing response %T %+v", resp, resp)
	buf := bytes.NewBuffer([]byte{})
	if err := binary.Write(buf, binary.LittleEndian, msgs.BLOCKS_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if _, err := buf.Write([]byte{uint8(resp.BlocksResponseKind())}); err != nil {
		return err
	}
	if err := resp.Pack(buf); err != nil {
		return err
	}
	if _, err := w.Write(buf.Bytes()); err != nil {
		return err
	}
	return nil
}

func writeBlocksResponseError(log *lib.Logger, w io.Writer, err msgs.EggsError) error {
	log.Debug("writing blocks error %v", err)
	buf := bytes.NewBuffer([]byte{})
	if err := binary.Write(buf, binary.LittleEndian, msgs.BLOCKS_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if _, err := buf.Write([]byte{msgs.ERROR}); err != nil {
		return err
	}
	if err := binary.Write(buf, binary.LittleEndian, uint16(err)); err != nil {
		return err
	}
	if _, err := w.Write(buf.Bytes()); err != nil {
		return err
	}
	return nil
}

func sendFetchBlock(log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId, offset uint32, count uint32, conn *net.TCPConn, withCrc bool, crc msgs.Crc) error {
	if offset%msgs.EGGS_PAGE_SIZE != 0 {
		log.RaiseAlert("trying to read from offset other than page boundary")
		writeBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	if count%msgs.EGGS_PAGE_SIZE != 0 {
		log.RaiseAlert("trying to read count which is not a multiple of page size")
		writeBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	pageCount := count / msgs.EGGS_PAGE_SIZE
	offsetPageCount := offset / msgs.EGGS_PAGE_SIZE
	blockPathWithCrc := path.Join(basePath, "with_crc", blockId.Path())
	blockPathNoCrc := path.Join(basePath, "without_crc", blockId.Path())
	log.Debug("fetching block id %v at path %v", blockId, blockPathNoCrc)
	f, err := os.Open(blockPathNoCrc)
	openedWithCrc := false

	if os.IsNotExist(err) {
		f, err = os.Open(blockPathWithCrc)
		openedWithCrc = true
	}
	if errors.Is(err, syscall.ENODATA) {
		// see <internal-repo/issues/106>
		log.RaiseAlert("could not open block %v, got ENODATA, this probably means that the block/disk is gone", blockPathNoCrc)
		// return io error, downstream code will pick it up
		return syscall.EIO
	}

	if os.IsNotExist(err) {
		log.RaiseAlert("could not find block to fetch at path %v", blockPathWithCrc)
		return msgs.BLOCK_NOT_FOUND
	}

	if err != nil {
		return err
	}
	defer func() {
		if f != nil {
			f.Close()
		}
	}()
	fi, err := f.Stat()
	if err != nil {
		return err
	}
	filePageCount := uint32(0)
	if openedWithCrc {
		filePageCount = uint32(fi.Size()) / msgs.EGGS_PAGE_WITH_CRC_SIZE
	} else {
		filePageCount = uint32(fi.Size()) / msgs.EGGS_PAGE_SIZE
	}
	if offsetPageCount+pageCount > filePageCount {
		if pageCount > filePageCount {
			log.RaiseAlert("trying to read beyond EOF")
			writeBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
			return nil
		}
		log.RaiseAlert("was requested %v bytes, but only got %v", count, (filePageCount-pageCount)*msgs.EGGS_PAGE_SIZE)
		writeBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	if openedWithCrc {
		// we try to delete file in old format. this is best effort
		err = os.Remove(blockPathNoCrc)
		if err != nil && !os.IsNotExist(err) {
			log.RaiseAlert("could not remove old block %v, got error: %v", blockPathNoCrc, err)
		}
	} else if withCrc {
		tmpName, actualCrc, err := writeToTempWithCRC(log, env, blockServiceId, path.Join(basePath, "with_crc"), uint64(fi.Size()), f)
		if err != nil {
			log.RaiseAlert("could not convert file %v to crc format, got error: %v", blockPathNoCrc, err)
			writeBlocksResponseError(log, conn, msgs.BLOCK_IO_ERROR_FILE)
			return nil
		}
		if crc != msgs.Crc(actualCrc) {
			os.Remove(tmpName)
			log.RaiseAlert("file %v has wrong CRC, expected: %x, got: %x", blockPathNoCrc, crc, actualCrc)
			writeBlocksResponseError(log, conn, msgs.BAD_BLOCK_CRC)
			return nil
		}
		f.Close()
		f = nil
		f, err = os.Open(tmpName)
		if err != nil {
			os.Remove(tmpName)
			log.RaiseAlert("could not open file %v, got error: %v", tmpName, err)
			writeBlocksResponseError(log, conn, msgs.BLOCK_IO_ERROR_FILE)
			return nil
		}
		openedWithCrc = true
		// delegate rename, or delete tmp if channel full
		select {
		case env.conversionChannels[blockServiceId] <- atomicRenameDeleteReq{
			oldPath: blockPathNoCrc,
			tmpPath: tmpName,
			newPath: blockPathWithCrc,
			delete:  false,
		}:
		default:
			os.Remove(tmpName)
		}
	}

	if (openedWithCrc && withCrc) || (!openedWithCrc && !withCrc) {
		if openedWithCrc {
			offset = offsetPageCount * msgs.EGGS_PAGE_WITH_CRC_SIZE
			count = pageCount * msgs.EGGS_PAGE_WITH_CRC_SIZE
		}

		if _, err := f.Seek(int64(offset), 0); err != nil {
			return err
		}
		var resp msgs.BlocksResponse = &msgs.FetchBlockResp{}
		if withCrc {
			resp = &msgs.FetchBlockWithCrcResp{}
		}

		if err := writeBlocksResponse(log, conn, resp); err != nil {
			return err
		}
		lf := io.LimitedReader{
			R: f,
			N: int64(count),
		}
		read, err := conn.ReadFrom(&lf)
		if err != nil {
			return err
		}
		if read != int64(count) {
			log.RaiseAlert("expected to read at least %v bytes, but only got %v for file %q", count, read, blockPathNoCrc)
			return msgs.INTERNAL_ERROR
		}
	} else {
		// the only remaining case is that we have a file in new format and client wants old format
		offset = offsetPageCount * msgs.EGGS_PAGE_WITH_CRC_SIZE
		if _, err := f.Seek(int64(offset), 0); err != nil {
			return err
		}
		if err := writeBlocksResponse(log, conn, &msgs.FetchBlockResp{}); err != nil {
			return err
		}
		for count > 0 {
			toRead := msgs.EGGS_PAGE_SIZE
			if count < msgs.EGGS_PAGE_SIZE {
				count = msgs.EGGS_PAGE_SIZE
			}
			lf := io.LimitedReader{
				R: f,
				N: int64(toRead),
			}
			read, err := conn.ReadFrom(&lf)
			if err != nil {
				return err
			}
			if read != int64(toRead) {
				log.RaiseAlert("expected to read at least %v bytes, but only got %v for file %q", toRead, read, blockPathWithCrc)
				return msgs.INTERNAL_ERROR
			}
			f.Seek(4, 1) // skip crc32
			count -= uint32(read)
		}
	}

	s := env.stats[blockServiceId]
	atomic.AddUint64(&s.blocksFetched, 1)
	atomic.AddUint64(&s.bytesFetched, uint64(count))
	return nil
}

func getPhysicalBlockSize(path string) (int, error) {
	fs := syscall.Statfs_t{}
	err := syscall.Statfs(path, &fs)
	if err != nil {
		return 0, err
	}
	return int(fs.Bsize), nil
}

func checkBlock(log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId, expectedSize uint32, crc msgs.Crc, conn *net.TCPConn) error {
	blockPathNoCrc := path.Join(basePath, "without_crc", blockId.Path())
	blockPathWithCrc := path.Join(basePath, "with_crc", blockId.Path())
	blockPath := path.Join(basePath, blockId.Path())
	log.Debug("checking block id %v at path %v", blockId, blockPath)

	// we try to open no crc first as this file is deleted only after file with crc is created
	// if it doesn't exist in both places then it really is deleted
	f, err := os.Open(blockPathNoCrc)
	fileWithCrc := false
	if os.IsNotExist(err) {
		f, err = os.Open(blockPathWithCrc)
		fileWithCrc = true
	}

	if errors.Is(err, syscall.ENODATA) {
		// see <internal-repo/issues/106>
		log.RaiseAlert("could not open block %v, got ENODATA, this probably means that the block/disk is gone", blockPath)
		// return io error, downstream code will pick it up
		return syscall.EIO
	}
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
		log.RaiseAlert("could not read file %v : %v", blockPath, err)
		return err
	}
	actualSize := uint32(fi.Size())
	actualDataSize := actualSize
	processChunkSize := int(msgs.EGGS_PAGE_SIZE)
	if fileWithCrc {
		if actualSize%msgs.EGGS_PAGE_WITH_CRC_SIZE != 0 {
			log.RaiseAlert("size %v for block %v, not multiple of EGGS_PAGE_WITH_CRC_SIZE", actualSize, blockPath)
			return msgs.BAD_BLOCK_CRC
		}
		actualDataSize = (actualDataSize / msgs.EGGS_PAGE_WITH_CRC_SIZE) * msgs.EGGS_PAGE_SIZE
		processChunkSize = int(msgs.EGGS_PAGE_WITH_CRC_SIZE)
	} else {
		if actualSize%msgs.EGGS_PAGE_SIZE != 0 {
			log.RaiseAlert("size %v for block %v, not multiple of EGGS_PAGE__SIZE", actualSize, blockPath)
			return msgs.BAD_BLOCK_CRC
		}
	}

	buf := env.bufPool.Get(1 << 20)
	defer env.bufPool.Put(buf)
	cursor := uint32(0)
	remainingData := 0
	actualCrc := uint32(0)
	for cursor < actualSize {
		read, err := f.Read((*buf)[remainingData:])
		if err != nil {
			log.RaiseAlert("could not read file %v : %v", blockPath, err)
			return msgs.BAD_BLOCK_CRC
		}
		remainingData += read
		cursor += uint32(read)
		if remainingData < processChunkSize {
			continue
		}
		numAvailableChunks := remainingData / processChunkSize
		for i := 0; i < numAvailableChunks; i++ {
			actualPageCrc := crc32c.Sum(0, (*buf)[i*processChunkSize:i*processChunkSize+int(msgs.EGGS_PAGE_SIZE)])
			if fileWithCrc {
				storedPageCrc := binary.LittleEndian.Uint32((*buf)[i*processChunkSize+int(msgs.EGGS_PAGE_SIZE) : i*processChunkSize+int(msgs.EGGS_PAGE_WITH_CRC_SIZE)])
				if storedPageCrc != actualPageCrc {
					// pageCrc mismatch. We don't care about the rest of the file, so we return here
					log.RaiseAlert("page crc mismatch for block %v", blockPath)
					return msgs.BAD_BLOCK_CRC
				}
			}
			actualCrc = crc32c.Append(actualCrc, actualPageCrc, int(msgs.EGGS_PAGE_SIZE))
		}
		copy((*buf)[:], (*buf)[numAvailableChunks*processChunkSize:remainingData])
		remainingData -= numAvailableChunks * processChunkSize
	}
	if remainingData > 0 {
		panic("unexpected data left")
	}
	if actualDataSize != expectedSize {
		log.RaiseAlert("expected size %v for block %v, got size %v instead", expectedSize, blockPath, actualDataSize)
		return msgs.BAD_BLOCK_CRC
	}
	s := env.stats[blockServiceId]
	atomic.AddUint64(&s.blocksChecked, 1)
	atomic.AddUint64(&s.bytesChecked, uint64(expectedSize))
	if msgs.Crc(actualCrc) != crc {
		log.RaiseAlert("expected crc %v for block %v, got %v instead", crc, blockPath, msgs.Crc(actualCrc))
		return msgs.BAD_BLOCK_CRC
	}
	if err := writeBlocksResponse(log, conn, &msgs.CheckBlockResp{}); err != nil {
		return err
	}
	return nil
}

func checkWriteCertificate(log *lib.Logger, cipher cipher.Block, blockServiceId msgs.BlockServiceId, req *msgs.WriteBlockReq) error {
	expectedMac, good := certificate.CheckBlockWriteCertificate(cipher, blockServiceId, req)
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

func writeToTempWithCRC(
	log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, size uint64, reader io.Reader,
) (tmpName string, crc uint32, err error) {
	if size%uint64(msgs.EGGS_PAGE_SIZE) != 0 {
		return "", 0, msgs.BLOCK_SIZE_NOT_MULTIPLE_OF_PAGE_SIZE
	}
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
	pageCRC := uint32(0)
	for {
		log.Debug("size=%v readSoFar=%v", size, readSoFar)
		buf := *bufPtr
		if uint64(len(buf)) > size-readSoFar {
			buf = buf[:int(size-readSoFar)]
		}
		var read int
		read, err = reader.Read(buf)
		if err != nil {
			return tmpName, crc, err
		}

		begin := uint64(0)
		for begin < uint64(read) {
			bytesToPageEnd := uint64(msgs.EGGS_PAGE_SIZE) - (readSoFar % uint64(msgs.EGGS_PAGE_SIZE))
			end := begin + bytesToPageEnd
			if end > uint64(read) {
				end = uint64(read)
			}
			pageCRC = crc32c.Sum(pageCRC, buf[begin:end])

			// Write the data chunk
			if _, err := f.Write(buf[begin:end]); err != nil {
				return tmpName, crc, err
			}

			readSoFar += end - begin
			begin = end
			// If at page boundary write out the CRC
			if readSoFar%uint64(msgs.EGGS_PAGE_SIZE) == 0 {
				if err := binary.Write(f, binary.LittleEndian, pageCRC); err != nil {
					return tmpName, crc, err
				}
				crc = crc32c.Append(crc, pageCRC, int(msgs.EGGS_PAGE_SIZE))
				pageCRC = uint32(0)
			}
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
	basePath = path.Join(basePath, "with_crc")
	filePath := path.Join(basePath, blockId.Path())
	log.Debug("writing block %v at path %v", blockId, basePath)
	if err := os.Mkdir(path.Dir(filePath), 0777); err != nil && !os.IsExist(err) {
		return err
	}
	tmpName, crc, err := writeToTempWithCRC(log, env, blockServiceId, basePath, uint64(size), conn)
	if err != nil {
		return err
	}
	if msgs.Crc(crc) != expectedCrc {
		os.Remove(tmpName)
		log.RaiseAlert("bad crc for block %v, got %v in req, computed %v", blockId, expectedCrc, msgs.Crc(crc))
		writeBlocksResponseError(log, conn, msgs.BAD_BLOCK_CRC)
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
	if err := writeBlocksResponse(log, conn, &msgs.WriteBlockResp{Proof: BlockWriteProof(blockServiceId, blockId, cipher)}); err != nil {
		return err
	}
	atomic.AddUint64(&env.stats[blockServiceId].blocksWritten, 1)
	return nil
}

func testWrite(
	log *lib.Logger, env *env, blockServiceId msgs.BlockServiceId, basePath string, size uint64, conn *net.TCPConn,
) error {
	basePath = path.Join(basePath, "without_crc")
	tmpName, _, err := writeToTemp(log, env, blockServiceId, basePath, size, conn)
	if err != nil {
		return err
	}
	os.Remove(tmpName)
	if err := writeBlocksResponse(log, conn, &msgs.TestWriteResp{}); err != nil {
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
	blockServices map[msgs.BlockServiceId]*blockService,
	deadBlockServices map[msgs.BlockServiceId]deadBlockService,
	conn *net.TCPConn,
	lastError *error,
	blockServiceId msgs.BlockServiceId,
	req msgs.BlocksMessageKind,
	err error,
) bool {
	defer func() {
		*lastError = err
	}()

	if err == io.EOF {
		log.Debug("got EOF from %v, terminating", conn.RemoteAddr())
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
		log.Debug("got timeout from %v, terminating", conn.RemoteAddr())
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

	if errors.Is(err, syscall.EIO) && blockServiceId != 0 {
		blockService := blockServices[blockServiceId]
		if blockService.couldNotUpdateInfoBlocks || blockService.couldNotUpdateInfoCapacity {
			err = msgs.BLOCK_IO_ERROR_DEVICE
		} else {
			err = msgs.BLOCK_IO_ERROR_FILE
		}
		atomic.AddUint64(&blockService.ioErrors, 1)
		log.RaiseAlertStack("", 1, "got unxpected IO error %v from %v for req kind %v, block service %v, will return %v, previous error: %v", err, conn.RemoteAddr(), req, blockServiceId, err, *lastError)
		writeBlocksResponseError(log, conn, err.(msgs.EggsError))
		return false
	}

	// In general, refuse to service requests for block services that we
	// don't have. In the case of checking a dead block, we don't emit
	// an alert, since it's expected when the scrubber is running.
	// Similarly, reading blocks from old block services can happen if
	// the cached span structure is used in the kmod.
	if _, isDead := deadBlockServices[blockServiceId]; isDead && (req == msgs.CHECK_BLOCK || req == msgs.FETCH_BLOCK || req == msgs.FETCH_BLOCK_WITH_CRC) {
		log.Info("got fetch/check block request for dead block service %v", blockServiceId)
		if eggsErr, isEggsErr := err.(msgs.EggsError); isEggsErr {
			writeBlocksResponseError(log, conn, eggsErr)
		}
		return true
	}

	// we always raise an alert since this is almost always bad news in the block service
	log.RaiseAlertStack("", 1, "got unexpected error %v from %v for req kind %v, block service %v, previous error %v", err, conn.RemoteAddr(), req, blockServiceId, *lastError)

	if eggsErr, isEggsErr := err.(msgs.EggsError); isEggsErr {
		writeBlocksResponseError(log, conn, eggsErr)
		// kill the connection in bad cases
		// BLOCK_NOT_FOUND + WriteBlock is bad because we get
		// data right after causing malformed request errors.
		// We might want want to do this for all WriteBlock
		// cases.
		if eggsErr == msgs.MALFORMED_REQUEST || (req == msgs.WRITE_BLOCK && eggsErr == msgs.BAD_BLOCK_CRC) || (req == msgs.WRITE_BLOCK && eggsErr == msgs.BLOCK_NOT_FOUND) {
			log.Info("not preserving connection from %v after err %v", conn.RemoteAddr(), err)
			return false
		} else {
			log.Info("preserving connection from %v after err %v", conn.RemoteAddr(), err)
			return true
		}
	} else {
		// attempt to say goodbye, ignore errors
		writeBlocksResponseError(log, conn, msgs.INTERNAL_ERROR)
		log.Info("tearing down connection from %v after internal error %v", conn.RemoteAddr(), err)
		return false
	}
}

type deadBlockService struct {
	cipher cipher.Block
	// we store the last seen data from shuckle so that we keep registering that
	info msgs.RegisterBlockServiceInfo
}

func readBlocksRequest(
	log *lib.Logger,
	r io.Reader,
) (msgs.BlockServiceId, msgs.BlocksRequest, error) {
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return 0, nil, err
	}
	if protocol != msgs.BLOCKS_REQ_PROTOCOL_VERSION {
		log.RaiseAlert("bad blocks protocol, expected %v, got %v", msgs.BLOCKS_REQ_PROTOCOL_VERSION, protocol)
		return 0, nil, msgs.MALFORMED_REQUEST
	}
	var blockServiceId uint64
	if err := binary.Read(r, binary.LittleEndian, &blockServiceId); err != nil {
		return 0, nil, err
	}
	var kindByte [1]byte
	if _, err := io.ReadFull(r, kindByte[:]); err != nil {
		return 0, nil, err
	}
	kind := msgs.BlocksMessageKind(kindByte[0])
	var req msgs.BlocksRequest
	switch kind {
	case msgs.ERASE_BLOCK:
		req = &msgs.EraseBlockReq{}
	case msgs.FETCH_BLOCK:
		req = &msgs.FetchBlockReq{}
	case msgs.FETCH_BLOCK_WITH_CRC:
		req = &msgs.FetchBlockWithCrcReq{}
	case msgs.WRITE_BLOCK:
		req = &msgs.WriteBlockReq{}
	case msgs.TEST_WRITE:
		req = &msgs.TestWriteReq{}
	case msgs.CHECK_BLOCK:
		req = &msgs.CheckBlockReq{}
	default:
		log.RaiseAlert("bad blocks request kind %v", kind)
		return 0, nil, msgs.MALFORMED_REQUEST
	}
	if err := req.Unpack(r); err != nil {
		return 0, nil, err
	}
	return msgs.BlockServiceId(blockServiceId), req, nil
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
	blockServiceId, req, err := readBlocksRequest(log, conn)
	if err != nil {
		return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, 0, 0, err)
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
		if deadBlockService, isDead := deadBlockServices[blockServiceId]; isDead {
			if whichReq, isErase := req.(*msgs.EraseBlockReq); isErase {
				log.Debug("servicing erase block request for dead block service from %v", conn.RemoteAddr())
				resp := msgs.EraseBlockResp{
					Proof: certificate.BlockEraseProof(blockServiceId, whichReq.BlockId, deadBlockService.cipher),
				}
				if err := writeBlocksResponse(log, conn, &resp); err != nil {
					log.Info("could not send blocks response to %v: %v", conn.RemoteAddr(), err)
					return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
				}
				atomic.AddUint64(&env.stats[blockServiceId].blocksErased, 1)
				return true
			}
		}
		// In general, refuse to service requests for block services that we
		// don't have.
		return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, msgs.BLOCK_SERVICE_NOT_FOUND)
	}
	switch whichReq := req.(type) {
	case *msgs.EraseBlockReq:
		if err := checkEraseCertificate(log, blockServiceId, blockService.cipher, whichReq); err != nil {
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
		cutoffTime := msgs.EggsTime(uint64(whichReq.BlockId)).Time().Add(futureCutoff)
		now := time.Now()
		if now.Before(cutoffTime) {
			log.RaiseAlert("block %v is too recent to be deleted (now=%v, cutoffTime=%v)", whichReq.BlockId, now, cutoffTime)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, msgs.BLOCK_TOO_RECENT_FOR_DELETION)
		}
		if err := eraseBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId); err != nil {
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}

		resp := msgs.EraseBlockResp{
			Proof: certificate.BlockEraseProof(blockServiceId, whichReq.BlockId, blockService.cipher),
		}
		if err := writeBlocksResponse(log, conn, &resp); err != nil {
			log.Info("could not send blocks response to %v: %v", conn.RemoteAddr(), err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
	case *msgs.FetchBlockReq:
		if err := sendFetchBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId, whichReq.Offset, whichReq.Count, conn, false, 0); err != nil {
			log.Info("could not send block response to %v: %v", conn.RemoteAddr(), err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
	case *msgs.FetchBlockWithCrcReq:
		if err := sendFetchBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId, whichReq.Offset, whichReq.Count, conn, true, whichReq.BlockCrc); err != nil {
			log.Info("could not send block response to %v: %v", conn.RemoteAddr(), err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
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
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, msgs.BLOCK_TOO_OLD_FOR_WRITE)
		}
		if err := checkWriteCertificate(log, blockService.cipher, blockServiceId, whichReq); err != nil {
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
		if whichReq.Size > MAX_OBJECT_SIZE {
			log.RaiseAlert("block %v exceeds max object size: %v > %v", whichReq.BlockId, whichReq.Size, MAX_OBJECT_SIZE)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, msgs.BLOCK_TOO_BIG)
		}
		if err := writeBlock(log, env, blockServiceId, blockService.cipher, blockService.path, whichReq.BlockId, whichReq.Crc, whichReq.Size, conn); err != nil {
			log.Info("could not write block: %v", err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
	case *msgs.CheckBlockReq:
		if err := checkBlock(log, env, blockServiceId, blockService.path, whichReq.BlockId, whichReq.Size, whichReq.Crc, conn); err != nil {
			log.Info("checking block failed, conn %v, err %v", conn.RemoteAddr(), err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
	case *msgs.TestWriteReq:
		if err := testWrite(log, env, blockServiceId, blockService.path, whichReq.Size, conn); err != nil {
			log.Info("could not perform test write: %v", err)
			return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, err)
		}
	default:
		return handleRequestError(log, blockServices, deadBlockServices, conn, lastError, blockServiceId, kind, fmt.Errorf("bad request type %T", req))
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
		keyCrc := crc32c.Sum(0, key[:])
		if err := binary.Write(keyFile, binary.LittleEndian, keyCrc); err != nil {
			panic(err)
		}
	} else if read != 16 {
		panic(fmt.Errorf("short secret key (%v rather than 16 bytes)", read))
	} else {
		expectedKeyCrc := crc32c.Sum(0, key[:])
		var actualKeyCrc uint32
		if err := binary.Read(keyFile, binary.LittleEndian, &actualKeyCrc); err != nil {
			panic(err)
		}
		if expectedKeyCrc != actualKeyCrc {
			panic(fmt.Errorf("expected crc %v, got %v", msgs.Crc(expectedKeyCrc), msgs.Crc(actualKeyCrc)))
		}
	}
	return key
}

type diskStats struct {
	readMs       uint64
	writeMs      uint64
	weightedIoMs uint64
}

func getDiskStats(log *lib.Logger, statsPath string) (map[string]diskStats, error) {
	file, err := os.Open(statsPath)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)

	ret := make(map[string]diskStats)
	for scanner.Scan() {
		line := scanner.Text()
		fields := strings.Fields(line)
		if len(fields) < 11 {
			log.RaiseAlert("malformed disk entry in %v: %v", statsPath, line)
			continue
		}
		devId := fmt.Sprintf("%s:%s", fields[0], fields[1])
		// https://www.kernel.org/doc/html/v5.4/admin-guide/iostats.html
		readMs, err := strconv.ParseUint(fields[6], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("failed to parse reads ms from %s", line)
		}
		writeMs, err := strconv.ParseUint(fields[10], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("failed to parse write ms from %s", line)
		}
		weightedIoMs, err := strconv.ParseUint(fields[11], 10, 64)
		if err != nil {
			return nil, fmt.Errorf("failed to parse weighted IO ms from %s", line)
		}

		ret[devId] = diskStats{
			readMs:       readMs,
			writeMs:      writeMs,
			weightedIoMs: weightedIoMs,
		}
	}
	return ret, nil
}

func sendMetrics(log *lib.Logger, env *env, blockServices map[msgs.BlockServiceId]*blockService, failureDomain string) {
	metrics := lib.MetricsBuilder{}
	rand := wyhash.New(rand.Uint64())
	alert := log.NewNCAlert(10 * time.Second)
	for {
		diskMetrics, err := getDiskStats(log, "/proc/diskstats")
		if err != nil {
			log.RaiseAlert("failed reading diskstats: %v", err)
		}
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

			metrics.Measurement("eggsfs_blocks_check")
			metrics.Tag("blockservice", bsId.String())
			metrics.Tag("failuredomain", failureDomain)
			metrics.FieldU64("blocks", bsStats.blocksChecked)
			metrics.FieldU64("bytes", bsStats.bytesChecked)
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
			metrics.FieldU64("io_errors", bsInfo.ioErrors)
			dm, found := diskMetrics[bsInfo.devId]
			if found {
				metrics.FieldU64("read_ms", dm.readMs)
				metrics.FieldU64("write_ms", dm.writeMs)
				metrics.FieldU64("weighted_io_ms", dm.weightedIoMs)
			}
			metrics.Timestamp(now)
		}
		err = lib.SendMetrics(metrics.Payload())
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
	path                            string
	devId                           string
	key                             [16]byte
	cipher                          cipher.Block
	storageClass                    msgs.StorageClass
	cachedInfo                      msgs.RegisterBlockServiceInfo
	couldNotUpdateInfoBlocks        bool
	couldNotUpdateInfoBlocksAlert   lib.XmonNCAlert
	couldNotUpdateInfoCapacity      bool
	couldNotUpdateInfoCapacityAlert lib.XmonNCAlert
	ioErrors                        uint64
}

func getMountsInfo(log *lib.Logger, mountsPath string) (map[string]string, error) {
	file, err := os.Open(mountsPath)
	if err != nil {
		return nil, err
	}
	defer file.Close()
	scanner := bufio.NewScanner(file)

	ret := make(map[string]string)
	for scanner.Scan() {
		line := scanner.Text()
		mountFields := strings.Fields(line)
		if len(mountFields) < 11 {
			log.RaiseAlert("malformed mount in %v: %v", mountsPath, line)
			continue
		}
		path := mountFields[4]
		// should be major:minor of the mounted disk
		ret[path] = mountFields[2]
	}
	return ret, nil
}

func normalBlockServiceInfo(info *msgs.BlockServiceInfo) *msgs.RegisterBlockServiceInfo {
	// Everything else is filled in by `initBlockServicesInfo`
	return &msgs.RegisterBlockServiceInfo{
		CapacityBytes:  info.CapacityBytes,
		AvailableBytes: info.AvailableBytes,
		Blocks:         info.Blocks,
	}
}

func deadBlockServiceInfo(info *msgs.BlockServiceInfo) *msgs.RegisterBlockServiceInfo {
	// We just replicate everything from the cached one, forever -- but no flags.
	return &msgs.RegisterBlockServiceInfo{
		Id:             info.Id,
		Addrs:          info.Addrs,
		StorageClass:   info.StorageClass,
		FailureDomain:  info.FailureDomain,
		SecretKey:      info.SecretKey,
		CapacityBytes:  info.CapacityBytes,
		AvailableBytes: info.AvailableBytes,
		Blocks:         info.Blocks,
		Path:           info.Path,
		// just to be explicit about it
		Flags:     0,
		FlagsMask: 0,
	}
}

func main() {
	flag.Usage = usage
	failureDomainStr := flag.String("failure-domain", "", "Failure domain")
	futureCutoff := flag.Duration("future-cutoff", DEFAULT_FUTURE_CUTOFF, "")
	addr1 := flag.String("addr-1", "", "First address to bind to, and that will be advertised to shuckle.")
	addr2 := flag.String("addr-2", "", "Second address to bind to, and that will be advertised to shuckle. Optional.")
	verbose := flag.Bool("verbose", false, "")
	xmon := flag.String("xmon", "", "Xmon environment (empty, prod, qa)")
	trace := flag.Bool("trace", false, "")
	logFile := flag.String("log-file", "", "If empty, stdout")
	shuckleAddress := flag.String("shuckle", "", "Shuckle address (host:port).")
	profileFile := flag.String("profile-file", "", "")
	syslog := flag.Bool("syslog", false, "")
	connectionTimeout := flag.Duration("connection-timeout", 10*time.Minute, "")
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

	if *shuckleAddress == "" {
		fmt.Fprintf(os.Stderr, "You need to specify -shuckle.\n")
		os.Exit(2)
	}

	if *addr1 == "" {
		fmt.Fprintf(os.Stderr, "-addr-1 must be provided.\n\n")
		usage()
		os.Exit(2)
	}

	ownIp1, port1, err := lib.ParseIPV4Addr(*addr1)
	if err != nil {
		panic(err)
	}
	var ownIp2 [4]byte
	var port2 uint16
	if *addr2 != "" {
		ownIp2, port2, err = lib.ParseIPV4Addr(*addr2)
		if err != nil {
			panic(err)
		}
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
		AppInstance:      "eggsblocks",
		AppType:          "restech_eggsfs.daytime",
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
	log.Info("  addr1 = '%v'", *addr1)
	log.Info("  addr2 = '%v'", *addr2)
	log.Info("  logLevel = %v", level)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  shuckleAddress = '%v'", *shuckleAddress)
	log.Info("  connectionTimeout = %v", *connectionTimeout)

	mountsInfo, err := getMountsInfo(log, "/proc/self/mountinfo")
	if err != nil {
		log.RaiseAlert("Disk stats for mounted paths will not be collected due to failure collecting mount info: %v", err)
	}

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
		devId, found := mountsInfo[dir]
		if !found {
			devId = ""
		}
		blockServices[id] = &blockService{
			path:                            dir,
			devId:                           devId,
			key:                             key,
			cipher:                          cipher,
			storageClass:                    storageClass,
			couldNotUpdateInfoBlocksAlert:   *log.NewNCAlert(time.Second),
			couldNotUpdateInfoCapacityAlert: *log.NewNCAlert(time.Second),
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
		var shuckleBlockServices []msgs.BlockServiceInfo
		{
			alert := log.NewNCAlert(0)
			log.RaiseNC(alert, "fetching block services")
			timeouts := lib.NewReqTimeouts(client.DefaultShuckleTimeout.Initial, client.DefaultShuckleTimeout.Max, 0, client.DefaultShuckleTimeout.Growth, client.DefaultShuckleTimeout.Jitter)
			resp, err := client.ShuckleRequest(log, timeouts, *shuckleAddress, &msgs.AllBlockServicesReq{})
			if err != nil {
				panic(fmt.Errorf("could not request block services from shuckle: %v", err))
			}
			log.ClearNC(alert)
			shuckleBlockServices = resp.(*msgs.AllBlockServicesResp).BlockServices
		}
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
					info:   *deadBlockServiceInfo(bs),
				}
			}
			// we can't have a decommissioned block service
			if weHaveBs && isDecommissioned {
				log.RaiseAlert("We have block service %v, which is decommissioned according to shuckle. We will treat it as if it doesn't exist.", bs.Id)
				delete(blockServices, bs.Id)
				deadBlockServices[bs.Id] = deadBlockService{
					cipher: ourBs.cipher,
					info:   *deadBlockServiceInfo(bs),
				}
			}
			// fill in information from shuckle, if it's recent enough
			if weHaveBs && time.Since(bs.LastSeen.Time()) < maximumRegisterInterval*2 {
				// everything else is filled in by initBlockServicesInfo
				ourBs.cachedInfo = msgs.RegisterBlockServiceInfo{
					CapacityBytes:  bs.CapacityBytes,
					AvailableBytes: bs.AvailableBytes,
					Blocks:         bs.Blocks,
				}
			}
		}
	}

	for _, blockService := range blockServices {
		if err := convertBlockServiceFolderStructureToCrcBased(log, blockService); err != nil {
			panic(err)
		}
	}

	listener1, err := net.Listen("tcp4", fmt.Sprintf("%v:%v", net.IP(ownIp1[:]), port1))
	if err != nil {
		panic(err)
	}
	defer listener1.Close()

	log.Info("running 1 on %v", listener1.Addr())
	actualPort1 := uint16(listener1.Addr().(*net.TCPAddr).Port)

	var listener2 net.Listener
	var actualPort2 uint16
	if *addr2 != "" {
		listener2, err = net.Listen("tcp4", fmt.Sprintf("%v:%v", net.IP(ownIp2[:]), port2))
		if err != nil {
			panic(err)
		}
		defer listener2.Close()

		log.Info("running 2 on %v", listener2.Addr())
		actualPort2 = uint16(listener2.Addr().(*net.TCPAddr).Port)
	}

	initBlockServicesInfo(log, msgs.AddrsInfo{msgs.IpPort{ownIp1, actualPort1}, msgs.IpPort{ownIp2, actualPort2}}, failureDomain, blockServices)
	log.Info("finished updating block service info, will now start")

	terminateChan := make(chan any)

	bufPool := lib.NewBufPool()

	env := &env{
		bufPool:            bufPool,
		stats:              make(map[msgs.BlockServiceId]*blockServiceStats),
		conversionChannels: make(map[msgs.BlockServiceId]chan atomicRenameDeleteReq),
	}
	for bsId := range blockServices {
		env.stats[bsId] = &blockServiceStats{}
		// we want to handle short bursts of requests
		env.conversionChannels[bsId] = make(chan atomicRenameDeleteReq, 1000)
		go func(c <-chan atomicRenameDeleteReq) {
			defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
			for {
				req := <-c
				if req.delete {
					err := os.Remove(req.newPath)
					if err == nil {
						dir, err := os.Open(filepath.Dir(req.newPath))
						if err != nil {
							req.deleteDone <- err
							continue
						}
						// sync directory before returning response
						if err := dir.Sync(); err != nil {
							dir.Close()
							req.deleteDone <- err
							continue
						}
					} else if !os.IsNotExist(err) {
						req.deleteDone <- err
						continue
					}

					err = os.Remove(req.oldPath)
					if err == nil {
						dir, err := os.Open(filepath.Dir(req.oldPath))
						if err != nil {
							req.deleteDone <- err
							continue
						}
						// sync directory before returning response
						if err := dir.Sync(); err != nil {
							dir.Close()
							req.deleteDone <- err
							continue
						}
					} else if !os.IsNotExist(err) {
						req.deleteDone <- err
						continue
					}
					req.deleteDone <- nil
				} else {
					err := os.Rename(req.tmpPath, req.newPath)
					if err != nil {
						log.ErrorNoAlert("failed to rename block from %v to %v", req.tmpPath, req.newPath)
						// best effort to remove tmp file
						os.Remove(req.tmpPath)
						continue
					}
					dir, err := os.Open(filepath.Dir(req.newPath))
					if err != nil {
						req.deleteDone <- err
						continue
					}
					// sync directory before returning response
					if err := dir.Sync(); err != nil {
						dir.Close()
						req.deleteDone <- err
						continue
					}
					// best effort to remove old file
					os.Remove(req.oldPath)
				}
			}
		}(env.conversionChannels[bsId])
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
		registerPeriodically(log, blockServices, deadBlockServices, *shuckleAddress)
	}()

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		updateBlockServiceInfoBlocksForever(log, blockServices)
	}()

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		updateBlockServiceInfoCapacityForever(log, blockServices)
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
