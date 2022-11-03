package janitor

import (
	"fmt"
	"xtx/eggsfs/msgs"
	"xtx/eggsfs/request"
)

type scratchFile struct {
	id     msgs.InodeId
	cookie uint64
	offset uint64
}

func (env *Env) ensureScratchFile(file *scratchFile) error {
	if file.id != msgs.NULL_INODE_ID {
		return nil
	}
	resp := msgs.ConstructFileResp{}
	err := env.ShardRequest(
		&msgs.ConstructFileReq{
			Type: msgs.FILE,
			Note: "repair",
		},
		&resp,
	)
	if err != nil {
		return err
	}
	file.id = resp.Id
	file.cookie = resp.Cookie
	file.offset = 0
	return nil
}

func (env *Env) copyBlock(
	file *scratchFile, blockServices []msgs.BlockService, blockSize uint64, storageClass msgs.StorageClass, block *msgs.FetchedBlock,
) error {
	blockService := blockServices[block.BlockServiceIx]
	initiateSpanReq := msgs.AddSpanInitiateReq{
		FileId:       file.id,
		Cookie:       file.cookie,
		ByteOffset:   file.offset,
		StorageClass: storageClass,
		Blacklist:    []msgs.BlockServiceBlacklist{{Id: blockService.Id}},
		Parity:       msgs.MkParity(1, 0),
		Crc32:        block.Crc32,
		Size:         blockSize,
		BlockSize:    blockSize,
		BodyBlocks:   []msgs.NewBlockInfo{{Crc32: block.Crc32}},
	}
	initiateSpanResp := msgs.AddSpanInitiateResp{}
	if err := env.ShardRequest(&initiateSpanReq, &initiateSpanResp); err != nil {
		return err
	}
	dstBlock := &initiateSpanResp.Blocks[0]
	proof, err := request.CopyBlock(env, blockServices, blockSize, block, dstBlock)
	if err != nil {
		return err
	}
	certifySpanResp := msgs.AddSpanCertifyResp{}
	err = env.ShardRequest(
		&msgs.AddSpanCertifyReq{
			FileId:     file.id,
			Cookie:     file.cookie,
			ByteOffset: file.offset,
			Proofs:     []msgs.BlockProof{{BlockId: dstBlock.BlockId, Proof: proof}},
		},
		&certifySpanResp,
	)
	if err != nil {
		return err
	}
	return nil
}

// Migrates the blocks in that block service, in that file.
//
// If the source block service it's still healthy, it'll just copy the block over, otherwise
// it'll be recovered from the other. If possible, anyway.
//
// Returns the number of migrated blocks.
func (env *Env) MigrateBlock(fileId msgs.InodeId, id msgs.BlockServiceId) (int, error) {
	migrated := 0
	byteOffset := uint64(0)
	scratchFile := scratchFile{}
	for {
		req := msgs.FileSpansReq{
			FileId:     fileId,
			ByteOffset: byteOffset,
		}
		resp := msgs.FileSpansResp{}
		err := env.ShardRequest(&req, &resp)
		if err != nil {
			return 0, err
		}
		for _, span := range resp.Spans {
			if span.Parity.DataBlocks() != 1 {
				panic(fmt.Errorf("non-mirroring not supported for now"))
			}
			blockToMigrate := -1
			for blockIx, block := range span.BodyBlocks {
				blockService := resp.BlockServices[block.BlockServiceIx]
				if blockService.Id == id {
					blockToMigrate := blockIx
					break
				}
			}
			if blockToMigrate < 0 {
				continue
			}
			// Right now we only support mirroring, so this is pretty easy --
			// we just get the first non-stale block.
			for _, block := range span.BodyBlocks {
				blockService := resp.BlockServices[block.BlockServiceIx]
				// TODO actually decide how flags work
				if blockService.Flags == 0 {
					if err := env.ensureScratchFile(&scratchFile); err != nil {
						return 0, err
					}
					// copy to scratch file
					if err := env.copyBlock(&scratchFile, resp.BlockServices, span.BlockSize, span.StorageClass, &block); err != nil {
						return 0, err
					}
					// swap blocks
				}
			}
		}
	}
	return migrated, nil
}
