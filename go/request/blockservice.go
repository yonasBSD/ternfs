package request

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"xtx/eggsfs/loglevels"
	"xtx/eggsfs/msgs"
)

// TODO connection pool rather than opening a new one each time

type blockServiceReq struct {
	id     msgs.BlockServiceId
	ip     net.IP
	port   int
	sock   *net.TCPConn
	kind   byte
	buf    []byte
	cursor int
}

func (req *blockServiceReq) init(id msgs.BlockServiceId, ip net.IP, port int, kind byte) error {
	sock, err := net.DialTCP("tcp4", nil, &net.TCPAddr{IP: ip, Port: port})
	if err != nil {
		return fmt.Errorf("could not connect to block service %v (%v:%d): %w", id, ip, port, err)
	}
	req.id = id
	req.ip = ip
	req.port = port
	req.sock = sock
	req.kind = kind
	req.writeUint64(uint64(id))
	req.writeByte(kind)
	return nil
}

func (req *blockServiceReq) close() {
	req.sock.Close()
}

func (req *blockServiceReq) send() error {
	if len(req.buf) != req.cursor {
		panic(fmt.Errorf("buf is not full, len=%d, cursor=%d", len(req.buf), req.cursor))
	}
	_, err := req.sock.Write(req.buf)
	if err != nil {
		return req.err(fmt.Sprintf("send req of kind %v", req.kind), err)
	}
	return nil
}

func (req *blockServiceReq) err(what string, err error) error {
	return fmt.Errorf("could not %s from block service %v (%v:%d): %w", what, req.id, req.ip, req.port, err)
}

func (req *blockServiceReq) writeByte(x byte) {
	req.buf[req.cursor] = x
	req.cursor++
}

func (req *blockServiceReq) writeUint64(x uint64) {
	binary.LittleEndian.PutUint64(req.buf[req.cursor:], x)
	req.cursor += 8
}

func (req *blockServiceReq) writeBytes(xs []byte) {
	copy(req.buf[req.cursor:], xs)
	req.cursor += len(req.buf)
}

type blockServiceResp struct {
	req    *blockServiceReq
	kind   byte
	buf    []byte
	cursor int
}

func (resp *blockServiceResp) init(req *blockServiceReq, kind byte) error {
	resp.req = req
	resp.kind = kind
	_, err := io.ReadFull(req.sock, resp.buf[:])
	if err != nil {
		return req.err("read response", err)
	}
	if err := resp.expectUint64(uint64(resp.req.id)); err != nil {
		return err
	}
	if err := resp.expectByte(kind); err != nil {
		return err
	}
	return resp.expectByte(kind)
}

func (resp *blockServiceResp) readByte() byte {
	x := resp.buf[resp.cursor]
	resp.cursor++
	return x
}

func (resp *blockServiceResp) expectByte(expected byte) error {
	got := resp.readByte()
	if expected != got {
		return fmt.Errorf("expecting byte %v, got %v from block service %v (%v:%d)", expected, got, resp.req.id, resp.req.ip, resp.req.port)
	}
	return nil
}

func (resp *blockServiceResp) readUint64() uint64 {
	x := binary.LittleEndian.Uint64(resp.buf[resp.cursor:])
	resp.cursor += 8
	return x
}

func (resp *blockServiceResp) expectUint64(expected uint64) error {
	got := resp.readUint64()
	if expected != got {
		return fmt.Errorf("expecting uint64 %v, got %v from block service %v (%v:%d)", expected, got, resp.req.id, resp.req.ip, resp.req.port)
	}
	return nil
}

func (resp *blockServiceResp) readBytes(bs []byte) {
	copy(bs, resp.buf[resp.cursor:])
	resp.cursor += len(resp.buf)
}

func EraseBlock(
	logger loglevels.LogLevels,
	block msgs.BlockInfo,
) ([8]byte, error) {
	var proof [8]byte
	// message: (block_service_id, 'e', block_id, certificate)
	var reqBuf [8 + 1 + 8 + 8]byte
	req := blockServiceReq{buf: reqBuf[:]}
	if err := req.init(block.BlockServiceId, net.IP(block.BlockServiceIp[:]), int(block.BlockServicePort), 'e'); err != nil {
		return proof, err
	}
	defer req.close()
	req.writeUint64(block.BlockId)
	req.writeBytes(block.Certificate[:])
	if err := req.send(); err != nil {
		return proof, err
	}
	// response: ('E', block_id, proof)
	var respBuf [17]byte
	resp := blockServiceResp{buf: respBuf[:]}
	if err := resp.init(&req, 'E'); err != nil {
		return proof, err
	}
	resp.readBytes(proof[:])
	return proof, nil
}

// returns the write proof
func CopyBlock(
	logger loglevels.LogLevels,
	sourceBlockServices []msgs.BlockService,
	sourceBlockSize uint64,
	sourceBlock *msgs.FetchedBlock,
	dstBlock *msgs.BlockInfo,
) ([8]byte, error) {
	var proof [8]byte
	sourceBlockService := sourceBlockServices[sourceBlock.BlockServiceIx]
	// start reading the block: message (block_service_id, 'f', block_id)
	var readReqBuf [8 + 1 + 8]byte
	readReq := blockServiceReq{buf: readReqBuf[:]}
	if err := readReq.init(sourceBlockService.Id, net.IP(sourceBlockService.Ip[:]), int(sourceBlockService.Port), 'f'); err != nil {
		return proof, err
	}
	defer readReq.close()
	readReq.writeUint64(uint64(sourceBlock.BlockId))
	readReq.send()
	// read response: (block_service_id, 'F', size)
	var readRespBuf [8 + 1 + 8]byte
	readResp := blockServiceResp{buf: readRespBuf[:]}
	if err := readResp.init(&readReq, 'F'); err != nil {
		return proof, err
	}
	if err := readResp.expectUint64(sourceBlockSize); err != nil {
		return proof, err
	}
	// read block
	block := make([]byte, sourceBlockSize)
	if _, err := io.ReadFull(readReq.sock, block); err != nil {
		return proof, readReq.err("read block", err)
	}
	// start writing the block: message (block_service_id, 'w', block_id, crc32, block_size, certificate)
	var writeReqBuf [8 + 1 + 8 + 4 + 8 + 8]byte
	writeReq := blockServiceReq{buf: writeReqBuf[:]}
	if err := writeReq.init(dstBlock.BlockServiceId, net.IP(dstBlock.BlockServiceIp[:]), int(dstBlock.BlockServicePort), 'w'); err != nil {
		return proof, err
	}
	defer writeReq.close()
	writeReq.writeUint64(dstBlock.BlockId)
	writeReq.writeBytes(sourceBlock.Crc32[:])
	writeReq.writeUint64(sourceBlockSize)
	writeReq.writeBytes(dstBlock.Certificate[:])
	// write response: (block_service_id, 'W', block_id, proof)
	var writeRespBuf [8 + 1 + 8 + 8]byte
	writeResp := blockServiceResp{buf: writeRespBuf[:]}
	if err := writeResp.init(&writeReq, 'W'); err != nil {
		return proof, err
	}
	writeResp.expectUint64(dstBlock.BlockId)
	writeResp.readBytes(proof[:])
	// Write block
	if _, err := writeReq.sock.Write(block); err != nil {
		return proof, writeReq.err("write block", err)
	}
	// we're finally done
	return proof, nil
}
