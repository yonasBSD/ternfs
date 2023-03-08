package lib

import (
	"bytes"
	"crypto/cipher"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type shardSocketFactory interface {
	getShardSocket(shid msgs.ShardId, addr [4]byte, port uint16) (*net.UDPConn, error)
	releaseShardSocket(shid msgs.ShardId, sock *net.UDPConn)
}

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
	shardIps           [256][4]byte
	shardPorts         [256]uint16
	shardSocketFactory shardSocketFactory
	cdcIp              [4]byte
	cdcPort            uint16
	cdcSocket          *net.UDPConn
	cdcLock            sync.Mutex
	counters           *ClientCounters
	cdcKey             cipher.Block
	blocksConns        blocksConnFactory
}

// If `shid` is present, the client will only create a socket for that shard,
// otherwise sockets for all 256 shards will be created.
//
// The other two parameters are optional too.
//
// The client is thread safe, and more sockets might be temporarily created
// if multiple things are trying to talk to the same shard. So the assumption
// is that there won't be much contention otherwise you might as well create
// a socket each time. TODO not sure this is the best way forward
func NewClient(
	log *Logger,
	shuckleAddress string,
	shid *msgs.ShardId,
	counters *ClientCounters,
	cdcKey cipher.Block,
) (*Client, error) {
	var shardIps [256][4]byte
	var shardPorts [256]uint16
	var cdcIp [4]byte
	var cdcPort uint16
	{
		log.Info("Getting shard/CDC info from shuckle at '%v'", shuckleAddress)
		resp, err := ShuckleRequest(log, shuckleAddress, &msgs.ShardsReq{})
		if err != nil {
			return nil, fmt.Errorf("could not request shards from shuckle: %w", err)
		}
		shards := resp.(*msgs.ShardsResp)
		for i, shard := range shards.Shards {
			if shard.Port == 0 {
				return nil, fmt.Errorf("shard %v not present in shuckle", i)
			}
			shardIps[i] = shard.Ip
			shardPorts[i] = shard.Port
		}
		resp, err = ShuckleRequest(log, shuckleAddress, &msgs.CdcReq{})
		if err != nil {
			return nil, fmt.Errorf("could not request CDC from shuckle: %w", err)
		}
		cdc := resp.(*msgs.CdcResp)
		if cdc.Port == 0 {
			return nil, fmt.Errorf("CDC not present in shuckle")
		}
		cdcIp = cdc.Ip
		cdcPort = cdc.Port
	}
	return NewClientDirect(log, shid, counters, cdcKey, cdcIp, cdcPort, &shardIps, &shardPorts)
}

func NewClientDirect(
	log *Logger,
	shid *msgs.ShardId,
	counters *ClientCounters,
	cdcKey cipher.Block,
	cdcIp [4]byte,
	cdcPort uint16,
	shardIps *[256][4]byte,
	shardPorts *[256]uint16,
) (c *Client, err error) {
	c = &Client{
		shardIps:   *shardIps,
		shardPorts: *shardPorts,
		cdcIp:      cdcIp,
		cdcPort:    cdcPort,
		blocksConns: blocksConnFactory{
			cached: make(map[msgs.BlockServiceId]*cachedBlocksConns),
		},
	}
	c.cdcSocket, err = CreateCDCSocket(c.cdcIp, c.cdcPort)
	if err != nil {
		return nil, err
	}
	if shid != nil {
		c.shardSocketFactory = &shardSpecificFactory{shid: *shid}
	} else {
		c.shardSocketFactory = &allShardsFactory{}
	}
	c.counters = counters
	c.cdcKey = cdcKey
	return c, nil
}

func (c *Client) GetShardSocket(shid msgs.ShardId) (*net.UDPConn, error) {
	return c.shardSocketFactory.getShardSocket(shid, c.shardIps[int(shid)], c.shardPorts[int(shid)])
}

func (c *Client) ReleaseShardSocket(shid msgs.ShardId, sock *net.UDPConn) {
	c.shardSocketFactory.releaseShardSocket(shid, sock)
}

func (c *Client) GetCDCSocket() (*net.UDPConn, error) {
	if c.cdcLock.TryLock() {
		return c.cdcSocket, nil
	} else {
		conn, err := CreateCDCSocket(c.cdcIp, c.cdcPort)
		if err != nil {
			return nil, err
		}
		return conn, nil
	}
}

func (c *Client) ReleaseCDCSocket(sock *net.UDPConn) {
	if sock == c.cdcSocket {
		c.cdcLock.Unlock()
	} else {
		if err := sock.Close(); err != nil {
			panic(err)
		}
	}
}

func (c *Client) Close() {
	switch factory := c.shardSocketFactory.(type) {
	case *allShardsFactory:
		factory.close()
	case *shardSpecificFactory:
		factory.close()
	default:
		panic(fmt.Errorf("bad factory %T", c.shardSocketFactory))
	}
	if err := c.cdcSocket.Close(); err != nil {
		fmt.Fprintf(os.Stderr, "Could not close CDC conn: %v", err)
	}
}

// Holds sockets to all 256 shards
type allShardsFactory struct {
	shardSocks [256]*net.UDPConn
	shardLocks [256]sync.Mutex
}

func (c *allShardsFactory) close() {
	for _, sock := range c.shardSocks {
		if sock == nil {
			continue
		}
		if err := sock.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "could not close shard socket: %v", err)
		}
	}
}

func (c *allShardsFactory) getShardSocket(shid msgs.ShardId, ip [4]byte, port uint16) (*net.UDPConn, error) {
	ix := int(shid)
	if c.shardLocks[ix].TryLock() {
		if c.shardSocks[ix] == nil {
			var err error
			c.shardSocks[ix], err = CreateShardSocket(shid, ip, port)
			if err != nil {
				return nil, err
			}
		}
		return c.shardSocks[int(shid)], nil
	} else {
		conn, err := CreateShardSocket(shid, ip, port)
		if err != nil {
			return nil, err
		}
		return conn, nil
	}
}

func (c *allShardsFactory) releaseShardSocket(shid msgs.ShardId, sock *net.UDPConn) {
	if c.shardSocks[int(shid)] == sock {
		c.shardLocks[int(shid)].Unlock()
	} else {
		if err := sock.Close(); err != nil {
			panic(err)
		}
	}
}

// For when you almost always do requests to a single shard (e.g. in GC).
type shardSpecificFactory struct {
	shid      msgs.ShardId
	shardSock *net.UDPConn
	shardLock sync.Mutex
}

func (c *shardSpecificFactory) close() {
	if c.shardSock == nil {
		return
	}
	if err := c.shardSock.Close(); err != nil {
		fmt.Fprintf(os.Stderr, "could not close shard sock: %v\n", err)
	}
}

func (c *shardSpecificFactory) getShardSocket(shid msgs.ShardId, ip [4]byte, port uint16) (*net.UDPConn, error) {
	if shid == c.shid && c.shardLock.TryLock() {
		if c.shardSock == nil {
			var err error
			c.shardSock, err = CreateShardSocket(shid, ip, port)
			if err != nil {
				return nil, err
			}
		}
		return c.shardSock, nil
	} else {
		shardSock, err := CreateShardSocket(shid, ip, port)
		if err != nil {
			return nil, err
		}
		return shardSock, nil
	}
}

func (c *shardSpecificFactory) releaseShardSocket(shid msgs.ShardId, sock *net.UDPConn) {
	if sock == c.shardSock {
		c.shardLock.Unlock()
	} else {
		if err := sock.Close(); err != nil {
			panic(err)
		}
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

type BlocksConn interface {
	io.Writer
	io.Reader
	io.ReaderFrom
	io.Closer

	Taint()
}

type trackedBlocksConn struct {
	blockService msgs.BlockServiceId
	conn         *net.TCPConn
	tainted      bool
	factory      *blocksConnFactory
}

func (c *trackedBlocksConn) Read(p []byte) (int, error) {
	read, err := c.conn.Read(p)
	if err != nil {
		c.tainted = true
	}
	return read, err
}

func (c *trackedBlocksConn) Write(p []byte) (int, error) {
	written, err := c.conn.Write(p)
	if err != nil {
		c.tainted = true
	}
	return written, err
}

func (c *trackedBlocksConn) ReadFrom(r io.Reader) (int64, error) {
	n, err := c.conn.ReadFrom(r)
	if err != nil {
		c.tainted = true
	}
	return n, err
}

func (c *trackedBlocksConn) Close() error {
	if c.tainted {
		return c.conn.Close()
	} else {
		c.factory.put(c.blockService, time.Now(), c.conn)
		return nil
	}
}

func (c *trackedBlocksConn) Taint() {
	c.tainted = true
}

// The first ip1/port1 cannot be zeroed, the second one can. One of them
// will be tried at random.
//
// The connections might be cached, assuming no errors are present when
// doing read/write/etc. This means that, before closing, you need to
// consume the current stream fully for future users. If you explicitly
// do not want to do that, call `Taint()` to prevent the caching after
// `Close()`.
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
		tainted:      false,
		factory:      &c.blocksConns,
	}, nil
}
