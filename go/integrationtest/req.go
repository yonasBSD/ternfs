package main

import (
	"bytes"
	"io"
	"xtx/eggsfs/eggs"
	"xtx/eggsfs/msgs"
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

func shardReq(log eggs.LogLevels, client *eggs.Client, shid msgs.ShardId, reqBody msgs.ShardRequest, respBody msgs.ShardResponse) {
	err := client.ShardRequest(log, shid, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

func cdcReq(log eggs.LogLevels, client *eggs.Client, reqBody msgs.CDCRequest, respBody msgs.CDCResponse) {
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
	log eggs.LogLevels,
	client *eggs.Client,
	mbs eggs.MockableBlockServices,
	dirId msgs.InodeId,
	spanSize uint64,
	name string,
	size uint64,
	genData func(size uint64) []byte,
) (id msgs.InodeId, creationTime msgs.EggsTime) {
	// construct
	constructReq := msgs.ConstructFileReq{
		Type: msgs.FILE,
		Note: "",
	}
	constructResp := msgs.ConstructFileResp{}
	shardReq(log, client, dirId.Shard(), &constructReq, &constructResp)
	// add spans
	if size < 256 {
		bytes := genData(size)
		addSpanReq := msgs.AddSpanInitiateReq{
			FileId:       constructResp.Id,
			Cookie:       constructResp.Cookie,
			ByteOffset:   0,
			StorageClass: msgs.INLINE_STORAGE,
			Parity:       0,
			Crc32:        eggs.CRC32C(bytes),
			Size:         size,
			BlockSize:    0,
			BodyBytes:    bytes,
		}
		shardReq(log, client, dirId.Shard(), &addSpanReq, &msgs.AddSpanInitiateResp{})
	} else {
		// We do this because just allocating this data each time is very slow.
		_, isMocked := mbs.(*eggs.MockedBlockServices)
		var allSpansData []byte
		if !isMocked {
			allSpansData = genData(size)
		}
		for offset := uint64(0); offset < size; offset += spanSize {
			thisSpanSize := eggs.Min(spanSize, size-offset)
			var data0 io.Reader
			var data1 io.Reader
			var crc [4]byte
			if isMocked {
				data0 = noopReader{}
				data1 = noopReader{}
			} else {
				bs := allSpansData[offset : offset+thisSpanSize]
				crc = eggs.CRC32C(bs)
				data0 = bytes.NewReader(bs)
				data1 = bytes.NewReader(bs)
			}
			addSpanReq := msgs.AddSpanInitiateReq{
				FileId:       constructResp.Id,
				Cookie:       constructResp.Cookie,
				ByteOffset:   offset,
				StorageClass: 2,
				Parity:       msgs.MkParity(1, 1),
				Crc32:        crc,
				Size:         thisSpanSize,
				BlockSize:    thisSpanSize,
				BodyBlocks:   []msgs.NewBlockInfo{{Crc32: crc}, {Crc32: crc}},
			}
			addSpanResp := msgs.AddSpanInitiateResp{}
			shardReq(log, client, dirId.Shard(), &addSpanReq, &addSpanResp)
			block0 := &addSpanResp.Blocks[0]
			block1 := &addSpanResp.Blocks[1]
			var proof0 [8]byte
			var proof1 [8]byte
			var err error
			var conn eggs.MockableBlockServiceConn
			conn, err = mbs.BlockServiceConnection(block0.BlockServiceId, block0.BlockServiceIp[:], block0.BlockServicePort)
			if err != nil {
				panic(err)
			}
			proof0, err = mbs.WriteBlock(log, conn, block0, data0, uint32(thisSpanSize), crc)
			conn.Close()
			if err != nil {
				panic(err)
			}
			conn, err = mbs.BlockServiceConnection(block1.BlockServiceId, block1.BlockServiceIp[:], block1.BlockServicePort)
			if err != nil {
				panic(err)
			}
			proof1, err = mbs.WriteBlock(log, conn, block1, data1, uint32(thisSpanSize), crc)
			conn.Close()
			if err != nil {
				panic(err)
			}
			certifySpanReq := msgs.AddSpanCertifyReq{
				FileId:     constructResp.Id,
				Cookie:     constructResp.Cookie,
				ByteOffset: offset,
				Proofs: []msgs.BlockProof{
					{
						BlockId: block0.BlockId,
						Proof:   proof0,
					},
					{
						BlockId: block1.BlockId,
						Proof:   proof1,
					},
				},
			}
			certifySpanResp := msgs.AddSpanCertifyResp{}
			shardReq(log, client, dirId.Shard(), &certifySpanReq, &certifySpanResp)
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

func readFile(log eggs.LogLevels, client *eggs.Client, mbs eggs.MockableBlockServices, id msgs.InodeId) []byte {
	data := bytes.NewBuffer([]byte{})
	spansReq := msgs.FileSpansReq{
		FileId: id,
	}
	spansResp := msgs.FileSpansResp{}
	for {
		shardReq(log, client, id.Shard(), &spansReq, &spansResp)
		for _, span := range spansResp.Spans {
			if span.StorageClass == msgs.INLINE_STORAGE {
				data.Write(span.BodyBytes)
			} else {
				block := &span.BodyBlocks[0]
				blockService := &spansResp.BlockServices[block.BlockServiceIx]
				conn, err := mbs.BlockServiceConnection(blockService.Id, blockService.Ip[:], blockService.Port)
				if err != nil {
					panic(err)
				}
				if err := mbs.FetchBlock(log, conn, blockService, block.BlockId, block.Crc32, 0, uint32(span.BlockSize)); err != nil {
					conn.Close()
					panic(err)
				}
				lr := &io.LimitedReader{
					R: conn,
					N: int64(span.BlockSize),
				}
				if _, err := data.ReadFrom(lr); err != nil {
					conn.Close()
					panic(err)
				}
				conn.Close()
			}
		}
		if spansResp.NextOffset == 0 {
			break
		}
	}
	return data.Bytes()
}

func readDir(log eggs.LogLevels, client *eggs.Client, dir msgs.InodeId) []edge {
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

func fullReadDir(log eggs.LogLevels, client *eggs.Client, dirId msgs.InodeId) []fullEdge {
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
		req.Cursor = resp.Next
		if req.Cursor.StartHash == 0 {
			break
		}
	}
	return edges
}
