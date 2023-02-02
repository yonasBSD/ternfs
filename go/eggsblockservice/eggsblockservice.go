package main

import (
	"bytes"
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"crypto/sha1"
	"encoding/binary"
	"flag"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"path"
	"runtime/debug"
	"runtime/pprof"
	"strings"
	"sync"
	"syscall"
	"time"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"

	"golang.org/x/sys/unix"
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
		fmt.Printf("%v blocks\n", blocks)
	}
	return blocks
}

func registerPeriodically(
	log eggs.LogLevels,
	ip1 [4]byte,
	port1 uint16,
	ip2 [4]byte,
	port2 uint16,
	failureDomain [16]byte,
	blockServices map[msgs.BlockServiceId]*blockService,
	shuckleAddress string,
) {
	req := msgs.RegisterBlockServicesReq{}
	for id, blockService := range blockServices {
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
		_, err := eggs.ShuckleRequest(log, shuckleAddress, &req)
		if err != nil {
			log.RaiseAlert(fmt.Errorf("could not register block services with %+v: %w", shuckleAddress, err))
			time.Sleep(100 * time.Millisecond)
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
	if int(count) > remainingSize {
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
	lf := io.LimitedReader{
		R: f,
		N: int64(count),
	}
	if _, err := conn.ReadFrom(&lf); err != nil {
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
	log eggs.LogLevels,
	terminateChan chan any,
	blockServices map[msgs.BlockServiceId]*blockService,
	conn *net.TCPConn,
	timeCheck bool,
) {
	defer conn.Close()

	// We currently do not have error responses, we just close the
	// connection.
	conn.SetLinger(0)

	for {
		var blockServiceId msgs.BlockServiceId
		if err := binary.Read(conn, binary.LittleEndian, (*uint64)(&blockServiceId)); err != nil {
			if err == io.EOF {
				log.Debug("got EOF, terminating")
			} else {
				handleReqError(log, err)
			}
			return
		}
		blockService, found := blockServices[blockServiceId]
		if !found {
			handleReqError(log, fmt.Errorf("received unknown block service id %v", blockServiceId))
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
			blockId, err := deserEraseBlock(blockServiceId, blockService.cipher, kind, conn)
			if err != nil {
				handleReqError(log, err)
				return
			}
			if timeCheck && uint64(blockId) <= (uint64(msgs.Now())+FUTURE_CUTOFF) {
				log.RaiseAlert(fmt.Errorf("block %v is too recent to be deleted", blockId))
				return
			}
			eraseBlock(log, blockService.path, blockId) // can never fail
			if err := serEraseCert(blockServiceId, blockService.cipher, blockId, conn); err != nil {
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
			if err := sendFetchBlock(log, blockServiceId, blockService.path, blockId, offset, count, conn); err != nil {
				handleReqError(log, err)
				return
			}
		case 'w':
			// write block
			// (block_id, crc, size, mac) -> data
			blockId, crc, size, err := deserWriteBlock(blockService.cipher, blockServiceId, kind, conn)
			if err != nil {
				handleReqError(log, err)
				return
			}
			if size > MAX_OBJECT_SIZE {
				handleReqError(log, fmt.Errorf("block %v exceeds max object size: %v > %v", blockId, size, MAX_OBJECT_SIZE))
				return
			}
			if err := writeBlock(log, blockService.path, blockId, crc, size, conn); err != nil {
				handleReqError(log, err)
				return
			}
			if err := serWriteCert(blockServiceId, blockService.cipher, blockId, conn); err != nil {
				handleReqError(log, err)
				return
			}
		}
		log.Debug("serviced request of kind %v from %v", string([]byte{kind}), conn.RemoteAddr())
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

func retrieveOrCreateKey(log eggs.LogLevels, dir string) [16]byte {
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
		if _, err := rand.Read(key[:]); err != nil {
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
	logFile := flag.String("log-file", "", "If empty, stdout")
	shuckleAddress := flag.String("shuckle", eggs.DEFAULT_SHUCKLE_ADDRESS, "Shuckle address (host:port).")
	profileFile := flag.String("profile-file", "", "")
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

	if *profileFile != "" {
		f, err := os.Create(*profileFile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Could not open profile file %v", *profileFile)
			os.Exit(1)
		}
		pprof.StartCPUProfile(f)
		// Save CPU profile if we get killed by a signal
		signalChan := make(chan os.Signal, 1)
		signal.Notify(signalChan, syscall.SIGHUP, syscall.SIGINT, syscall.SIGTERM, syscall.SIGQUIT, syscall.SIGILL, syscall.SIGTRAP, syscall.SIGABRT, syscall.SIGSTKFLT, syscall.SIGSYS)
		go func() {
			sig := <-signalChan
			signal.Stop(signalChan)
			pprof.StopCPUProfile()
			syscall.Kill(syscall.Getpid(), sig.(syscall.Signal))
		}()
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
			path:         dir,
			key:          key,
			cipher:       cipher,
			storageClass: storageClass,
		}
	}
	if len(blockServices) != flag.NArg()/2 {
		panic(fmt.Errorf("duplicate block services!"))
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

	go func() {
		defer func() { handleRecover(log, terminateChan, recover()) }()
		registerPeriodically(log, ownIp1, actualPort1, ownIp2, actualPort2, failureDomain, blockServices, *shuckleAddress)
	}()
	go func() {
		defer func() { handleRecover(log, terminateChan, recover()) }()
		for {
			conn, err := listener1.Accept()
			if err != nil {
				terminateChan <- err
				return
			}
			go func() {
				defer func() { handleRecover(log, terminateChan, recover()) }()
				handleRequest(log, terminateChan, blockServices, conn.(*net.TCPConn), !*noTimeCheck)
			}()
		}
	}()
	if listener2 != nil {
		go func() {
			defer func() { handleRecover(log, terminateChan, recover()) }()
			for {
				conn, err := listener2.Accept()
				if err != nil {
					terminateChan <- err
					return
				}
				go func() {
					defer func() { handleRecover(log, terminateChan, recover()) }()
					handleRequest(log, terminateChan, blockServices, conn.(*net.TCPConn), !*noTimeCheck)
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
