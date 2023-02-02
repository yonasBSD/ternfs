package eggs

import (
	"bytes"
	"crypto/cipher"
	"encoding/binary"
	"fmt"
	"io"
	"math/rand"
	"net"
	"xtx/eggsfs/msgs"
)

// TODO connection pool rather than opening a new one each time

// The first ip1/port1 cannot be zeroed, the second one can. One of them
// will be tried at random.
func BlockServiceConnection(log LogLevels, id msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (*net.TCPConn, error) {
	if port1 == 0 {
		panic(fmt.Errorf("ip1/port1 must be provided"))
	}
	var ips [2][4]byte
	ips[0] = ip1
	ips[1] = ip2
	var ports [2]uint16
	ports[0] = port1
	ports[1] = port2
	var errs [2]error
	var sock *net.TCPConn
	start := int(rand.Uint32())
	for i := start; i < start+2; i++ {
		ip := net.IP(ips[i%2][:])
		port := int(ports[i%2])
		if port == 0 {
			continue
		}
		sock, errs[i%2] = net.DialTCP("tcp4", nil, &net.TCPAddr{IP: ip, Port: port})
		if errs[i%2] == nil {
			return sock, nil
		}
		log.RaiseAlert(fmt.Errorf("Could not connect to block service %v:%v: %w. Might try other ip/port.", ip, port, errs[i%2]))
	}
	// return one of the two errors, we don't want to mess with them too much and they are alerts
	for _, err := range errs {
		if err != nil {
			return nil, err
		}
	}
	panic("impossible")
}

func bsReqInit(id msgs.BlockServiceId, kind byte) *bytes.Buffer {
	buf := bytes.NewBuffer([]byte{})
	bsWrite(buf, uint64(id))
	bsWrite(buf, kind)
	return buf
}

func bsWrite[V uint8 | uint32 | uint64](buf *bytes.Buffer, x V) {
	if err := binary.Write(buf, binary.LittleEndian, x); err != nil {
		panic(err)
	}
}

func bsSend(sock io.Writer, buf *bytes.Buffer) error {
	if _, err := sock.Write(buf.Bytes()); err != nil {
		return err
	}
	return nil
}

func bsRespInit(sock io.Reader, id msgs.BlockServiceId, kind byte) error {
	if err := bsExpect("block service id", sock, (uint64)(id)); err != nil {
		return err
	}
	if err := bsExpect("kind", sock, kind); err != nil {
		return err
	}
	return nil
}

func bsRead[V uint8 | uint32 | uint64](sock io.Reader) (x V, err error) {
	if err := binary.Read(sock, binary.LittleEndian, &x); err != nil {
		return 0, nil
	}
	return x, nil
}

func bsExpect[V uint8 | uint32 | uint64](what string, sock io.Reader, x V) error {
	y, err := bsRead[V](sock)
	if err != nil {
		return err
	}
	if x != y {
		return fmt.Errorf("expecting %s %v, got %v from block service", what, x, y)
	}
	return nil
}

func WriteBlock(
	logger LogLevels,
	conn interface {
		io.ReaderFrom
		io.Reader
		io.Writer
	},
	block *msgs.BlockInfo,
	r io.Reader, // only `size` bytes will be read
	size uint32,
	crc [4]byte,
) ([8]byte, error) {
	logger.Debug("writing block %+v with CRC %v", block, crc)
	var proof [8]byte
	// start writing the block: message (block_service_id, 'w', block_id, crc32, block_size, certificate)
	writeReq := bsReqInit(block.BlockServiceId, 'w')
	bsWrite(writeReq, uint64(block.BlockId))
	writeReq.Write(crc[:])
	bsWrite(writeReq, size)
	writeReq.Write(block.Certificate[:])
	err := bsSend(conn, writeReq)
	if err != nil {
		return proof, err
	}
	// Write block data
	lr := io.LimitedReader{
		R: r,
		N: int64(size),
	}
	if _, err := conn.ReadFrom(&lr); err != nil {
		return proof, fmt.Errorf("could not write block data to: %w", err)
	}
	// write response: (block_service_id, 'W', proof)
	if err := bsRespInit(conn, block.BlockServiceId, 'W'); err != nil {
		return proof, err
	}
	if _, err := io.ReadFull(conn, proof[:]); err != nil {
		return proof, fmt.Errorf("could not read proof from: %w", err)
	}
	// we're finally done
	return proof, nil
}

// Won't actually fetch the block -- it'll be readable from `conn` as this function terminates.
func FetchBlock(
	logger LogLevels,
	conn interface {
		io.Reader
		io.Writer
	},
	blockService *msgs.BlockService,
	blockId msgs.BlockId,
	crc32 [4]byte,
	offset uint32,
	count uint32,
) error {
	logger.Debug("fetching block %v from block service %+v, offset=%v, count=%v", blockId, blockService, offset, count)
	// start reading the block: message (block_service_id, 'f', block_id, offset)
	readReq := bsReqInit(blockService.Id, 'f')
	bsWrite(readReq, uint64(blockId))
	bsWrite(readReq, offset)
	bsWrite(readReq, count)
	err := bsSend(conn, readReq)
	if err != nil {
		return fmt.Errorf("could not read source block: %w", err)
	}
	// read response: (block_service_id, 'F', size)
	if err := bsRespInit(conn, blockService.Id, 'F'); err != nil {
		return err
	}
	return nil
}

func EraseBlock(
	logger LogLevels,
	conn interface {
		io.Writer
		io.Reader
	},
	block msgs.BlockInfo,
) ([8]byte, error) {
	logger.Debug("erasing block %+v", block)
	var proof [8]byte
	// message: (block_service_id, 'e', block_id, certificate)
	req := bsReqInit(block.BlockServiceId, 'e')
	bsWrite(req, uint64(block.BlockId))
	req.Write(block.Certificate[:])
	err := bsSend(conn, req)
	if err != nil {
		return proof, err
	}
	// response: (block_service_id, 'E', block_id, proof)
	if err := bsRespInit(conn, block.BlockServiceId, 'E'); err != nil {
		return proof, err
	}
	if _, err := io.ReadFull(conn, proof[:]); err != nil {
		return proof, fmt.Errorf("could not read proof from: %w", err)
	}
	return proof, nil
}

// returns the write proof
func CopyBlock(
	logger LogLevels,
	sourceConn interface {
		io.Reader
		io.Writer
	},
	sourceBlockService *msgs.BlockService,
	sourceBlockId msgs.BlockId,
	sourceBlockCrc [4]byte,
	sourceBlockSize uint64,
	dstConn interface {
		io.ReaderFrom
		io.Reader
		io.Writer
	},
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	var proof [8]byte
	var err error
	if err := FetchBlock(logger, sourceConn, sourceBlockService, sourceBlockId, sourceBlockCrc, 0, uint32(sourceBlockSize)); err != nil {
		return proof, err
	}
	proof, err = WriteBlock(logger, dstConn, dstBlock, sourceConn, uint32(sourceBlockSize), sourceBlockCrc)
	if err != nil {
		return proof, err
	}
	return proof, nil
}

// This is used to mock the block service in test
func BlockWriteProof(blockServiceId msgs.BlockServiceId, blockId msgs.BlockId, key cipher.Block) [8]byte {
	buf := bytes.NewBuffer([]byte{})
	// struct.pack_into('<QcQ', b, 0,  block_service_id, b'W', block_id)
	binary.Write(buf, binary.LittleEndian, uint64(blockServiceId))
	buf.Write([]byte{'W'})
	binary.Write(buf, binary.LittleEndian, uint64(blockId))
	return CBCMAC(key, buf.Bytes())
}

// This is used to mock the block service in test
func BlockEraseProof(blockServiceId msgs.BlockServiceId, blockId msgs.BlockId, key cipher.Block) [8]byte {
	buf := bytes.NewBuffer([]byte{})
	// struct.pack_into('<QcQ', b, 0, block['block_service_id'], b'E', block['block_id'])
	binary.Write(buf, binary.LittleEndian, uint64(blockServiceId))
	buf.Write([]byte{'E'})
	binary.Write(buf, binary.LittleEndian, uint64(blockId))

	return CBCMAC(key, buf.Bytes())
}
