package lib

import (
	"bytes"
	"fmt"
	"io"
	"math/rand"
	"sync/atomic"
	"testing"
	"xtx/eggsfs/crc32c"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/rs"

	"github.com/stretchr/testify/assert"
)

// Simulates block service conns, which will wait forever when reading past the block
// unless we issue other requests.
type mockedBlockConn struct {
	data      []byte
	openConns *int64
}

func (mbc *mockedBlockConn) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	if len(mbc.data) == 0 {
		panic(fmt.Errorf("trying to read %v bytes after full block is consumed", len(p)))
	}
	read := copy(p, mbc.data)
	mbc.data = mbc.data[read:]
	return read, nil
}

func (c *mockedBlockConn) Close() error {
	atomic.AddInt64(c.openConns, -1)
	return nil
}

func testSpan(
	t *testing.T,
	bufPool *ReadSpanBufPool,
	rand *rand.Rand,
	parity rs.Parity,
	spanSize uint32,
	stripeSize uint32,
) {
	// These two are uninteresting apart from the parity
	spanPolicies := msgs.SpanPolicy{
		Entries: []msgs.SpanPolicyEntry{
			{
				MaxSize: 100 << 20, // 100MiB
				Parity:  parity,
			},
		},
	}
	blockPolicies := msgs.BlockPolicy{
		Entries: []msgs.BlockPolicyEntry{
			{
				StorageClass: msgs.HDD_STORAGE,
				MinSize:      5 << 20, // 5MiB
			},
		},
	}
	stripePolicy := msgs.StripePolicy{
		TargetStripeSize: stripeSize,
	}
	sizeWithZeros := spanSize + rand.Uint32()%100
	data := make([]byte, sizeWithZeros)
	rand.Read(data[:spanSize])
	buf := append([]byte{}, data[:spanSize]...)
	var req *msgs.AddSpanInitiateReq
	buf, req = prepareSpanInitiateReq(
		[]msgs.BlockServiceBlacklist{},
		&spanPolicies,
		&blockPolicies,
		&stripePolicy,
		msgs.NULL_INODE_ID, [8]byte{0, 0, 0, 0, 0, 0, 0, 0}, 0,
		sizeWithZeros,
		buf,
	)
	assert.Equal(t, crc32c.Sum(0, data), uint32(req.Crc))
	blockSize := int(req.CellSize) * int(req.Stripes)
	tmpBuf := make([]byte, 1<<11+int(rand.Uint32()%(1<<11))) // between 1 and 2 KiB
	D := req.Parity.DataBlocks()
	B := req.Parity.Blocks()
	blocksCrcs := make([]msgs.Crc, B)
	stripesCrcs := make([]msgs.Crc, req.Stripes)
	for i := 0; i < B; i++ {
		blockCrc, blockReader := mkBlockReader(req, buf, i)
		actualBlockCrc := uint32(0)
		actualBlockSize := 0
		for {
			read, err := blockReader.Read(tmpBuf)
			if err == io.EOF {
				break
			}
			if !assert.NoError(t, err) {
				return
			}
			actualBlockCrc = crc32c.Sum(actualBlockCrc, tmpBuf[:read])
			actualBlockSize += read
		}
		assert.Equal(t, blockSize, actualBlockSize)
		assert.Equal(t, blockCrc, msgs.Crc(actualBlockCrc))
		blocksCrcs[i] = blockCrc
	}
	blocks := make([][]byte, B)
	for i := range blocks {
		_, r := mkBlockReader(req, buf, i)
		blocks[i] = make([]byte, blockSize)
		_, err := io.ReadFull(r, blocks[i])
		if !assert.NoError(t, err) {
			return
		}
	}
	for s := 0; s < int(req.Stripes); s++ {
		stripeCrc := uint32(0)
		for d := 0; d < D; d++ {
			stripeCrc = crc32c.Append(stripeCrc, uint32(req.Crcs[B*s+d]), int(req.CellSize))
		}
		stripesCrcs[s] = msgs.Crc(stripeCrc)
	}
	blocksToRead := append([][]byte{}, blocks...)
	P := req.Parity.ParityBlocks()
	badConnections := []uint8{}
	if P > 0 {
		for i := 0; i < int(rand.Uint32())%(P+1); i++ {
			// have half failures to be bad bytes, half to be bad connections
			if rand.Uint32()%2 == 0 {
				badConnections = append(badConnections, uint8(int(rand.Uint32())%B))
			} else {
				byteIx := int(rand.Uint32()) % (blockSize * B)
				blockIx := byteIx / blockSize
				blocksToRead[blockIx] = append([]byte{}, blocksToRead[blockIx]...)
				blocksToRead[blockIx][byteIx%blockSize] = byte(rand.Uint32())
			}
		}
	}
	openBlockConns := int64(0)
	spanReader, err := readSpanFromBlocks(
		bufPool, sizeWithZeros, req.Crc, req.Parity, req.Stripes, req.CellSize, blocksCrcs, stripesCrcs,
		func(i int, offset uint32, size uint32) (io.ReadCloser, error) {
			for _, b := range badConnections {
				if int(b) == i {
					return nil, nil
				}
			}
			openBlockConns++
			return &mockedBlockConn{data: blocksToRead[i][int(offset):int(offset+size)], openConns: &openBlockConns}, nil
		},
	)
	if !assert.NoError(t, err) {
		return
	}
	cursor := 0
	for {
		read, err := spanReader.Read(tmpBuf)
		if err == io.EOF {
			break
		}
		if !assert.NoError(t, err) {
			return
		}
		assert.True(t, bytes.Equal(data[cursor:cursor+read], tmpBuf[:read]), "bad span contents from %v to %v", cursor, cursor+read)
		cursor += read
	}
	assert.Equal(t, len(data), cursor)
	spanReader.Close()
	assert.Equal(t, int64(0), openBlockConns)
}

func TestSpan(t *testing.T) {
	bufPool := NewReadSpanBufPool()
	rand := rand.New(rand.NewSource(0))
	for i := 0; i < 2000; i++ {
		D := 1 + uint8(rand.Uint32()%15)
		P := uint8(rand.Uint32() % 16)
		if D > 1 { // can't have D=1+N and P=0
			for P == 0 {
				P = uint8(rand.Uint32() % 16)
			}
		}
		parity := rs.MkParity(D, P)
		size := 256 + rand.Uint32()%(100<<10) // up to 100KiB
		stripeSize := 1 + rand.Uint32()%(size*2)
		testSpan(t, bufPool, rand, parity, size, stripeSize)
	}
}
