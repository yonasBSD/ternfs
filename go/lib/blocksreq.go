package lib

import (
	"encoding/binary"
	"fmt"
	"io"
	"math/rand"
	"net"
	"xtx/eggsfs/msgs"
)

func BlockServiceConnection(log *Logger, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (*net.TCPConn, error) {
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
		log.RaiseAlert(fmt.Errorf("could not connect to block service %v:%v: %w, might try other ip/port", ip, port, errs[i%2]))
	}
	// return one of the two errors, we don't want to mess with them too much and they are alerts
	for _, err := range errs {
		if err != nil {
			return nil, err
		}
	}
	panic("impossible")
}

func ReadBlocksRequest(
	log *Logger,
	r io.Reader,
) (msgs.BlockServiceId, msgs.BlocksRequest, error) {
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return 0, nil, err
	}
	if protocol != msgs.BLOCKS_REQ_PROTOCOL_VERSION {
		log.RaiseAlert(fmt.Errorf("bad blocks protocol, expected %v, got %v", msgs.BLOCKS_REQ_PROTOCOL_VERSION, protocol))
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
	case msgs.WRITE_BLOCK:
		req = &msgs.WriteBlockReq{}
	case msgs.BLOCK_WRITTEN:
		req = &msgs.BlockWrittenReq{}
	default:
		log.RaiseAlert(fmt.Errorf("bad blocks request kind %v", kind))
		return 0, nil, msgs.MALFORMED_REQUEST
	}
	if err := req.Unpack(r); err != nil {
		return 0, nil, err
	}
	return msgs.BlockServiceId(blockServiceId), req, nil
}

func WriteBlocksRequest(log *Logger, w io.Writer, blockServiceId msgs.BlockServiceId, req msgs.BlocksRequest) error {
	// log.Debug("writing blocks request %v for block service id %v: %+v", req.BlocksRequestKind(), blockServiceId, req)
	if err := binary.Write(w, binary.LittleEndian, msgs.BLOCKS_REQ_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, blockServiceId); err != nil {
		return err
	}
	if _, err := w.Write([]byte{uint8(req.BlocksRequestKind())}); err != nil {
		return err
	}
	if err := req.Pack(w); err != nil {
		return err
	}
	return nil
}

func ReadBlocksResponse(
	log *Logger,
	r io.Reader,
	resp msgs.BlocksResponse,
) error {
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return fmt.Errorf("could not read protocol: %w", err)
	}
	if protocol != msgs.BLOCKS_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("bad blocks protocol, expected %v, got %v", msgs.BLOCKS_RESP_PROTOCOL_VERSION, protocol)
	}
	var kindByte [1]byte
	if _, err := io.ReadFull(r, kindByte[:]); err != nil {
		return fmt.Errorf("could not read kind: %w", err)
	}
	if kindByte[0] == msgs.ERROR {
		var err uint16
		if err := binary.Read(r, binary.LittleEndian, &err); err != nil {
			return fmt.Errorf("could not read error: %w", err)
		}
		return msgs.ErrCode(err)
	}
	kind := msgs.BlocksMessageKind(kindByte[0])
	if kind != resp.BlocksResponseKind() {
		return fmt.Errorf("bad blocks response kind %v, expected %v", kind, resp.BlocksResponseKind())
	}
	if err := resp.Unpack(r); err != nil {
		return fmt.Errorf("could not unpack response: %w", err)
	}
	return nil
}

func WriteBlocksResponse(log *Logger, w io.Writer, resp msgs.BlocksResponse) error {
	if err := binary.Write(w, binary.LittleEndian, msgs.BLOCKS_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if _, err := w.Write([]byte{uint8(resp.BlocksResponseKind())}); err != nil {
		return err
	}
	if err := resp.Pack(w); err != nil {
		return err
	}
	return nil
}

func WriteBlocksResponseError(log *Logger, w io.Writer, err msgs.ErrCode) error {
	if err := binary.Write(w, binary.LittleEndian, msgs.BLOCKS_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if _, err := w.Write([]byte{msgs.ERROR}); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, uint16(err)); err != nil {
		return err
	}
	return nil

}

func WriteBlock(
	logger *Logger,
	conn interface {
		io.ReaderFrom
		io.Reader
		io.Writer
	},
	block *msgs.BlockInfo,
	r io.Reader, // only `size` bytes will be read
	size uint32,
	crc msgs.Crc,
) ([8]byte, error) {
	logger.Debug("writing block %+v, size %v, CRC %v", block, size, crc)
	var proof [8]byte
	err := WriteBlocksRequest(logger, conn, block.BlockServiceId, &msgs.WriteBlockReq{
		BlockId:     block.BlockId,
		Crc:         crc,
		Size:        size,
		Certificate: block.Certificate,
	})
	if err != nil {
		return proof, err
	}
	if err := ReadBlocksResponse(logger, conn, &msgs.WriteBlockResp{}); err != nil {
		return proof, err
	}
	logger.Debug("got blocks response, starting to write data")
	lr := io.LimitedReader{
		R: r,
		N: int64(size),
	}
	if _, err := conn.ReadFrom(&lr); err != nil {
		return proof, fmt.Errorf("could not write block data to: %w", err)
	}
	logger.Debug("data written, getting proof")
	writtenResp := msgs.BlockWrittenResp{}
	if err := ReadBlocksResponse(logger, conn, &writtenResp); err != nil {
		return proof, err
	}
	logger.Debug("proof received")
	proof = writtenResp.Proof
	return proof, nil
}

// Won't actually fetch the block -- it'll be readable from `conn` as this function terminates.
// Note that this function will _not_ check the CRC of the block! You should probably do that
// before using the block in any meaningful way.
func FetchBlock(
	logger *Logger,
	conn interface {
		io.Reader
		io.Writer
	},
	blockService *msgs.BlockService,
	blockId msgs.BlockId,
	offset uint32,
	count uint32,
) error {
	logger.Debug("fetching block %v from block service %+v, offset=%v, count=%v", blockId, blockService, offset, count)
	req := msgs.FetchBlockReq{
		BlockId: blockId,
		Offset:  offset,
		Count:   count,
	}
	if err := WriteBlocksRequest(logger, conn, blockService.Id, &req); err != nil {
		return err
	}
	resp := msgs.FetchBlockResp{}
	if err := ReadBlocksResponse(logger, conn, &resp); err != nil {
		return err
	}
	return nil
}

func EraseBlock(
	logger *Logger,
	conn interface {
		io.Writer
		io.Reader
	},
	block msgs.BlockInfo,
) ([8]byte, error) {
	logger.Debug("erasing block %+v", block)
	var proof [8]byte
	req := msgs.EraseBlockReq{
		BlockId:     block.BlockId,
		Certificate: block.Certificate,
	}
	if err := WriteBlocksRequest(logger, conn, block.BlockServiceId, &req); err != nil {
		return proof, err
	}
	resp := msgs.EraseBlockResp{}
	if err := ReadBlocksResponse(logger, conn, &resp); err != nil {
		return proof, err
	}
	proof = resp.Proof
	return proof, nil
}
