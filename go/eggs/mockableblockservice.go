package eggs

import (
	"crypto/cipher"
	"fmt"
	"io"
	"net"
	"time"
	"xtx/eggsfs/msgs"
)

type MockableBlockServiceConn interface {
	io.Writer
	io.Reader
	io.ReaderFrom
}

type MockableBlockServices interface {
	GetBlockServiceConnection(
		log *Logger, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16,
	) (MockableBlockServiceConn, error)
	ReleaseBlockServiceConnection(
		log *Logger, conn MockableBlockServiceConn,
	) error
	WriteBlock(
		logger *Logger,
		conn MockableBlockServiceConn,
		block *msgs.BlockInfo,
		r io.Reader,
		size uint32,
		crc [4]byte,
	) ([8]byte, error)
	FetchBlock(
		logger *Logger,
		conn MockableBlockServiceConn,
		blockService *msgs.BlockService,
		blockId msgs.BlockId,
		crc32 [4]byte,
		offset uint32,
		count uint32,
	) error
	EraseBlock(
		logger *Logger,
		conn MockableBlockServiceConn,
		block msgs.BlockInfo,
	) ([8]byte, error)
	CopyBlock(
		logger *Logger,
		sourceConn MockableBlockServiceConn,
		sourceBlockService *msgs.BlockService,
		sourceBlockId msgs.BlockId,
		sourceBlockCrc [4]byte,
		sourceBlockSize uint64,
		dstConn MockableBlockServiceConn,
		dstBlock *msgs.BlockInfo,
	) ([8]byte, error)
}

func (c *Client) existingBlockServiceConnection(ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) *blockServiceConn {
	c.blockServicesLock.RLock()
	defer c.blockServicesLock.RUnlock()
	conn1, found1 := c.blockServicesConns[blockServicesConnsKey(ip1, port1)]
	if found1 {
		conn1.lastActive = time.Now()
		return conn1
	}
	conn2, found2 := c.blockServicesConns[blockServicesConnsKey(ip2, port2)]
	if found2 {
		conn2.lastActive = time.Now()
		return conn2
	}
	return nil
}

func (c *Client) newBlockServiceConnection(log *Logger, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (*blockServiceConn, error) {
	conn, err := BlockServiceConnection(log, ip1, port1, ip2, port2)
	if err != nil {
		return nil, err
	}
	c.blockServicesLock.Lock()
	defer c.blockServicesLock.Unlock()
	k := blockServicesConnsKeyFromConn(conn)
	otherConn, found := c.blockServicesConns[k] // somebody got here first
	if found {
		conn.Close()
		otherConn.lastActive = time.Now()
		return otherConn, nil
	}
	bsConn := &blockServiceConn{
		conn:       conn,
		lastActive: time.Now(),
	}
	c.blockServicesConns[k] = bsConn
	return bsConn, nil
}

// The first ip1/port1 cannot be zeroed, the second one can. One of them
// will be tried at random.
func (c *Client) GetBlockServiceConnection(log *Logger, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (MockableBlockServiceConn, error) {
	conn := c.existingBlockServiceConnection(ip1, port1, ip2, port2)
	if conn == nil {
		var err error
		conn, err = c.newBlockServiceConnection(log, ip1, port1, ip2, port2)
		if err != nil {
			return nil, err
		}
	}
	conn.mu.Lock()
	return conn.conn, nil
}

func (c *Client) ReleaseBlockServiceConnection(log *Logger, conn0 MockableBlockServiceConn) error {
	conn := conn0.(*net.TCPConn)
	c.blockServicesLock.RLock()
	defer c.blockServicesLock.RUnlock()
	bsConn, found := c.blockServicesConns[blockServicesConnsKeyFromConn(conn)]
	if !found {
		panic(fmt.Errorf("could not find connection for %v", conn.RemoteAddr()))
	}
	// Theoretical race -- the reaper might have closed this conn in the meantime. But amazingly
	// unlikely for a minute to pass between getting the conn and here. Note that we can't lock
	// inside the map or we'd hog the map lock.
	bsConn.mu.Unlock()
	return nil
}

func (c *Client) WriteBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	block *msgs.BlockInfo,
	r io.Reader,
	size uint32,
	crc [4]byte,
) ([8]byte, error) {
	return WriteBlock(logger, conn, block, r, size, crc)
}

func (c *Client) FetchBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	blockService *msgs.BlockService,
	blockId msgs.BlockId,
	blockCrc [4]byte,
	offset uint32,
	count uint32,
) error {
	return FetchBlock(logger, conn, blockService, blockId, blockCrc, offset, count)
}

func (c *Client) EraseBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	block msgs.BlockInfo,
) ([8]byte, error) {
	return EraseBlock(logger, conn, block)
}

func (c *Client) CopyBlock(
	logger *Logger,
	sourceConn MockableBlockServiceConn,
	sourceBlockService *msgs.BlockService,
	sourceBlockId msgs.BlockId,
	sourceBlockCrc [4]byte,
	sourceBlockSize uint64,
	dstConn MockableBlockServiceConn,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	return CopyBlock(logger, sourceConn, sourceBlockService, sourceBlockId, sourceBlockCrc, sourceBlockSize, dstConn, dstBlock)
}

var _ = (MockableBlockServices)((*Client)(nil))

type MockedBlockServices struct {
	Keys map[msgs.BlockServiceId]cipher.Block
}

type dummyConn struct{}

func (dummyConn) Write(p []byte) (n int, err error) {
	return len(p), nil
}

func (dummyConn) Close() error {
	return nil
}

func (dummyConn) Read(p []byte) (n int, err error) {
	return len(p), nil
}

func (dummyConn) ReadFrom(r io.Reader) (n int64, err error) {
	return 0, nil
}

func (mbs *MockedBlockServices) GetBlockServiceConnection(log *Logger, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (MockableBlockServiceConn, error) {
	return dummyConn{}, nil
}

func (mbs *MockedBlockServices) ReleaseBlockServiceConnection(log *Logger, conn MockableBlockServiceConn) error {
	return nil
}

func (mbs *MockedBlockServices) WriteBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	block *msgs.BlockInfo,
	r io.Reader,
	size uint32,
	crc [4]byte,
) ([8]byte, error) {
	key, wasPresent := mbs.Keys[block.BlockServiceId]
	if !wasPresent {
		panic(fmt.Errorf("cannot find key for block service %v", block.BlockServiceId))
	}
	buf := make([]byte, 512)
	read := 0
	for read < int(size) {
		toRead := 512
		if int(size)-read < toRead {
			toRead = int(size) - read
		}
		readNow, err := r.Read(buf[:toRead])
		if err != nil {
			panic(err)
		}
		read += readNow
	}
	return BlockWriteProof(block.BlockServiceId, block.BlockId, key), nil
}

func (mbs *MockedBlockServices) FetchBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	blockService *msgs.BlockService,
	blockId msgs.BlockId,
	blockCrc [4]byte,
	offset uint32,
	count uint32,
) error {
	panic("trying to fetch block with mockable block service")
}

func (mbs *MockedBlockServices) EraseBlock(
	logger *Logger,
	conn MockableBlockServiceConn,
	block msgs.BlockInfo,
) ([8]byte, error) {
	key, wasPresent := mbs.Keys[block.BlockServiceId]
	if !wasPresent {
		panic(fmt.Errorf("cannot find key for block service %v", block.BlockServiceId))
	}
	return BlockEraseProof(block.BlockServiceId, block.BlockId, key), nil
}

func (mbs *MockedBlockServices) CopyBlock(
	logger *Logger,
	sourceConn MockableBlockServiceConn,
	sourceBlockService *msgs.BlockService,
	sourceBlockId msgs.BlockId,
	sourceBlockCrc [4]byte,
	sourceBlockSize uint64,
	dstConn MockableBlockServiceConn,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	key, wasPresent := mbs.Keys[dstBlock.BlockServiceId]
	if !wasPresent {
		panic(fmt.Errorf("cannot find key for block service %v", dstBlock.BlockServiceId))
	}
	return BlockWriteProof(dstBlock.BlockServiceId, dstBlock.BlockId, key), nil
}

var _ = (MockableBlockServices)((*MockedBlockServices)(nil))
