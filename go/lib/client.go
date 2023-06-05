package lib

import (
	"bytes"
	"crypto/cipher"
	"fmt"
	"io"
	"net"
	"net/netip"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type ReqCounters struct {
	Timings  Timings
	Attempts uint64
}

type ClientCounters struct {
	// these maps are indexed by req type
	Shard [256]ReqCounters
	CDC   [10]ReqCounters
}

func (counters *ClientCounters) Log(log *Logger) {
	formatCounters := func(c *ReqCounters) {
		totalCount := uint64(0)
		for i := 0; i < c.Timings.Buckets(); i++ {
			_, count, _ := c.Timings.Bucket(i)
			totalCount += count
		}
		log.Info("    count: %v", totalCount)
		if totalCount == 0 {
			log.Info("    attempts: %v", c.Attempts)
		} else {
			log.Info("    attempts: %v (%v)", c.Attempts, float64(c.Attempts)/float64(totalCount))
		}
		log.Info("    total time: %v", time.Duration(c.Timings.TotalTime()))
		log.Info("    avg time: %v", time.Duration(uint64(c.Timings.TotalTime())/totalCount))
		hist := bytes.NewBuffer([]byte{})
		first := true
		countSoFar := uint64(0)
		for i := 0; i < c.Timings.Buckets(); i++ {
			lowerBound, count, upperBound := c.Timings.Bucket(i)
			if count == 0 {
				continue
			}
			countSoFar += count
			if first {
				fmt.Fprintf(hist, "%v < ", lowerBound)
			} else {
				fmt.Fprintf(hist, ", ")
			}
			first = false
			fmt.Fprintf(hist, "%v (%0.2f%%) < %v", count, float64(countSoFar*100)/float64(totalCount), upperBound)
		}
		log.Info("    hist: %v", hist.String())
	}
	var shardTime time.Duration
	for i := 0; i < len(counters.Shard[:]); i++ {
		shardTime += counters.Shard[i].Timings.TotalTime()
	}
	log.Info("Shard stats (total shard time %v):", shardTime)
	for i := 0; i < len(counters.Shard[:]); i++ {
		c := &counters.Shard[i]
		if c.Attempts == 0 {
			continue
		}
		kind := msgs.ShardMessageKind(i)
		log.Info("  %v", kind)
		formatCounters(c)
	}
	var cdcTime time.Duration
	for i := 0; i < len(counters.CDC[:]); i++ {
		cdcTime += counters.CDC[i].Timings.TotalTime()
	}
	log.Info("CDC stats (total CDC time %v):", cdcTime)
	for i := 0; i < len(counters.CDC[:]); i++ {
		c := &counters.CDC[i]
		if c.Attempts == 0 {
			continue
		}
		kind := msgs.CDCMessageKind(i)
		log.Info("  %v", kind)
		formatCounters(c)
	}

}

type timedBlocksConn struct {
	conn     any
	lastSeen time.Time
}

const maxBlocksConns int = 3
const blocksConnsTimeout time.Duration = 10 * time.Second

// ring buffer, oldest conn is at the `head`.
type cachedBlocksConns struct {
	buf  [maxBlocksConns]timedBlocksConn
	size int
	head int
}

func (conns *cachedBlocksConns) purge(now time.Time) {
	for conns.size > 0 {
		conn := &conns.buf[conns.head]
		if now.Sub(conn.lastSeen) > blocksConnsTimeout {
			conns.pop()
		} else {
			break
		}
	}
}

func (conns *cachedBlocksConns) pop() any {
	if conns.size > 0 {
		conn := &conns.buf[conns.head]
		v := conn.conn
		conn.conn = nil
		conn.lastSeen = time.Time{}
		conns.size--
		conns.head = (conns.head + 1) % maxBlocksConns
		return v
	} else {
		return nil
	}
}

func (conns *cachedBlocksConns) push(tcpConn any, now time.Time) {
	if conns.size < maxBlocksConns {
		// add new entry
		conn := &conns.buf[(conns.head+conns.size)%maxBlocksConns]
		conn.conn = tcpConn
		conn.lastSeen = now
		conns.size++
	} else {
		// replace oldest entry
		conn := &conns.buf[conns.head]
		conn.conn = tcpConn
		conn.lastSeen = now
		conns.head = (conns.head + 1) % maxBlocksConns
	}
}

type blocksConnFactory struct {
	mu     sync.Mutex
	cached map[msgs.BlockServiceId]*cachedBlocksConns
}

// must be locked
func (factory *blocksConnFactory) lookup(blockService msgs.BlockServiceId, now time.Time) *cachedBlocksConns {
	conns, found := factory.cached[blockService]
	if !found {
		conns = &cachedBlocksConns{}
		factory.cached[blockService] = conns
	}
	conns.purge(now)
	return conns
}

func (factory *blocksConnFactory) get(blockServiceId msgs.BlockServiceId, now time.Time, getConn func() (any, error)) (any, error) {
	factory.mu.Lock()
	defer factory.mu.Unlock()

	conns := factory.lookup(blockServiceId, now)
	conn := conns.pop()
	if conn != nil {
		return conn, nil
	}
	var err error
	conn, err = getConn()
	if err != nil {
		return nil, err
	}
	return conn, nil
}

func (factory *blocksConnFactory) put(blockServiceId msgs.BlockServiceId, now time.Time, conn any) {
	factory.mu.Lock()
	defer factory.mu.Unlock()

	conns := factory.lookup(blockServiceId, now)
	conns.push(conn, now)
}

type Client struct {
	shardAddrs  [256][2]net.UDPAddr
	cdcAddrs    [2]net.UDPAddr
	udpSocks    []net.PacketConn
	socksLock   sync.Mutex
	counters    *ClientCounters
	cdcKey      cipher.Block
	blocksConns blocksConnFactory
}

func NewClient(
	log *Logger,
	shuckleAddress string,
	// How many sockets to use for cdc/shard communications, if there's not going
	// to be contention just pick 1. If no sockets are available when `GetSocket`
	// is called, a new one will be created.
	udpSockets int,
	// Can be nil (won't perform perf counting)
	counters *ClientCounters,
	// Can be nil (won't be able to restricted requests)
	cdcKey cipher.Block,
) (*Client, error) {
	var shardIps [256][2][4]byte
	var shardPorts [256][2]uint16
	var cdcIps [2][4]byte
	var cdcPorts [2]uint16
	{
		log.Info("Getting shard/CDC info from shuckle at '%v'", shuckleAddress)
		resp, err := ShuckleRequest(log, shuckleAddress, &msgs.ShardsReq{})
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
		resp, err = ShuckleRequest(log, shuckleAddress, &msgs.CdcReq{})
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
	return NewClientDirect(log, udpSockets, counters, cdcKey, &cdcIps, &cdcPorts, &shardIps, &shardPorts)
}

func NewClientDirect(
	log *Logger,
	udpSockets int,
	counters *ClientCounters,
	cdcKey cipher.Block,
	cdcIps *[2][4]byte,
	cdcPorts *[2]uint16,
	shardIps *[256][2][4]byte,
	shardPorts *[256][2]uint16,
) (c *Client, err error) {
	c = &Client{
		blocksConns: blocksConnFactory{
			cached: make(map[msgs.BlockServiceId]*cachedBlocksConns),
		},
	}
	for i := 0; i < 2; i++ {
		for j := 0; j < 256; j++ {
			c.shardAddrs[j][i] = *net.UDPAddrFromAddrPort(netip.AddrPortFrom(netip.AddrFrom4(shardIps[j][i]), shardPorts[j][i]))
		}
		c.cdcAddrs[i] = *net.UDPAddrFromAddrPort(netip.AddrPortFrom(netip.AddrFrom4(cdcIps[i]), cdcPorts[i]))
	}
	c.udpSocks = make([]net.PacketConn, udpSockets)
	for i := 0; i < udpSockets; i++ {
		sock, err := net.ListenPacket("udp", ":0")
		if err != nil {
			for j := 0; j < i; j++ {
				c.udpSocks[j].Close()
			}
			return nil, err
		}
		c.udpSocks[i] = sock
	}
	c.counters = counters
	c.cdcKey = cdcKey
	return c, nil
}

func (c *Client) CDCAddrs() *[2]net.UDPAddr {
	return &c.cdcAddrs
}

func (c *Client) ShardAddrs(shid msgs.ShardId) *[2]net.UDPAddr {
	return &c.shardAddrs[int(shid)]
}

func (c *Client) GetUDPSocket() (net.PacketConn, error) {
	c.socksLock.Lock()
	for i := range c.udpSocks {
		if c.udpSocks[i] != nil {
			sock := c.udpSocks[i]
			c.udpSocks[i] = nil
			c.socksLock.Unlock()
			return sock, nil
		}
	}
	// no socket found, we need to create one
	sock, err := net.ListenPacket("udp", ":0")
	if err != nil {
		return nil, err
	}
	return sock, nil
}

func (c *Client) ReleaseUDPSocket(sock net.PacketConn) {
	c.socksLock.Lock()
	for i := range c.udpSocks {
		if c.udpSocks[i] == nil {
			c.udpSocks[i] = sock
			c.socksLock.Unlock()
			return
		}
	}
	// no slot found, close
	sock.Close()
}

func (c *Client) Close() {
	for i := range c.udpSocks {
		if c.udpSocks[i] == nil {
			panic(fmt.Errorf("not all UDP sockets were returned! found one at %v", i))
		}
		c.udpSocks[i].Close()
	}
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

type trackedBlocksConn struct {
	blockService msgs.BlockServiceId
	conn         *net.TCPConn
	factory      *blocksConnFactory
}

func (c *trackedBlocksConn) Read(p []byte) (int, error) {
	return c.conn.Read(p)
}

func (c *trackedBlocksConn) Write(p []byte) (int, error) {
	return c.conn.Write(p)
}

func (c *trackedBlocksConn) ReadFrom(r io.Reader) (int64, error) {
	return c.conn.ReadFrom(r)
}

func (c *trackedBlocksConn) Close() error {
	return c.conn.Close()
}

func (c *trackedBlocksConn) Put() {
	c.factory.put(c.blockService, time.Now(), c.conn)
	c.conn = nil
}

// The first ip1/port1 cannot be zeroed, the second one can. One of them
// will be tried at random.
func (c *Client) GetBlocksConn(log *Logger, blockServiceId msgs.BlockServiceId, ip1 [4]byte, port1 uint16, ip2 [4]byte, port2 uint16) (BlocksConn, error) {
	conn, err := c.blocksConns.get(blockServiceId, time.Now(), func() (any, error) {
		return BlockServiceConnection(log, ip1, port1, ip2, port2)
	})
	if err != nil {
		return nil, err
	}
	return &trackedBlocksConn{
		blockService: blockServiceId,
		conn:         conn.(*net.TCPConn),
		factory:      &c.blocksConns,
	}, nil
}
