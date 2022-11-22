// TODO Embarassing amounts of duplication between this and shard.go. Probably good to deduplicate.
package eggs

import (
	"fmt"
	"io"
	"net"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type cdcRequest struct {
	RequestId uint64
	Body      bincode.Packable
}

func (req *cdcRequest) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.CDC_REQ_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(uint8(msgs.GetCDCMessageKind(req.Body)))
	req.Body.Pack(buf)
}

type CDCResponse struct {
	RequestId uint64
	Body      bincode.Bincodable
}

func (req *CDCResponse) Pack(buf *bincode.Buf) {
	buf.PackU32(msgs.CDC_RESP_PROTOCOL_VERSION)
	buf.PackU64(req.RequestId)
	buf.PackU8(uint8(msgs.GetCDCMessageKind(req.Body)))
	req.Body.Pack(buf)
}

type UnpackedCDCResponse struct {
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

func (resp *UnpackedCDCResponse) Unpack(buf *bincode.Buf) error {
	// panic immediately if we get passed a bogus body
	expectedKind := msgs.GetCDCMessageKind(resp.Body)
	// decode message header
	var ver uint32
	if err := buf.UnpackU32(&ver); err != nil {
		return err
	}
	if ver != msgs.CDC_RESP_PROTOCOL_VERSION {
		return fmt.Errorf("expected protocol version %v, but got %v", msgs.CDC_RESP_PROTOCOL_VERSION, ver)
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
	if msgs.CDCMessageKind(kind) != expectedKind {
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

func CDCRequest(
	logger LogLevels,
	writer io.Writer,
	reader io.Reader,
	requestId uint64,
	reqBody bincode.Packable,
	// Result will be written in here. If an error is returned, no guarantees
	// are made regarding the contents of `respBody`.
	respBody bincode.Unpackable,
) error {
	req := cdcRequest{
		RequestId: requestId,
		Body:      reqBody,
	}
	buffer := make([]byte, msgs.UDP_MTU)
	logger.Debug("about to send request %T to CDC", reqBody)
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
		resp := UnpackedCDCResponse{
			Body: respBody,
		}
		if err := bincode.UnpackFromBytes(&resp, respBytes); err != nil {
			logger.RaiseAlert(fmt.Errorf("could not decode response to request %v, will continue waiting for responses: %w", req.RequestId, err))
			continue
		}
		if resp.RequestId != req.RequestId {
			logger.RaiseAlert(fmt.Errorf("dropping response %v, since we expected request id %v. body: %v, error: %w", resp.RequestId, req.RequestId, resp.Body, resp.Error))
			continue
		}
		// we managed to decode, we just need to check that it's not an error
		if resp.Error != nil {
			logger.Debug("got error %v from CDC", resp.Error)
			return resp.Error
		}
		logger.Debug("got response %T from CDC", respBody)
		return nil
	}
}

// This function will set the deadline for the socket.
// TODO does the deadline persist -- i.e. are we permanently modifying this socket.
func CDCRequestSocket(
	logger LogLevels,
	sock *net.UDPConn,
	timeout time.Duration,
	reqBody bincode.Packable,
	respBody bincode.Unpackable,
) error {
	if timeout == time.Duration(0) {
		panic("zero duration")
	}
	sock.SetReadDeadline(time.Now().Add(timeout))
	return CDCRequest(logger, sock, sock, uint64(msgs.Now()), reqBody, respBody)
}

func CDCSocket() (*net.UDPConn, error) {
	socket, err := net.DialUDP("udp4", nil, &net.UDPAddr{Port: msgs.CDC_PORT})
	if err != nil {
		return nil, fmt.Errorf("could not create CDC socket: %w", err)
	}
	return socket, nil
}
