package main

import (
	"crypto/cipher"
	"fmt"
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

type harness struct {
	log               eggs.LogLevels
	client            *eggs.Client
	blockServicesKeys map[msgs.BlockServiceId]cipher.Block
}

func (h *harness) shardReq(shid msgs.ShardId, reqBody msgs.ShardRequest, respBody msgs.ShardResponse) {
	err := h.client.ShardRequest(h.log, shid, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

func (h *harness) cdcReq(reqBody msgs.CDCRequest, respBody msgs.CDCResponse) {
	err := h.client.CDCRequest(h.log, reqBody, respBody)
	if err != nil {
		panic(err)
	}
}

func (h *harness) createFile(dirId msgs.InodeId, spanSize uint64, name string, size uint64) (id msgs.InodeId, creationTime msgs.EggsTime) {
	// construct
	constructReq := msgs.ConstructFileReq{
		Type: msgs.FILE,
		Note: "",
	}
	constructResp := msgs.ConstructFileResp{}
	h.shardReq(dirId.Shard(), &constructReq, &constructResp)
	// add spans
	if size < 256 {
		bytes := make([]byte, size)
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
		h.shardReq(dirId.Shard(), &addSpanReq, &msgs.AddSpanInitiateResp{})
	} else {
		for offset := uint64(0); offset < size; offset += spanSize {
			thisSpanSize := eggs.Min(spanSize, size-offset)
			var data []byte
			var crc [4]byte
			if h.blockServicesKeys == nil {
				data = make([]byte, thisSpanSize)
				crc = eggs.CRC32C(data)
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
			h.shardReq(dirId.Shard(), &addSpanReq, &addSpanResp)
			block0 := &addSpanResp.Blocks[0]
			block1 := &addSpanResp.Blocks[1]
			var proof0 [8]byte
			var proof1 [8]byte
			if h.blockServicesKeys != nil {
				blockServiceKey0, blockServiceFound0 := h.blockServicesKeys[block0.BlockServiceId]
				if !blockServiceFound0 {
					panic(fmt.Errorf("could not find block service %v", block0.BlockServiceId))
				}
				proof0 = eggs.BlockWriteProof(block0.BlockServiceId, block0.BlockId, blockServiceKey0)
				blockServiceKey1, blockServiceFound1 := h.blockServicesKeys[block1.BlockServiceId]
				if !blockServiceFound1 {
					panic(fmt.Errorf("could not find block service %v", block1.BlockServiceId))
				}
				proof1 = eggs.BlockWriteProof(block1.BlockServiceId, block1.BlockId, blockServiceKey1)
			} else {
				var err error
				proof0, err = eggs.WriteBlock(h.log, block0, data, crc)
				if err != nil {
					panic(err)
				}
				proof1, err = eggs.WriteBlock(h.log, block1, data, crc)
				if err != nil {
					panic(err)
				}
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
			h.shardReq(dirId.Shard(), &certifySpanReq, &certifySpanResp)
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
	h.shardReq(dirId.Shard(), &linkReq, &linkResp)
	return constructResp.Id, linkResp.CreationTime
}

func (h *harness) readDir(dir msgs.InodeId) []edge {
	req := msgs.ReadDirReq{
		DirId:     dir,
		StartHash: 0,
	}
	resp := msgs.ReadDirResp{}
	edges := []edge{}
	for {
		h.shardReq(dir.Shard(), &req, &resp)
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

func (h *harness) fullReadDir(dirId msgs.InodeId) []fullEdge {
	req := msgs.FullReadDirReq{
		DirId: msgs.ROOT_DIR_INODE_ID,
	}
	resp := msgs.FullReadDirResp{}
	edges := []fullEdge{}
	for {
		h.shardReq(dirId.Shard(), &req, &resp)
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

func newHarness(log eggs.LogLevels, client *eggs.Client, blockServicesKeys map[msgs.BlockServiceId]cipher.Block) *harness {
	return &harness{
		log:               log,
		client:            client,
		blockServicesKeys: blockServicesKeys,
	}
}
