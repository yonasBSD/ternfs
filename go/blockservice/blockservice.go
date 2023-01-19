package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/sha1"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"path"
	"runtime/debug"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
)

var stacktraceLock sync.Mutex

func handleRecover(log eggs.LogLevels, terminateChan chan any, err any) {
	if err != nil {
		log.RaiseAlert(err.(error))
		stacktraceLock.Lock()
		fmt.Fprintf(os.Stderr, "PANIC %v. Stacktrace:\n", err)
		for _, line := range strings.Split(string(debug.Stack()), "\n") {
			fmt.Fprintf(os.Stderr, "%s\n", line)
		}
		stacktraceLock.Unlock()
		terminateChan <- err
	}
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

func registerPeriodically(log eggs.LogLevels, port uint16, storageClass uint8, failureDomain [16]byte, secretKey [16]byte, shuckleAddress string) {
	for {
		req := msgs.RegisterBlockServiceReq{}
		req.BlockService.Id = blockServiceIdFromKey(secretKey)
		req.BlockService.Ip = [4]byte{127, 0, 0, 1}
		req.BlockService.SecretKey = secretKey
		req.BlockService.Port = port
		req.BlockService.StorageClass = storageClass
		req.BlockService.FailureDomain = failureDomain

		_, err := eggs.ShuckleRequest(log, shuckleAddress, &req)
		if err != nil {
			log.RaiseAlert(fmt.Errorf("could not register block service %v with %v: %w", req.BlockService.Id, shuckleAddress, err))
			time.Sleep(10 * time.Millisecond)
			continue
		}
		log.Info("registered with %v, waiting a minute", shuckleAddress)
		time.Sleep(time.Minute)
	}
}

func deserEraseBlock(blockServiceId msgs.BlockServiceId, cipher cipher.Block, kind byte, r io.Reader) (blockId msgs.BlockId, err error) {
	// Extract blockId and MAC
	if err := binary.Read(r, binary.LittleEndian, (*uint64)(&blockId)); err != nil {
		return 0, err
	}
	var mac [8]byte
	if _, err := io.ReadFull(r, mac[:]); err != nil {
		return 0, err
	}
	// compute mac
	w := bytes.NewBuffer([]byte{})
	binary.Write(w, binary.LittleEndian, uint64(blockServiceId))
	w.Write([]byte{kind})
	binary.Write(w, binary.LittleEndian, uint64(blockId))
	expectedMac := eggs.CBCMAC(cipher, w.Bytes())
	if expectedMac != mac {
		return 0, fmt.Errorf("bad MAC, got %v, expected %v", mac, expectedMac)
	}
	return blockId, nil
}

func blockIdToPath(basePath string, blockId msgs.BlockId) string {
	// 256 subdirs
	hex := fmt.Sprintf("%08x", uint64(blockId))
	// dir is the first byte of the SHA1 of the little endian blockId. We need this
	// because the block ids are not necessarily uniform (and they're very much
	// not uniform right now, since it's the timestamp).
	h := sha1.New()
	binary.Write(h, binary.LittleEndian, uint64(blockId))
	dir := fmt.Sprintf("%02x", h.Sum(nil)[0])
	return path.Join(path.Join(basePath, dir), hex)
}

func eraseBlock(log eggs.LogLevels, basePath string, blockId msgs.BlockId) {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("deleting block %v at path %v", blockId, blockPath)
	if err := os.Remove(blockPath); err != nil {
		panic(err)
	}
}

func serEraseCert(blockServiceId msgs.BlockServiceId, key cipher.Block, blockId msgs.BlockId, conn *net.TCPConn) error {
	// confirm block_id was erased
	// prefix=E
	if err := binary.Write(conn, binary.LittleEndian, uint64(blockServiceId)); err != nil {
		return nil
	}
	if _, err := conn.Write([]byte{'E'}); err != nil {
		return err
	}
	proof := eggs.BlockEraseProof(blockServiceId, blockId, key)
	if _, err := conn.Write(proof[:]); err != nil {
		return err
	}
	return nil
}

func deserFetchBlock(r io.Reader) (blockId msgs.BlockId, offset uint32, count uint32, err error) {
	if err := binary.Read(r, binary.LittleEndian, (*uint64)(&blockId)); err != nil {
		return 0, 0, 0, err
	}
	if err := binary.Read(r, binary.LittleEndian, (*uint32)(&offset)); err != nil {
		return 0, 0, 0, err
	}
	if err := binary.Read(r, binary.LittleEndian, (*uint32)(&count)); err != nil {
		return 0, 0, 0, err
	}
	return blockId, offset, count, nil
}

func sendFetchBlock(log eggs.LogLevels, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId, offset uint32, count uint32, conn *net.TCPConn) error {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("fetching block id %v at path %v", blockId, blockPath)
	f, err := os.Open(blockPath)
	if err != nil {
		return err
	}
	fi, err := f.Stat()
	if err != nil {
		return err
	}
	remainingSize := int(fi.Size()) - int(offset)
	if remainingSize > int(count) {
		return fmt.Errorf("was requested %v bytes, but only got %v", count, remainingSize)
	}
	if remainingSize < 0 {
		return fmt.Errorf("trying to read beyond EOF")
	}
	if _, err := f.Seek(int64(offset), 0); err != nil {
		return err
	}
	// prefix=F
	if err := binary.Write(conn, binary.LittleEndian, uint64(blockServiceId)); err != nil {
		return nil
	}
	if _, err := conn.Write([]byte{'F'}); err != nil {
		return err
	}
	if _, err := conn.ReadFrom(f); err != nil {
		return err
	}
	return nil
}

func deserWriteBlock(cipher cipher.Block, blockServiceId msgs.BlockServiceId, kind uint8, r io.Reader) (blockId msgs.BlockId, crc [4]byte, size uint32, err error) {
	// deser block id
	if err := binary.Read(r, binary.LittleEndian, (*uint64)(&blockId)); err != nil {
		return 0, crc, 0, err
	}
	// deser crc
	if _, err := io.ReadFull(r, crc[:]); err != nil {
		return 0, crc, 0, err
	}
	// deser size
	if err := binary.Read(r, binary.LittleEndian, &size); err != nil {
		return 0, crc, 0, err
	}
	// deser MAC
	var mac [8]byte
	if _, err := io.ReadFull(r, mac[:]); err != nil {
		return 0, crc, 0, err
	}
	// Compute mac
	w := bytes.NewBuffer([]byte{})
	binary.Write(w, binary.LittleEndian, uint64(blockServiceId))
	binary.Write(w, binary.LittleEndian, kind)
	binary.Write(w, binary.LittleEndian, uint64(blockId))
	w.Write(crc[:])
	binary.Write(w, binary.LittleEndian, size)
	expectedMac := eggs.CBCMAC(cipher, w.Bytes())
	if expectedMac != mac {
		return 0, crc, 0, fmt.Errorf("bad MAC for %v, got %v, expected %v", w.Bytes(), mac, expectedMac)
	}
	return blockId, crc, size, nil
}

func writeBlock(log eggs.LogLevels, basePath string, blockId msgs.BlockId, expectedCrc [4]byte, size uint32, conn *net.TCPConn) error {
	filePath := blockIdToPath(basePath, blockId)
	log.Debug("writing block %v at path %v", blockId, basePath)
	if err := os.Mkdir(path.Dir(filePath), 0777); err != nil && !os.IsExist(err) {
		return err
	}
	f, err := os.CreateTemp(basePath, "tmp.")
	if err != nil {
		return err
	}
	tmpName := f.Name()
	defer func() {
		f.Close()
		os.Remove(tmpName)
	}()
	buf := make([]byte, int(size))
	if _, err := io.ReadFull(conn, buf); err != nil {
		return err
	}
	if eggs.CRC32C(buf) != expectedCrc {
		return fmt.Errorf("bad crc for block %v", blockId)
	}
	if _, err := f.Write(buf); err != nil {
		return err
	}
	if err := f.Sync(); err != nil {
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	if err := os.Rename(f.Name(), filePath); err != nil {
		return err
	}
	return nil
}

func serWriteCert(blockServiceId msgs.BlockServiceId, key cipher.Block, blockId msgs.BlockId, conn *net.TCPConn) error {
	// confirm block was written
	// prefix=W
	if err := binary.Write(conn, binary.LittleEndian, uint64(blockServiceId)); err != nil {
		return nil
	}
	if _, err := conn.Write([]byte{'W'}); err != nil {
		return err
	}
	proof := eggs.BlockWriteProof(blockServiceId, blockId, key)
	if _, err := conn.Write(proof[:]); err != nil {
		return err
	}
	return nil
}

func handleReqError(log eggs.LogLevels, err error) {
	log.RaiseAlertStack(1, fmt.Errorf("error while handling request: %w", err))
}

const ONE_HOUR_IN_NS uint64 = 60 * 60 * 1000 * 1000 * 1000
const PAST_CUTOFF uint64 = ONE_HOUR_IN_NS * 22
const FUTURE_CUTOFF uint64 = ONE_HOUR_IN_NS * 2

const MAX_OBJECT_SIZE uint32 = 100e6

func handleRequest(
	log eggs.LogLevels, terminateChan chan any, basePath string, conn *net.TCPConn, timeCheck bool, key [16]byte,
) {
	defer conn.Close()

	// we don't have error responses, we just close the connn
	conn.SetLinger(0)

	blockServiceId := blockServiceIdFromKey(key)

	cipher, err := aes.NewCipher(key[:])
	if err != nil {
		panic(fmt.Errorf("could not create AES-128 key: %w", err))
	}

	for {
		var reqBlockServiceId msgs.BlockServiceId
		if err := binary.Read(conn, binary.LittleEndian, (*uint64)(&reqBlockServiceId)); err != nil {
			if err != io.EOF {
				handleReqError(log, err)
			}
			return
		}
		if reqBlockServiceId != blockServiceIdFromKey(key) {
			handleReqError(log, fmt.Errorf("expected block service id %v, got %v", blockServiceIdFromKey(key), reqBlockServiceId))
			return
		}
		var kind byte
		if err := binary.Read(conn, binary.LittleEndian, &kind); err != nil {
			handleReqError(log, err)
			return
		}
		log.Debug("servicing request of kind %v from %v", string([]byte{kind}), conn.RemoteAddr())
		switch kind {
		case 'e':
			// erase block
			// (block_id, mac) -> data
			blockId, err := deserEraseBlock(blockServiceId, cipher, kind, conn)
			if err != nil {
				handleReqError(log, err)
				return
			}
			if timeCheck && uint64(blockId) <= (uint64(msgs.Now())+FUTURE_CUTOFF) {
				log.RaiseAlert(fmt.Errorf("block %v is too recent to be deleted", blockId))
				return
			}
			eraseBlock(log, basePath, blockId) // can never fail
			if err := serEraseCert(blockServiceId, cipher, blockId, conn); err != nil {
				handleReqError(log, err)
				return
			}
		case 'f':
			// fetch block (no MAC is required)
			// block_id, offset, count -> data
			blockId, offset, count, err := deserFetchBlock(conn)
			if err != nil {
				handleReqError(log, err)
				return
			}
			if err := sendFetchBlock(log, blockServiceId, basePath, blockId, offset, count, conn); err != nil {
				handleReqError(log, err)
				return
			}
		case 'w':
			// write block
			// (block_id, crc, size, mac) -> data
			blockId, crc, size, err := deserWriteBlock(cipher, blockServiceId, kind, conn)
			if err != nil {
				handleReqError(log, err)
				return
			}
			if size > MAX_OBJECT_SIZE {
				handleReqError(log, fmt.Errorf("block %v exceeds max object size: %v > %v", blockId, size, MAX_OBJECT_SIZE))
				return
			}
			if err := writeBlock(log, basePath, blockId, crc, size, conn); err != nil {
				handleReqError(log, err)
				return
			}
			if err := serWriteCert(blockServiceId, cipher, blockId, conn); err != nil {
				handleReqError(log, err)
				return
			}
		}
		log.Debug("serviced request of kind %v from %v", string([]byte{kind}), conn.RemoteAddr())
	}
}

func main() {
	storageClassStr := flag.String("storage-class", "HDD", "Storage class")
	failureDomainStr := flag.String("failure-domain", "", "Failure domain")
	noTimeCheck := flag.Bool("no-time-check", false, "Do not perform block deletion time check (to prevent replay attacks). Useful for testing.")
	port := flag.Uint("port", 0, "")
	verbose := flag.Bool("verbose", false, "")
	logFile := flag.String("log-file", "", "If empty, stdout")
	shuckleAddress := flag.String("shuckle", "localhost:5000", "Shuckle address")
	flag.Parse()
	storageClass := eggs.StorageClass(*storageClassStr)
	if flag.NArg() != 1 {
		fmt.Fprintf(os.Stderr, "Expected one positional argument with the path to the root of the storage partition.\n")
		os.Exit(2)
	}
	dataDir := flag.Args()[0]
	var failureDomain [16]byte
	if copy(failureDomain[:], []byte(*failureDomainStr)) != len(*failureDomainStr) {
		fmt.Fprintf(os.Stderr, "Failure domain too long -- must be at most 16 characters: %v\n", *failureDomainStr)
		os.Exit(2)
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
	log := &eggs.LogLogger{
		Verbose: *verbose,
		Logger:  eggs.NewLogger(logOut),
	}

	var err error
	var keyFile *os.File
	keyFilePath := path.Join(dataDir, "secret.key")
	keyFile, err = os.OpenFile(keyFilePath, os.O_APPEND|os.O_CREATE|os.O_RDWR, 0600)
	if err != nil {
		fmt.Fprintf(os.Stderr, "could not open key file %v: %v\n", *logFile, err)
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
		var urandom *os.File
		urandom, err = os.Open("/dev/urandom")
		if err != nil {
			panic(err)
		}
		if _, err := io.ReadFull(urandom, key[:]); err != nil {
			panic(err)
		}
	} else if read != 16 {
		fmt.Fprintf(os.Stderr, "short secret key (%v rather than 16 bytes)\n", read)
		os.Exit(1)
	}

	listener, err := net.Listen("tcp", fmt.Sprintf("0.0.0.0:%v", *port))
	if err != nil {
		panic(err)
	}
	defer listener.Close()

	log.Info("running on %v", listener.Addr())
	actualPort := uint16(listener.Addr().(*net.TCPAddr).Port)

	terminateChan := make(chan any)

	go func() {
		defer func() { handleRecover(log, terminateChan, recover()) }()
		registerPeriodically(log, actualPort, storageClass, failureDomain, key, *shuckleAddress)
	}()
	go func() {
		defer func() { handleRecover(log, terminateChan, recover()) }()
		for {
			conn, err := listener.Accept()
			if err != nil {
				terminateChan <- err
				return
			}
			go func() {
				defer func() { handleRecover(log, terminateChan, recover()) }()
				handleRequest(log, terminateChan, dataDir, conn.(*net.TCPConn), !*noTimeCheck, key)
			}()
		}
	}()

	{
		err := <-terminateChan
		if err != nil {
			panic(err)
		}
	}
}
