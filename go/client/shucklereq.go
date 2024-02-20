package client

import (
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"syscall"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

const DEFAULT_SHUCKLE_ADDRESS = "REDACTED"

func writeShuckleRequest(log *lib.Logger, w io.Writer, req msgs.ShuckleRequest) error {
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

func readShuckleResponse(
	log *lib.Logger,
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
	if data[0] == msgs.ERROR {
		var err uint16
		if err := binary.Read(r, binary.LittleEndian, &err); err != nil {
			return nil, fmt.Errorf("could not read error: %w", err)
		}
		return nil, msgs.ErrCode(err)
	}
	kind := msgs.ShuckleMessageKind(data[0])
	var resp msgs.ShuckleResponse
	switch kind {
	case msgs.REGISTER_BLOCK_SERVICES:
		resp = &msgs.RegisterBlockServicesResp{}
	case msgs.SHARDS:
		resp = &msgs.ShardsResp{}
	case msgs.REGISTER_SHARD:
		resp = &msgs.RegisterShardResp{}
	case msgs.REGISTER_SHARD_REPLICA:
		resp = &msgs.RegisterShardReplicaResp{}
	case msgs.SHARD_REPLICAS:
		resp = &msgs.ShardReplicasResp{}
	case msgs.ALL_BLOCK_SERVICES:
		resp = &msgs.AllBlockServicesResp{}
	case msgs.SET_BLOCK_SERVICE_FLAGS:
		resp = &msgs.SetBlockServiceFlagsResp{}
	case msgs.REGISTER_CDC:
		resp = &msgs.RegisterCdcResp{}
	case msgs.REGISTER_CDC_REPLICA:
		resp = &msgs.RegisterCdcReplicaResp{}
	case msgs.CDC:
		resp = &msgs.CdcResp{}
	case msgs.CDC_REPLICAS:
		resp = &msgs.CdcReplicasResp{}
	case msgs.INFO:
		resp = &msgs.InfoResp{}
	case msgs.BLOCK_SERVICE:
		resp = &msgs.BlockServiceResp{}
	case msgs.INSERT_STATS:
		resp = &msgs.InsertStatsResp{}
	case msgs.SHARD:
		resp = &msgs.ShardResp{}
	case msgs.GET_STATS:
		resp = &msgs.GetStatsResp{}
	default:
		return nil, fmt.Errorf("bad shuckle response kind %v", kind)
	}
	log.Debug("read response %v from shuckle", kind)
	if err := bincode.Unpack(data[1:], resp); err != nil {
		return nil, err
	}
	return resp, nil
}

var DefaultShuckleTimeout = lib.ReqTimeouts{
	Initial: 100 * time.Millisecond,
	Max:     1 * time.Second,
	Overall: 10 * time.Second,
	Growth:  1.5,
	Jitter:  0.1,
}

func ShuckleRequest(
	log *lib.Logger,
	timeout *lib.ReqTimeouts,
	shuckleAddress string,
	req msgs.ShuckleRequest,
) (msgs.ShuckleResponse, error) {
	start := time.Now()

	if timeout == nil {
		timeout = &DefaultShuckleTimeout
	}

	alert := log.NewNCAlert(10 * time.Second)
	defer log.ClearNC(alert)

	var err error
	var conn net.Conn
	var delay time.Duration

	goto ReconnectBegin

Reconnect:

	delay = timeout.Next(start)
	if delay == 0 {
		log.Info("could not connect to shuckle and we're out of attempts: %v", err)
		return nil, err
	}
	log.RaiseNCStack(alert, 1, "could not connect to shuckle, will retry in %v: %v", delay, err)
	time.Sleep(delay)

ReconnectBegin:
	conn, err = net.Dial("tcp", shuckleAddress)
	if err != nil {
		if opErr, ok := err.(*net.OpError); ok {
			if syscallErr, ok := opErr.Err.(*os.SyscallError); ok {
				if errno, ok := syscallErr.Err.(syscall.Errno); ok && (errno == syscall.ECONNREFUSED || errno == syscall.ETIMEDOUT) {
					goto Reconnect
				}
			}
			if opErr, ok := err.(*net.OpError); ok {
				if dnsErr, ok := opErr.Err.(*net.DNSError); ok && (dnsErr.IsTemporary || dnsErr.IsTimeout) {
					goto Reconnect
				}
			}
		}
		return nil, err
	}
	defer conn.Close()
	log.Debug("connected to shuckle")
	if err := writeShuckleRequest(log, conn, req); err != nil {
		return nil, err
	}
	return readShuckleResponse(log, conn)
}
