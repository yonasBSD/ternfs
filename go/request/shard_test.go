package request

import (
	"bytes"
	"fmt"
	"io"
	"net"
	"testing"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"

	"github.com/stretchr/testify/assert"
)

type mockResponses [][]byte

func (responses *mockResponses) Read(p []byte) (n int, err error) {
	if len(*responses) == 0 {
		return 0, io.EOF
	}
	written := copy(p, (*responses)[0])
	if written < len((*responses)[0]) {
		panic("not enough space")
	}
	*responses = (*responses)[1:]
	return written, nil
}

type mockAlerter []error

func (alerter *mockAlerter) RaiseAlert(err error) {
	*alerter = append(*alerter, err)
}

func TestReqOK(t *testing.T) {
	request := msgs.VisitDirectoriesReq{
		BeginId: 0,
	}
	responses := mockResponses(make([][]byte, 3))
	for i := range responses {
		responses[i] = make([]byte, msgs.UDP_MTU)
	}
	// First response: bad req id
	bincode.PackIntoBytes(
		&responses[0],
		&ShardResponse{
			RequestId: 42,
			Body:      &msgs.VisitDirectoriesResp{},
		},
	)
	// Second response: bad magic number
	responses[1][0] = 'Z'
	// Last response: a good one
	expectedResponse := msgs.VisitDirectoriesResp{
		NextId: 42,
		Ids:    []msgs.InodeId{1, 2, 3},
	}
	requestId := uint64(msgs.Now())
	bincode.PackIntoBytes(
		&responses[len(responses)-1],
		&ShardResponse{
			RequestId: requestId,
			Body:      &expectedResponse,
		},
	)
	// Go for it
	alerter := mockAlerter{}
	response := msgs.VisitDirectoriesResp{}
	err := ShardRequest(
		&alerter, new(bytes.Buffer), &responses, make([]byte, msgs.UDP_MTU), requestId, &request, &response,
	)
	for _, err := range alerter {
		fmt.Printf("err: %v\n", err)
	}
	assert.Nil(t, err)
	assert.Equal(t, expectedResponse, response)
	// Verify errors
	assert.Equal(t, 2, len(alerter))
	assert.Contains(t, alerter[0].Error(), "expected request id")
	assert.Contains(t, alerter[1].Error(), "expected protocol")
}

func TestReqTimeout(t *testing.T) {
	mockSock, err := net.ListenUDP("udp4", nil)
	assert.Nil(t, err)
	defer mockSock.Close()
	sock, err := net.DialUDP("udp4", nil, mockSock.LocalAddr().(*net.UDPAddr))
	assert.Nil(t, err)
	defer sock.Close()
	buffer := make([]byte, msgs.UDP_MTU)
	err = ShardRequestSocket(
		&mockAlerter{}, sock, buffer, time.Millisecond, &msgs.VisitTransientFilesReq{}, &msgs.VisitTransientFilesResp{},
	)
	assert.NotNil(t, err)
}
