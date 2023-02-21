package lib

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

const DEFAULT_SHUCKLE_ADDRESS = "REDACTED"

func ReadShuckleRequest(
	log *Logger,
	r io.Reader,
) (msgs.ShuckleRequest, error) {
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return nil, fmt.Errorf("could not read protocol: %w", err)
	}
	if protocol != msgs.SHUCKLE_REQ_PROTOCOL_VERSION {
		return nil, fmt.Errorf("bad shuckle protocol, expected %v, got %v", msgs.SHUCKLE_REQ_PROTOCOL_VERSION, protocol)
	}
	var len uint32
	if err := binary.Read(r, binary.LittleEndian, &len); err != nil {
		return nil, fmt.Errorf("could not read len: %w", err)
	}
	data := make([]byte, len)
	if _, err := io.ReadFull(r, data); err != nil {
		return nil, err
	}
	kind := msgs.ShuckleMessageKind(data[0])
	var req msgs.ShuckleRequest
	switch kind {
	case msgs.BLOCK_SERVICES_FOR_SHARD:
		req = &msgs.BlockServicesForShardReq{}
	case msgs.REGISTER_BLOCK_SERVICES:
		req = &msgs.RegisterBlockServicesReq{}
	case msgs.SHARDS:
		req = &msgs.ShardsReq{}
	case msgs.REGISTER_SHARD:
		req = &msgs.RegisterShardReq{}
	case msgs.ALL_BLOCK_SERVICES:
		req = &msgs.AllBlockServicesReq{}
	case msgs.REGISTER_CDC:
		req = &msgs.RegisterCdcReq{}
	case msgs.CDC:
		req = &msgs.CdcReq{}
	default:
		return nil, fmt.Errorf("bad shuckle request kind %v", kind)
	}
	if err := bincode.Unpack(data[1:], req); err != nil {
		return nil, err
	}
	return req, nil
}

func WriteShuckleRequest(log *Logger, w io.Writer, req msgs.ShuckleRequest) error {
	log.Debug("sending request %v to shuckle", req.ShuckleRequestKind())
	// serialize
	bytes := bincode.Pack(req)
	// write out
	if err := binary.Write(w, binary.LittleEndian, msgs.SHUCKLE_REQ_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, uint32(1+len(bytes))); err != nil {
		return err
	}
	if _, err := w.Write([]byte{uint8(req.ShuckleRequestKind())}); err != nil {
		return err
	}
	if _, err := w.Write(bytes); err != nil {
		return err
	}
	return nil
}

func ReadShuckleResponse(
	log *Logger,
	r io.Reader,
) (msgs.ShuckleResponse, error) {
	log.Debug("reading response from shuckle")
	var protocol uint32
	if err := binary.Read(r, binary.LittleEndian, &protocol); err != nil {
		return nil, fmt.Errorf("could not read protocol: %w", err)
	}
	if protocol != msgs.SHUCKLE_RESP_PROTOCOL_VERSION {
		return nil, fmt.Errorf("bad shuckle protocol, expected %v, got %v", msgs.SHUCKLE_RESP_PROTOCOL_VERSION, protocol)
	}
	var len uint32
	if err := binary.Read(r, binary.LittleEndian, &len); err != nil {
		return nil, fmt.Errorf("could not read len: %w", err)
	}
	data := make([]byte, len)
	if _, err := io.ReadFull(r, data); err != nil {
		return nil, err
	}
	kind := msgs.ShuckleMessageKind(data[0])
	var resp msgs.ShuckleResponse
	switch kind {
	case msgs.BLOCK_SERVICES_FOR_SHARD:
		resp = &msgs.BlockServicesForShardResp{}
	case msgs.REGISTER_BLOCK_SERVICES:
		resp = &msgs.RegisterBlockServicesResp{}
	case msgs.SHARDS:
		resp = &msgs.ShardsResp{}
	case msgs.REGISTER_SHARD:
		resp = &msgs.RegisterShardResp{}
	case msgs.ALL_BLOCK_SERVICES:
		resp = &msgs.AllBlockServicesResp{}
	case msgs.REGISTER_CDC:
		resp = &msgs.RegisterCdcResp{}
	case msgs.CDC:
		resp = &msgs.CdcResp{}
	default:
		return nil, fmt.Errorf("bad shuckle response kind %v", kind)
	}
	log.Debug("read response %v from shuckle", kind)
	if err := bincode.Unpack(data[1:], resp); err != nil {
		return nil, err
	}
	return resp, nil
}

func WriteShuckleResponse(log *Logger, w io.Writer, resp msgs.ShuckleResponse) error {
	// serialize
	bytes := bincode.Pack(resp)
	// write out
	if err := binary.Write(w, binary.LittleEndian, msgs.SHUCKLE_RESP_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := binary.Write(w, binary.LittleEndian, uint32(1+len(bytes))); err != nil {
		return err
	}
	if _, err := w.Write([]byte{uint8(resp.ShuckleResponseKind())}); err != nil {
		return err
	}
	if _, err := w.Write(bytes); err != nil {
		return err
	}
	return nil
}

func ShuckleRequest(
	log *Logger,
	shuckleAddress string,
	req msgs.ShuckleRequest,
) (msgs.ShuckleResponse, error) {
	conn, err := net.Dial("tcp", shuckleAddress)
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	log.Debug("connected to shuckle")
	if err := WriteShuckleRequest(log, conn, req); err != nil {
		return nil, err
	}
	return ReadShuckleResponse(log, conn)
}
