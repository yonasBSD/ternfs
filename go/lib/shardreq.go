package lib

import (
	"bytes"
	"crypto/cipher"
	"fmt"
	"io"
	"math"
	"net"
	"sync/atomic"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type shardRequest struct {
	requestId uint64
	body      msgs.ShardRequest
}

func (req *shardRequest) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, msgs.SHARD_REQ_PROTOCOL_VERSION); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, req.requestId); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(req.body.ShardRequestKind())); err != nil {
		return err
	}
	if err := req.body.Pack(w); err != nil {
		return err
	}
	return nil
}

func packShardRequest(req *shardRequest, cdcKey cipher.Block) []byte {
	buf := bytes.NewBuffer([]byte{})
	if err := req.Pack(buf); err != nil {
		panic(err)
	}
	bs := buf.Bytes()
	if (req.body.ShardRequestKind() & 0x80) != 0 {
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

type ShardResponse struct {
	RequestId uint64
	Body      msgs.ShardResponse
}

type unpackedShardRequestId uint64

func (requestId *unpackedShardRequestId) Unpack(r io.Reader) error {
	var ver uint32
	if err := bincode.UnpackScalar(r, &ver); err != nil {
		return err
	}
	if ver != msgs.SHARD_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", msgs.SHARD_RESP_PROTOCOL_VERSION, ver)
	}
	if err := bincode.UnpackScalar(r, (*uint64)(requestId)); err != nil {
		return err
	}
	return nil
}

const minShardSingleTimeout = 10 * time.Millisecond
const maxShardSingleTimeout = 100 * time.Millisecond
const shardMaxElapsed = 5 * time.Second // TODO these are a bit ridicolous now because of the valgrind test, adjust

var requestIdGenerator = uint64(0)

// Starts from 1, we use 0 as a placeholder in `requestIds`
func newRequestId() uint64 {
	return atomic.AddUint64(&requestIdGenerator, 1)
}

func (c *Client) checkDeletedEdge(
	logger *Logger,
	dirId msgs.InodeId,
	targetId msgs.InodeId,
	name string,
	creationTime msgs.EggsTime,
	owned bool,
) bool {
	// First we check the edge we expect to have moved away
	snapshotResp := msgs.SnapshotLookupResp{}
	err := c.ShardRequest(logger, dirId.Shard(), &msgs.SnapshotLookupReq{DirId: dirId, Name: name, StartFrom: creationTime}, &snapshotResp)
	if err != nil {
		logger.Info("failed to get snapshot edge (err %v), giving up and returning original error", err)
		return false
	}
	if len(snapshotResp.Edges) != 2 {
		logger.Info("expected 1 snapshot edges but got %v, giving up and returning original error", len(snapshotResp.Edges))
		return false
	}
	oldEdge := snapshotResp.Edges[0]
	if oldEdge.TargetId.Extra() != owned || oldEdge.TargetId.Id() != targetId || oldEdge.CreationTime != creationTime {
		logger.Info("got mismatched snapshot edge (%+v), giving up and returning original error", oldEdge)
		return false
	}
	deleteEdge := snapshotResp.Edges[1]
	if deleteEdge.TargetId.Id() != msgs.NULL_INODE_ID {
		logger.Info("expected deletion edge but got %+v, giving up and returning original error", deleteEdge)
		return false
	}
	return true
}

func (c *Client) checkNewEdgeAfterRename(
	logger *Logger,
	dirId msgs.InodeId,
	targetId msgs.InodeId,
	name string,
	creationTime *msgs.EggsTime,
) bool {
	// Then we check the target edge
	lookupResp := msgs.LookupResp{}
	err := c.ShardRequest(logger, dirId.Shard(), &msgs.LookupReq{DirId: dirId, Name: name}, &lookupResp)
	if err != nil {
		logger.Info("failed to get current edge (err %v), giving up and returning original error", err)
		return false
	}
	if lookupResp.TargetId != targetId {
		logger.Info("got mismatched current target (%v), giving up and returning original error", lookupResp.TargetId)
		return false
	}
	*creationTime = lookupResp.CreationTime
	return true
}

func (c *Client) checkRepeatedShardRequestError(
	logger *Logger,
	// these are already filled in by now
	req shardRequest,
	resp msgs.ShardResponse,
	respErr msgs.ErrCode,
) *msgs.ErrCode {
	switch reqBody := req.body.(type) {
	case *msgs.SameDirectoryRenameReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			// Happens when a request succeeds, and then the response gets lost.
			// We try to apply some heuristics to let this slide. See convo following
			// <https://eulergamma.slack.com/archives/C03PCJMGAAC/p1673547512380409>.
			//
			// Specifically, check that the last snapshot edge is what we expect if
			// we had  just moved it, and that the target edge also exists.
			logger.Info("following up on EDGE_NOT_FOUND after repeated SameDirectoryRenameReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.DirId, reqBody.TargetId, reqBody.OldName, reqBody.OldCreationTime, false) {
				return &respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.SameDirectoryRenameResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.DirId, reqBody.TargetId, reqBody.NewName, &respBody.NewCreationTime) {
				return &respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	case *msgs.SoftUnlinkFileReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated SoftUnlinkFileReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.OwnerId, reqBody.FileId, reqBody.Name, reqBody.CreationTime, true) {
				return &respErr
			}
			return nil
		}
	}
	return &respErr
}

func (c *Client) ShardRequest(
	logger *Logger,
	shid msgs.ShardId,
	reqBody msgs.ShardRequest,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody msgs.ShardResponse,
) error {
	msgKind := reqBody.ShardRequestKind()
	if msgKind != respBody.ShardResponseKind() {
		panic(fmt.Errorf("mismatching req %T and resp %T", reqBody, respBody))
	}
	sock, err := c.GetShardSocket(shid)
	if err != nil {
		return err
	}
	defer c.ReleaseShardSocket(shid, sock)
	respBuf := make([]byte, msgs.UDP_MTU)
	requestIds := make([]uint64, shardMaxElapsed/minShardSingleTimeout)
	attempts := 0
	startedAt := time.Now()
	// will keep trying as long as we get timeouts
	for {
		elapsed := time.Since(startedAt)
		if elapsed > shardMaxElapsed {
			logger.RaiseAlert(fmt.Errorf("giving up on request to shard %v after waiting for %v", shid, elapsed))
			return msgs.TIMEOUT
		}
		if c.counters != nil {
			atomic.AddUint64(&c.counters.Shard[msgKind].Attempts, 1)
		}
		requestId := newRequestId()
		requestIds[attempts] = requestId
		req := shardRequest{
			requestId: requestId,
			body:      reqBody,
		}
		reqBytes := packShardRequest(&req, c.cdcKey)
		logger.DebugStack(1, "about to send request id %v (type %T) to shard %v using conn %v->%v, after %v attempts", requestId, reqBody, shid, sock.RemoteAddr(), sock.LocalAddr(), attempts)
		logger.Trace("reqBody %+v", reqBody)
		written, err := sock.Write(reqBytes)
		if err != nil {
			return fmt.Errorf("couldn't send request to shard %v: %w", shid, err)
		}
		if written < len(reqBytes) {
			panic(fmt.Sprintf("incomplete send to shard %v -- %v bytes written instead of %v", shid, written, len(reqBytes)))
		}
		// Keep going until we found the right request id -- we can't assume that what we get isn't
		// some other request we thought was timed out.
		timeout := time.Duration(math.Min(float64(minShardSingleTimeout)*math.Pow(2.0, float64(attempts)), float64(maxShardSingleTimeout)))
		readLoopStart := time.Now()
		readLoopDeadline := readLoopStart.Add(timeout)
		sock.SetReadDeadline(readLoopDeadline)
		for {
			read, err := sock.Read(respBuf)
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
					logger.Info("got network error %v to shard %v, will try to retry", err, shid)
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
			var respRequestId unpackedShardRequestId
			if err := (&respRequestId).Unpack(respReader); err != nil {
				logger.RaiseAlert(fmt.Errorf("could not decode Shard response header for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err))
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
				prefix := []uint64{}
				for _, req := range requestIds {
					if req == 0 {
						break
					}
					prefix = append(prefix, req)
				}
				logger.Info("dropping response %v from shard %v, since we expected one of %v", uint64(respRequestId), shid, prefix)
				continue
			}
			// We are interested, parse the kind
			var kind uint8
			if err := bincode.UnpackScalar(respReader, &kind); err != nil {
				logger.RaiseAlert(fmt.Errorf("could not decode Shard response kind for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err))
				continue
			}
			var eggsError *msgs.ErrCode
			if kind == msgs.ERROR_KIND {
				// If the kind is an error, parse it
				var eggsErrVal msgs.ErrCode
				eggsError = &eggsErrVal
				if err := eggsError.Unpack(respReader); err != nil {
					logger.RaiseAlert(fmt.Errorf("could not decode Shard response error for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err))
					continue
				}
			} else {
				// If the kind dosen't match, it's bad, since it's the same request id
				if msgs.ShardMessageKind(kind) != msgKind {
					logger.RaiseAlert(fmt.Errorf("dropping response %v from shard %v, since we it is of kind %v while we expected %v", uint64(respRequestId), shid, msgs.ShardMessageKind(kind), msgKind))
					continue
				}
				// Otherwise, finally parse the body
				if err := respBody.Unpack(respReader); err != nil {
					logger.RaiseAlert(fmt.Errorf("could not decode Shard response body for request %v (%T) from shard %v, will continue waiting for responses: %w", req.requestId, req.body, shid, err))
					continue
				}
			}
			// Check that we've parsed everything
			if respReader.Len() != 0 {
				return fmt.Errorf("malformed response, %v leftover bytes", respReader.Len())
			}
			// If we've got a timeout, keep trying
			if eggsError != nil && *eggsError == msgs.TIMEOUT {
				logger.Info("got resp timeout error %v from shard %v, will try to retry", err, shid)
				break // keep trying
			}
			// At this point, we know we've got a response
			elapsed := time.Since(startedAt)
			if c.counters != nil {
				c.counters.Shard[msgKind].Timings.Add(elapsed)
			}
			// If we're past the first attempt, there are cases where errors are not what they seem.
			if eggsError != nil && attempts > 0 {
				eggsError = c.checkRepeatedShardRequestError(logger, req, respBody, *eggsError)
			}
			// Check if it's an error or not. We only use debug here because some errors are legitimate
			// responses (e.g. FILE_EMPTY)
			if eggsError != nil {
				logger.Debug("got error %v from shard %v (took %v)", *eggsError, shid, elapsed)
				return *eggsError
			}
			logger.Debug("got response %T from shard %v (took %v)", respBody, shid, elapsed)
			logger.Trace("respBody %+v", respBody)
			return nil
		}
		// We got through the loop busily consuming responses, now try again by sending a new request
		attempts++
	}
}

func CreateShardSocket(shid msgs.ShardId, ip [4]byte, port uint16) (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{IP: ip[:], Port: int(port)})
	if err != nil {
		return nil, fmt.Errorf("could not create shard %v socket at %v:%v: %w", shid, ip, port, err)
	}
	return socket, nil
}
