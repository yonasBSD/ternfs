package request

import (
	"fmt"
	"io"
	"net"
	"time"
	"xtx/eggsfs/alerts"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

const (
	ERROR                 uint8 = 0x0
	VISIT_DIRECTORIES     uint8 = 0x15
	VISIT_TRANSIENT_FILES uint8 = 0x16
	HISTORICAL_READ_DIR   uint8 = 0x21
)

// >>> format(struct.unpack('<I', b'SHA\0')[0], 'x')
// '414853'
const SHARD_PROTOCOL_VERSION uint32 = 0x414853

func msgKind(body any) uint8 {
	switch body.(type) {
	case msgs.ErrCode:
		return ERROR
	case *msgs.VisitDirectoriesReq, *msgs.VisitDirectoriesResp:
		return VISIT_DIRECTORIES
	case *msgs.VisitTransientFilesReq, *msgs.VisitTransientFilesResp:
		return VISIT_TRANSIENT_FILES
	case *msgs.FullReadDirReq, *msgs.FullReadDirResp:
		return HISTORICAL_READ_DIR
	default:
		panic(fmt.Sprintf("bad shard req/resp body %T", body))
	}
}

type shardRequest struct {
	RequestId uint64
	Body      bincode.Packable
}

func (req *shardRequest) Pack(buf *bincode.Buf) {
	buf.PackU32(SHARD_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(msgKind(req.Body))
	req.Body.Pack(buf)
}

type ShardResponse struct {
	RequestId uint64
	Body      bincode.Bincodable
}

func (req *ShardResponse) Pack(buf *bincode.Buf) {
	buf.PackU32(SHARD_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(msgKind(req.Body))
	req.Body.Pack(buf)
}

type UnpackedShardResponse struct {
	RequestId uint64
	Body      bincode.Unpackable
	// This is where we could decode as far as decoding the request id,
	// but then errored after. We are interested in this case because
	// we can safely drop every erroring request that is not our request
	// id. And on the contrary, we want to know if things failed for
	// the request we're interested in.
	//
	// If this is non-nil, the body will be set to nil.
	Error error
}

func (resp *UnpackedShardResponse) Unpack(buf *bincode.Buf) error {
	// panic immediately if we get passed a bogus body
	expectedKind := msgKind(resp.Body)
	// decode message header
	var ver uint32
	if err := buf.UnpackU32(&ver); err != nil {
		return err
	}
	if ver != SHARD_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", SHARD_PROTOCOL_VERSION, ver)
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
	if kind == ERROR {
		var errCode msgs.ErrCode
		if err := errCode.Unpack(buf); err != nil {
			resp.Error = fmt.Errorf("could not decode error body: %w", err)
			return nil
		}
		resp.Error = errCode
		return nil
	}
	if kind != expectedKind {
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

func ShardRequest(
	alerter alerts.Alerter,
	writer io.Writer,
	reader io.Reader,
	// Will be used for writing and decoding the request. This function will panic if
	// it is not enough to write the request into.
	buffer []byte,
	requestId uint64,
	reqBody bincode.Packable,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody bincode.Unpackable,
) error {
	req := shardRequest{
		RequestId: requestId,
		Body:      reqBody,
	}
	reqBytes := buffer
	bincode.PackIntoBytes(&reqBytes, &req)
	written, err := writer.Write(reqBytes)
	if err != nil {
		return fmt.Errorf("couldn't send request: %w", err)
	}
	if written < len(reqBytes) {
		panic(fmt.Sprintf("incomplete send -- %v bytes written instead of %v", written, len(reqBytes)))
	}
	respBytes := buffer
	// Keep going until we found the right request id --
	// we can't assume that what we get isn't some other
	// request we thought was timed out.
	for {
		respBytes = respBytes[:cap(respBytes)]
		read, err := reader.Read(respBytes)
		respBytes = respBytes[:read]
		if err != nil {
			// pipe is broken, terminate with this err
			return err
		}
		resp := UnpackedShardResponse{
			Body: respBody,
		}
		if err := bincode.UnpackFromBytes(&resp, respBytes); err != nil {
			alerter.RaiseAlert(fmt.Errorf("could not decode response to request %v, will continue waiting for responses: %w", req.RequestId, err))
			continue
		}
		if resp.RequestId != req.RequestId {
			alerter.RaiseAlert(fmt.Errorf("dropping response %v, since we expected request id %v. body: %v, error: %w", resp.RequestId, req.RequestId, resp.Body, resp.Error))
			continue
		}
		// we managed to decode, we just need to check that it's not an error
		if resp.Error != nil {
			return resp.Error
		}
		return nil
	}
}

// This function will set the deadline for the socket.
// TODO does the deadline persist -- i.e. are we permanently modifying this socket.
func ShardRequestSocket(
	alerter alerts.Alerter,
	sock *net.UDPConn,
	buffer []byte,
	timeout time.Duration,
	reqBody bincode.Packable,
	respBody bincode.Unpackable,
) error {
	sock.SetReadDeadline(time.Now().Add(timeout))
	return ShardRequest(alerter, sock, sock, buffer, uint64(msgs.Now()), reqBody, respBody)
}
