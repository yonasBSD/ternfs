package main

import (
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
	blockServicesKeys map[msgs.BlockServiceId][16]byte
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

func (h *harness) createFile(dirId msgs.InodeId, name string, size uint64) (id msgs.InodeId, creationTime msgs.EggsTime) {
	// construct
	constructReq := msgs.ConstructFileReq{
		Type: msgs.FILE,
		Note: "",
	}
	constructResp := msgs.ConstructFileResp{}
	h.shardReq(dirId.Shard(), &constructReq, &constructResp)
	// add spans
	spanSize := uint64(10) << 20 // 10MiB
	for offset := uint64(0); offset < size; offset += spanSize {
		thisSpanSize := eggs.Min(spanSize, size-offset)
		addSpanReq := msgs.AddSpanInitiateReq{
			FileId:       constructResp.Id,
			Cookie:       constructResp.Cookie,
			ByteOffset:   offset,
			StorageClass: 2,
			Parity:       msgs.MkParity(1, 1),
			Crc32:        [4]byte{0, 0, 0, 0},
			Size:         thisSpanSize,
			BlockSize:    thisSpanSize,
			BodyBlocks: []msgs.NewBlockInfo{
				{Crc32: [4]byte{0, 0, 0, 0}},
				{Crc32: [4]byte{0, 0, 0, 0}},
			},
		}
		addSpanResp := msgs.AddSpanInitiateResp{}
		h.shardReq(dirId.Shard(), &addSpanReq, &addSpanResp)
		block0 := &addSpanResp.Blocks[0]
		block1 := &addSpanResp.Blocks[1]
		blockServiceKey0, blockServiceFound0 := h.blockServicesKeys[block0.BlockServiceId]
		if !blockServiceFound0 {
			panic(fmt.Errorf("could not find block service %v", block0.BlockServiceId))
		}
		blockServiceKey1, blockServiceFound1 := h.blockServicesKeys[block1.BlockServiceId]
		if !blockServiceFound1 {
			panic(fmt.Errorf("could not find block service %v", block1.BlockServiceId))
		}
		certifySpanReq := msgs.AddSpanCertifyReq{
			FileId:     constructResp.Id,
			Cookie:     constructResp.Cookie,
			ByteOffset: offset,
			Proofs: []msgs.BlockProof{
				{
					BlockId: block0.BlockId,
					Proof:   eggs.BlockAddProof(block0.BlockServiceId, block0.BlockId, blockServiceKey0),
				},
				{
					BlockId: block1.BlockId,
					Proof:   eggs.BlockAddProof(block1.BlockServiceId, block1.BlockId, blockServiceKey1),
				},
			},
		}
		certifySpanResp := msgs.AddSpanCertifyResp{}
		h.shardReq(dirId.Shard(), &certifySpanReq, &certifySpanResp)
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

func newHarness(log eggs.LogLevels, client *eggs.Client, blockServicesKeys map[msgs.BlockServiceId][16]byte) *harness {
	return &harness{
		log:               log,
		client:            client,
		blockServicesKeys: blockServicesKeys,
	}
}
