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
		return nil, msgs.EggsError(err)
	}
	kind := msgs.ShuckleMessageKind(data[0])
	var resp msgs.ShuckleResponse
	switch kind {
	case msgs.LOCAL_SHARDS:
		resp = &msgs.LocalShardsResp{}
	case msgs.REGISTER_SHARD:
		resp = &msgs.RegisterShardResp{}
	case msgs.ALL_BLOCK_SERVICES:
		resp = &msgs.AllBlockServicesResp{}
	case msgs.SET_BLOCK_SERVICE_FLAGS:
		resp = &msgs.SetBlockServiceFlagsResp{}
	case msgs.DECOMMISSION_BLOCK_SERVICE:
		resp = &msgs.DecommissionBlockServiceResp{}
	case msgs.REGISTER_CDC:
		resp = &msgs.RegisterCdcResp{}
	case msgs.LOCAL_CDC:
		resp = &msgs.LocalCdcResp{}
	case msgs.CDC_REPLICAS_DE_PR_EC_AT_ED:
		resp = &msgs.CdcReplicasDEPRECATEDResp{}
	case msgs.INFO:
		resp = &msgs.InfoResp{}
	case msgs.SHARD_BLOCK_SERVICES_DE_PR_EC_AT_ED:
		resp = &msgs.ShardBlockServicesDEPRECATEDResp{}
	case msgs.ERASE_DECOMMISSIONED_BLOCK:
		resp = &msgs.EraseDecommissionedBlockResp{}
	case msgs.ALL_CDC:
		resp = &msgs.AllCdcResp{}
	case msgs.CLEAR_CDC_INFO:
		resp = &msgs.ClearCdcInfoResp{}
	case msgs.CLEAR_SHARD_INFO:
		resp = &msgs.ClearShardInfoResp{}
	case msgs.CREATE_LOCATION:
		resp = &msgs.CreateLocationResp{}
	case msgs.LOCAL_CHANGED_BLOCK_SERVICES:
		resp = &msgs.LocalChangedBlockServicesResp{}
	case msgs.LOCATIONS:
		resp = &msgs.LocationsResp{}
	case msgs.MOVE_CDC_LEADER:
		resp = &msgs.MoveCdcLeaderResp{}
	case msgs.MOVE_SHARD_LEADER:
		resp = &msgs.MoveShardLeaderResp{}
	case msgs.RENAME_LOCATION:
		resp = &msgs.RenameLocationResp{}
	case msgs.ALL_SHARDS:
		resp = &msgs.AllShardsResp{}
	case msgs.SHUCKLE:
		resp = &msgs.ShuckleResp{}
	case msgs.CDC_AT_LOCATION:
		resp = &msgs.CdcAtLocationResp{}
	case msgs.CHANGED_BLOCK_SERVICES_AT_LOCATION:
		resp = &msgs.ChangedBlockServicesAtLocationResp{}
	case msgs.REGISTER_BLOCK_SERVICES:
		resp = &msgs.RegisterBlockServicesResp{}
	case msgs.SHARDS_AT_LOCATION:
		resp = &msgs.ShardsAtLocationResp{}
	case msgs.SHARD_BLOCK_SERVICES:
		resp = &msgs.ShardBlockServicesResp{}
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
	conn := MakeShuckleConn(log, timeout, shuckleAddress, 1)
	defer conn.Close()
	return conn.Request(req)
}

type shuckResp struct {
	resp msgs.ShuckleResponse
	err  error
}

type shuckReq struct {
	req   msgs.ShuckleRequest
	respC chan<- shuckResp
}

type ShuckleConn struct {
	log            *lib.Logger
	timeout        *lib.ReqTimeouts
	connTimeout    time.Duration
	shuckleAddress string
	reqChan        chan shuckReq
	numHandlers    uint
}

func MakeShuckleConn(
	log *lib.Logger,
	timeout *lib.ReqTimeouts,
	shuckleAddress string,
	numHandlers uint,
) *ShuckleConn {
	if timeout == nil {
		timeout = &DefaultShuckleTimeout
	}
	shuckConn := ShuckleConn{log, timeout, 5 * time.Second, shuckleAddress, make(chan shuckReq), 0}
	shuckConn.IncreaseNumHandlersTo(numHandlers)
	return &shuckConn
}

func (c *ShuckleConn) IncreaseNumHandlersTo(numHandlers uint) {
	for i := c.numHandlers; i < numHandlers; i++ {
		go func() {
			c.requestHandler()
		}()
	}
	c.numHandlers = numHandlers
}

func (c *ShuckleConn) requestHandler() {
	var conn net.Conn
	var err error
	defer func() {
		if conn != nil {
			conn.Close()
		}
	}()
	for {
		reconnectAttempted := false
		req, ok := <-c.reqChan
		if !ok {
			return
		}
	Reconnect:
		if conn == nil {
			if !reconnectAttempted {
				reconnectAttempted = true
				if conn, err = c.connect(); err != nil {
					req.respC <- shuckResp{nil, err}
					continue
				}
			} else {
				req.respC <- shuckResp{nil, fmt.Errorf("couldn't connect to to shuckle")}
				continue
			}
		}
		if err = writeShuckleRequest(c.log, conn, req.req); err != nil {
			conn.Close()
			conn = nil
			goto Reconnect
		}
		resp, err := readShuckleResponse(c.log, conn)
		if err != nil {
			if _, isEggsErr := err.(msgs.EggsError); !isEggsErr {
				conn.Close()
				conn = nil
			}
		}
		req.respC <- shuckResp{resp, err}
	}
}

func (c *ShuckleConn) connect() (net.Conn, error) {
	start := time.Now()

	var err error
	var conn net.Conn
	var delay time.Duration

	goto ReconnectBegin

Reconnect:
	delay = c.timeout.Next(start)
	if delay == 0 {
		c.log.Info("could not connect to shuckle and we're out of attempts: %v", err)
		return nil, err
	}
	time.Sleep(delay)

ReconnectBegin:
	conn, err = net.DialTimeout("tcp", c.shuckleAddress, c.connTimeout)
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
		c.log.Info("could not connect to shuckle: %v", err)
		return nil, err
	}
	return conn, nil
}

func (c *ShuckleConn) Request(req msgs.ShuckleRequest) (msgs.ShuckleResponse, error) {
	respC := make(chan shuckResp)
	c.reqChan <- shuckReq{req, respC}
	resp := <-respC
	return resp.resp, resp.err
}

func (c *ShuckleConn) ShuckleAddress() string{
	return c.shuckleAddress
}

func (c *ShuckleConn) Close() {
	close(c.reqChan)
}
