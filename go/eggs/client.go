package eggs

import (
	"fmt"
	"net"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/msgs"
)

type Client interface {
	ShardRequest(log LogLevels, shid msgs.ShardId, req bincode.Packable, resp bincode.Unpackable) error
	CDCRequest(log LogLevels, req bincode.Packable, resp bincode.Unpackable) error
	Close() error
}

// For when you almost always do requests to a single shard (e.g. in GC).
type ShardSpecificClient struct {
	timeout   time.Duration
	shid      msgs.ShardId
	shardSock *net.UDPConn
	cdcSock   *net.UDPConn
}

// TODO probably convert these errors to stderr, we can't do much with them usually
// but they'd be worth knowing about
func (c *ShardSpecificClient) Close() error {
	if err := c.shardSock.Close(); err != nil {
		return err
	}
	if err := c.cdcSock.Close(); err != nil {
		return err
	}
	return nil
}

func NewShardSpecificClient(shid msgs.ShardId) (*ShardSpecificClient, error) {
	c := ShardSpecificClient{
		timeout: time.Second,
		shid:    shid,
	}
	var err error
	c.shardSock, err = ShardSocket(shid)
	if err != nil {
		return nil, err
	}
	c.cdcSock, err = CDCSocket()
	if err != nil {
		return nil, err
	}
	return &c, nil
}

func (c *ShardSpecificClient) ShardRequest(log LogLevels, shid msgs.ShardId, req bincode.Packable, resp bincode.Unpackable) error {
	var shardSock *net.UDPConn
	var err error
	if shid == c.shid {
		shardSock = c.shardSock
	} else {
		shardSock, err = ShardSocket(shid)
		if err != nil {
			return err
		}
		defer shardSock.Close()
	}
	return ShardRequestSocket(log, nil, shardSock, c.timeout, req, resp)
}

func (c *ShardSpecificClient) CDCRequest(log LogLevels, req bincode.Packable, resp bincode.Unpackable) error {
	return CDCRequestSocket(log, c.cdcSock, c.timeout, req, resp)
}

func GetDirectoryInfo(log LogLevels, c Client, id msgs.InodeId) (*msgs.DirectoryInfoBody, error) {
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
		panic(fmt.Errorf("empty info"))
	}
	return &info, nil
}

func SetDirectoryInfo(log LogLevels, c Client, id msgs.InodeId, inherited bool, info *msgs.DirectoryInfoBody) error {
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
