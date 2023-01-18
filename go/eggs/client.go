package eggs

import (
	"crypto/cipher"
	"fmt"
	"net"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type ShardSocketFactory interface {
	GetShardSocket(shid msgs.ShardId) (*net.UDPConn, error)
	ReleaseShardSocket(shid msgs.ShardId)
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
	ShardSocketFactory ShardSocketFactory
	CDCSocket          *net.UDPConn
	Counters           *ClientCounters
	CDCKey             cipher.Block
}

func NewClient(shid *msgs.ShardId, counters *ClientCounters, cdcKey cipher.Block) (*Client, error) {
	var err error
	c := Client{}
	c.CDCSocket, err = CreateCDCSocket()
	if err != nil {
		return nil, err
	}
	if shid != nil {
		c.ShardSocketFactory, err = NewShardSpecificFactory(*shid)
		if err != nil {
			c.CDCSocket.Close()
			return nil, err
		}
	} else {
		c.ShardSocketFactory, err = NewAllShardsFactory()
		if err != nil {
			c.CDCSocket.Close()
			return nil, err
		}
	}
	c.Counters = counters
	c.CDCKey = cdcKey
	return &c, nil
}

func (c *Client) Close() {
	switch factory := c.ShardSocketFactory.(type) {
	case *AllShardsFactory:
		factory.Close()
	case *ShardSpecificFactory:
		factory.Close()
	default:
		panic(fmt.Errorf("bad factory %T", c.ShardSocketFactory))
	}
	if err := c.CDCSocket.Close(); err != nil {
		panic(err)
	}
}

// Holds sockets to all 256 shards
type AllShardsFactory struct {
	shardSocks [256]*net.UDPConn
}

func NewAllShardsFactory() (*AllShardsFactory, error) {
	var err error
	c := AllShardsFactory{}
	for i := 0; i < 256; i++ {
		c.shardSocks[msgs.ShardId(i)], err = CreateShardSocket(msgs.ShardId(i))
		if err != nil {
			return nil, err
		}
	}
	return &c, nil
}

func (c *AllShardsFactory) Close() {
	for _, sock := range c.shardSocks {
		if err := sock.Close(); err != nil {
			panic(err)
		}
	}
}

func (c *AllShardsFactory) GetShardSocket(shid msgs.ShardId) (*net.UDPConn, error) {
	return c.shardSocks[int(shid)], nil
}

func (c *AllShardsFactory) ReleaseShardSocket(msgs.ShardId) {}

// For when you almost always do requests to a single shard (e.g. in GC).
type ShardSpecificFactory struct {
	shid      msgs.ShardId
	shardSock *net.UDPConn
}

// TODO probably convert these errors to stderr, we can't do much with them usually
// but they'd be worth knowing about
func (c *ShardSpecificFactory) Close() error {
	if err := c.shardSock.Close(); err != nil {
		return err
	}
	return nil
}

func NewShardSpecificFactory(shid msgs.ShardId) (*ShardSpecificFactory, error) {
	c := ShardSpecificFactory{
		shid: shid,
	}
	var err error
	c.shardSock, err = CreateShardSocket(shid)
	if err != nil {
		return nil, err
	}
	return &c, nil
}

func (c *ShardSpecificFactory) GetShardSocket(shid msgs.ShardId) (*net.UDPConn, error) {
	if shid == c.shid {
		return c.shardSock, nil
	} else {
		shardSock, err := CreateShardSocket(shid)
		if err != nil {
			return nil, err
		}
		return shardSock, nil
	}
}

func (c *ShardSpecificFactory) ReleaseShardSocket(shid msgs.ShardId) {
	if shid == c.shid {
		return
	}
	if err := c.shardSock.Close(); err != nil {
		panic(err)
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
	if err := bincode.UnpackFromBytes(&info, resp.Info); err != nil {
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
		buf = make([]byte, 255)
		bincode.PackIntoBytes(&buf, info)
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
