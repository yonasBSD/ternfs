package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	crand "crypto/rand"
	"crypto/sha1"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	mrand "math/rand"
	"net"
	"os"
	"os/signal"
	"path"
	"runtime/pprof"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"

	"golang.org/x/sys/unix"
)

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

func countBlocks(basePath string) uint64 {
	blocks := uint64(0)
	for i := 0; i < 256; i++ {
		blockDir := fmt.Sprintf("%02x", i)
		d, err := os.Open(path.Join(basePath, blockDir))
		if os.IsNotExist(err) {
			continue
		}
		if err != nil {
			panic(err)
		}
		defer d.Close()
		for {
			entries, err := d.Readdirnames(1000)
			if err == io.EOF {
				break
			}
			if err != nil {
				panic(err)
			}
			blocks += uint64(len(entries))
		}
	}
	return blocks
}

func registerPeriodically(
	log *lib.Logger,
	ip1 [4]byte,
	port1 uint16,
	ip2 [4]byte,
	port2 uint16,
	failureDomain [16]byte,
	blockServices map[msgs.BlockServiceId]blockService,
	shuckleAddress string,
) {
	req := msgs.RegisterBlockServicesReq{}
	for id := range blockServices {
		blockService := blockServices[id]
		bs := msgs.BlockServiceInfo{
			Id:            id,
			Ip1:           ip1,
			Port1:         port1,
			Ip2:           ip2,
			Port2:         port2,
			SecretKey:     blockService.key,
			StorageClass:  blockService.storageClass,
			FailureDomain: failureDomain,
			Path:          blockService.path,
		}
		req.BlockServices = append(req.BlockServices, bs)
	}
	for {
		for ix := range req.BlockServices {
			bsInfo := &req.BlockServices[ix]
			blockService := blockServices[bsInfo.Id]
			var statfs unix.Statfs_t
			if err := unix.Statfs(path.Join(blockService.path, "secret.key"), &statfs); err != nil {
				panic(err)
			}
			log.Debug("statfs for %v: %+v", blockService.path, statfs)
			bsInfo.Blocks = countBlocks(blockService.path)
			bsInfo.CapacityBytes = statfs.Blocks * uint64(statfs.Bsize)
			bsInfo.AvailableBytes = statfs.Bavail * uint64(statfs.Bsize)
		}
		log.Debug("registering with %+v", req)
		_, err := lib.ShuckleRequest(log, shuckleAddress, &req)
		if err != nil {
			log.RaiseAlert(fmt.Errorf("could not register block services with %+v: %w", shuckleAddress, err))
			time.Sleep(100 * time.Millisecond)
			continue
		}
		waitForRange := time.Minute * 2
		waitFor := time.Duration(mrand.Uint64() % uint64(waitForRange.Nanoseconds()))
		log.Info("registered with %v, waiting %v", shuckleAddress, waitFor)
		time.Sleep(waitFor)
	}
}

func checkEraseCertificate(log *lib.Logger, blockServiceId msgs.BlockServiceId, cipher cipher.Block, req *msgs.EraseBlockReq) msgs.ErrCode {
	expectedMac, good := lib.CheckBlockEraseCertificate(blockServiceId, cipher, req)
	if !good {
		log.RaiseAlert(fmt.Errorf("bad MAC, got %v, expected %v", req.Certificate, expectedMac))
		return msgs.BAD_CERTIFICATE
	}
	return 0
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

func eraseBlock(log *lib.Logger, basePath string, blockId msgs.BlockId) error {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("deleting block %v at path %v", blockId, blockPath)
	if err := os.Remove(blockPath); err != nil {
		if os.IsNotExist(err) {
			log.RaiseAlert(fmt.Errorf("could not find block to erase at path %v", blockPath))
			return msgs.BLOCK_NOT_FOUND
		}
		panic(err)
	}
	return nil
}

func sendFetchBlock(log *lib.Logger, blockServiceId msgs.BlockServiceId, basePath string, blockId msgs.BlockId, offset uint32, count uint32, conn *net.TCPConn) error {
	blockPath := blockIdToPath(basePath, blockId)
	log.Debug("fetching block id %v at path %v", blockId, blockPath)
	f, err := os.Open(blockPath)
	if os.IsNotExist(err) {
		log.RaiseAlert(fmt.Errorf("could not find block to erase at path %v", blockPath))
		return msgs.BLOCK_NOT_FOUND
	}
	if err != nil {
		return err
	}
	fi, err := f.Stat()
	if err != nil {
		return err
	}
	remainingSize := int(fi.Size()) - int(offset)
	if int(count) > remainingSize {
		log.RaiseAlert(fmt.Errorf("was requested %v bytes, but only got %v", count, remainingSize))
		lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_FETCH_OUT_OF_BOUNDS)
		return nil
	}
	if remainingSize < 0 {
		log.RaiseAlert(fmt.Errorf("trying to read beyond EOF"))
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
	return nil
}

func checkWriteCertificate(log *lib.Logger, cipher cipher.Block, blockServiceId msgs.BlockServiceId, req *msgs.WriteBlockReq) msgs.ErrCode {
	expectedMac, good := lib.CheckBlockWriteCertificate(cipher, blockServiceId, req)
	if !good {
		log.Debug("mac computed for %v %v %v %v", blockServiceId, req.BlockId, req.Crc, req.Size)
		log.RaiseAlert(fmt.Errorf("bad MAC, got %v, expected %v", req.Certificate, expectedMac))
		return msgs.BAD_CERTIFICATE
	}
	return 0
}

func writeToTemp(
	log *lib.Logger, bufPool *sync.Pool, basePath string, size uint64, conn *net.TCPConn,
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
	bufPtr := bufPool.Get().(*[]byte)
	defer bufPool.Put(bufPtr)
	readSoFar := uint64(0)
	for {
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
	return tmpName, crc, err
}

func writeBlock(
	log *lib.Logger, bufPool *sync.Pool,
	blockServiceId msgs.BlockServiceId, cipher cipher.Block, basePath string,
	blockId msgs.BlockId, expectedCrc msgs.Crc, size uint32, conn *net.TCPConn,
) error {
	filePath := blockIdToPath(basePath, blockId)
	log.Debug("writing block %v at path %v", blockId, basePath)
	if err := os.Mkdir(path.Dir(filePath), 0777); err != nil && !os.IsExist(err) {
		return err
	}
	tmpName, crc, err := writeToTemp(log, bufPool, basePath, uint64(size), conn)
	if err != nil {
		return err
	}
	if msgs.Crc(crc) != expectedCrc {
		os.Remove(tmpName)
		log.RaiseAlert(fmt.Errorf("bad crc for block %v, got %v in req, computed %v", blockId, msgs.Crc(crc), expectedCrc))
		lib.WriteBlocksResponseError(log, conn, msgs.BAD_BLOCK_CRC)
		return nil
	}
	if err := os.Rename(tmpName, filePath); err != nil {
		os.Remove(tmpName)
		return err
	}
	defer os.Remove(tmpName)
	if err := lib.WriteBlocksResponse(log, conn, &msgs.WriteBlockResp{Proof: BlockWriteProof(blockServiceId, blockId, cipher)}); err != nil {
		return err
	}
	return nil
}

func consumeBlock(size uint32, conn *net.TCPConn) error {
	lr := io.LimitReader(conn, int64(size))
	_, err := io.Copy(io.Discard, lr)
	return err
}

func testWrite(
	log *lib.Logger, bufPool *sync.Pool, basePath string, size uint64, conn *net.TCPConn,
) error {
	tmpName, _, err := writeToTemp(log, bufPool, basePath, size, conn)
	if err != nil {
		return err
	}
	os.Remove(tmpName)
	if err := lib.WriteBlocksResponse(log, conn, &msgs.TestWriteResp{}); err != nil {
		return err
	}
	return nil
}

const ONE_HOUR_IN_NS uint64 = 60 * 60 * 1000 * 1000 * 1000
const PAST_CUTOFF uint64 = ONE_HOUR_IN_NS * 22
const FUTURE_CUTOFF uint64 = ONE_HOUR_IN_NS * 2

const MAX_OBJECT_SIZE uint32 = 100 << 20

func handleError(
	log *lib.Logger,
	conn *net.TCPConn,
	err error,
) {
	// we always raise an alert since this is almost always bad news in the block service
	log.RaiseAlert(err)
	eggsErr, isEggsErr := err.(msgs.ErrCode)
	if isEggsErr {
		lib.WriteBlocksResponseError(log, conn, eggsErr)
	} else {
		// attempt to say goodbye
		lib.WriteBlocksResponseError(log, conn, msgs.INTERNAL_ERROR)
	}
}

func handleRequest(
	log *lib.Logger,
	bufPool *sync.Pool,
	terminateChan chan any,
	blockServices map[msgs.BlockServiceId]blockService,
	conn *net.TCPConn,
	timeCheck bool,
) {
	defer conn.Close()

NextRequest:
	for {
		blockServiceId, req, err := lib.ReadBlocksRequest(log, conn)
		if err != nil {
			switch v := err.(type) {
			case msgs.ErrCode:
				lib.WriteBlocksResponseError(log, conn, v)
				return
			default:
				if err == io.EOF {
					log.Debug("got EOF, terminating")
					return
				} else {
					log.RaiseAlert(fmt.Errorf("got error %w when reading request, terminating", err))
					return
				}
			}
		}
		blockService, found := blockServices[blockServiceId]
		if !found {
			log.RaiseAlert(fmt.Errorf("received unknown block service id %v", blockServiceId))
			lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_SERVICE_NOT_FOUND)
			continue
		}
		log.Debug("servicing request of type %T from %v", req, conn.RemoteAddr())
		log.Trace("req %+v", req)
		switch whichReq := req.(type) {
		case *msgs.EraseBlockReq:
			if err := checkEraseCertificate(log, blockServiceId, blockService.cipher, whichReq); err != 0 {
				lib.WriteBlocksResponseError(log, conn, err)
				continue NextRequest
			}
			cutoffTime := msgs.EggsTime(uint64(whichReq.BlockId) + FUTURE_CUTOFF).Time()
			now := time.Now()
			if timeCheck && now.Before(cutoffTime) {
				log.RaiseAlert(fmt.Errorf("block %v is too recent to be deleted (now=%v, cutoffTime=%v)", whichReq.BlockId, now, cutoffTime))
				lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_TOO_RECENT_FOR_DELETION)
				continue NextRequest
			}
			// note: we might consider allow BLOCK_NOT_FOUND here, because it could be that
			// a process that was in the process of destructing a span managed to erase the
			// block but not certify the deletion in the shard.
			if err := eraseBlock(log, blockService.path, whichReq.BlockId); err != nil {
				handleError(log, conn, err)
				return
			}

			resp := msgs.EraseBlockResp{
				Proof: BlockEraseProof(blockServiceId, whichReq.BlockId, blockService.cipher),
			}
			if err := lib.WriteBlocksResponse(log, conn, &resp); err != nil {
				handleError(log, conn, fmt.Errorf("could not send blocks response to %v: %w", conn.RemoteAddr(), err))
				return
			}
		case *msgs.FetchBlockReq:
			if err := sendFetchBlock(log, blockServiceId, blockService.path, whichReq.BlockId, whichReq.Offset, whichReq.Count, conn); err != nil {
				if _, isEggsErr := err.(msgs.ErrCode); !isEggsErr {
					err = fmt.Errorf("could not send block response to %v: %w", conn.RemoteAddr(), err)
				}
				handleError(log, conn, err)
				return
			}
		case *msgs.WriteBlockReq:
			if err := checkWriteCertificate(log, blockService.cipher, blockServiceId, whichReq); err != 0 {
				if err := consumeBlock(whichReq.Size, conn); err != nil {
					handleError(log, conn, fmt.Errorf("could not consume block from %v: %w", conn.RemoteAddr(), err))
				}
				lib.WriteBlocksResponseError(log, conn, err)
				continue NextRequest
			}
			if whichReq.Size > MAX_OBJECT_SIZE {
				log.RaiseAlert(fmt.Errorf("block %v exceeds max object size: %v > %v", whichReq.BlockId, whichReq.Size, MAX_OBJECT_SIZE))
				if err := consumeBlock(whichReq.Size, conn); err != nil {
					handleError(log, conn, fmt.Errorf("could not consume block from %v: %w", conn.RemoteAddr(), err))
				}
				lib.WriteBlocksResponseError(log, conn, msgs.BLOCK_TOO_BIG)
				continue NextRequest
			}
			if err := writeBlock(log, bufPool, blockServiceId, blockService.cipher, blockService.path, whichReq.BlockId, whichReq.Crc, whichReq.Size, conn); err != nil {
				handleError(log, conn, fmt.Errorf("could not write block: %w", err))
				return
			}
		case *msgs.TestWriteReq:
			if err := testWrite(log, bufPool, blockService.path, whichReq.Size, conn); err != nil {
				handleError(log, conn, fmt.Errorf("could not perform test write: %w", err))
				return
			}
		default:
			handleError(log, conn, fmt.Errorf("bad request type %T", req))
			return
		}
		log.Debug("serviced request of type %T from %v", req, conn.RemoteAddr())
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

type blockService struct {
	path         string
	key          [16]byte
	cipher       cipher.Block
	storageClass msgs.StorageClass
}

func main() {
	flag.Usage = usage
	failureDomainStr := flag.String("failure-domain", "", "Failure domain")
	noTimeCheck := flag.Bool("no-time-check", false, "Do not perform block deletion time check (to prevent replay attacks). Useful for testing.")
	ownIp1Str := flag.String("own-ip-1", "", "First IP that we'll bind to, and that we'll advertise to shuckle.")
	port1 := flag.Uint("port-1", 0, "First port on which to run on. By default it will be picked automatically.")
	ownIp2Str := flag.String("own-ip-2", "", "Second IP that we'll advertise to shuckle. If it is not provided, we will only bind to the first IP.")
	port2 := flag.Uint("port-2", 0, "Port on which to run on. By default it will be picked automatically.")
	verbose := flag.Bool("verbose", false, "")
	trace := flag.Bool("trace", false, "")
	logFile := flag.String("log-file", "", "If empty, stdout")
	shuckleAddress := flag.String("shuckle", lib.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	profileFile := flag.String("profile-file", "", "")
	syslog := flag.Bool("syslog", false, "")
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
		Level:  level,
		Syslog: *syslog,
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
	log.Info("  noTimeCheck = %v", *noTimeCheck)
	log.Info("  ownIp1 = '%v'", *ownIp1Str)
	log.Info("  port1 = %v", *port1)
	log.Info("  ownIp2 = %v", *ownIp2Str)
	log.Info("  port2 = %v", *port2)
	log.Info("  logLevel = %v", level)
	log.Info("  logFile = '%v'", *logFile)
	log.Info("  shuckleAddress = '%v'", *shuckleAddress)

	blockServices := make(map[msgs.BlockServiceId]blockService)
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
		blockServices[id] = blockService{
			path:         dir,
			key:          key,
			cipher:       cipher,
			storageClass: storageClass,
		}
	}
	for id, blockService := range blockServices {
		log.Info("block service %v at %v, storage class %v", id, blockService.path, blockService.storageClass)
	}

	if len(blockServices) != flag.NArg()/2 {
		panic(fmt.Errorf("duplicate block services"))
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

	terminateChan := make(chan any)

	bufPool := &sync.Pool{
		New: func() any {
			buf := make([]byte, 1<<20)
			return &buf
		},
	}

	go func() {
		defer func() { lib.HandleRecoverChan(log, terminateChan, recover()) }()
		registerPeriodically(log, ownIp1, actualPort1, ownIp2, actualPort2, failureDomain, blockServices, *shuckleAddress)
	}()
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
				handleRequest(log, bufPool, terminateChan, blockServices, conn.(*net.TCPConn), !*noTimeCheck)
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
					handleRequest(log, bufPool, terminateChan, blockServices, conn.(*net.TCPConn), !*noTimeCheck)
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
