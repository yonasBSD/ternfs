package eggs

import (
	"bytes"
	"crypto/cipher"
	"fmt"
	"net"
	"os"
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

type blockServiceConn struct {
	mu         sync.Mutex
	lastActive time.Time
	conn       *net.TCPConn
}

type Client struct {
	shardIps            [256][4]byte
	shardPorts          [256]uint16
	shardSocketFactory  shardSocketFactory
	cdcIp               [4]byte
	cdcPort             uint16
	cdcSocket           *net.UDPConn
	cdcLock             sync.Mutex
	counters            *ClientCounters
	cdcKey              cipher.Block
	blockServicesConns  map[string]*blockServiceConn
	blockServicesLock   sync.RWMutex
	blockServicesReaper chan<- bool
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
	c.blockServicesConns = make(map[string]*blockServiceConn)
	{
		ch := make(chan bool)
		c.blockServicesReaper = ch
		go func() {
			for {
				keepGoing := <-ch
				if !keepGoing {
					break
				}
				c.blockServicesLock.RLock()
				now := time.Now()
				for _, conn := range c.blockServicesConns {
					if !conn.mu.TryLock() { // somebody else is using this
						continue
					}
					if now.Sub(conn.lastActive) > time.Minute {
						if err := conn.conn.Close(); err != nil {
							log.RaiseAlert(fmt.Errorf("could not close connection %v", conn.conn.RemoteAddr()))
						}
					}
					conn.mu.Unlock()
				}
				c.blockServicesLock.RUnlock()
				go func() {
					time.Sleep(time.Minute)
					ch <- true
				}()
			}
		}()
	}
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
	c.blockServicesReaper <- false
	for _, conn := range c.blockServicesConns {
		if err := conn.conn.Close(); err != nil {
			fmt.Fprintf(os.Stderr, "Could not close block service conn: %v", err)
		}
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

// nil if the directory has no directory info (i.e. if it is inherited)
func GetDirectoryInfo(log *Logger, c *Client, id msgs.InodeId) (*msgs.DirectoryInfoBody, error) {
	req := msgs.StatDirectoryReq{
		Id: id,
	}
	resp := msgs.StatDirectoryResp{}
	if err := c.ShardRequest(log, id.Shard(), &req, &resp); err != nil {
		return nil, err
	}
	info := msgs.DirectoryInfoBody{}
	if err := bincode.Unpack(resp.Info, &info); err != nil {
		return nil, err
	}
	if len(resp.Info) == 0 {
		return nil, nil
	}
	return &info, nil
}

func SetDirectoryInfo(log *Logger, c *Client, id msgs.InodeId, inherited bool, info *msgs.DirectoryInfoBody) error {
	var buf []byte
	if inherited {
		if info != nil {
			panic(fmt.Errorf("unexpected inherited=true and non-empty info %+v", info))
		}
		buf = make([]byte, 0)
	} else {
		info.Version = 1
		buf = bincode.Pack(info)
	}
	req := msgs.SetDirectoryInfoReq{
		Id: id,
		Info: msgs.SetDirectoryInfo{
			Inherited: inherited,
			Body:      buf,
		},
	}
	if err := c.ShardRequest(log, id.Shard(), &req, &msgs.SetDirectoryInfoResp{}); err != nil {
		return err
	}
	return nil
}

func ResolveDirectoryInfo(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	dirId msgs.InodeId,
) (*msgs.DirectoryInfoBody, error) {
	statResp := msgs.StatDirectoryResp{}
	err := client.ShardRequest(log, dirId.Shard(), &msgs.StatDirectoryReq{Id: dirId}, &statResp)
	if err != nil {
		return nil, fmt.Errorf("could not send stat to external shard %v to resolve directory info: %w", dirId.Shard(), err)
	}

	return resolveDirectoryInfo(log, client, dirInfoCache, dirId, &statResp)
}

func resolveDirectoryInfo(
	log *Logger,
	client *Client,
	dirInfoCache *DirInfoCache,
	dirId msgs.InodeId,
	statResp *msgs.StatDirectoryResp,
) (*msgs.DirectoryInfoBody, error) {
	// we have the data directly in the stat response
	if len(statResp.Info) > 0 {
		infoBody := msgs.DirectoryInfoBody{}
		if err := bincode.Unpack(statResp.Info, &infoBody); err != nil {
			return nil, err
		}
		dirInfoCache.UpdateCachedDirInfo(dirId, &infoBody)
		return &infoBody, nil
	}

	// we have the data in the cache
	dirInfoBody := dirInfoCache.LookupCachedDirInfo(dirId)
	if dirInfoBody != nil {
		return dirInfoBody, nil
	}

	// we need to traverse upwards
	dirInfoBody, err := ResolveDirectoryInfo(log, client, dirInfoCache, statResp.Owner)
	if err != nil {
		return nil, err
	}

	return dirInfoBody, nil
}

func blockServicesConnsKey(ip [4]byte, port uint16) string {
	return fmt.Sprintf("%v:%v", net.IP(ip[:]), port)
}

func blockServicesConnsKeyFromConn(conn *net.TCPConn) string {
	return conn.RemoteAddr().String()
}
