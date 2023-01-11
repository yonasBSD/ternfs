// TODO Embarassing amounts of duplication between this and shard.go. Probably good to deduplicate.
package eggs

import (
	"fmt"
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

func (req *cdcRequest) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.CDC_REQ_PROTOCOL_VERSION)
	buf.PackU64(req.requestId)
	buf.PackU8(uint8(req.body.CDCRequestKind()))
	req.body.Pack(buf)
}

type CDCResponse struct {
	RequestId uint64
	Body      msgs.CDCResponse
}

func (req *CDCResponse) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.CDC_RESP_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(uint8(req.Body.CDCResponseKind()))
	req.Body.Pack(buf)
}

type unpackedCDCResponse struct {
	requestId uint64
	body      msgs.CDCResponse
	// This is where we could decode as far as decoding the request id,
	// but then errored after. We are interested in this case because
	// we can safely drop every erroring request that is not our request
	// id. And on the contrary, we want to know if things failed for
	// the request we're interested in.
	//
	// If this is non-nil, the body will be set to nil.
	error error
}

func (resp *unpackedCDCResponse) Unpack(buf *bincode.Buf) error {
	// panic immediately if we get passed a bogus body
	expectedKind := resp.body.CDCResponseKind()
	// decode message header
	var ver uint32
	if err := buf.UnpackU32(&ver); err != nil {
		return err
	}
	if ver != msgs.CDC_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", msgs.CDC_RESP_PROTOCOL_VERSION, ver)
	}
	if err := buf.UnpackU64(&resp.requestId); err != nil {
		return err
	}
	// We've made it with the request id, from now on if we fail we set
	// the error inside the object, rather than returning an error.
	body := resp.body
	resp.body = nil
	var kind uint8
	if err := buf.UnpackU8(&kind); err != nil {
		resp.error = fmt.Errorf("could not decode response kind: %w", err)
		return nil
	}
	if kind == msgs.ERROR_KIND {
		var errCode msgs.ErrCode
		if err := errCode.Unpack(buf); err != nil {
			resp.error = fmt.Errorf("could not decode error body: %w", err)
			return nil
		}
		resp.error = errCode
		return nil
	}
	if msgs.CDCMessageKind(kind) != expectedKind {
		resp.error = fmt.Errorf("expected body of kind %v, got %v instead", expectedKind, kind)
		return nil
	}
	if err := body.Unpack(buf); err != nil {
		resp.error = fmt.Errorf("could not decode response body: %w", err)
		return nil
	}
	resp.body = body
	resp.error = nil
	return nil
}

const cdcSingleTimeout = 100 * time.Millisecond
const cdcMaxElapsed = 10 * time.Second

func (c *Client) checkRepeatedCDCRequestError(
	logger LogLevels,
	// these are already filled in by now
	req cdcRequest,
	resp msgs.CDCResponse,
	respErr msgs.ErrCode,
) error {
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
				return respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.RenameDirectoryResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.NewOwnerId, reqBody.TargetId, reqBody.NewName, &respBody.CreationTime) {
				return respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	// in a decent language this branch and the previous could be merged
	case *msgs.RenameFileReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated RenameFileReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.OldOwnerId, reqBody.TargetId, reqBody.OldName, reqBody.OldCreationTime, false) {
				return respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.RenameFileResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.NewOwnerId, reqBody.TargetId, reqBody.NewName, &respBody.CreationTime) {
				return respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	case *msgs.SoftUnlinkDirectoryReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated SoftUnlinkDirectoryReq %+v", reqBody)
			// Note that here we expect a non-owned edge, since we're deleting a directory.
			if !c.checkDeletedEdge(logger, reqBody.OwnerId, reqBody.TargetId, reqBody.Name, reqBody.CreationTime, false) {
				return respErr
			}
			return nil
		}
	}
	return respErr
}

func (c *Client) CDCRequest(
	logger LogLevels,
	reqBody msgs.CDCRequest,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody msgs.CDCResponse,
) error {
	if reqBody.CDCRequestKind() != respBody.CDCResponseKind() {
		panic(fmt.Errorf("mismatching req %T and resp %T", reqBody, respBody))
	}
	sock := c.CDCSocket
	buffer := make([]byte, msgs.UDP_MTU)
	attempts := 0
	startedAt := time.Now()
	// will keep trying as long as we get timeouts
	for {
		elapsed := time.Since(startedAt)
		if elapsed > cdcMaxElapsed {
			logger.RaiseAlert(fmt.Errorf("giving up on request to CDC after waiting for %v", elapsed))
			return msgs.TIMEOUT
		}
		requestId := newRequestId()
		req := cdcRequest{
			requestId: requestId,
			body:      reqBody,
		}
		reqBytes := buffer
		bincode.PackIntoBytes(&reqBytes, &req)
		logger.Debug("about to send request id %v (%T) to CDC, after %v attempts", requestId, reqBody, attempts)
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
		sock.SetReadDeadline(time.Now().Add(cdcSingleTimeout))
		for {
			respBytes := buffer
			read, err := sock.Read(respBytes)
			respBytes = respBytes[:read]
			if err != nil {
				isTimeout := false
				switch netErr := err.(type) {
				case net.Error:
					isTimeout = netErr.Timeout()
				}
				if isTimeout {
					logger.Debug("got network timeout error %v, will try to retry", err)
					break // keep trying
				}
				// pipe is broken somehow, terminate immediately with this err
				return err
			}
			resp := unpackedCDCResponse{
				body: respBody,
			}
			if err := bincode.UnpackFromBytes(&resp, respBytes); err != nil {
				logger.RaiseAlert(fmt.Errorf("could not decode response to request %v, will continue waiting for responses: %w", req.requestId, err))
				continue
			}
			if resp.requestId != req.requestId {
				logger.RaiseAlert(fmt.Errorf("dropping response %v, since we expected request id %v. body: %v, error: %v", resp.requestId, req.requestId, resp.body, resp.error))
				continue
			}
			// we've gotten a response
			elapsed := time.Since(startedAt)
			if c.Counters != nil {
				msgKind := reqBody.CDCRequestKind()
				atomic.AddInt64(&c.Counters.CDCReqsCounts[msgKind], 1)
				atomic.AddInt64(&c.Counters.CDCReqsNanos[msgKind], elapsed.Nanoseconds())
			}
			respErr := resp.error
			if respErr != nil {
				isTimeout := false
				switch eggsErr := err.(type) {
				case msgs.ErrCode:
					isTimeout = eggsErr == msgs.TIMEOUT
				}
				if isTimeout {
					logger.Debug("got resp timeout error %v, will try to retry", err)
					break // keep trying
				}
			}
			switch eggsErr := respErr.(type) {
			case msgs.ErrCode:
				// If we're past the first attempt, there are cases where errors are not what they
				// seem.
				if attempts > 0 {
					respErr = c.checkRepeatedCDCRequestError(logger, req, respBody, eggsErr)
				}
			}
			// check if it's an error or not
			if respErr != nil {
				logger.Debug("got error %v (%T) from CDC (took %v)", respErr, respErr, elapsed)
				return respErr
			}
			logger.Debug("got response %T from CDC (took %v)", respBody, elapsed)
			return nil
		}
		attempts++
	}
}

func CreateCDCSocket() (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{Port: msgs.CDC_PORT})
	if err != nil {
		return nil, fmt.Errorf("could not create CDC socket: %w", err)
	}
	return socket, nil
}
