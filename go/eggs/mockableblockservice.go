package eggs

import (
	"crypto/cipher"
	"fmt"
	"io"
	"net"
	"xtx/eggsfs/msgs"
)

type MockableBlockServiceConn interface {
	io.Writer
	io.Reader
	io.ReaderFrom
	io.Closer
}

type MockableBlockServices interface {
	BlockServiceConnection(id msgs.BlockServiceId, ip net.IP, port uint16) (MockableBlockServiceConn, error)
	WriteBlock(
		logger LogLevels,
		conn MockableBlockServiceConn,
		block *msgs.BlockInfo,
		r io.Reader,
		size uint32,
		crc [4]byte,
	) ([8]byte, error)
	FetchBlock(
		logger LogLevels,
		conn MockableBlockServiceConn,
		blockService *msgs.BlockService,
		block *msgs.FetchedBlock,
		offset uint32,
		count uint32,
	) error
	EraseBlock(
		logger LogLevels,
		conn MockableBlockServiceConn,
		block msgs.BlockInfo,
	) ([8]byte, error)
	CopyBlock(
		logger LogLevels,
		sourceConn MockableBlockServiceConn,
		sourceBlockService *msgs.BlockService,
		sourceBlockSize uint64,
		sourceBlock *msgs.FetchedBlock,
		dstConn MockableBlockServiceConn,
		dstBlock *msgs.BlockInfo,
	) ([8]byte, error)
}

type RealBlockServices struct{}

func (RealBlockServices) BlockServiceConnection(id msgs.BlockServiceId, ip net.IP, port uint16) (MockableBlockServiceConn, error) {
	return BlockServiceConnection(id, ip, port)
}

func (RealBlockServices) WriteBlock(
	logger LogLevels,
	conn MockableBlockServiceConn,
	block *msgs.BlockInfo,
	r io.Reader,
	size uint32,
	crc [4]byte,
) ([8]byte, error) {
	return WriteBlock(logger, conn, block, r, size, crc)
}

func (RealBlockServices) FetchBlock(
	logger LogLevels,
	conn MockableBlockServiceConn,
	blockService *msgs.BlockService,
	block *msgs.FetchedBlock,
	offset uint32,
	count uint32,
) error {
	return FetchBlock(logger, conn, blockService, block, offset, count)
}

func (RealBlockServices) EraseBlock(
	logger LogLevels,
	conn MockableBlockServiceConn,
	block msgs.BlockInfo,
) ([8]byte, error) {
	return EraseBlock(logger, conn, block)
}

func (RealBlockServices) CopyBlock(
	logger LogLevels,
	sourceConn MockableBlockServiceConn,
	sourceBlockService *msgs.BlockService,
	sourceBlockSize uint64,
	sourceBlock *msgs.FetchedBlock,
	dstConn MockableBlockServiceConn,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	return CopyBlock(logger, sourceConn, sourceBlockService, sourceBlockSize, sourceBlock, dstConn, dstBlock)
}

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

func (mbs *MockedBlockServices) BlockServiceConnection(id msgs.BlockServiceId, ip net.IP, port uint16) (MockableBlockServiceConn, error) {
	return dummyConn{}, nil
}

func (mbs *MockedBlockServices) WriteBlock(
	logger LogLevels,
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
	logger LogLevels,
	conn MockableBlockServiceConn,
	blockService *msgs.BlockService,
	block *msgs.FetchedBlock,
	offset uint32,
	count uint32,
) error {
	panic("trying to fetch block with mockable block service")
}

func (mbs *MockedBlockServices) EraseBlock(
	logger LogLevels,
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
	logger LogLevels,
	sourceConn MockableBlockServiceConn,
	sourceBlockService *msgs.BlockService,
	sourceBlockSize uint64,
	sourceBlock *msgs.FetchedBlock,
	dstConn MockableBlockServiceConn,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	key, wasPresent := mbs.Keys[dstBlock.BlockServiceId]
	if !wasPresent {
		panic(fmt.Errorf("cannot find key for block service %v", dstBlock.BlockServiceId))
	}
	return BlockWriteProof(dstBlock.BlockServiceId, dstBlock.BlockId, key), nil
}
