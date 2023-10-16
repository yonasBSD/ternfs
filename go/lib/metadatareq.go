package lib

import (
	"bytes"
	"crypto/cipher"
	"fmt"
	"io"
	"net"
	"os"
	"sync/atomic"
	"syscall"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

// Starts from 1, we use 0 as a placeholder in `requestIds`
func (c *Client) newRequestId() uint64 {
	return atomic.AddUint64(&c.requestIdCounter, 1)
}

type metadataRequest struct {
	requestId uint64
	shid      int16
	kind      uint8
	body      bincode.Packable
}

func (req *metadataRequest) Pack(w io.Writer) error {
	proto := msgs.SHARD_REQ_PROTOCOL_VERSION
	if req.shid < 0 {
		proto = msgs.CDC_REQ_PROTOCOL_VERSION
	}
	if err := bincode.PackScalar(w, proto); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, req.requestId); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, req.kind); err != nil {
		return err
	}
	if err := req.body.Pack(w); err != nil {
		return err
	}
	return nil
}

func packMetadataRequest(req *metadataRequest, cdcKey cipher.Block) []byte {
	buf := bytes.NewBuffer([]byte{})
	if err := req.Pack(buf); err != nil {
		panic(err)
	}
	bs := buf.Bytes()
	if req.shid >= 0 && (req.kind&0x80) != 0 {
		// privileged, we must have a key
		if cdcKey == nil {
			panic(fmt.Errorf("trying to encode request of privileged kind %T, but got no CDC key", req.body))
		}
		mac := CBCMAC(cdcKey, bs)
		buf.Write(mac[:])
		bs = buf.Bytes()
	}
	return bs
}

// We get EPERMs when nf drops packets, we want to retry in those cases.
func isSendToEPERM(err error) bool {
	if opErr, ok := err.(*net.OpError); ok {
		if syscallErr, ok := opErr.Err.(*os.SyscallError); ok {
			if errno, ok := syscallErr.Err.(syscall.Errno); ok && errno == syscall.EPERM {
				return true
			}
		}
	}
	return false
}

type unpackedMetadataRequestId struct {
	shid      int16
	requestId uint64
}

type badRespProtocol struct {
	expectedProtocol uint32
	receivedProtocol uint32
}

func (e *badRespProtocol) Error() string {
	return fmt.Sprintf("expected protocol version %v, but got %v", e.expectedProtocol, e.receivedProtocol)
}

func (requestId *unpackedMetadataRequestId) Unpack(r io.Reader) error {
	var ver uint32
	if err := bincode.UnpackScalar(r, &ver); err != nil {
		return err
	}
	protocol := msgs.SHARD_RESP_PROTOCOL_VERSION
	if requestId.shid < 0 {
		protocol = msgs.CDC_RESP_PROTOCOL_VERSION
	}
	if ver != protocol {
		return &badRespProtocol{
			expectedProtocol: protocol,
			receivedProtocol: ver,
		}
	}
	if err := bincode.UnpackScalar(r, &requestId.requestId); err != nil {
		return err
	}
	return nil
}

func (c *Client) metadataRequest(
	log *Logger,
	shid int16, // -1 for cdc
	addrs *[2]net.UDPAddr,
	msgKind uint8,
	reqBody bincode.Packable,
	respBody bincode.Unpackable,
	counters map[uint8]*ReqCounters,
	dontWait bool,
) error {
	hasSecondIp := 0
	if addrs[1].Port != 0 {
		hasSecondIp = 1
	}
	attempts := 0
	startedAt := time.Now()
	requestId := c.newRequestId()
	defer func() {
		c.clientMetadata.requests <- &metadataProcessorRequest{
			requestId: requestId,
			clear:     true,
		}
	}()
	respChan := make(chan []byte, 16)
	// will keep trying as long as we get timeouts
	timeoutAlertQuietPeriod := time.Second
	if shid < 0 {
		// currently the CDC can be extremely slow as we sync stuff
		timeoutAlertQuietPeriod = time.Minute
	}
	timeoutAlert := log.NewNCAlert(timeoutAlertQuietPeriod)
	// We return the error anyway, so once we exit this function this alert must be gone
	defer log.ClearNC(timeoutAlert)
	timeoutAlertAndStandalone := func(f string, v ...any) {
		log.RaiseNC(timeoutAlert, f, v...)
		log.RaiseAlert(f, v...)
	}
	timeouts := c.shardTimeout
	if shid < 0 {
		timeouts = c.cdcTimeout
	}
	for {
		now := time.Now()
		timeout := timeouts.NextNow(startedAt, now)
		if timeout == 0 {
			log.RaiseAlert("giving up on request to shard %v after waiting for %v", shid, now.Sub(startedAt))
			return msgs.TIMEOUT
		}
		if counters != nil {
			atomic.AddUint64(&counters[msgKind].Attempts, 1)
		}
		req := metadataRequest{
			requestId: requestId,
			body:      reqBody,
			kind:      msgKind,
			shid:      shid,
		}
		reqBytes := packMetadataRequest(&req, c.cdcKey)
		addr := &addrs[(startedAt.Nanosecond()+attempts)&1&hasSecondIp]
		log.DebugStack(1, "about to send request id %v (type %T) to shard %v through processor, after %v attempts", requestId, reqBody, shid, attempts)
		log.Trace("reqBody %+v", reqBody)
		c.clientMetadata.requests <- &metadataProcessorRequest{
			requestId: requestId,
			addr:      addr,
			body:      reqBytes,
			respCh:    respChan,
			timeout:   timeout,
		}
		if dontWait {
			log.Debug("dontWait is on, we've sent the request, goodbye")
			return nil
		}
		log.DebugStack(1, "waiting for response for req id %v on channel", requestId)
		respBytes := <-respChan
		if respBytes != nil { // timed out
			// we got something
			// Start parsing, from the header with request id
			respReader := bytes.NewReader(respBytes)
			respRequestId := unpackedMetadataRequestId{
				shid: shid,
			}
			if err := (&respRequestId).Unpack(respReader); err != nil {
				if protocolError, ok := err.(*badRespProtocol); ok && (protocolError.receivedProtocol == msgs.CDC_RESP_PROTOCOL_VERSION || protocolError.receivedProtocol == msgs.SHARD_RESP_PROTOCOL_VERSION) {
					log.RaiseNC(timeoutAlert, "received shard/CDC protocol, probably a late shard/CDC request: %v", err)
				} else {
					timeoutAlertAndStandalone("could not decode Shard response header for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err)
				}
				continue
			}
			// we can never get the wrong resp id
			if respRequestId.requestId != requestId {
				panic(fmt.Errorf("got response %v rather than %v", respRequestId.requestId, requestId))
			}
			// pparse the kind
			var kind uint8
			if err := bincode.UnpackScalar(respReader, &kind); err != nil {
				timeoutAlertAndStandalone("could not decode Shard response kind for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err)
				continue
			}
			var eggsError *msgs.ErrCode
			if kind == msgs.ERROR_KIND {
				// If the kind is an error, parse it
				var eggsErrVal msgs.ErrCode
				eggsError = &eggsErrVal
				if err := eggsError.Unpack(respReader); err != nil {
					timeoutAlertAndStandalone("could not decode Shard response error for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err)
					continue
				}
			} else {
				// If the kind doesn't match, it's bad, since it's the same request id
				if kind != msgKind {
					timeoutAlertAndStandalone("dropping response %v from shard %v, since we it is of kind %v while we expected %v", respRequestId.requestId, shid, msgs.ShardMessageKind(kind), msgKind)
					continue
				}
				// Otherwise, finally parse the body
				if err := respBody.Unpack(respReader); err != nil {
					timeoutAlertAndStandalone("could not decode Shard response body for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err)
					continue
				}
			}
			// Check that we've parsed everything
			if respReader.Len() != 0 {
				return fmt.Errorf("malformed response, %v leftover bytes", respReader.Len())
			}
			// If we've got a timeout, keep trying
			if eggsError != nil && *eggsError == msgs.TIMEOUT {
				log.RaiseNC(timeoutAlert, "got resp timeout error %v from shard %v, will try to retry", eggsError, shid)
				break // keep trying
			}
			// At this point, we know we've got a response
			elapsed := time.Since(startedAt)
			if counters != nil {
				counters[msgKind].Timings.Add(elapsed)
			}
			// If we're past the first attempt, there are cases where errors are not what they seem.
			if eggsError != nil && attempts > 0 {
				if shid >= 0 {
					eggsError = c.checkRepeatedShardRequestError(log, reqBody.(msgs.ShardRequest), respBody.(msgs.ShardResponse), *eggsError)
				} else {
					eggsError = c.checkRepeatedCDCRequestError(log, reqBody.(msgs.CDCRequest), respBody.(msgs.CDCResponse), *eggsError)
				}
			}
			// Check if it's an error or not. We only use debug here because some errors are legitimate
			// responses (e.g. FILE_EMPTY)
			if eggsError != nil {
				log.DebugStack(1, "got error %v for req %T id %v from shard %v (took %v)", *eggsError, req.body, req.requestId, shid, elapsed)
				return *eggsError
			}
			log.Debug("got response %T from shard %v (took %v)", respBody, shid, elapsed)
			log.Trace("respBody %+v", respBody)
			return nil
		} else {
			log.RaiseNC(timeoutAlert, "timed out when receiving resp %v of typ %T (started at %v) to shard %v, might retry", requestId, reqBody, startedAt, shid)
		}
		attempts++
	}
	panic("IMPOSSIBLE")
}
