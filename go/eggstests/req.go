package main

import (
	"fmt"
	"io"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/wyhash"
)

type edge struct {
	name     string
	targetId msgs.InodeId
}

type fullEdge struct {
	current      bool
	name         string
	targetId     msgs.InodeId
	creationTime msgs.EggsTime
}

func shardReq(log *lib.Logger, client *lib.Client, shid msgs.ShardId, reqBody msgs.ShardRequest, respBody msgs.ShardResponse) {
	err := client.ShardRequest(log, shid, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

func cdcReq(log *lib.Logger, client *lib.Client, reqBody msgs.CDCRequest, respBody msgs.CDCResponse) {
	err := client.CDCRequest(log, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

type noopReader struct{}

func (noopReader) Read(bs []byte) (int, error) {
	return len(bs), nil
}

/*
func createFileData(
	mbs eggs.MockableBlockServices,

	genData func(size uint64) []byte,
) (io.Reader, [4]byte) {
	_, isFake := mbs.(*eggs.MockedBlockServices)
	if isFake {
		var crc [4]byte
		return noopReader{}, crc
	}
	data :=
}
*/

func createFile(
	log *lib.Logger,
	client *lib.Client,
	dirInfoCache *lib.DirInfoCache,
	dirId msgs.InodeId,
	spanSize uint32,
	name string,
	size uint64,
	dataSeed uint64,
	bufPool *lib.BufPool,
) (id msgs.InodeId, creationTime msgs.EggsTime) {
	// construct
	constructReq := msgs.ConstructFileReq{
		Type: msgs.FILE,
		Note: "",
	}
	constructResp := msgs.ConstructFileResp{}
	shardReq(log, client, dirId.Shard(), &constructReq, &constructResp)
	rand := wyhash.New(dataSeed)
	if size > 0 {
		var spanPolicy msgs.SpanPolicy
		if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &spanPolicy); err != nil {
			panic(err)
		}
		var blockPolicies msgs.BlockPolicy
		if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &blockPolicies); err != nil {
			panic(err)
		}
		var stripePolicy msgs.StripePolicy
		if _, err := client.ResolveDirectoryInfoEntry(log, dirInfoCache, dirId, &stripePolicy); err != nil {
			panic(err)
		}
		// add spans
		for offset := uint64(0); offset < size; offset += uint64(spanSize) {
			thisSpanSize := spanSize
			if uint32(size-offset) < thisSpanSize {
				thisSpanSize = uint32(size - offset)
			}
			spanBuf := bufPool.Get(int(thisSpanSize))
			rand.Read(*spanBuf)
			err := client.CreateSpan(log, []msgs.BlacklistEntry{}, &spanPolicy, &blockPolicies, &stripePolicy, constructResp.Id, constructResp.Cookie, offset, uint32(thisSpanSize), spanBuf)
			bufPool.Put(spanBuf)
			if err != nil {
				panic(err)
			}
		}
	}
	// link
	linkReq := msgs.LinkFileReq{
		FileId:  constructResp.Id,
		Cookie:  constructResp.Cookie,
		OwnerId: dirId,
		Name:    name,
	}
	linkResp := msgs.LinkFileResp{}
	shardReq(log, client, dirId.Shard(), &linkReq, &linkResp)
	return constructResp.Id, linkResp.CreationTime
}

func readFile(log *lib.Logger, bufPool *lib.BufPool, client *lib.Client, id msgs.InodeId, size uint64) *[]byte {
	buf := bufPool.Get(int(size))
	r, err := client.ReadFile(log, bufPool, id)
	if err != nil {
		panic(err)
	}
	defer r.Close()
	cursor := 0
	for {
		read, err := r.Read((*buf)[cursor:])
		if err == io.EOF {
			if cursor != len(*buf) {
				panic(fmt.Errorf("expected file read of size %v, got %v", len(*buf), cursor))
			}
			break
		}
		if err != nil {
			panic(err)
		}
		cursor += read
	}
	return buf
}

func readDir(log *lib.Logger, client *lib.Client, dir msgs.InodeId) []edge {
	req := msgs.ReadDirReq{
		DirId:     dir,
		StartHash: 0,
	}
	resp := msgs.ReadDirResp{}
	edges := []edge{}
	for {
		shardReq(log, client, dir.Shard(), &req, &resp)
		for _, result := range resp.Results {
			edges = append(edges, edge{
				name:     result.Name,
				targetId: result.TargetId,
			})
		}
		req.StartHash = resp.NextHash
		if req.StartHash == 0 {
			break
		}
	}
	return edges
}

func fullReadDir(log *lib.Logger, client *lib.Client, dirId msgs.InodeId) []fullEdge {
	req := msgs.FullReadDirReq{
		DirId: msgs.ROOT_DIR_INODE_ID,
	}
	resp := msgs.FullReadDirResp{}
	edges := []fullEdge{}
	for {
		shardReq(log, client, dirId.Shard(), &req, &resp)
		for _, result := range resp.Results {
			edges = append(edges, fullEdge{
				name:         result.Name,
				targetId:     result.TargetId.Id(),
				creationTime: result.CreationTime,
				current:      result.Current,
			})
		}
		if resp.Next.StartName == "" {
			break
		}
		req.Flags = 0
		if resp.Next.Current {
			req.Flags = msgs.FULL_READ_DIR_CURRENT
		}
		req.StartName = resp.Next.StartName
		req.StartTime = resp.Next.StartTime
	}
	return edges
}
