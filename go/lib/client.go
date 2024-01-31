package lib

import (
	"bytes"
	"container/heap"
	"crypto/cipher"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math/rand"
	"net"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
	"unsafe"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

type ReqCounters struct {
	Timings  Timings
	Attempts uint64
}

func MergeReqCounters(cs []ReqCounters) *ReqCounters {
	counters := cs[0] // important to copy
	for i := 1; i < len(cs); i++ {
		counters.Attempts += cs[i].Attempts
		counters.Timings.Merge(&cs[i].Timings)
	}
	return &counters
}

type ClientCounters struct {
	Shard map[uint8]*[256]ReqCounters
	CDC   map[uint8]*ReqCounters
}

func NewClientCounters() *ClientCounters {
	counters := ClientCounters{
		Shard: make(map[uint8]*[256]ReqCounters),
		CDC:   make(map[uint8]*ReqCounters),
	}
	for _, k := range msgs.AllShardMessageKind {
		// max = ~1min
		var shards [256]ReqCounters
		counters.Shard[uint8(k)] = &shards
		for i := 0; i < 256; i++ {
			shards[i].Timings = *NewTimings(40, time.Microsecond*10, 1.5)
		}
	}
	for _, k := range msgs.AllCDCMessageKind {
		// max = ~2min
		counters.CDC[uint8(k)] = &ReqCounters{
			Timings: *NewTimings(35, time.Millisecond, 1.5),
		}
	}
	return &counters
}

func (counters *ClientCounters) Log(log *Logger) {
	formatCounters := func(c *ReqCounters) {
		totalCount := uint64(0)
		for _, bin := range c.Timings.Histogram() {
			totalCount += bin.Count
		}
		log.Info("    count: %v", totalCount)
		if totalCount == 0 {
			log.Info("    attempts: %v", c.Attempts)
		} else {
			log.Info("    attempts: %v (%v)", c.Attempts, float64(c.Attempts)/float64(totalCount))
		}
		log.Info("    total time: %v", c.Timings.TotalTime())
		log.Info("    avg time: %v", c.Timings.Mean())
		log.Info("    median time: %v", c.Timings.Median())
		hist := bytes.NewBuffer([]byte{})
		first := true
		countSoFar := uint64(0)
		lowerBound := time.Duration(0)
		for _, bin := range c.Timings.Histogram() {
			if bin.Count == 0 {
				continue
			}
			countSoFar += bin.Count
			if first {
				fmt.Fprintf(hist, "%v < ", lowerBound)
			} else {
				fmt.Fprintf(hist, ", ")
			}
			first = false
			fmt.Fprintf(hist, "%v (%0.2f%%) < %v", bin.Count, float64(countSoFar*100)/float64(totalCount), bin.UpperBound)
		}
		log.Info("    hist: %v", hist.String())
	}
	var shardTime time.Duration
	for _, k := range msgs.AllShardMessageKind {
		for i := 0; i < 256; i++ {
			shardTime += counters.Shard[uint8(k)][i].Timings.TotalTime()
		}
	}
	log.Info("Shard stats (total shard time %v):", shardTime)
	for _, k := range msgs.AllShardMessageKind {
		c := MergeReqCounters(counters.Shard[uint8(k)][:])
		if c.Attempts == 0 {
			continue
		}
		log.Info("  %v", k)
		formatCounters(c)
	}
	var cdcTime time.Duration
	for _, k := range msgs.AllCDCMessageKind {
		cdcTime += counters.CDC[uint8(k)].Timings.TotalTime()
	}
	log.Info("CDC stats (total CDC time %v):", cdcTime)
	for _, k := range msgs.AllCDCMessageKind {
		c := counters.CDC[uint8(k)]
		if c.Attempts == 0 {
			continue
		}
		log.Info("  %v", k)
		formatCounters(c)
	}

}

var DefaultShardTimeout = ReqTimeouts{
	Initial: 100 * time.Millisecond,
	Max:     2 * time.Second,
	Overall: 10 * time.Second,
	Growth:  1.5,
	Jitter:  0.1,
	rand:    wyhash.Rand{State: 0},
}

var DefaultCDCTimeout = ReqTimeouts{
	Initial: time.Second,
	Max:     10 * time.Second,
	Overall: time.Minute,
	Growth:  1.5,
	Jitter:  0.1,
	rand:    wyhash.Rand{State: 0},
}

var DefaultBlockTimeout = ReqTimeouts{
	Initial: time.Second,
	Max:     10 * time.Second,
	Overall: 5 * time.Minute,
	Growth:  1.5,
	Jitter:  0.1,
	rand:    wyhash.Rand{State: 0},
}

type metadataProcessorRequest struct {
	requestId uint64
	timeout   time.Duration
	shard     int16 // -1 = cdc
	req       bincode.Packable
	resp      bincode.Unpackable
	extra     any
	respCh    chan *metadataProcessorResponse
	// filled in by request processor
	deadline time.Time
	index    int // index in the heap
}

type metadataProcessorResponse struct {
	requestId uint64
	resp      any
	err       error
}

type metadataRequestsPQ []*metadataProcessorRequest

func (pq metadataRequestsPQ) Len() int { return len(pq) }

func (pq metadataRequestsPQ) Less(i, j int) bool {
	return pq[i].deadline.UnixNano() < pq[j].deadline.UnixNano()
}

func (pq metadataRequestsPQ) Swap(i, j int) {
	pq[i], pq[j] = pq[j], pq[i]
	pq[i].index = i
	pq[j].index = j
}

func (pq *metadataRequestsPQ) Push(x any) {
	n := len(*pq)
	item := x.(*metadataProcessorRequest)
	item.index = n
	*pq = append(*pq, item)
}

func (pq *metadataRequestsPQ) Pop() any {
	old := *pq
	n := len(old)
	item := old[n-1]
	old[n-1] = nil  // avoid memory leak
	item.index = -1 // for safety
	*pq = old[0 : n-1]
	return item
}

type rawMetadataResponse struct {
	receivedAt time.Time
	protocol   uint32
	requestId  uint64
	kind       uint8
	respLen    int
	buf        *[]byte // the buf contains the header
}

type clientMetadata struct {
	client *Client
	sock   *net.UDPConn

	requestsById      map[uint64]*metadataProcessorRequest // requests we've sent, by req id
	requestsByTimeout metadataRequestsPQ                   // requests we've sent, by timeout (earlier first)
	earlyRequests     map[uint64]rawMetadataResponse       // requests we've received a response for, but that we haven't seen that we've sent yet. should be uncommon.

	quitResponseProcessor chan struct{}                  // channel to quit the response processor, which in turn closes the socket
	incoming              chan *metadataProcessorRequest // channel where user requests come in
	inFlight              chan *metadataProcessorRequest // channel going from request processor to response processor
	rawResponses          chan rawMetadataResponse       // channel going from the socket drainer to the response processor
	responsesBufs         chan *[]byte                   // channel to store a cache of buffers to read into
	timeoutTicker         *time.Ticker                   // channel to notify the response processor to time out requests
}

var whichMetadatataAddr int

func (cm *clientMetadata) init(log *Logger, client *Client) error {
	log.Debug("initiating clientMetadata")
	defer log.Debug("finished initializing clientMetadata")
	cm.client = client
	sock, err := net.ListenPacket("udp", ":0")
	if err != nil {
		return err
	}
	cm.sock = sock.(*net.UDPConn)
	// 10MiB/100byte ~ 100k requests in the pipe. 100byte is
	// kinda conservative.
	if err := cm.sock.SetReadBuffer(1 << 20); err != nil {
		cm.sock.Close()
		return err
	}

	cm.requestsById = make(map[uint64]*metadataProcessorRequest)
	cm.requestsByTimeout = make(metadataRequestsPQ, 0)
	cm.earlyRequests = make(map[uint64]rawMetadataResponse)

	cm.quitResponseProcessor = make(chan struct{})
	cm.incoming = make(chan *metadataProcessorRequest, 10_000)
	cm.inFlight = make(chan *metadataProcessorRequest, 10_000)
	cm.rawResponses = make(chan rawMetadataResponse, 10_000)
	cm.responsesBufs = make(chan *[]byte, 128)
	for i := 0; i < len(cm.responsesBufs); i++ {
		buf := make([]byte, clientMtu)
		cm.responsesBufs <- &buf
	}

	cm.timeoutTicker = time.NewTicker(DefaultShardTimeout.Initial / 2)

	go cm.processRequests(log)
	go cm.processResponses(log)
	go cm.drainSocket(log)

	return nil
}

func (cm *clientMetadata) close() {
	cm.timeoutTicker.Stop()
	cm.quitResponseProcessor <- struct{}{}
	cm.incoming <- nil
}

// terminates when cm.incoming gets nil
func (cm *clientMetadata) processRequests(log *Logger) {
	buf := bytes.NewBuffer([]byte{})
	for {
		req := <-cm.incoming
		if req == nil {
			log.Debug("got nil request in request processor, winding down")
			return
		}
		dontWait := req.resp == nil
		log.Debug("sending request %T %+v req id %v to shard %v", req.req, req.req, req.requestId, req.shard)
		buf.Reset()
		var addrs *[2]net.UDPAddr
		var kind uint8
		var protocol uint32
		if req.shard >= 0 { // shard
			addrs = cm.client.ShardAddrs(msgs.ShardId(req.shard))
			kind = uint8(req.req.(msgs.ShardRequest).ShardRequestKind())
			protocol = msgs.SHARD_REQ_PROTOCOL_VERSION
		} else { // CDC
			addrs = cm.client.CDCAddrs()
			kind = uint8(req.req.(msgs.CDCRequest).CDCRequestKind())
			protocol = msgs.CDC_REQ_PROTOCOL_VERSION
		}
		binary.Write(buf, binary.LittleEndian, protocol)
		binary.Write(buf, binary.LittleEndian, req.requestId)
		binary.Write(buf, binary.LittleEndian, kind)
		if err := req.req.Pack(buf); err != nil {
			log.RaiseAlert("could not pack request %v to shard %v: %v", req.req, req.shard, err)
			if !dontWait {
				req.respCh <- &metadataProcessorResponse{
					requestId: req.requestId,
					err:       err,
					resp:      nil,
				}
			}
			// keep running even if the socket is totally broken to process all the requests
			continue
		}
		addr := &addrs[whichMetadatataAddr%2]
		if addr.Port == 0 {
			addr = &addrs[0]
		}
		whichMetadatataAddr++
		written, err := cm.sock.WriteToUDP(buf.Bytes(), addr)
		if err != nil {
			log.RaiseAlert("could not send request %v to shard %v addr %v: %v", req.req, req.shard, addr, err)
			if !dontWait {
				req.respCh <- &metadataProcessorResponse{
					requestId: req.requestId,
					err:       err,
					resp:      nil,
				}
			}
			// keep running even if the socket is totally broken to process all the requests
			continue
		}
		if written != len(buf.Bytes()) {
			panic(fmt.Errorf("%v != %v", written, len(buf.Bytes())))
		}
		if !dontWait {
			req.deadline = time.Now().Add(req.timeout)
			cm.inFlight <- req
		}
	}
}

func (cm *clientMetadata) parseResponse(log *Logger, req *metadataProcessorRequest, rawResp *rawMetadataResponse) {
	// discharge the raw request at the end
	defer func() {
		select {
		case cm.responsesBufs <- rawResp.buf:
		default:
		}
	}()
	// check protocol
	if req.shard < 0 { // CDC
		if rawResp.protocol != msgs.CDC_RESP_PROTOCOL_VERSION {
			log.RaiseAlert("got bad cdc protocol %v for request id %v, ignoring", rawResp.protocol, req.requestId)
			return
		}
	} else {
		if rawResp.protocol != msgs.SHARD_RESP_PROTOCOL_VERSION {
			log.RaiseAlert("got bad shard protocol %v for request id %v, shard %v, ignoring", rawResp.protocol, req.shard, req.requestId)
			return
		}
	}
	// remove everywhere
	delete(cm.earlyRequests, req.requestId)
	if _, found := cm.requestsById[req.requestId]; found {
		delete(cm.requestsById, req.requestId)
		heap.Remove(&cm.requestsByTimeout, req.index)
	}
	if rawResp.kind == msgs.ERROR {
		var err error
		if rawResp.respLen != 4+8+1+2 {
			log.RaiseAlert("bad error response length %v, expected %v", rawResp.respLen, 4+8+1+2)
			err = msgs.MALFORMED_RESPONSE
		} else {
			err = msgs.ErrCode(binary.LittleEndian.Uint16((*rawResp.buf)[4+8+1:]))
		}
		req.respCh <- &metadataProcessorResponse{
			requestId: req.requestId,
			err:       err,
			resp:      nil,
		}
	} else {
		// check kind
		if req.shard < 0 { // CDC
			expectedKind := req.req.(msgs.CDCRequest).CDCRequestKind()
			if uint8(expectedKind) != rawResp.kind {
				log.RaiseAlert("got bad cdc kind %v for request id %v, expected %v", msgs.CDCMessageKind(rawResp.kind), req.requestId, expectedKind)
				req.respCh <- &metadataProcessorResponse{
					requestId: req.requestId,
					err:       msgs.MALFORMED_RESPONSE,
					resp:      nil,
				}
				return
			}
		} else {
			expectedKind := req.req.(msgs.ShardRequest).ShardRequestKind()
			if uint8(expectedKind) != rawResp.kind {
				log.RaiseAlert("got bad shard kind %v for request id %v, shard %v, expected %v", msgs.ShardMessageKind(rawResp.kind), req.requestId, req.shard, expectedKind)
				req.respCh <- &metadataProcessorResponse{
					requestId: req.requestId,
					err:       msgs.MALFORMED_RESPONSE,
					resp:      nil,
				}
				return
			}
		}
		// unpack
		if err := bincode.Unpack((*rawResp.buf)[4+8+1:rawResp.respLen], req.resp); err != nil {
			log.RaiseAlert("could not unpack resp %T for request id %v, shard %v: %v", req.resp, req.requestId, req.shard, err)
			req.respCh <- &metadataProcessorResponse{
				requestId: req.requestId,
				err:       err,
				resp:      nil,
			}
			return
		}
		log.Debug("received resp %v req id %v from shard %v", req.resp, req.requestId, req.shard)
		// done
		req.respCh <- &metadataProcessorResponse{
			requestId: req.requestId,
			err:       nil,
			resp:      req.resp,
		}
	}
}

// terminates when `cm.quitResponseProcessor` gets a message
func (cm *clientMetadata) processResponses(log *Logger) {
	for {
		select {
		case req := <-cm.inFlight:
			if rawResp, found := cm.earlyRequests[req.requestId]; found {
				// uncommon case: we have a response for this already.
				cm.parseResponse(log, req, &rawResp)
			} else {
				// common case: we don't have the response yet, put it in the data structures and wait.
				// if the request was there before, we remove it from the heap so that we don't have
				// dupes and the deadline is right
				if _, found := cm.requestsById[req.requestId]; found {
					heap.Remove(&cm.requestsByTimeout, req.index)
				}
				cm.requestsById[req.requestId] = req
				heap.Push(&cm.requestsByTimeout, req)
			}
		case rawResp := <-cm.rawResponses:
			if req, found := cm.requestsById[rawResp.requestId]; found {
				// common case, the request is already there
				cm.parseResponse(log, req, &rawResp)
			} else {
				// uncommon case, the request is missing
				cm.earlyRequests[rawResp.requestId] = rawResp
			}
		case now := <-cm.timeoutTicker.C:
			// expire requests
			for len(cm.requestsById) > 0 {
				first := cm.requestsByTimeout[0]
				if now.After(first.deadline) {
					log.Debug("request %v %T to shard %v has timed out", first.requestId, first.req, first.shard)
					heap.Pop(&cm.requestsByTimeout)
					delete(cm.requestsById, first.requestId)
					// consumer might very plausibly be gone by now,
					// don't risk it
					go func() {
						first.respCh <- &metadataProcessorResponse{
							requestId: first.requestId,
							err:       msgs.TIMEOUT,
							resp:      nil,
						}
					}()
				} else {
					log.Debug("first request %v %T has not passed deadline %v", first.requestId, first.req, first.deadline)
					break
				}
			}
			// expire request we got past timeouts -- this map should always be small
			for reqId, rawReq := range cm.earlyRequests {
				if now.Sub(rawReq.receivedAt) > 10*time.Minute {
					delete(cm.earlyRequests, reqId)
				}
			}
		case <-cm.quitResponseProcessor:
			log.Info("got quit signal, closing socket and terminating")
			cm.sock.Close()
			return
		}
	}
}

// terminates when the socket is closed
func (cm *clientMetadata) drainSocket(log *Logger) {
	for {
		var buf *[]byte
		select {
		case buf = <-cm.responsesBufs:
		default:
		}
		if buf == nil {
			log.Debug("allocating new MTU buffer")
			bufv := make([]byte, clientMtu)
			buf = &bufv
		}
		read, _, err := cm.sock.ReadFromUDP(*buf)
		if err != nil {
			if errors.Is(err, net.ErrClosed) {
				log.Info("socket is closed, winding down")
				return
			} else {
				log.RaiseAlert("got error when reading socket: %v", err)
			}
		}
		if read < 4+8+1 {
			log.RaiseAlert("got runt metadata message, expected at least %v bytes, got %v", 4+8+1, read)
			continue
		}
		rawResp := rawMetadataResponse{
			receivedAt: time.Now(),
			respLen:    read,
			buf:        buf,
			protocol:   binary.LittleEndian.Uint32(*buf),
			requestId:  binary.LittleEndian.Uint64((*buf)[4:]),
			kind:       (*buf)[4+8],
		}
		cm.rawResponses <- rawResp
	}
}

type BlockCompletion struct {
	Resp  msgs.BlocksResponse
	Extra any
	Error error
}

type clientBlockResponse struct {
	req   msgs.BlocksRequest
	resp  msgs.BlocksResponse
	extra any
	// when fetching block, this gets written into
	additionalBodyWriter io.ReaderFrom
	// stores the error, if any
	err error
	// called when we're done
	completionChan chan *BlockCompletion
}

func (resp *clientBlockResponse) done(log *Logger, addr1 *net.TCPAddr, addr2 *net.TCPAddr, extra any, err error) {
	if resp.err == nil && err != nil {
		log.InfoStack(1, "failing request %T %+v addr1=%+v addr2=%+v extra=%+v: %v", resp.req, resp.req, addr1, addr2, extra, err)
		resp.err = err
	}
	completion := &BlockCompletion{
		Resp:  resp.resp,
		Error: resp.err,
		Extra: resp.extra,
	}
	resp.completionChan <- completion
}

type clientBlockRequest struct {
	blockService         msgs.BlockServiceId
	req                  msgs.BlocksRequest
	additionalBodyReader io.Reader // when writing block, this will be written after the request
	resp                 *clientBlockResponse
}

type blocksProcessorConn struct {
	conn       *net.TCPConn
	generation uint64
}

type clientBlockResponseWithGeneration struct {
	generation uint64
	resp       *clientBlockResponse
}

type blocksProcessor struct {
	reqChan         chan *clientBlockRequest
	inFlightReqChan chan clientBlockResponseWithGeneration
	addr1           net.TCPAddr
	addr2           net.TCPAddr
	what            string
	_conn           *blocksProcessorConn // this must be loaded through loadConn
}

func (proc *blocksProcessor) loadConn() *blocksProcessorConn {
	return (*blocksProcessorConn)(atomic.LoadPointer((*unsafe.Pointer)(unsafe.Pointer(&proc._conn))))
}

func (proc *blocksProcessor) storeConn(conn *net.TCPConn) *blocksProcessorConn {
	gen := proc.loadConn().generation
	newConn := &blocksProcessorConn{
		conn:       conn,
		generation: gen + 1,
	}
	atomic.StorePointer((*unsafe.Pointer)(unsafe.Pointer(&proc._conn)), unsafe.Pointer(newConn))
	return newConn
}

var whichBlockIp uint

func (proc *blocksProcessor) connect(log *Logger) (*net.TCPConn, error) {
	var err error
	whichBlockIp++
	for i := whichBlockIp; i < whichBlockIp+2; i++ {
		var addr *net.TCPAddr
		if i&1 == 0 {
			addr = &proc.addr1
		} else {
			addr = &proc.addr2
		}
		if addr.Port == 0 {
			continue
		}
		log.Debug("trying to connect to block service %v", addr)
		var sock *net.TCPConn
		sock, err = net.DialTCP("tcp4", nil, addr)
		if err == nil {
			log.Debug("connected to block service at %v", addr)
			return sock, nil
		}
		log.Info("could not connect to block service %v, might try next connection: %v", addr, err)
	}
	if err == nil {
		panic(fmt.Errorf("impossible: got out without errors"))
	}
	return nil, err
}

// From <https://stackoverflow.com/a/58664631>, checks if a connection
// is still alive.
func connCheck(conn *net.TCPConn) error {
	var sysErr error = nil
	rc, err := conn.SyscallConn()
	if err != nil {
		return err
	}
	err = rc.Read(func(fd uintptr) bool {
		var buf []byte = []byte{0}
		n, _, err := syscall.Recvfrom(int(fd), buf, syscall.MSG_PEEK|syscall.MSG_DONTWAIT)
		switch {
		case n == 0 && err == nil:
			sysErr = io.EOF
		case err == syscall.EAGAIN || err == syscall.EWOULDBLOCK:
			sysErr = nil
		default:
			sysErr = err
		}
		return true
	})
	if err != nil {
		return err
	}
	return sysErr
}

func (proc *blocksProcessor) processRequests(log *Logger) {
	log.Debug("%v: starting request processor for addr1=%v addr2=%v", proc.what, proc.addr1, proc.addr2)
	// one iteration = one request
	for {
		conn := proc.loadConn()
		req := <-proc.reqChan
		if req == nil {
			log.Debug("%v: got nil request, tearing down", proc.what)
			if conn.conn != nil {
				conn.conn.Close()
			}
			proc.inFlightReqChan <- clientBlockResponseWithGeneration{
				resp:       nil, // this tears down the response processor
				generation: 0,
			}
			return
		}
		// empty queue, check that conn is alive, otherwise we might still succeed sending,
		// but inevitably fail when reading.
		if conn.conn != nil && len(proc.inFlightReqChan) == 0 {
			if err := connCheck(conn.conn); err != nil {
				log.Debug("connection for addr1=%+v addr2=%+v is dead: %v", proc.addr1, proc.addr2, err)
				conn.conn.Close()
				conn.conn = nil
			}
		}
		// clear stale connections otherwise we might succeed writing but fail reading on the other side
		if conn.conn == nil {
			tcpConn, err := proc.connect(log)
			if err != nil { // we couldn't connect, not much to do
				req.resp.done(log, &proc.addr1, &proc.addr2, req.resp.extra, err)
				continue
			}
			// we did connect
			conn = proc.storeConn(tcpConn)
		}
		log.Debug("writing block request %T %+v for addr1=%v addr2=%v", req.req, req.req, proc.addr1, proc.addr2)
		if err := WriteBlocksRequest(log, conn.conn, req.blockService, req.req); err != nil {
			log.Info("got error when writing block request of kind %v in %v->%v: %v", req.req.BlocksRequestKind(), conn.conn.LocalAddr(), conn.conn.RemoteAddr(), err)
			req.resp.done(log, &proc.addr1, &proc.addr2, req.resp.extra, err)
			conn.conn.Close()
			conn.conn = nil
			continue
		}
		if req.req.BlocksRequestKind() == msgs.WRITE_BLOCK {
			writeReq := req.req.(*msgs.WriteBlockReq)
			lr := &io.LimitedReader{
				R: req.additionalBodyReader,
				N: int64(writeReq.Size),
			}
			log.Debug("writing block body to %v->%v", conn.conn.LocalAddr(), conn.conn.RemoteAddr())
			writtenBytes, err := conn.conn.ReadFrom(lr)
			if err != nil || writtenBytes < int64(writeReq.Size) {
				if err == nil {
					err = io.EOF
				}
				log.Info("got error when writing block body: %v", err)
				req.resp.done(log, &proc.addr1, &proc.addr2, req.resp.extra, err)
				conn.conn.Close()
				conn.conn = nil
				continue
			}
		}
		// we wrote it fine, proceed
		proc.inFlightReqChan <- clientBlockResponseWithGeneration{
			generation: conn.generation,
			resp:       req.resp,
		}
	}
}

func (proc *blocksProcessor) processResponses(log *Logger) {
	log.Debug("%v: starting response processor for addr1=%v addr2=%v", proc.what, proc.addr1, proc.addr2)
	// one iteration = one request
	for {
		resp := <-proc.inFlightReqChan
		if resp.resp == nil {
			return
		}
		conn := proc.loadConn()
		connr := conn.conn
		if connr == nil {
			log.Info("%v: resp %T %+v has no conn, skipping", proc.what, resp.resp.resp, resp.resp.resp)
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, io.EOF)
			continue
		}
		if conn.generation != resp.generation {
			log.Info("%v: resp %T %+v has bad generation %v vs %v, skipping", proc.what, resp.resp.resp, resp.resp.resp, resp.generation, conn.generation)
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, io.EOF)
			continue
		}
		log.Debug("reading block response %T for req %+v from %v->%v", resp.resp.resp, resp.resp.req, connr.LocalAddr(), connr.RemoteAddr())
		// the responsibility for cleaning up the connection is always in the request processor
		if err := ReadBlocksResponse(log, connr, resp.resp.resp); err != nil {
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, err)
			continue
		}
		if resp.resp.resp.BlocksResponseKind() == msgs.FETCH_BLOCK {
			req := resp.resp.req.(*msgs.FetchBlockReq)
			lr := &io.LimitedReader{
				R: connr,
				N: int64(req.Count),
			}
			log.Debug("reading block body from %v->%v", connr.LocalAddr(), connr.RemoteAddr())
			readBytes, err := resp.resp.additionalBodyWriter.ReadFrom(lr)
			if err != nil || readBytes < int64(req.Count) {
				if err == nil {
					err = io.EOF
				}
				resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, err)
				continue
			}
		}
		log.Debug("read block response %T %+v for req %v from %v->%v", resp.resp.resp, resp.resp.resp, resp.resp.req, connr.LocalAddr(), connr.RemoteAddr())
		resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, nil)
	}
}

type blocksProcessorKey struct {
	blockServiceKey uint64
	ip1             [4]byte
	port1           uint16
	ip2             [4]byte
	port2           uint16
}

type blocksProcessors struct {
	what string
	// how many bits of the block service id to use for blockServiceKey
	blockServiceBits uint8
	// blocksProcessorKey -> *blocksProcessor
	processors sync.Map
}

func (procs *blocksProcessors) init(what string) {
	procs.what = what
}

type sendArgs struct {
	blockService       msgs.BlockServiceId
	ip1                [4]byte
	port1              uint16
	ip2                [4]byte
	port2              uint16
	req                msgs.BlocksRequest
	reqAdditionalBody  io.Reader
	resp               msgs.BlocksResponse
	respAdditionalBody io.ReaderFrom
	extra              any
}

// This currently never fails (everything network related happens in
// the processor loops), keeping error since it might fail in the future
func (procs *blocksProcessors) send(
	log *Logger,
	args *sendArgs,
	completionChan chan *BlockCompletion,
) error {
	if args.port1 == 0 && args.port2 == 0 {
		panic(fmt.Errorf("got zero ports for both addresses for block service %v: %v:%v %v:%v", args.blockService, args.ip1, args.port1, args.ip2, args.port2))
	}
	resp := &clientBlockResponse{
		req:                  args.req,
		resp:                 args.resp,
		additionalBodyWriter: args.respAdditionalBody,
		completionChan:       completionChan,
		extra:                args.extra,
	}
	req := &clientBlockRequest{
		blockService:         args.blockService,
		req:                  args.req,
		additionalBodyReader: args.reqAdditionalBody,
		resp:                 resp,
	}
	key := blocksProcessorKey{
		ip1:   args.ip1,
		port1: args.port1,
		ip2:   args.ip2,
		port2: args.port2,
	}
	key.blockServiceKey = uint64(args.blockService) & ((1 << uint64(procs.blockServiceBits)) - 1)
	// likely case, we already have something. we could
	// LoadOrStore directly but this saves us allocating new
	// chans
	if procAny, found := procs.processors.Load(key); found {
		proc := procAny.(*blocksProcessor)
		proc.reqChan <- req
		return nil
	}
	// unlikely case, create new one
	procAny, loaded := procs.processors.LoadOrStore(key, &blocksProcessor{
		reqChan:         make(chan *clientBlockRequest, 128),
		inFlightReqChan: make(chan clientBlockResponseWithGeneration, 128),
		addr1:           net.TCPAddr{IP: net.IP(args.ip1[:]), Port: int(args.port1)},
		addr2:           net.TCPAddr{IP: net.IP(args.ip2[:]), Port: int(args.port2)},
		what:            procs.what,
		_conn:           &blocksProcessorConn{},
	})
	proc := procAny.(*blocksProcessor)
	if !loaded {
		// we're the first ones here, start the routines
		go proc.processRequests(log)
		go proc.processResponses(log)
	}
	proc.reqChan <- req
	return nil
}

func (procs *blocksProcessors) close() {
	procs.processors.Range(func(key, value any) bool {
		value.(*blocksProcessor).reqChan <- nil
		return true
	})
}

type Client struct {
	shardRawAddrs        [256][2]uint64
	cdcRawAddr           [2]uint64
	clientMetadata       clientMetadata
	counters             *ClientCounters
	cdcKey               cipher.Block
	writeBlockProcessors blocksProcessors
	fetchBlockProcessors blocksProcessors
	fetchBlockBufs       sync.Pool
	eraseBlockProcessors blocksProcessors
	checkBlockProcessors blocksProcessors
	shardTimeout         *ReqTimeouts
	cdcTimeout           *ReqTimeouts
	blockTimeout         *ReqTimeouts
	requestIdCounter     uint64
}

func NewClient(
	log *Logger,
	shuckleTimeout *ReqTimeouts,
	shuckleAddress string,
) (*Client, error) {
	var shardIps [256][2][4]byte
	var shardPorts [256][2]uint16
	var cdcIps [2][4]byte
	var cdcPorts [2]uint16
	{
		log.Info("Getting shard/CDC info from shuckle at '%v'", shuckleAddress)
		resp, err := ShuckleRequest(log, shuckleTimeout, shuckleAddress, &msgs.ShardsReq{})
		if err != nil {
			return nil, fmt.Errorf("could not request shards from shuckle: %w", err)
		}
		shards := resp.(*msgs.ShardsResp)
		for i, shard := range shards.Shards {
			if shard.Port1 == 0 {
				return nil, fmt.Errorf("shard %v not present in shuckle", i)
			}
			shardIps[i][0] = shard.Ip1
			shardPorts[i][0] = shard.Port1
			shardIps[i][1] = shard.Ip2
			shardPorts[i][1] = shard.Port2
		}
		resp, err = ShuckleRequest(log, shuckleTimeout, shuckleAddress, &msgs.CdcReq{})
		if err != nil {
			return nil, fmt.Errorf("could not request CDC from shuckle: %w", err)
		}
		cdc := resp.(*msgs.CdcResp)
		if cdc.Port1 == 0 {
			return nil, fmt.Errorf("CDC not present in shuckle")
		}
		cdcIps[0] = cdc.Ip1
		cdcPorts[0] = cdc.Port1
		cdcIps[1] = cdc.Ip2
		cdcPorts[1] = cdc.Port2
	}
	return NewClientDirect(log, &cdcIps, &cdcPorts, &shardIps, &shardPorts)
}

func (c *Client) SetCounters(counters *ClientCounters) {
	c.counters = counters
}

func (c *Client) SetCDCKey(cdcKey cipher.Block) {
	c.cdcKey = cdcKey
}

func (c *Client) SetShardTimeouts(t *ReqTimeouts) {
	c.shardTimeout = t
}

func (c *Client) SetCDCTimeouts(t *ReqTimeouts) {
	c.cdcTimeout = t
}

func (c *Client) SetBlockTimeout(t *ReqTimeouts) {
	c.blockTimeout = t
}

var clientMtu uint16 = 1472

// MTU used for large responses (file spans etc)
func SetMTU(mtu uint64) {
	if mtu < msgs.DEFAULT_UDP_MTU {
		panic(fmt.Errorf("mtu (%v) < DEFAULT_UDP_MTU (%v)", mtu, msgs.DEFAULT_UDP_MTU))
	}
	if mtu > msgs.MAX_UDP_MTU {
		panic(fmt.Errorf("mtu (%v) > MAX_UDP_MTU (%v)", mtu, msgs.MAX_UDP_MTU))
	}
	clientMtu = uint16(mtu)
}

func NewClientDirectNoAddrs(
	log *Logger,
) (c *Client, err error) {
	c = &Client{
		// do not catch requests from previous executions
		requestIdCounter: rand.Uint64(),
		fetchBlockBufs: sync.Pool{
			New: func() any {
				return bytes.NewBuffer([]byte{})
			},
		},
	}
	c.shardTimeout = &DefaultShardTimeout
	c.cdcTimeout = &DefaultCDCTimeout
	c.blockTimeout = &DefaultBlockTimeout
	if err := c.clientMetadata.init(log, c); err != nil {
		return nil, err
	}
	// Ideally, for write/fetch we'd want to have one socket
	// per block service. However we don't have flash yet, so
	// it's hard to saturate a socket because of seek time.
	//
	// Currently we have 102 disks per server, 96 servers. 5
	// bits splits the block services in 32 buckets, and the
	// block service id is uniformly distributed.
	//
	// So we'll have roughly 30 connections per server for the first
	// two, and, for a maximum of (currently) 30*96*2 + 102*96*2 =
	// 25k connections, which is within limits.
	//
	// (The exact expected number of connections per server is
	// ~30.7447, I'll let you figure out why.)
	c.writeBlockProcessors.init("write")
	c.writeBlockProcessors.blockServiceBits = 5
	c.fetchBlockProcessors.init("fetch")
	c.fetchBlockProcessors.blockServiceBits = 5
	c.eraseBlockProcessors.init("erase")
	// we're not constrained by bandwidth here, we want to have requests
	// for all block services in parallel.
	c.eraseBlockProcessors.blockServiceBits = 63
	// here we're also not constrained by bandwidth, but the requests
	// take a long time, so have a separate channel from the erase ones.
	c.checkBlockProcessors.init("erase")
	c.checkBlockProcessors.blockServiceBits = 63
	return c, nil
}

func NewClientDirect(
	log *Logger,
	cdcIps *[2][4]byte,
	cdcPorts *[2]uint16,
	shardIps *[256][2][4]byte,
	shardPorts *[256][2]uint16,
) (c *Client, err error) {
	c, err = NewClientDirectNoAddrs(log)
	if err != nil {
		return nil, err
	}
	c.UpdateAddrs(cdcIps, cdcPorts, shardIps, shardPorts)
	return c, nil
}

func uint64ToUDPAddr(addr uint64) *net.UDPAddr {
	udpAddr := &net.UDPAddr{}
	udpAddr.IP = []byte{byte(addr >> 24), byte(addr >> 16), byte(addr >> 8), byte(addr)}
	udpAddr.Port = int(addr >> 32)
	return udpAddr
}

func (c *Client) CDCAddrs() *[2]net.UDPAddr {
	return &[2]net.UDPAddr{
		*uint64ToUDPAddr(atomic.LoadUint64(&c.cdcRawAddr[0])),
		*uint64ToUDPAddr(atomic.LoadUint64(&c.cdcRawAddr[1])),
	}
}

func (c *Client) ShardAddrs(shid msgs.ShardId) *[2]net.UDPAddr {
	return &[2]net.UDPAddr{
		*uint64ToUDPAddr(atomic.LoadUint64(&c.shardRawAddrs[shid][0])),
		*uint64ToUDPAddr(atomic.LoadUint64(&c.shardRawAddrs[shid][1])),
	}
}

func (c *Client) UpdateAddrs(
	cdcIps *[2][4]byte,
	cdcPorts *[2]uint16,
	shardIps *[256][2][4]byte,
	shardPorts *[256][2]uint16,
) {
	for i := 0; i < 2; i++ {
		for j := 0; j < 256; j++ {
			atomic.StoreUint64(
				&c.shardRawAddrs[j][i],
				uint64(shardPorts[j][i])<<32|uint64(shardIps[j][i][0])<<24|uint64(shardIps[j][i][1])<<16|uint64(shardIps[j][i][2])<<8|uint64(shardIps[j][i][3]),
			)
		}
		atomic.StoreUint64(
			&c.cdcRawAddr[i],
			uint64(cdcPorts[i])<<32|uint64(cdcIps[i][0])<<24|uint64(cdcIps[i][1])<<16|uint64(cdcIps[i][2])<<8|uint64(cdcIps[i][3]),
		)
	}
}

func (c *Client) Close() {
	c.clientMetadata.close()
	c.writeBlockProcessors.close()
	c.fetchBlockProcessors.close()
	c.eraseBlockProcessors.close()
	c.checkBlockProcessors.close()
}

// Not atomic between the read/write
func (c *Client) MergeDirectoryInfo(log *Logger, id msgs.InodeId, entry msgs.IsDirectoryInfoEntry) error {
	packedEntry := msgs.DirectoryInfoEntry{
		Body: bincode.Pack(entry),
		Tag:  entry.Tag(),
	}
	statResp := msgs.StatDirectoryResp{}
	if err := c.ShardRequest(log, id.Shard(), &msgs.StatDirectoryReq{Id: id}, &statResp); err != nil {
		return err
	}
	info := statResp.Info
	found := false
	for i := 0; i < len(info.Entries); i++ {
		if info.Entries[i].Tag == packedEntry.Tag {
			info.Entries[i] = packedEntry
			found = true
			break
		}
	}
	if !found {
		info.Entries = append(info.Entries, packedEntry)
	}
	if err := c.ShardRequest(log, id.Shard(), &msgs.SetDirectoryInfoReq{Id: id, Info: info}, &msgs.SetDirectoryInfoResp{}); err != nil {
		return err
	}
	return nil
}

// Not atomic between the read/write
func (c *Client) RemoveDirectoryInfoEntry(log *Logger, id msgs.InodeId, tag msgs.DirectoryInfoTag) error {
	statResp := msgs.StatDirectoryResp{}
	if err := c.ShardRequest(log, id.Shard(), &msgs.StatDirectoryReq{Id: id}, &statResp); err != nil {
		return err
	}
	info := statResp.Info
	for i := 0; i < len(info.Entries); i++ {
		if info.Entries[i].Tag == tag {
			info.Entries = append(info.Entries[:i], info.Entries[i+1:]...)
			break
		}
	}
	if err := c.ShardRequest(log, id.Shard(), &msgs.SetDirectoryInfoReq{Id: id, Info: info}, &msgs.SetDirectoryInfoResp{}); err != nil {
		return err
	}
	return nil
}

func (client *Client) ResolveDirectoryInfoEntry(
	log *Logger,
	dirInfoCache *DirInfoCache,
	dirId msgs.InodeId,
	entry msgs.IsDirectoryInfoEntry, // output will be stored in here
) (inheritedFrom msgs.InodeId, err error) {
	statReq := msgs.StatDirectoryReq{
		Id: dirId,
	}
	statResp := msgs.StatDirectoryResp{}
	visited := []msgs.InodeId{}
TraverseDirectories:
	for {
		inheritedFrom = dirInfoCache.LookupCachedDirInfoEntry(statReq.Id, entry)
		if inheritedFrom != msgs.NULL_INODE_ID {
			break
		}
		visited = append(visited, statReq.Id)
		if err := client.ShardRequest(log, statReq.Id.Shard(), &statReq, &statResp); err != nil {
			return msgs.NULL_INODE_ID, err
		}
		for i := len(statResp.Info.Entries) - 1; i >= 0; i-- {
			respEntry := &statResp.Info.Entries[i]
			if entry.Tag() == respEntry.Tag {
				inheritedFrom = statReq.Id
				if err := bincode.Unpack(respEntry.Body, entry); err != nil {
					return msgs.NULL_INODE_ID, fmt.Errorf("could not decode dir info entry for dir %v, inherited from %v, with tag %v, body %v: %v", dirId, statReq.Id, entry.Tag(), respEntry.Body, err)
				}
				break TraverseDirectories
			}
		}
		if statReq.Id == msgs.ROOT_DIR_INODE_ID {
			return msgs.NULL_INODE_ID, fmt.Errorf("could not find directory info entry with tag %v %+v", entry.Tag(), statResp)
		}
		statReq.Id = statResp.Owner
	}
	for _, id := range visited {
		dirInfoCache.UpdateCachedDirInfo(id, entry, inheritedFrom)
	}
	return inheritedFrom, nil
}

func (client *Client) ResolvePathWithParent(log *Logger, path string) (msgs.InodeId, msgs.InodeId, error) {
	if !filepath.IsAbs(path) {
		return msgs.NULL_INODE_ID, msgs.NULL_INODE_ID, fmt.Errorf("expected absolute path, got '%v'", path)
	}
	parent := msgs.NULL_INODE_ID
	id := msgs.ROOT_DIR_INODE_ID
	for _, segment := range strings.Split(filepath.Clean(path), "/")[1:] {
		if segment == "" {
			continue
		}
		resp := msgs.LookupResp{}
		if err := client.ShardRequest(log, id.Shard(), &msgs.LookupReq{DirId: id, Name: segment}, &resp); err != nil {
			return msgs.NULL_INODE_ID, msgs.NULL_INODE_ID, err
		}
		parent = id
		id = resp.TargetId
	}
	return id, parent, nil
}

func (client *Client) ResolvePath(log *Logger, path string) (msgs.InodeId, error) {
	id, _, err := client.ResolvePathWithParent(log, path)
	return id, err
}

func writeBlockSendArgs(block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc, extra any) *sendArgs {
	return &sendArgs{
		block.BlockServiceId,
		block.BlockServiceIp1,
		block.BlockServicePort1,
		block.BlockServiceIp2, block.BlockServicePort2,
		&msgs.WriteBlockReq{
			BlockId:     block.BlockId,
			Crc:         crc,
			Size:        size,
			Certificate: block.Certificate,
		},
		r,
		&msgs.WriteBlockResp{},
		nil,
		extra,
	}
}

func (client *Client) StartWriteBlock(log *Logger, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc, extra any, completion chan *BlockCompletion) error {
	return client.writeBlockProcessors.send(log, writeBlockSendArgs(block, r, size, crc, extra), completion)
}

func RetriableBlockError(err error) bool {
	return errors.Is(err, syscall.ECONNREFUSED) || errors.Is(err, syscall.EPIPE) || errors.Is(err, syscall.ECONNRESET) || errors.Is(err, io.EOF)
}

func (client *Client) singleBlockReq(log *Logger, timeouts *ReqTimeouts, processor *blocksProcessors, args *sendArgs) (msgs.BlocksResponse, error) {
	if timeouts == nil {
		timeouts = client.blockTimeout
	}
	timeoutAlert := log.NewNCAlert(0)
	defer log.ClearNC(timeoutAlert)
	startedAt := time.Now()
	for {
		ch := make(chan *BlockCompletion, 1)
		err := processor.send(log, args, ch)
		if err != nil {
			log.Debug("failed to send block request to %v:%v %v:%v: %v", net.IP(args.ip1[:]), args.port1, net.IP(args.ip2[:]), args.port2, err)
			return nil, err
		}
		resp := <-ch
		err = resp.Error
		if err == nil {
			return resp.Resp, nil
		}
		if RetriableBlockError(err) {
			next := timeouts.Next(startedAt)
			if next == 0 {
				log.RaiseNCStack(timeoutAlert, ERROR, 2, "block request to %v:%v %v:%v failed with retriable error, will not retry since time is up: %v", net.IP(args.ip1[:]), args.port1, net.IP(args.ip2[:]), args.port2, err)
				return nil, err
			}
			log.RaiseNCStack(timeoutAlert, ERROR, 2, "block request to %v:%v %v:%v failed with retriable error, might retry: %v", net.IP(args.ip1[:]), args.port1, net.IP(args.ip2[:]), args.port2, err)
			time.Sleep(next)
		} else {
			return nil, err
		}
	}
}

func (client *Client) WriteBlock(log *Logger, timeouts *ReqTimeouts, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc) (proof [8]byte, err error) {
	resp, err := client.singleBlockReq(log, timeouts, &client.writeBlockProcessors, writeBlockSendArgs(block, r, size, crc, nil))
	if err != nil {
		return proof, err
	}
	return resp.(*msgs.WriteBlockResp).Proof, nil
}

func fetchBlockSendArgs(blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32, w io.ReaderFrom, extra any) *sendArgs {
	return &sendArgs{
		blockService.Id,
		blockService.Ip1,
		blockService.Port1,
		blockService.Ip2,
		blockService.Port2,
		&msgs.FetchBlockReq{
			BlockId: blockId,
			Offset:  offset,
			Count:   count,
		},
		nil,
		&msgs.FetchBlockResp{},
		w,
		extra,
	}
}

func (client *Client) StartFetchBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32, w io.ReaderFrom, extra any, completion chan *BlockCompletion) error {
	return client.fetchBlockProcessors.send(log, fetchBlockSendArgs(blockService, blockId, offset, count, w, extra), completion)
}

func (c *Client) PutFetchedBlock(body *bytes.Buffer) {
	c.fetchBlockBufs.Put(body)
}

func (client *Client) FetchBlock(log *Logger, timeouts *ReqTimeouts, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32) (body *bytes.Buffer, err error) {
	buf := client.fetchBlockBufs.Get().(*bytes.Buffer)
	buf.Reset()
	_, err = client.singleBlockReq(log, timeouts, &client.fetchBlockProcessors, fetchBlockSendArgs(blockService, blockId, offset, count, buf, nil))
	if err != nil {
		return nil, err
	}
	return buf, nil
}

func eraseBlockSendArgs(block *msgs.RemoveSpanInitiateBlockInfo, extra any) *sendArgs {
	return &sendArgs{
		block.BlockServiceId,
		block.BlockServiceIp1,
		block.BlockServicePort1,
		block.BlockServiceIp2,
		block.BlockServicePort2,
		&msgs.EraseBlockReq{
			BlockId:     block.BlockId,
			Certificate: block.Certificate,
		},
		nil,
		&msgs.EraseBlockResp{},
		nil,
		extra,
	}
}

func (client *Client) StartEraseBlock(log *Logger, block *msgs.RemoveSpanInitiateBlockInfo, extra any, completion chan *BlockCompletion) error {
	return client.eraseBlockProcessors.send(log, eraseBlockSendArgs(block, extra), completion)
}

func (client *Client) EraseBlock(log *Logger, block *msgs.RemoveSpanInitiateBlockInfo) (proof [8]byte, err error) {
	resp, err := client.singleBlockReq(log, nil, &client.eraseBlockProcessors, eraseBlockSendArgs(block, nil))
	if err != nil {
		return proof, err
	}
	return resp.(*msgs.EraseBlockResp).Proof, nil
}

func checkBlockSendArgs(blockService *msgs.BlockService, blockId msgs.BlockId, size uint32, crc msgs.Crc, extra any) *sendArgs {
	return &sendArgs{
		blockService.Id,
		blockService.Ip1,
		blockService.Port1,
		blockService.Ip2,
		blockService.Port2,
		&msgs.CheckBlockReq{
			BlockId: blockId,
			Size:    size,
			Crc:     crc,
		},
		nil,
		&msgs.CheckBlockResp{},
		nil,
		extra,
	}
}

func (client *Client) StartCheckBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, size uint32, crc msgs.Crc, extra any, completion chan *BlockCompletion) error {
	return client.checkBlockProcessors.send(log, checkBlockSendArgs(blockService, blockId, size, crc, extra), completion)
}

func (client *Client) CheckBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, size uint32, crc msgs.Crc) error {
	_, err := client.singleBlockReq(log, nil, &client.checkBlockProcessors, checkBlockSendArgs(blockService, blockId, size, crc, nil))
	return err
}
