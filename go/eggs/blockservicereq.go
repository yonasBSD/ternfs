package eggs

import (
	"bytes"
	"crypto/cipher"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"xtx/eggsfs/msgs"
)

// TODO connection pool rather than opening a new one each time

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

func bsSend(ip net.IP, port uint16, buf *bytes.Buffer) (*net.TCPConn, error) {
	sock, err := net.DialTCP("tcp4", nil, &net.TCPAddr{IP: ip, Port: int(port)})
	if err != nil {
		return nil, fmt.Errorf("could not connect to block service %v:%d: %w", ip, port, err)
	}
	if _, err := sock.Write(buf.Bytes()); err != nil {
		return nil, err
	}
	return sock, nil
}

func bsRespInit(id msgs.BlockServiceId, kind byte, sock *net.TCPConn) error {
	if err := bsExpect("block service id", sock, (uint64)(id)); err != nil {
		return err
	}
	if err := bsExpect("kind", sock, kind); err != nil {
		return err
	}
	return nil
}

func bsRead[V uint8 | uint32 | uint64](sock *net.TCPConn) (x V, err error) {
	if err := binary.Read(sock, binary.LittleEndian, &x); err != nil {
		return 0, nil
	}
	return x, nil
}

func bsExpect[V uint8 | uint32 | uint64](what string, sock *net.TCPConn, x V) error {
	y, err := bsRead[V](sock)
	if err != nil {
		return err
	}
	if x != y {
		return fmt.Errorf("expecting %s %v, got %v from block service %v", what, x, y, sock.RemoteAddr())
	}
	return nil
}

func WriteBlock(
	logger LogLevels,
	block *msgs.BlockInfo,
	data []byte,
	crc [4]byte,
) ([8]byte, error) {
	logger.Debug("writing block %+v with CRC %v", block, crc)
	var proof [8]byte
	// start writing the block: message (block_service_id, 'w', block_id, crc32, block_size, certificate)
	writeReq := bsReqInit(block.BlockServiceId, 'w')
	bsWrite(writeReq, uint64(block.BlockId))
	writeReq.Write(crc[:])
	bsWrite(writeReq, uint32(len(data)))
	writeReq.Write(block.Certificate[:])
	conn, err := bsSend(block.BlockServiceIp[:], block.BlockServicePort, writeReq)
	if err != nil {
		return proof, err
	}
	defer conn.Close()
	// Write block data
	logger.Debug("writing block data")
	if _, err := conn.Write(data); err != nil {
		return proof, fmt.Errorf("could not write block data to %v: %w", conn.RemoteAddr(), err)
	}
	logger.Debug("block data written")
	// write response: (block_service_id, 'W', block_id, proof)
	if err := bsRespInit(block.BlockServiceId, 'W', conn); err != nil {
		return proof, err
	}
	if _, err := io.ReadFull(conn, proof[:]); err != nil {
		return proof, fmt.Errorf("could not read proof from %v: %w", conn.RemoteAddr(), err)
	}
	logger.Debug("response gotten")
	// we're finally done
	return proof, nil
}

// Reads so that `data` is filled, or fails. It might partially fill `data` before failing.
func FetchBlock(
	logger LogLevels,
	blockServices []msgs.BlockService,
	block *msgs.FetchedBlock,
	offset uint32,
	data []byte,
) error {
	blockService := blockServices[block.BlockServiceIx]
	logger.Debug("fetching block %+v from block service %+v", block, blockService)
	// start reading the block: message (block_service_id, 'f', block_id, offset)
	readReq := bsReqInit(blockService.Id, 'f')
	bsWrite(readReq, uint64(block.BlockId))
	bsWrite(readReq, offset)
	bsWrite(readReq, uint32(len(data)))
	conn, err := bsSend(blockService.Ip[:], blockService.Port, readReq)
	if err != nil {
		return fmt.Errorf("could not read source block from %v: %w", conn.RemoteAddr(), err)
	}
	defer conn.Close()
	// read response: (block_service_id, 'F', size)
	if err := bsRespInit(blockService.Id, 'F', conn); err != nil {
		return err
	}
	// read block data
	if _, err := io.ReadFull(conn, data); err != nil {
		return fmt.Errorf("could not read block data from %v: %w", conn.RemoteAddr(), err)
	}
	return nil
}

func FetchFullBlock(
	logger LogLevels,
	blockServices []msgs.BlockService,
	block *msgs.FetchedBlock,
	data []byte,
) error {
	if err := FetchBlock(logger, blockServices, block, 0, data); err != nil {
		return err
	}
	// verify crc
	crc := CRC32C(data)
	if crc != block.Crc32 {
		return fmt.Errorf("mismatching CRC32 for block %v: expected %v, got %v", block.BlockId, block.Crc32, crc)
	}
	return nil
}

func EraseBlock(
	logger LogLevels,
	block msgs.BlockInfo,
) ([8]byte, error) {
	logger.Debug("erasing block %+v", block)
	var proof [8]byte
	// message: (block_service_id, 'e', block_id, certificate)
	req := bsReqInit(block.BlockServiceId, 'e')
	bsWrite(req, uint64(block.BlockId))
	req.Write(block.Certificate[:])
	conn, err := bsSend(block.BlockServiceIp[:], block.BlockServicePort, req)
	if err != nil {
		return proof, err
	}
	defer conn.Close()
	// response: (block_service_id, 'E', block_id, proof)
	if err := bsRespInit(block.BlockServiceId, 'E', conn); err != nil {
		return proof, err
	}
	if _, err := io.ReadFull(conn, proof[:]); err != nil {
		return proof, fmt.Errorf("could not read proof from %v: %w", conn.RemoteAddr(), err)
	}
	return proof, nil
}

// returns the write proof
func CopyBlock(
	logger LogLevels,
	sourceBlockServices []msgs.BlockService,
	sourceBlockSize uint64,
	sourceBlock *msgs.FetchedBlock,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	var proof [8]byte
	var err error
	sourceData := make([]byte, sourceBlockSize)
	if err := FetchFullBlock(logger, sourceBlockServices, sourceBlock, sourceData); err != nil {
		return proof, err
	}
	proof, err = WriteBlock(logger, dstBlock, sourceData, sourceBlock.Crc32)
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
