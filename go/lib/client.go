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
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/crc32c"
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

type blockConnKey uint64

func newBlockConnKey(ip [4]byte, port uint16) blockConnKey {
	ip4 := uint64(ip[0])<<24 | uint64(ip[1])<<16 | uint64(ip[2])<<8 | uint64(ip[3])<<0
	return blockConnKey(ip4<<32 | uint64(port))
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
	Initial: 100 * time.Millisecond,
	Max:     5 * time.Second,
	Overall: 10 * time.Second,
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
	sock       net.PacketConn
	sockClosed bool // to terminate the receiver
	requests   chan *metadataProcessorRequest
	responses  chan metadataProcessorResponse
}

type clientBlockResponse interface {
	// err == io.EOF means that we're done.
	consume(log *Logger, b []byte) (read int, err error)
	done(err error)
}

type clientBlockRequest struct {
	// We separate header and body so that if the body is a file or whatever
	// we can use sendfile/splice directly.
	header []byte
	body   io.Reader
	resp   clientBlockResponse
}

type blocksProcessor struct {
	tornDown        int32 // we've stopped processing request
	shouldStop      int32 // we want to stop processing requests
	reqChan         chan *clientBlockRequest
	inFlightReqChan chan clientBlockResponse
	conn            *net.TCPConn
	key             blockConnKey
	procs           *blocksProcessors
}

func (proc *blocksProcessor) tearDown(log *Logger, err error, resp clientBlockResponse) {
	// error out the current response, if any, which we own
	if resp != nil {
		resp.done(err)
	}

	// Now we check if we're the first ones here. If we aren't, nothing to do.
	if !atomic.CompareAndSwapInt32(&proc.tornDown, 0, 1) {
		log.Info("we've already torn down blocks processor, not doing it again")
		return
	}

	// Then we remove ourselves from the map. After this we know that no requests are
	// going to come through anymore.
	proc.procs.mu.Lock()
	delete(proc.procs.processors, proc.key)
	proc.procs.mu.Unlock()

	// Close the sock
	proc.conn.Close()

	// Now we just drain all the queues and error things out
	for {
		select {
		case req := <-proc.reqChan:
			req.resp.done(err)
		case resp := <-proc.inFlightReqChan:
			resp.done(err)
		default:
			goto Finish
		}
	}
Finish:
}

func (proc *blocksProcessor) processRequests(log *Logger) {
	log.Debug("%v: starting request processor for %v", proc.conn.RemoteAddr(), proc.procs.what)
	for {
		if atomic.LoadInt32(&proc.tornDown) == 1 {
			log.Debug("%v: block processor torn down, terminating", proc.procs.what)
			return
		}
		if atomic.LoadInt32(&proc.shouldStop) == 1 {
			log.Debug("%v: block processor should stop, tearing down", proc.procs.what)
			proc.tearDown(log, io.EOF, nil)
			return
		}
		req := <-proc.reqChan
		if _, err := proc.conn.Write(req.header); err != nil {
			proc.tearDown(log, err, req.resp)
			return
		}
		if req.body != nil {
			if _, err := proc.conn.ReadFrom(req.body); err != nil {
				proc.tearDown(log, err, req.resp)
				return
			}
		}
		proc.inFlightReqChan <- req.resp
	}
}

func (proc *blocksProcessor) processResponses(log *Logger) {
	log.Debug("%v: starting response processor for %v", proc.procs.what, proc.conn.RemoteAddr())
	buf := make([]byte, 10<<20)  // 10MB buffer for receiving
	var resp clientBlockResponse // what we're currently parsing
	for {
		if atomic.LoadInt32(&proc.tornDown) == 1 {
			log.Debug("%v: block processor torn down, terminating", proc.procs.what)
			return
		}
		if atomic.LoadInt32(&proc.shouldStop) == 1 {
			log.Debug("%v: block processor should stop, tearing down", proc.procs.what)
			proc.tearDown(log, io.EOF, resp)
			return
		}
		read, err := proc.conn.Read(buf)
		if err != nil {
			proc.tearDown(log, err, resp)
			return
		}
		consumed := 0
		for consumed < read {
			// load the response if we don't have any
			if resp == nil {
				resp = <-proc.inFlightReqChan
			}
			log.Debug("%v: consuming from %v to %v", proc.procs.what, consumed, read)
			consumedNow, err := resp.consume(log, buf[consumed:read])
			log.Debug("%v: consumed %v: %v", proc.procs.what, consumedNow, err)
			if err == io.EOF {
				resp.done(nil)
				resp = nil
			} else if err != nil {
				proc.tearDown(log, err, resp)
				return
			}
			consumed += consumedNow
		}
	}
}

type blocksProcessors struct {
	what                   string
	mu                     sync.RWMutex
	oneSockPerBlockService bool
	processors             map[blockConnKey]*blocksProcessor
}

func (procs *blocksProcessors) init(what string) {
	procs.what = what
	procs.processors = make(map[blockConnKey]*blocksProcessor)
}

var whichBlockIp uint

func (procs *blocksProcessors) send(log *Logger, alert bool, blockService msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16, req *clientBlockRequest) error {
	// try both ips at random
	var err error
	whichBlockIp++
	for i := whichBlockIp; i < whichBlockIp+2; i++ {
		var ip [4]byte
		var port uint16
		if i&1 == 0 {
			ip = ip1
			port = port1
		} else {
			ip = ip2
			port = port2
		}
		if port == 0 {
			continue
		}
		addr := &net.TCPAddr{IP: net.IP(ip[:]), Port: int(port)}
		log.Debug("block processor send for %v", addr)
		var key blockConnKey
		if procs.oneSockPerBlockService {
			key = blockConnKey(blockService)
		} else {
			key = newBlockConnKey(ip, port)
		}
		procs.mu.RLock()
		proc, found := procs.processors[key]
		if found { // we already have a conn
			log.Debug("block processor for %v already present", addr)
			proc.reqChan <- req
			procs.mu.RUnlock()
			return nil
		}
		procs.mu.RUnlock()
		// we try to create a new conn
		log.Debug("block processor for %v not present, creating", addr)
		var sock *net.TCPConn
		sock, err = net.DialTCP("tcp4", nil, addr)
		if err == nil {
			procs.mu.Lock()
			proc, found = procs.processors[key]
			if found { // somebody got there before us
				log.Debug("somebody got there before us for %v", addr)
				sock.Close()
				proc.reqChan <- req
				procs.mu.Unlock()
				return nil
			}
			proc := &blocksProcessor{
				reqChan:         make(chan *clientBlockRequest, 100),
				inFlightReqChan: make(chan clientBlockResponse, 100),
				conn:            sock,
				key:             key,
				procs:           procs,
			}
			procs.processors[key] = proc
			proc.reqChan <- req
			go proc.processRequests(log)
			go proc.processResponses(log)
			procs.mu.Unlock()
			return nil
		} else {
			if alert {
				log.RaiseAlert("could not connect to block service %v: %v, might try other ip/port", addr, err)
			} else {
				log.Debug("could not connect to block service %v: %v, might try other ip/port", addr, err)
			}
		}
	}
	if err == nil {
		panic(fmt.Errorf("exiting loop without error"))
	}
	return err
}

func (procs *blocksProcessors) close() {
	procs.mu.Lock()
	for _, proc := range procs.processors {
		atomic.StoreInt32(&proc.shouldStop, 1)
	}
	procs.mu.Unlock()
}

type Client struct {
	shardRawAddrs        [256][2]uint64
	cdcRawAddr           [2]uint64
	clientMetadata       clientMetadata
	counters             *ClientCounters
	cdcKey               cipher.Block
	writeBlockProcessors blocksProcessors
	fetchBlockProcessors blocksProcessors
	fetchBlockBufs       *BufPool
	eraseBlockProcessors blocksProcessors
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
	var err error
	cm.sock, err = net.ListenPacket("udp", ":0")
	if err != nil {
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
			log.Debug("waiting for metadata response")
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
			log.Debug("got metadata response %v", requestId)
			cm.responses <- metadataProcessorResponse{
				requestId: requestId,
				body:      respBuf[:read],
			}
		}
	}()
	// processor
	go func() {
		inFlight := make(map[uint64](chan []byte))
		defer func() {
			log.Debug("request processor terminating")
		}()
		for {
			if cm.sockClosed {
				log.Debug("request processor terminating since socket is closed")
				return
			}
			log.Debug("request processor waiting for request and responses")
			select {
			case resp := <-cm.responses:
				ch, ok := inFlight[resp.requestId]
				if ok {
					log.Debug("found request id %v, sending back", resp.requestId)
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
					log.Debug("clearing request id %v", req.requestId)
					delete(inFlight, req.requestId)
				} else {
					log.Debug("about to send request id %v using conn %v->%v", req.requestId, req.addr, cm.sock.LocalAddr())
					written, err := cm.sock.WriteTo(req.body, req.addr)
					log.Debug("sent request id %v, sending back", req.requestId)
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
						log.Debug("sending back timeout for req id %v", req.requestId)
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
		fetchBlockBufs:   NewBufPool(),
	}
	c.shardTimeout = &DefaultShardTimeout
	c.cdcTimeout = &DefaultCDCTimeout
	if err := c.clientMetadata.init(log); err != nil {
		return nil, err
	}
	c.writeBlockProcessors.init("write")
	c.fetchBlockProcessors.init("fetch")
	c.eraseBlockProcessors.init("erase")
	// we're not constrained by bandwidth here, we want to have requests
	// for all block services in parallel
	c.eraseBlockProcessors.oneSockPerBlockService = true
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

type WriteBlockFuture struct {
	mu  sync.Mutex // locked until done
	err error
	// protocol + kind + error or proof
	resp [4 + 1 + 8]byte
	read int
}

func (r *WriteBlockFuture) consume(log *Logger, b []byte) (int, error) {
	read := copy(r.resp[r.read:], b)
	r.read += read
	if r.read == len(r.resp) || (r.read >= 4+1+2 && r.resp[0] == msgs.ERROR) {
		resp := msgs.WriteBlockResp{}
		r.err = ReadBlocksResponse(log, bytes.NewReader(r.resp[:]), &resp)
		return read, io.EOF
	}
	return read, nil
}

func (r *WriteBlockFuture) done(err error) {
	if err != nil && r.err == nil {
		r.err = err
	}
	r.mu.Unlock()
}

func (r *WriteBlockFuture) Wait() (proof [8]byte, err error) {
	r.mu.Lock()
	r.mu.Unlock()
	if r.err != nil {
		return proof, r.err
	}
	copy(proof[:], r.resp[4+1:])
	return proof, nil
}

func (client *Client) StartWriteBlock(log *Logger, alert bool, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc) (*WriteBlockFuture, error) {
	header := bytes.NewBuffer([]byte{})
	err := WriteBlocksRequest(log, header, block.BlockServiceId, &msgs.WriteBlockReq{
		BlockId:     block.BlockId,
		Crc:         crc,
		Size:        size,
		Certificate: block.Certificate,
	})
	if err != nil {
		return nil, err
	}
	resp := &WriteBlockFuture{}
	resp.mu.Lock()
	req := &clientBlockRequest{
		header: header.Bytes(),
		body:   r,
		resp:   resp,
	}
	if err := client.writeBlockProcessors.send(log, alert, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2, req); err != nil {
		return nil, err
	}
	return resp, nil
}

func (client *Client) WriteBlock(log *Logger, alert bool, block *msgs.AddSpanInitiateBlockInfo, r io.Reader, size uint32, crc msgs.Crc) (proof [8]byte, err error) {
	resp, err := client.StartWriteBlock(log, alert, block, r, size, crc)
	if err != nil {
		var proof [8]byte
		return proof, err
	}
	return resp.Wait()
}

type FetchBlockFuture struct {
	mu  sync.Mutex // locked until done
	err error
	// protocol + kind
	header     [4 + 1]byte
	headerRead int
	errorBytes [2]byte
	errorRead  int
	body       *[]byte
	bodyRead   int
	crc        uint32
}

func (r *FetchBlockFuture) consume(log *Logger, b []byte) (int, error) {
	// header read, no error, we're reading the body
	if r.headerRead == len(r.header) && r.header[4] != msgs.ERROR {
		log.Debug("header read, reading body from %v", r.bodyRead)
		read := copy((*r.body)[r.bodyRead:], b)
		r.crc = crc32c.Sum(r.crc, (*r.body)[r.bodyRead:r.bodyRead+read])
		r.bodyRead += read
		if r.bodyRead == len(*r.body) {
			return read, io.EOF
		}
		return read, nil
	}

	log.Debug("reading header from %v", r.headerRead)
	read := copy(r.header[r.headerRead:], b)
	r.headerRead += read
	if r.headerRead == len(r.header) {
		log.Debug("header read %v", r.header)
		// we still parse the response normally since otherwise
		// we will not check the protocol
		resp := msgs.FetchBlockResp{}
		if r.header[4] != msgs.ERROR {
			// no error, return immediately, we'll be called in again
			if err := ReadBlocksResponse(log, bytes.NewReader(r.header[:]), &resp); err != nil {
				return read, err
			}
			return read, nil
		} else {
			// we have an error, read it fully
			log.Debug("reading error from %v", r.errorRead)
			errorRead := copy(r.errorBytes[r.errorRead:], b[read:])
			r.errorRead += errorRead
			if r.errorRead == len(r.errorBytes) {
				log.Debug("error read")
				if err := ReadBlocksResponse(log, io.MultiReader(bytes.NewReader(r.header[:]), bytes.NewReader(r.errorBytes[:])), &resp); err != nil {
					return read + errorRead, err
				}
				panic(fmt.Errorf("impossible -- we know we have an error: %+v, %+v", r.header, resp))
			}
			return read + errorRead, nil
		}
	}
	// header not fully read yet
	return read, nil
}

func (r *FetchBlockFuture) done(err error) {
	if err != nil && r.err == nil {
		r.err = err
	}
	r.mu.Unlock()
}

func (r *FetchBlockFuture) Wait() (body *[]byte, err error) {
	r.mu.Lock()
	r.mu.Unlock()
	if r.err != nil {
		return nil, r.err
	}
	// consume the body, so that the first caller can then put it back
	body = r.body
	if body == nil {
		return nil, fmt.Errorf("block body already consumed")
	}
	r.body = nil
	return body, r.err
}

func (c *Client) PutFetchedBlock(body *[]byte) {
	c.fetchBlockBufs.Put(body)
}

func (client *Client) StartFetchBlock(log *Logger, alert bool, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32) (*FetchBlockFuture, error) {
	header := bytes.NewBuffer([]byte{})
	err := WriteBlocksRequest(log, header, blockService.Id, &msgs.FetchBlockReq{
		BlockId: blockId,
		Offset:  offset,
		Count:   count,
	})
	if err != nil {
		return nil, err
	}
	resp := &FetchBlockFuture{
		body: client.fetchBlockBufs.Get(int(count)),
	}
	resp.mu.Lock()
	req := &clientBlockRequest{
		header: header.Bytes(),
		body:   nil,
		resp:   resp,
	}
	if err := client.fetchBlockProcessors.send(log, alert, blockService.Id, blockService.Ip1, blockService.Port1, blockService.Ip2, blockService.Port2, req); err != nil {
		return nil, err
	}
	return resp, nil
}

func (client *Client) FetchBlock(log *Logger, alert bool, blockService *msgs.BlockService, blockId msgs.BlockId, offset uint32, count uint32) (body *[]byte, err error) {
	resp, err := client.StartFetchBlock(log, alert, blockService, blockId, offset, count)
	if err != nil {
		return nil, err
	}
	return resp.Wait()
}

type EraseBlockFuture struct {
	// locked until mu.
	mu  sync.Mutex
	err error
	// protocol + kind + error or proof
	resp [4 + 1 + 8]byte
	read int
}

func (r *EraseBlockFuture) consume(log *Logger, b []byte) (int, error) {
	read := copy(r.resp[r.read:], b)
	r.read += read
	if (r.read >= 4+1+2 && r.resp[4] == msgs.ERROR) || (r.read == len(r.resp)) {
		resp := msgs.EraseBlockResp{}
		r.err = ReadBlocksResponse(log, bytes.NewReader(r.resp[:]), &resp)
		return read, io.EOF
	}
	return read, nil
}

func (r *EraseBlockFuture) done(err error) {
	if err != nil && r.err == nil {
		r.err = err
	}
	r.mu.Unlock()
}

func (r *EraseBlockFuture) Wait() (proof [8]byte, err error) {
	r.mu.Lock()
	r.mu.Unlock()
	if r.err != nil {
		return proof, r.err
	}
	copy(proof[:], r.resp[4+1:])
	return proof, nil
}

func (client *Client) StartEraseBlock(log *Logger, alert bool, block *msgs.RemoveSpanInitiateBlockInfo) (*EraseBlockFuture, error) {
	header := bytes.NewBuffer([]byte{})
	err := WriteBlocksRequest(log, header, block.BlockServiceId, &msgs.EraseBlockReq{
		BlockId:     block.BlockId,
		Certificate: block.Certificate,
	})
	if err != nil {
		return nil, err
	}
	resp := &EraseBlockFuture{}
	resp.mu.Lock()
	req := &clientBlockRequest{
		header: header.Bytes(),
		resp:   resp,
	}
	if err := client.eraseBlockProcessors.send(log, alert, block.BlockServiceId, block.BlockServiceIp1, block.BlockServicePort1, block.BlockServiceIp2, block.BlockServicePort2, req); err != nil {
		return nil, err
	}
	return resp, nil
}

func (client *Client) EraseBlock(log *Logger, alert bool, block *msgs.RemoveSpanInitiateBlockInfo) (proof [8]byte, err error) {
	resp, err := client.StartEraseBlock(log, alert, block)
	if err != nil {
		var proof [8]byte
		return proof, err
	}
	return resp.Wait()
}
