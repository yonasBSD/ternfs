package eggs

import (
	"crypto/cipher"
	"fmt"
	"net"
	"sync"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type shardSocketFactory interface {
	getShardSocket(shid msgs.ShardId, addr [4]byte, port uint16) (*net.UDPConn, error)
	releaseShardSocket(shid msgs.ShardId, sock *net.UDPConn)
}

type ReqCounters struct {
	Count    [256]int64
	Attempts [256]int64
	Nanos    [256]int64
}

func (c *ReqCounters) TotalRequests() int64 {
	total := int64(0)
	for i := 0; i < 256; i++ {
		total += c.Count[i]
	}
	return total
}

type ClientCounters struct {
	// these arrays are indexed by req type
	Shard ReqCounters
	CDC   ReqCounters
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
	log LogLevels,
	shuckleAddress string,
	shid *msgs.ShardId,
	counters *ClientCounters,
	cdcKey cipher.Block,
) (*Client, error) {
	c := Client{}
	{
		resp, err := ShuckleRequest(log, shuckleAddress, &msgs.ShardsReq{})
		if err != nil {
			return nil, err
		}
		shards := resp.(*msgs.ShardsResp)
		for i, shard := range shards.Shards {
			if shard.Port == 0 {
				return nil, fmt.Errorf("shard %v not present in shuckle", i)
			}
			c.shardIps[i] = shard.Ip
			c.shardPorts[i] = shard.Port
		}
		resp, err = ShuckleRequest(log, shuckleAddress, &msgs.CdcReq{})
		if err != nil {
			return nil, err
		}
		cdc := resp.(*msgs.CdcResp)
		if cdc.Port == 0 {
			return nil, fmt.Errorf("CDC not present in shuckle")
		}
		c.cdcIp = cdc.Ip
		c.cdcPort = cdc.Port
	}
	var err error
	c.cdcSocket, err = CreateCDCSocket(c.cdcIp, c.cdcPort)
	if err != nil {
		return nil, err
	}
	if shid != nil {
		c.shardSocketFactory, err = newShardSpecificFactory(*shid, c.shardIps[int(*shid)], c.shardPorts[int(*shid)])
		if err != nil {
			c.cdcSocket.Close()
			return nil, err
		}
	} else {
		c.shardSocketFactory, err = newAllShardsFactory(c.shardIps[:], c.shardPorts[:])
		if err != nil {
			c.cdcSocket.Close()
			return nil, err
		}
	}
	c.counters = counters
	c.cdcKey = cdcKey
	return &c, nil
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
		panic(err)
	}
}

// Holds sockets to all 256 shards
type allShardsFactory struct {
	shardSocks [256]*net.UDPConn
	shardLocks [256]sync.Mutex
}

func newAllShardsFactory(shardIps [][4]byte, shardPorts []uint16) (*allShardsFactory, error) {
	var err error
	c := allShardsFactory{}
	for i := 0; i < 256; i++ {
		c.shardSocks[msgs.ShardId(i)], err = CreateShardSocket(msgs.ShardId(i), shardIps[i], shardPorts[i])
		if err != nil {
			return nil, err
		}
	}
	return &c, nil
}

func (c *allShardsFactory) close() {
	for _, sock := range c.shardSocks {
		if err := sock.Close(); err != nil {
			panic(err)
		}
	}
}

func (c *allShardsFactory) getShardSocket(shid msgs.ShardId, ip [4]byte, port uint16) (*net.UDPConn, error) {
	if c.shardLocks[int(shid)].TryLock() {
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

// TODO probably convert these errors to stderr, we can't do much with them usually
// but they'd be worth knowing about
func (c *shardSpecificFactory) close() error {
	if err := c.shardSock.Close(); err != nil {
		return err
	}
	return nil
}

func newShardSpecificFactory(shid msgs.ShardId, ip [4]byte, port uint16) (*shardSpecificFactory, error) {
	c := shardSpecificFactory{
		shid: shid,
	}
	var err error
	c.shardSock, err = CreateShardSocket(shid, ip, port)
	if err != nil {
		return nil, err
	}
	return &c, nil
}

func (c *shardSpecificFactory) getShardSocket(shid msgs.ShardId, ip [4]byte, port uint16) (*net.UDPConn, error) {
	if shid == c.shid && c.shardLock.TryLock() {
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
func GetDirectoryInfo(log LogLevels, c *Client, id msgs.InodeId) (*msgs.DirectoryInfoBody, error) {
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

func SetDirectoryInfo(log LogLevels, c *Client, id msgs.InodeId, inherited bool, info *msgs.DirectoryInfoBody) error {
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
	log LogLevels,
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
	log LogLevels,
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
