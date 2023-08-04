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
	sock, err := c.GetUDPSocket()
	if err != nil {
		return err
	}
	defer c.ReleaseUDPSocket(sock)
	hasSecondIp := 0
	if addrs[1].Port != 0 {
		hasSecondIp = 1
	}
	mtu := clientMtu
	respBuf := make([]byte, mtu)
	attempts := 0
	startedAt := time.Now()
	requestId := c.newRequestId()
	// will keep trying as long as we get timeouts
	timeoutAlert := log.NewNCAlert()
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
		log.DebugStack(1, "about to send request id %v (type %T) to shard %v using conn %v->%v, after %v attempts", requestId, reqBody, shid, addr, sock.LocalAddr(), attempts)
		log.Trace("reqBody %+v", reqBody)
		written, err := sock.WriteTo(reqBytes, addr)
		if err != nil {
			if isSendToEPERM(err) {
				if dontWait {
					log.Info("dontWait is on, we couldn't send the request due to EPERM %v, goodbye", err)
					return nil
				} else {
					log.RaiseNC(timeoutAlert, "got possibly transient EPERM when sending to shard %v, might retry after waiting for %v: %v", shid, timeout, err)
					time.Sleep(timeout)
					attempts++
					continue
				}
			}
			return err
		}
		if written < len(reqBytes) {
			panic(fmt.Sprintf("incomplete send to shard %v -- %v bytes written instead of %v", shid, written, len(reqBytes)))
		}
		if dontWait {
			log.Debug("dontWait is on, we've sent the request, goodbye")
			return nil
		}
		// Keep going until we found the right request id -- we can't assume that what we get isn't
		// some other request we thought was timed out.
		readLoopStart := time.Now()
		readLoopDeadline := readLoopStart.Add(timeout)
		sock.SetReadDeadline(readLoopDeadline)
		for {
			// We could discard immediatly if the addr doesn't match, but the req id protects
			// ourselves well enough from this anyway.
			read, _, err := sock.ReadFrom(respBuf)
			respBytes := respBuf[:read]
			if err != nil {
				// We retry very liberally rather than only with timeouts (at least for now),
				// to survive the shard dying in the most exotic ways.
				shouldRetry := false
				switch err.(type) {
				case net.Error, *net.OpError:
					shouldRetry = true
				}
				if shouldRetry {
					log.RaiseNC(timeoutAlert, "got network error %v to shard %v for req id %v of type %T, will try to retry", err, shid, requestId, reqBody)
					// make sure we've waited as much as the expected timeout, otherwise we might
					// call in a busy loop due to the server just not being up.
					time.Sleep(time.Until(readLoopDeadline))
					break // keep trying
				}
				// Something unexpected happen, exit immediately
				return err
			}
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
			// Check if we're interested in the request id we got
			if respRequestId.requestId != requestId {
				log.RaiseNC(timeoutAlert, "dropping response %v from shard %v, since we expected one of %v", respRequestId.requestId, shid, requestId)
				continue
			}
			// We are interested, parse the kind
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
				// If the kind dosen't match, it's bad, since it's the same request id
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
				log.RaiseNC(timeoutAlert, "got resp timeout error %v from shard %v, will try to retry", err, shid)
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
				log.Debug("got error %v for req %T id %v from shard %v (took %v)", *eggsError, req.body, req.requestId, shid, elapsed)
				return *eggsError
			}
			log.Debug("got response %T from shard %v (took %v)", respBody, shid, elapsed)
			log.Trace("respBody %+v", respBody)
			return nil
		}
		// We got through the loop busily consuming responses, now try again by sending a new request
		attempts++
	}
}
