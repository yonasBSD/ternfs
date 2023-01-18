package eggs

import (
	"crypto/cipher"
	"fmt"
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

func (req *shardRequest) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.SHARD_REQ_PROTOCOL_VERSION)
	buf.PackU64(req.requestId)
	buf.PackU8(uint8(req.body.ShardRequestKind()))
	req.body.Pack(buf)
}

func packShardRequest(out *[]byte, req *shardRequest, cdcKey cipher.Block) {
	written := bincode.PackToBytes(*out, req)
	if (req.body.ShardRequestKind() & 0x80) != 0 {
		// privileged, we must have a key
		if cdcKey == nil {
			panic(fmt.Errorf("trying to encode request of privileged kind %T, but got no CDC key", req.body))
		}
		mac := CBCMAC(cdcKey, (*out)[:written])
		copy((*out)[written:written+len(mac)], mac[:])
		written += len(mac)
	}
	*out = (*out)[:written]
}

type ShardResponse struct {
	RequestId uint64
	Body      msgs.ShardResponse
}

func (req *ShardResponse) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.SHARD_RESP_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(uint8(req.Body.ShardResponseKind()))
	req.Body.Pack(buf)
}

type unpackedShardResponse struct {
	RequestId uint64
	Body      msgs.ShardResponse
	// This is where we could decode as far as decoding the request id,
	// but then errored after. We are interested in this case because
	// we can safely drop every erroring request that is not our request
	// id. And on the contrary, we want to know if things failed for
	// the request we're interested in.
	//
	// If this is non-nil, the body will be set to nil.
	Error error
}

func (resp *unpackedShardResponse) Unpack(buf *bincode.Buf) error {
	// panic immediately if we get passed a bogus body
	expectedKind := resp.Body.ShardResponseKind()
	// decode message header
	var ver uint32
	if err := buf.UnpackU32(&ver); err != nil {
		return err
	}
	if ver != msgs.SHARD_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", msgs.SHARD_RESP_PROTOCOL_VERSION, ver)
	}
	if err := buf.UnpackU64(&resp.RequestId); err != nil {
		return err
	}
	// We've made it with the request id, from now on if we fail we set
	// the error inside the object, rather than returning an error.
	body := resp.Body
	resp.Body = nil
	var kind uint8
	if err := buf.UnpackU8(&kind); err != nil {
		resp.Error = fmt.Errorf("could not decode response kind: %w", err)
		return nil
	}
	if kind == msgs.ERROR_KIND {
		var errCode msgs.ErrCode
		if err := errCode.Unpack(buf); err != nil {
			resp.Error = fmt.Errorf("could not decode error body: %w", err)
			return nil
		}
		resp.Error = errCode
		return nil
	}
	if msgs.ShardMessageKind(kind) != expectedKind {
		resp.Error = fmt.Errorf("expected body of kind %v, got %v instead", expectedKind, kind)
		return nil
	}
	if err := body.Unpack(buf); err != nil {
		resp.Error = fmt.Errorf("could not decode response body: %w", err)
		return nil
	}
	resp.Body = body
	resp.Error = nil
	return nil
}

const minShardSingleTimeout = 10 * time.Millisecond
const maxShardSingleTimeout = 100 * time.Millisecond
const shardMaxElapsed = 1 * time.Second

var requestIdGenerator = uint64(0)

func newRequestId() uint64 {
	return atomic.AddUint64(&requestIdGenerator, 1)
}

func (c *Client) checkDeletedEdge(
	logger LogLevels,
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
	logger LogLevels,
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
	logger LogLevels,
	// these are already filled in by now
	req shardRequest,
	resp msgs.ShardResponse,
	respErr msgs.ErrCode,
) error {
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
				return respErr
			}
			// Then we check the target edge, and update creation time
			respBody := resp.(*msgs.SameDirectoryRenameResp)
			if !c.checkNewEdgeAfterRename(logger, reqBody.DirId, reqBody.TargetId, reqBody.NewName, &respBody.NewCreationTime) {
				return respErr
			}
			logger.Info("recovered from EDGE_NOT_FOUND, will fill in creation time")
			return nil
		}
	case *msgs.SoftUnlinkFileReq:
		if respErr == msgs.EDGE_NOT_FOUND {
			logger.Info("following up on EDGE_NOT_FOUND after repeated SoftUnlinkFileReq %+v", reqBody)
			if !c.checkDeletedEdge(logger, reqBody.OwnerId, reqBody.FileId, reqBody.Name, reqBody.CreationTime, true) {
				return respErr
			}
			return nil
		}
	}
	return respErr
}

func (c *Client) ShardRequest(
	logger LogLevels,
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
	sock, err := c.ShardSocketFactory.GetShardSocket(shid)
	if err != nil {
		return err
	}
	defer c.ShardSocketFactory.ReleaseShardSocket(shid)
	buffer := make([]byte, msgs.UDP_MTU)
	attempts := 0
	startedAt := time.Now()
	// will keep trying as long as we get timeouts
	for {
		elapsed := time.Since(startedAt)
		if elapsed > shardMaxElapsed {
			logger.RaiseAlert(fmt.Errorf("giving up on request to shard %v after waiting for %v", shid, elapsed))
			return msgs.TIMEOUT
		}
		if c.Counters != nil {
			atomic.AddInt64(&c.Counters.Shard.Attempts[msgKind], 1)
		}
		requestId := newRequestId()
		req := shardRequest{
			requestId: requestId,
			body:      reqBody,
		}
		reqBytes := buffer
		packShardRequest(&reqBytes, &req, c.CDCKey)
		logger.Debug("about to send request id %v (%T) to shard %v, after %v attempts", requestId, reqBody, shid, attempts)
		written, err := sock.Write(reqBytes)
		if err != nil {
			return fmt.Errorf("couldn't send request to shard %v: %w", shid, err)
		}
		if written < len(reqBytes) {
			panic(fmt.Sprintf("incomplete send to shard %v -- %v bytes written instead of %v", shid, written, len(reqBytes)))
		}
		// Keep going until we found the right request id --
		// we can't assume that what we get isn't some other
		// request we thought was timed out.
		timeout := time.Duration(math.Min(float64(minShardSingleTimeout)*math.Pow(1.5, float64(attempts)), float64(maxShardSingleTimeout)))
		sock.SetReadDeadline(time.Now().Add(timeout))
		for {
			respBytes := buffer
			read, err := sock.Read(respBytes)
			respBytes = respBytes[:read]
			if err != nil {
				// We retry very liberally (at least for now), to survive the shard
				// dying in the most exotic ways.
				shouldRetry := false
				switch err.(type) {
				case net.Error, *net.OpError:
					shouldRetry = true
				}
				if shouldRetry {
					logger.Info("got network error %v to shard %v, will try to retry", err, shid)
					break // keep trying
				}
				// Something unexpected happen, exit immediately
				return err
			}
			resp := unpackedShardResponse{
				Body: respBody,
			}
			if err := bincode.UnpackFromBytes(&resp, respBytes); err != nil {
				logger.RaiseAlert(fmt.Errorf("could not decode response to request %v from shard %v, will continue waiting for responses: %w", req.requestId, shid, err))
				continue
			}
			if resp.RequestId != req.requestId {
				logger.RaiseAlert(fmt.Errorf("dropping response %v from shard %v, since we expected request id %v. body: %v, error: %v", resp.RequestId, shid, req.requestId, resp.Body, resp.Error))
				continue
			}
			// we've gotten a response
			elapsed := time.Since(startedAt)
			if c.Counters != nil {
				atomic.AddInt64(&c.Counters.Shard.Count[msgKind], 1)
				atomic.AddInt64(&c.Counters.Shard.Nanos[msgKind], elapsed.Nanoseconds())
			}
			respErr := resp.Error
			if respErr != nil {
				isTimeout := false
				switch eggsErr := err.(type) {
				case msgs.ErrCode:
					isTimeout = eggsErr == msgs.TIMEOUT
				}
				if isTimeout {
					logger.Info("got resp timeout error %v from shard %v, will try to retry", err, shid)
					break // keep trying
				}
			}
			switch eggsErr := respErr.(type) {
			case msgs.ErrCode:
				// If we're past the first attempt, there are cases where errors are not what they
				// seem.
				if attempts > 0 {
					respErr = c.checkRepeatedShardRequestError(logger, req, respBody, eggsErr)
				}
			}
			// check if it's an error or not
			if respErr != nil {
				logger.Debug("got error %v from shard %v (took %v)", respErr, shid, elapsed)
				return respErr
			}
			logger.Debug("got response %T from shard %v (took %v)", respBody, shid, elapsed)
			return nil
		}
		attempts++
	}
}

func CreateShardSocket(shid msgs.ShardId) (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{Port: shid.Port()})
	if err != nil {
		return nil, fmt.Errorf("could not create shard socket: %w", err)
	}
	return socket, nil
}
