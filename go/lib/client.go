package lib

import (
	"bytes"
	"crypto/cipher"
	"encoding/binary"
	"fmt"
	"io"
	"math/rand"
	"net"
	"net/netip"
	"path/filepath"
	"strings"
	"sync"
	"time"
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

type blockConnKey uint64

func newBlockConnKey(ip [4]byte, port uint16) blockConnKey {
	ip4 := uint64(ip[0])<<24 | uint64(ip[1])<<16 | uint64(ip[2])<<8 | uint64(ip[3])<<0
	return blockConnKey(ip4<<32 | uint64(port))
}

type blockConn struct {
	conn          *net.TCPConn // might be null
	mu            sync.Mutex
	possiblyStale bool
}

type blocksConnFactory struct {
	mu sync.RWMutex // to access the map
	// keys are never deleted apart from when we close the factory, when closing the connection
	// we just set the `.conn` to nil
	cached map[blockConnKey]*blockConn
}

func (cf *blocksConnFactory) close() {
	if !cf.mu.TryLock() {
		panic(fmt.Errorf("could not lock blockConnsFactory, did you close all connections before closing the client?"))
	}
	defer cf.mu.Unlock()
	for key, conn := range cf.cached {
		if !conn.mu.TryLock() {
			panic(fmt.Errorf("could not lock block conn %+v %p, did you close all connections before closing the client?", conn, &conn))
		}
		delete(cf.cached, key)
		if conn.conn != nil {
			conn.conn.Close()
		}
		conn.mu.Unlock()
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
	Initial: 250 * time.Millisecond,
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

type Client struct {
	shardAddrs       [256][2]net.UDPAddr
	cdcAddrs         [2]net.UDPAddr
	clientMetadata   clientMetadata
	counters         *ClientCounters
	cdcKey           cipher.Block
	writeBlocksConns blocksConnFactory
	readBlocksConns  blocksConnFactory
	shardTimeout     *ReqTimeouts
	cdcTimeout       *ReqTimeouts
	requestIdCounter uint64
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
				log.RaiseAlert("got error while reading from metadata socket: %v", err)
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
		for {
			if cm.sockClosed {
				return
			}
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

func NewClientDirect(
	log *Logger,
	cdcIps *[2][4]byte,
	cdcPorts *[2]uint16,
	shardIps *[256][2][4]byte,
	shardPorts *[256][2]uint16,
) (c *Client, err error) {
	c = &Client{
		writeBlocksConns: blocksConnFactory{
			cached: make(map[blockConnKey]*blockConn),
		},
		readBlocksConns: blocksConnFactory{
			cached: make(map[blockConnKey]*blockConn),
		},
		requestIdCounter: rand.Uint64(),
	}
	for i := 0; i < 2; i++ {
		for j := 0; j < 256; j++ {
			c.shardAddrs[j][i] = *net.UDPAddrFromAddrPort(netip.AddrPortFrom(netip.AddrFrom4(shardIps[j][i]), shardPorts[j][i]))
		}
		c.cdcAddrs[i] = *net.UDPAddrFromAddrPort(netip.AddrPortFrom(netip.AddrFrom4(cdcIps[i]), cdcPorts[i]))
	}
	c.shardTimeout = &DefaultShardTimeout
	c.cdcTimeout = &DefaultCDCTimeout
	if err := c.clientMetadata.init(log); err != nil {
		return nil, err
	}
	return c, nil
}

func (c *Client) CDCAddrs() *[2]net.UDPAddr {
	return &c.cdcAddrs
}

func (c *Client) ShardAddrs(shid msgs.ShardId) *[2]net.UDPAddr {
	return &c.shardAddrs[int(shid)]
}

func (c *Client) Close() {
	c.clientMetadata.close()
	c.readBlocksConns.close()
	c.writeBlocksConns.close()
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

func (client *Client) ResolvePath(log *Logger, path string) (msgs.InodeId, error) {
	if !filepath.IsAbs(path) {
		return msgs.NULL_INODE_ID, fmt.Errorf("expected absolute path, got '%v'", path)
	}
	id := msgs.ROOT_DIR_INODE_ID
	for _, segment := range strings.Split(filepath.Clean(path), "/")[1:] {
		if segment == "" {
			continue
		}
		resp := msgs.LookupResp{}
		if err := client.ShardRequest(log, id.Shard(), &msgs.LookupReq{DirId: id, Name: segment}, &resp); err != nil {
			return msgs.NULL_INODE_ID, err
		}
		id = resp.TargetId
	}
	return id, nil
}

// Some connection we can reuse
type Puttable interface {
	// Note: this can only be called if the current request (usually for blocks)
	// has been _fully consumed without errors_. If it hasn't, then you should
	// just close the connection.
	Put()
}

type PuttableReadCloser interface {
	io.ReadCloser
	Puttable
}

type PuttableCloser interface {
	io.Closer
	Puttable
}

type BlocksConn interface {
	io.Writer
	io.Reader
	io.ReaderFrom
	io.Closer
	Puttable
}

func dialBlockService(addr *net.TCPAddr) (*net.TCPConn, error) {
	return net.DialTCP("tcp4", nil, addr)
}

func (c *blockConn) refresh() bool {
	if !c.possiblyStale {
		return false
	}
	// try to reconnect
	sock, err := dialBlockService(c.conn.RemoteAddr().(*net.TCPAddr))
	if err == nil {
		c.possiblyStale = false
		c.conn = sock
		return true
	}
	return false
}

// probably should be made configurable
const blockConnDeadline time.Duration = 1 * time.Minute

func (c *blockConn) Read(p []byte) (int, error) {
	c.conn.SetReadDeadline(time.Now().Add(blockConnDeadline))
	r, err := c.conn.Read(p)
	if r == 0 && err != nil && err != io.EOF && c.refresh() {
		return c.Read(p)
	}
	return r, err
}

func (c *blockConn) ReadFrom(r io.Reader) (int64, error) {
	c.conn.SetWriteDeadline(time.Now().Add(blockConnDeadline))
	w, err := c.conn.ReadFrom(r)
	if w == 0 && err != nil && err != io.EOF && c.refresh() {
		return c.ReadFrom(r)
	}
	return c.conn.ReadFrom(r)
}

func (c *blockConn) Write(p []byte) (int, error) {
	c.conn.SetWriteDeadline(time.Now().Add(blockConnDeadline))
	w, err := c.conn.Write(p)
	if w == 0 && err != nil && c.refresh() {
		return c.Write(p)
	}
	return w, err
}

func (c *blockConn) Close() error {
	err := c.conn.Close()
	c.conn = nil
	c.mu.Unlock()
	return err
}

func (c *blockConn) Put() {
	c.possiblyStale = true
	c.mu.Unlock()
}

// if block=False, will return nil, nil when we found but couldn't acquire the conn.
func (f *blocksConnFactory) getBlocksConnInner(log *Logger, block bool, ip [4]byte, port uint16) (*blockConn, error) {
	key := newBlockConnKey(ip, port)

	// get the connection slot
	f.mu.RLock()
	conn, found := f.cached[key]
	f.mu.RUnlock()

	if found {
		// lock the connection slot
		if block {
			conn.mu.Lock()
		} else {
			locked := conn.mu.TryLock()
			if !locked {
				return nil, nil
			}
		}
		// if we couldn't return a connection, release the connection slot
		var err error
		defer func() {
			if err != nil {
				conn.mu.Unlock()
			}
		}()
		if conn.conn != nil { // a connection is already there, just return it
			return conn, nil
		} else { // a connection is _not_ there, try to connect to it
			var sock *net.TCPConn
			sock, err = dialBlockService(&net.TCPAddr{IP: net.IP(ip[:]), Port: int(port)})
			if err != nil {
				return nil, err
			} else {
				conn.possiblyStale = false
				conn.conn = sock
				return conn, nil
			}
		}
	} else {
		// if the connection slot does not exist yet, create it
		// and restart.
		f.mu.Lock()
		_, found = f.cached[key]
		if !found {
			conn := &blockConn{}
			f.cached[key] = conn
		}
		f.mu.Unlock()
		return f.getBlocksConnInner(log, block, ip, port)
	}
}

// The first ip1/port1 cannot be zeroed, the second one can. One of them
// will be tried at random.
func (f *blocksConnFactory) getBlocksConns(log *Logger, blockServiceId msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (*blockConn, error) {
	// decide at random which one we want to stimulate the two ports path
	if port1 == 0 {
		panic(fmt.Errorf("ip1/port1 must be provided"))
	}
	var ips [2][4]byte
	ips[0] = ip1
	ips[1] = ip2
	var ports [2]uint16
	ports[0] = port1
	ports[1] = port2
	var errs [2]error
	var sock *blockConn
	start := int(rand.Uint32())
	// first round, non blocking
	for i := start; i < start+2; i++ {
		ip := ips[i&1]
		port := ports[i&1]
		if port == 0 {
			continue
		}
		sock, errs[i&1] = f.getBlocksConnInner(log, false, ip, port)
		if sock != nil && errs[i&1] == nil {
			return sock, nil
		}
		if errs[i&1] != nil {
			log.RaiseAlert("could not connect to block service %v:%v: %v, might try other ip/port", ip, port, errs[i&1])
		}
	}
	// if we got two errors, return one of them
	if errs[0] != nil && (port2 == 0 && errs[1] != nil) {
		// return one of the two errors, we don't want to mess with them too much and they are alerts
		return nil, errs[0]
	}
	// second round, blocking
	for i := start; i < start+2; i++ {
		ip := ips[i&1]
		port := ports[i&1]
		if port == 0 {
			continue
		}
		sock, errs[i&1] = f.getBlocksConnInner(log, true, ip, port)
		if errs[i&1] == nil {
			return sock, nil
		}
		log.RaiseAlert("could not connect to block service %v:%v: %v, might try other ip/port", ip, port, errs[i&1])
	}
	for _, err := range errs {
		if err != nil {
			return nil, err
		}
	}
	panic("impossible")
}

// just so that Close/Put is idempotent.
type wrappedBlockConn struct {
	conn *blockConn
}

func (c *wrappedBlockConn) Read(p []byte) (int, error) {
	return c.conn.Read(p)
}

func (c *wrappedBlockConn) ReadFrom(p io.Reader) (int64, error) {
	return c.conn.ReadFrom(p)
}

func (c *wrappedBlockConn) Write(p []byte) (int, error) {
	return c.conn.Write(p)
}

func (c *wrappedBlockConn) Close() error {
	if c.conn == nil {
		return nil
	}
	err := c.conn.Close()
	c.conn = nil
	return err
}

func (c *wrappedBlockConn) Put() {
	if c.conn == nil {
		return
	}
	c.conn.Put()
	c.conn = nil
}

// These two will block until the connection gets freed -- you can't nest calls to these.
func (c *Client) GetWriteBlocksConn(log *Logger, blockServiceId msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (BlocksConn, error) {
	conn, err := c.writeBlocksConns.getBlocksConns(log, blockServiceId, ip1, port1, ip2, port2)
	if err == nil {
		return &wrappedBlockConn{conn: conn}, nil
	}
	return nil, err
}

func (c *Client) GetReadBlocksConn(log *Logger, blockServiceId msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (BlocksConn, error) {
	conn, err := c.readBlocksConns.getBlocksConns(log, blockServiceId, ip1, port1, ip2, port2)
	if err == nil {
		return &wrappedBlockConn{conn: conn}, nil
	}
	return nil, err

}
