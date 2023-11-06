package lib

import (
	"bytes"
	"crypto/cipher"
	"encoding/binary"
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

type ClientCounters struct {
	Shard map[uint8]*ReqCounters
	CDC   map[uint8]*ReqCounters
}

func NewClientCounters() *ClientCounters {
	counters := ClientCounters{
		Shard: make(map[uint8]*ReqCounters),
		CDC:   make(map[uint8]*ReqCounters),
	}
	for _, k := range msgs.AllShardMessageKind {
		// max = ~1min
		counters.Shard[uint8(k)] = &ReqCounters{
			Timings: *NewTimings(40, time.Microsecond*10, 1.5),
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
		shardTime += counters.Shard[uint8(k)].Timings.TotalTime()
	}
	log.Info("Shard stats (total shard time %v):", shardTime)
	for _, k := range msgs.AllShardMessageKind {
		c := counters.Shard[uint8(k)]
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

type ReqTimeouts struct {
	Initial time.Duration // the first timeout
	Max     time.Duration // the max timeout -- 0 for never
	Overall time.Duration // the max overall waiting time for a request
	Growth  float64
	Jitter  float64 // in percentage, e.g. 0.10 for a 10% jitter
	rand    wyhash.Rand
}

func NewReqTimeouts(initial time.Duration, max time.Duration, overall time.Duration, growth float64, jitter float64) *ReqTimeouts {
	if initial <= 0 {
		panic(fmt.Errorf("initial (%v) <= 0", initial))
	}
	if max <= 0 {
		panic(fmt.Errorf("max (%v) <= 0", max))
	}
	if overall < 0 {
		panic(fmt.Errorf("overall (%v) < 0", overall))
	}
	if growth <= 1.0 {
		panic(fmt.Errorf("growth (%v) <= 1.0", growth))
	}
	if jitter < 0.0 {
		panic(fmt.Errorf("jitter (%v) < 0.0", jitter))
	}
	return &ReqTimeouts{
		Initial: initial,
		Max:     max,
		Overall: overall,
		Growth:  growth,
		Jitter:  jitter,
		rand:    *wyhash.New(rand.Uint64()),
	}
}

// If 0, time's up.
func (r *ReqTimeouts) NextNow(startedAt time.Time, now time.Time) time.Duration {
	elapsed := now.Sub(startedAt)
	if r.Overall > 0 && elapsed >= r.Overall {
		return time.Duration(0)
	}
	g := r.Growth + r.Growth*r.Jitter*(r.rand.Float64()-0.5)
	timeout := r.Initial + startedAt.Add(time.Duration(float64(elapsed/1000000)*g)*1000000).Sub(now) // compute in milliseconds to avoid inf
	if timeout > r.Max {
		timeout = r.Max
	}
	return timeout
}

func (r *ReqTimeouts) Next(startedAt time.Time) time.Duration {
	return r.NextNow(startedAt, time.Now())
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

type metadataProcessorRequest struct {
	requestId uint64
	clear     bool // if this is set, the other fields are ignored: we just remove the req from the map.
	timeout   time.Duration
	addr      *net.UDPAddr
	body      []byte
	respCh    chan []byte
}

type metadataProcessorResponse struct {
	requestId uint64
	body      []byte
}

type clientMetadata struct {
	sock       *net.UDPConn
	sockClosed bool // to terminate the receiver
	requests   chan *metadataProcessorRequest
	responses  chan metadataProcessorResponse
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
	lastIterationAt := time.Now()
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
		// empty queue, and haven't done anything in a while, check that conn is alive,
		// otherwise we might still succeed sending, but inevitably fail when reading.
		if conn.conn != nil && time.Since(lastIterationAt) > 10*time.Second && len(proc.inFlightReqChan) == 0 {
			if err := connCheck(conn.conn); err != nil {
				log.Debug("connection for addr1=%+v addr2=%+v is dead: %v", proc.addr1, proc.addr2, err)
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
			if _, err := conn.conn.ReadFrom(lr); err != nil {
				log.Info("got error when writing block body: %v", err)
				req.resp.done(log, &proc.addr1, &proc.addr2, req.resp.extra, err)
				conn.conn = nil
				continue
			}
		}
		lastIterationAt = time.Now()
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
		if conn.conn == nil {
			log.Info("%v: resp %T %+v has no conn, skipping", proc.what, resp.resp.resp, resp.resp.resp)
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, io.EOF)
			continue
		}
		if conn.generation != resp.generation {
			log.Info("%v: resp %T %+v has bad generation %v vs %v, skipping", proc.what, resp.resp.resp, resp.resp.resp, resp.generation, conn.generation)
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, io.EOF)
			continue
		}
		log.Debug("reading block response %T for req %+v from %v->%v", resp.resp.resp, resp.resp.req, conn.conn.LocalAddr(), conn.conn.RemoteAddr())
		// the responsibility for cleaning up the connection is always in the request processor
		if err := ReadBlocksResponse(log, conn.conn, resp.resp.resp); err != nil {
			resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, err)
			continue
		}
		if resp.resp.resp.BlocksResponseKind() == msgs.FETCH_BLOCK {
			req := resp.resp.req.(*msgs.FetchBlockReq)
			lr := &io.LimitedReader{
				R: conn.conn,
				N: int64(req.Count),
			}
			log.Debug("reading block body from %v->%v", conn.conn.LocalAddr(), conn.conn.RemoteAddr())
			if _, err := resp.resp.additionalBodyWriter.ReadFrom(lr); err != nil {
				resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, err)
				continue
			}
		}
		log.Debug("read block response %T %+v for req %v from %v->%v", resp.resp.resp, resp.resp.resp, resp.resp.req, conn.conn.LocalAddr(), conn.conn.RemoteAddr())
		resp.resp.done(log, &proc.addr1, &proc.addr2, resp.resp.extra, nil)
	}
}

type blocksProcessorKey struct {
	// if oneSockPerBlockService=false, this field is always zero.
	blockService msgs.BlockServiceId
	ip1          [4]byte
	port1        uint16
	ip2          [4]byte
	port2        uint16
}

type blocksProcessors struct {
	what                   string
	oneSockPerBlockService bool
	// blocksProcessorKey -> *blocksProcessor
	processors sync.Map
}

func (procs *blocksProcessors) init(what string) {
	procs.what = what
}

func (procs *blocksProcessors) send(
	log *Logger,
	blockService msgs.BlockServiceId,
	ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16,
	breq msgs.BlocksRequest,
	breqAdditionalBody io.Reader,
	bresp msgs.BlocksResponse,
	brespAdditionalBody io.ReaderFrom,
	extra any,
	completionChan chan *BlockCompletion,
) error {
	if port1 == 0 && port2 == 0 {
		panic(fmt.Errorf("got zero ports for both addresses for block service %v: %v:%v %v:%v", blockService, ip1, port1, ip2, port2))
	}
	resp := &clientBlockResponse{
		req:                  breq,
		resp:                 bresp,
		additionalBodyWriter: brespAdditionalBody,
		completionChan:       completionChan,
		extra:                extra,
	}
	req := &clientBlockRequest{
		blockService:         blockService,
		req:                  breq,
		additionalBodyReader: breqAdditionalBody,
		resp:                 resp,
	}
	key := blocksProcessorKey{
		ip1:   ip1,
		port1: port1,
		ip2:   ip2,
		port2: port2,
	}
	if procs.oneSockPerBlockService {
		key.blockService = blockService
	}
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
		addr1:           net.TCPAddr{IP: net.IP(ip1[:]), Port: int(port1)},
		addr2:           net.TCPAddr{IP: net.IP(ip2[:]), Port: int(port2)},
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

func (cm *clientMetadata) init(log *Logger) error {
	sock, err := net.ListenPacket("udp", ":0")
	if err != nil {
		return err
	}
	cm.sock = sock.(*net.UDPConn)
	if err := cm.sock.SetReadBuffer(10 << 20); err != nil {
		cm.sock.Close()
		return err
	}
	if err := cm.sock.SetWriteBuffer(10 << 20); err != nil {
		cm.sock.Close()
		return err
	}
	cm.requests = make(chan *metadataProcessorRequest, 10_000)
	cm.responses = make(chan metadataProcessorResponse, 10_000)
	// sock drainer
	go func() {
		for {
			if cm.sockClosed {
				return
			}
			mtu := clientMtu
			respBuf := make([]byte, mtu)
			read, _, err := cm.sock.ReadFrom(respBuf)
			if err != nil {
				if cm.sockClosed {
					log.Debug("got error while reading from metadata socket when wound down: %v", err)
				} else {
					log.RaiseAlert("got error while reading from metadata socket: %v", err)
				}
				continue
			}
			// extract request id
			if read < 12 {
				log.RaiseAlert("got runt metadata response: %v < 12", read)
				continue
			}
			requestId := binary.LittleEndian.Uint64(respBuf[4 : 4+8])
			cm.responses <- metadataProcessorResponse{
				requestId: requestId,
				body:      respBuf[:read],
			}
		}
	}()
	// processor
	go func() {
		inFlight := make(map[uint64](chan []byte))
		for {
			if cm.sockClosed {
				log.Debug("request processor terminating since socket is closed")
				return
			}
			select {
			case resp := <-cm.responses:
				ch, ok := inFlight[resp.requestId]
				if ok {
					// don't block the main loop because of a full queue
					select {
					case ch <- resp.body:
					default:
					}
				} else {
					log.Debug("ignoring request %v which we could not find, probably late", resp.requestId)
				}
			case req := <-cm.requests:
				if req.clear {
					delete(inFlight, req.requestId)
				} else {
					log.Debug("about to send request id %v using conn %v->%v", req.requestId, req.addr, cm.sock.LocalAddr())
					written, err := cm.sock.WriteTo(req.body, req.addr)
					if err != nil {
						log.RaiseAlert("could not send request %v to %v: %v", req.requestId, req.addr, err)
					} else if written != len(req.body) {
						// this should never happen, but don't panic in this infinite loop
						log.RaiseAlert("expected write of %v, got %v", len(req.body), written)
					}
					// overwrites are fine
					inFlight[req.requestId] = req.respCh
					go func() {
						time.Sleep(req.timeout)
						select {
						case req.respCh <- nil:
						default:
						}
					}()
				}
			}
		}
	}()
	return nil
}

func (cm *clientMetadata) close() {
	cm.sockClosed = true
	cm.sock.Close()
}

func NewClientDirectNoAddrs(
	log *Logger,
) (c *Client, err error) {
	c = &Client{
		requestIdCounter: rand.Uint64(),
		fetchBlockBufs: sync.Pool{
			New: func() any {
				return bytes.NewBuffer([]byte{})
			},
		},
	}
	c.shardTimeout = &DefaultShardTimeout
	c.cdcTimeout = &DefaultCDCTimeout
	if err := c.clientMetadata.init(log); err != nil {
		return nil, err
	}
	// Ideally, for write/fetch we'd want to have one socket
	// per block service. However we don't have flash yet, so
	// it's hard to saturate a socket because of seek time.
	c.writeBlockProcessors.init("write")
	c.writeBlockProcessors.oneSockPerBlockService = true
	c.fetchBlockProcessors.init("fetch")
	c.fetchBlockProcessors.oneSockPerBlockService = true
	c.eraseBlockProcessors.init("erase")
	// we're not constrained by bandwidth here, we want to have requests
	// for all block services in parallel
	c.eraseBlockProcessors.oneSockPerBlockService = true
	// here we're also not constrained by bandwidth, but the requests
	// take a long time, so have a separate channel from the erase ones
	c.checkBlockProcessors.init("erase")
	c.checkBlockProcessors.oneSockPerBlockService = true
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

func (client *Client) StartWriteBlock(log *Logger, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc, extra any, completion chan *BlockCompletion) error {
	resp := &msgs.WriteBlockResp{}
	return client.writeBlockProcessors.send(
		log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2,
		&msgs.WriteBlockReq{
			BlockId:     block.BlockId,
			Crc:         crc,
			Size:        size,
			Certificate: block.Certificate,
		},
		r,
		resp,
		nil,
		extra,
		completion,
	)
}

func (client *Client) WriteBlock(log *Logger, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc) (proof [8]byte, err error) {
	ch := make(chan *BlockCompletion, 1)
	err = client.StartWriteBlock(log, block, r, size, crc, nil, ch)
	if err != nil {
		var proof [8]byte
		return proof, err
	}
	resp := <-ch
	if resp.Error != nil {
		return proof, resp.Error
	}
	return resp.Resp.(*msgs.WriteBlockResp).Proof, nil
}

func (client *Client) StartFetchBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32, w io.ReaderFrom, extra any, completion chan *BlockCompletion) error {
	resp := &msgs.FetchBlockResp{}
	return client.fetchBlockProcessors.send(
		log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2,
		&msgs.FetchBlockReq{
			BlockId: blockId,
			Offset:  offset,
			Count:   count,
		},
		nil,
		resp,
		w,
		extra,
		completion,
	)
}

func (c *Client) PutFetchedBlock(body *bytes.Buffer) {
	c.fetchBlockBufs.Put(body)
}

func (client *Client) FetchBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32) (body *bytes.Buffer, err error) {
	ch := make(chan *BlockCompletion, 1)
	buf := client.fetchBlockBufs.Get().(*bytes.Buffer)
	buf.Reset()
	if err := client.StartFetchBlock(log, blockService, blockId, offset, count, buf, nil, ch); err != nil {
		client.PutFetchedBlock(buf)
		return nil, err
	}
	resp := <-ch
	if resp.Error != nil {
		client.PutFetchedBlock(buf)
		return nil, resp.Error
	}
	return buf, nil
}

func (client *Client) StartEraseBlock(log *Logger, block *msgs.RemoveSpanInitiateBlockInfo, extra any, completion chan *BlockCompletion) error {
	resp := &msgs.EraseBlockResp{}
	return client.eraseBlockProcessors.send(
		log, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2,
		&msgs.EraseBlockReq{
			BlockId:     block.BlockId,
			Certificate: block.Certificate,
		},
		nil,
		resp,
		nil,
		extra,
		completion,
	)
}

func (client *Client) EraseBlock(log *Logger, block *msgs.RemoveSpanInitiateBlockInfo) (proof [8]byte, err error) {
	ch := make(chan *BlockCompletion, 1)
	if err := client.StartEraseBlock(log, block, nil, ch); err != nil {
		return proof, err
	}
	resp := <-ch
	if resp.Error != nil {
		return proof, resp.Error
	}
	return resp.Resp.(*msgs.EraseBlockResp).Proof, nil
}

func (client *Client) StartCheckBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, size uint32, crc msgs.Crc, extra any, completion chan *BlockCompletion) error {
	resp := &msgs.CheckBlockResp{}
	return client.checkBlockProcessors.send(
		log, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2,
		&msgs.CheckBlockReq{
			BlockId: blockId,
			Size:    size,
			Crc:     crc,
		},
		nil,
		resp,
		nil,
		extra,
		completion,
	)
}

func (client *Client) CheckBlock(log *Logger, blockService *msgs.BlockService, blockId msgs.BlockId, size uint32, crc msgs.Crc) error {
	ch := make(chan *BlockCompletion, 1)
	if err := client.StartCheckBlock(log, blockService, blockId, size, crc, nil, ch); err != nil {
		return err
	}
	resp := <-ch
	return resp.Error
}
