// TODO Embarassing amounts of duplication between this and shard.go. Probably good to deduplicate.
package eggs

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

/*
func (req *CDCResponse) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.CDC_RESP_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(uint8(req.Body.CDCResponseKind()))
	req.Body.Pack(buf)
}
*/

type unpackedCDCRequestId uint64

func (requestId *unpackedCDCRequestId) Unpack(r io.Reader) error {
	var ver uint32
	if err := bincode.UnpackScalar(r, &ver); err != nil {
		return err
	}
	if ver != msgs.CDC_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", msgs.CDC_RESP_PROTOCOL_VERSION, ver)
	}
	if err := bincode.UnpackScalar(r, (*uint64)(requestId)); err != nil {
		return err
	}
	return nil
}

// 5 times the shard numbers, given that we roughly do 5 shard requests per CDC request
const minCDCSingleTimeout = 25 * time.Millisecond
const maxCDCSingleTimeout = 500 * time.Millisecond
const cdcMaxElapsed = 25 * time.Second // TODO these are a bit ridicolous now because of the valgrind test, adjust

func (c *Client) checkRepeatedCDCRequestError(
	logger LogLevels,
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
	logger LogLevels,
	reqBody msgs.CDCRequest,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody msgs.CDCResponse,
) error {
	msgKind := reqBody.CDCRequestKind()
	if msgKind != respBody.CDCResponseKind() {
		panic(fmt.Errorf("mismatching req %T and resp %T", reqBody, respBody))
	}
	sock, err := c.GetCDCSocket()
	if err != nil {
		return err
	}
	defer c.ReleaseCDCSocket(sock)
	respBuf := make([]byte, msgs.UDP_MTU)
	requestIds := make([]uint64, cdcMaxElapsed/minCDCSingleTimeout)
	attempts := 0
	startedAt := time.Now()
	// will keep trying as long as we get timeouts
	for {
		elapsed := time.Since(startedAt)
		if elapsed > cdcMaxElapsed {
			logger.RaiseAlert(fmt.Errorf("giving up on request to CDC after waiting for %v", elapsed))
			return msgs.TIMEOUT
		}
		if c.counters != nil {
			atomic.AddInt64(&c.counters.CDC.Attempts[msgKind], 1)
		}
		requestId := newRequestId()
		requestIds[attempts] = requestId
		req := cdcRequest{
			requestId: requestId,
			body:      reqBody,
		}
		reqBytes := bincode.Pack(&req)
		logger.Debug("about to send request id %v (%T, %+v) to CDC, after %v attempts", requestId, reqBody, reqBody, attempts)
		written, err := sock.Write(reqBytes)
		if err != nil {
			return fmt.Errorf("couldn't send request: %w", err)
		}
		if written < len(reqBytes) {
			panic(fmt.Sprintf("incomplete send -- %v bytes written instead of %v", written, len(reqBytes)))
		}
		// Keep going until we found the right request id --
		// we can't assume that what we get isn't some other
		// request we thought was timed out.
		timeout := time.Duration(math.Min(float64(minCDCSingleTimeout)*math.Pow(2.0, float64(attempts)), float64(maxCDCSingleTimeout)))
		readLoopStart := time.Now()
		readLoopDeadline := readLoopStart.Add(timeout)
		sock.SetReadDeadline(readLoopDeadline)
		for {
			read, err := sock.Read(respBuf)
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
				logger.RaiseAlert(fmt.Errorf("could not decode CDC response header for request %v (%T), will continue waiting for responses: %w", req.requestId, req.body, err))
				continue
			}
			// Check if we're interested in the request id we got -- accept any we've sent
			// so far
			goodRequestId := false
			for _, requestId := range requestIds {
				if uint64(respRequestId) == requestId {
					goodRequestId = true
					break
				}
			}
			if !goodRequestId {
				logger.Info("dropping response %v from CDC, since we expected one of %v", uint64(respRequestId), requestIds)
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
				atomic.AddInt64(&c.counters.CDC.Count[msgKind], 1)
				atomic.AddInt64(&c.counters.CDC.Nanos[msgKind], elapsed.Nanoseconds())
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
