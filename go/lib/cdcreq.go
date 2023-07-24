// TODO Embarassing amounts of duplication between this and shard.go. Probably good to deduplicate.
package lib

import (
	"bytes"
	"fmt"
	"io"
	"math"
	"net"
	"sync/atomic"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type cdcRequest struct {
	requestId uint64
	body      msgs.CDCRequest
}

func (req *cdcRequest) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, msgs.CDC_REQ_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, req.requestId); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(req.body.CDCRequestKind())); err != nil {
		return err
	}
	if err := req.body.Pack(w); err != nil {
		return err
	}
	return nil
}

type CDCResponse struct {
	RequestId uint64
	Body      msgs.CDCResponse
}

type unpackedCDCRequestId uint64

func (requestId *unpackedCDCRequestId) Unpack(r io.Reader) error {
	var ver uint32
	if err := bincode.UnpackScalar(r, &ver); err != nil {
		return err
	}
	if ver != msgs.CDC_RESP_PROTOCOL_VERSION {
		return &badRespProtocol{
			expectedProtocol: msgs.CDC_RESP_PROTOCOL_VERSION,
			receivedProtocol: ver,
		}
	}
	if err := bincode.UnpackScalar(r, (*uint64)(requestId)); err != nil {
		return err
	}
	return nil
}

func (c *Client) checkRepeatedCDCRequestError(
	logger *Logger,
	// these are already filled in by now
	req cdcRequest,
	resp msgs.CDCResponse,
	respErr msgs.ErrCode,
) *msgs.ErrCode {
	switch reqBody := req.body.(type) {
	case *msgs.RenameDirectoryReq:
		// We repeat the request, but the previous had actually gone through:
		// we need to check if we haven't created the thing already.
		if respErr == msgs.EDGE_NOT_FOUND {
			// Happens when a request succeeds, and then the response gets lost.
			// We try to apply some heuristics to let this slide. See convo following
			// <https://eulergamma.slack.com/archives/C03PCJMGAAC/p1673547512380409>.
			//
			// Specifically, check that the last snapshot edge is what we expect if
			// we had  just moved it, and that the target edge also exists.
			logger.Info("following up on EDGE_NOT_FOUND after repeated RenameDirectoryReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.OldOwnerId, reqBody.TargetId, reqBody.OldName, reqBody.OldCreationTime, false) {
				return &respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.RenameDirectoryResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.NewOwnerId, reqBody.TargetId, reqBody.NewName, &respBody.CreationTime) {
				return &respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	// in a decent language this branch and the previous could be merged
	case *msgs.RenameFileReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated RenameFileReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.OldOwnerId, reqBody.TargetId, reqBody.OldName, reqBody.OldCreationTime, false) {
				return &respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.RenameFileResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.NewOwnerId, reqBody.TargetId, reqBody.NewName, &respBody.CreationTime) {
				return &respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	case *msgs.SoftUnlinkDirectoryReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated SoftUnlinkDirectoryReq %+v", reqBody)
			// Note that here we expect a non-owned edge, since we're deleting a directory.
			if !c.checkDeletedEdge(logger, reqBody.OwnerId, reqBody.TargetId, reqBody.Name, reqBody.CreationTime, false) {
				return &respErr
			}
			return nil
		}
	}
	return &respErr
}

func (c *Client) CDCRequest(
	logger *Logger,
	reqBody msgs.CDCRequest,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody msgs.CDCResponse,
) error {
	msgKind := reqBody.CDCRequestKind()
	if msgKind != respBody.CDCResponseKind() {
		panic(fmt.Errorf("mismatching req %T and resp %T", reqBody, respBody))
	}
	sock, err := c.GetUDPSocket()
	if err != nil {
		return err
	}
	defer c.ReleaseUDPSocket(sock)
	addrs := c.CDCAddrs()
	hasSecondIp := 0
	if addrs[1].Port != 0 {
		hasSecondIp = 1
	}
	respBuf := make([]byte, msgs.DEFAULT_UDP_MTU)
	attempts := 0
	startedAt := time.Now()
	requestId := newRequestId()
	// will keep trying as long as we get timeouts
	for {
		elapsed := time.Since(startedAt)
		if elapsed > cdcTimeout.Overall {
			logger.RaiseAlert(fmt.Errorf("giving up on request to CDC after waiting for %v", elapsed))
			return msgs.TIMEOUT
		}
		if c.counters != nil {
			atomic.AddUint64(&c.counters.CDC[msgKind].Attempts, 1)
		}
		req := cdcRequest{
			requestId: requestId,
			body:      reqBody,
		}
		reqBytes := bincode.Pack(&req)
		addr := &addrs[(startedAt.Nanosecond()+attempts)&1&hasSecondIp] // cycle through the two addrs
		logger.DebugStack(1, "about to send request id %v (type %T) to CDC using conn %v->%v, after %v attempts", requestId, reqBody, addr, sock.LocalAddr(), attempts)
		logger.Trace("reqBody %+v", reqBody)
		written, err := sock.WriteTo(reqBytes, addr)
		if err != nil {
			return fmt.Errorf("couldn't send request: %w", err)
		}
		if written < len(reqBytes) {
			panic(fmt.Sprintf("incomplete send -- %v bytes written instead of %v", written, len(reqBytes)))
		}
		// Keep going until we found the right request id --
		// we can't assume that what we get isn't some other
		// request we thought was timed out.
		timeout := time.Duration(math.Min(float64(cdcTimeout.Initial)*math.Pow(2.0, float64(attempts)), float64(cdcTimeout.Max)))
		readLoopStart := time.Now()
		readLoopDeadline := readLoopStart.Add(timeout)
		sock.SetReadDeadline(readLoopDeadline)
		for {
			// We could discard immediatly if the addr doesn't match, but the req id protects
			// ourselves well enough from this anyway.
			read, _, err := sock.ReadFrom(respBuf)
			respBytes := respBuf[:read]
			if err != nil {
				isTimeout := false
				switch netErr := err.(type) {
				case net.Error:
					isTimeout = netErr.Timeout()
				}
				if isTimeout {
					logger.Debug("got network timeout error %v, will try to retry", err)
					// make sure we've waited as much as the expected timeout, otherwise we might
					// call in a busy loop due to the server just not being up.
					time.Sleep(time.Until(readLoopDeadline))
					break // keep trying
				}
				// pipe is broken somehow, terminate immediately with this err
				return err
			}
			// Start parsing, from the header with request id
			respReader := bytes.NewReader(respBytes)
			var respRequestId unpackedCDCRequestId
			if err := (&respRequestId).Unpack(respReader); err != nil {
				if protocolError, ok := err.(*badRespProtocol); ok && protocolError.receivedProtocol == msgs.SHARD_RESP_PROTOCOL_VERSION {
					logger.Info("received CDC protocol, probably a late shard request: %v", err)
				} else {
					logger.RaiseAlert(fmt.Errorf("could not decode CDC response header for request %v (%T), will continue waiting for responses: %w", req.requestId, req.body, err))
				}
				continue
			}
			// Check if we're interested in the request id we got
			if uint64(respRequestId) != requestId {
				logger.Info("dropping response %v from CDC, since we expected %v", uint64(respRequestId), requestId)
				continue
			}
			// We are interested, parse the kind
			var kind uint8
			if err := bincode.UnpackScalar(respReader, &kind); err != nil {
				logger.RaiseAlert(fmt.Errorf("could not decode CDC response kind for request %v (%T), will continue waiting for responses: %w", req.requestId, req.body, err))
				continue
			}
			var eggsError *msgs.ErrCode
			if kind == msgs.ERROR_KIND {
				// If the kind is an error, parse it
				var eggsErrVal msgs.ErrCode
				eggsError = &eggsErrVal
				if err := eggsError.Unpack(respReader); err != nil {
					logger.RaiseAlert(fmt.Errorf("could not decode CDC response error for request %v (%T), will continue waiting for responses: %w", req.requestId, req.body, err))
					continue
				}
			} else {
				// If the kind dosen't match, it's bad, since it's the same request id
				if msgs.CDCMessageKind(kind) != msgKind {
					logger.RaiseAlert(fmt.Errorf("dropping response %v from CDC, since we it is of kind %v while we expected %v", uint64(respRequestId), msgs.CDCMessageKind(kind), msgKind))
					continue
				}
				// Otherwise, finally parse the body
				if err := respBody.Unpack(respReader); err != nil {
					logger.RaiseAlert(fmt.Errorf("could not decode CDC response body for request %v (%T), will continue waiting for responses: %w", req.requestId, req.body, err))
					continue
				}
			}
			// Check that we've parsed everything
			if respReader.Len() != 0 {
				return fmt.Errorf("malformed response, %v leftover bytes", respReader.Len())
			}
			// If we've got a timeout, keep trying
			if eggsError != nil && *eggsError == msgs.TIMEOUT {
				logger.Info("got resp timeout error %v from CDC, will try to retry", err)
				break // keep trying
			}
			// At this point, we know we've got a response
			elapsed := time.Since(startedAt)
			if c.counters != nil {
				c.counters.CDC[msgKind].Timings.Add(elapsed)
			}
			// If we're past the first attempt, there are cases where errors are not what they seem.
			if eggsError != nil && attempts > 0 {
				eggsError = c.checkRepeatedCDCRequestError(logger, req, respBody, *eggsError)
			}
			// Check if it's an error or not. We only use debug here because some errors are legitimate
			// responses (e.g. FILE_EMPTY)
			if eggsError != nil {
				logger.Debug("got error %v from CDC (took %v)", *eggsError, elapsed)
				return *eggsError
			}
			logger.Debug("got response %T from CDC (took %v)", respBody, elapsed)
			return nil
		}
		attempts++
	}
}

func CreateCDCSocket(ip [4]byte, port uint16) (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: ip[:], Port: int(port)})
	if err != nil {
		return nil, fmt.Errorf("could not create CDC socket: %w", err)
	}
	return socket, nil
}
