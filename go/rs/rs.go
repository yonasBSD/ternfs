package rs

import (
	"encoding/json"
	"fmt"
	"unsafe"
)

// #cgo LDFLAGS: -L${SRCDIR}/../../cpp/build/go/rs -lrs
// #include "../../cpp/rs/rs.h"
//
// void set_ptr(uint8_t** ptrs, size_t i, uint8_t* ptr) {
//     ptrs[i] = ptr;
// }
import "C"

func MkParity(dataBlocks uint8, parityBlocks uint8) Parity {
	if dataBlocks == 0 || dataBlocks >= 16 {
		panic(fmt.Errorf("bad data blocks %v", dataBlocks))
	}
	if parityBlocks >= 16 {
		panic(fmt.Errorf("bad parity blocks %v", parityBlocks))
	}
	return Parity(dataBlocks | (parityBlocks << 4))
}

func (parity Parity) DataBlocks() int {
	return int(parity) & 0x0F
}
func (parity Parity) ParityBlocks() int {
	return int(parity) >> 4
}
func (parity Parity) Blocks() int {
	return parity.DataBlocks() + parity.ParityBlocks()
}

func (parity Parity) String() string {
	return fmt.Sprintf("(%v,%v)", parity.DataBlocks(), parity.ParityBlocks())
}

func (p Parity) MarshalJSON() ([]byte, error) {
	return json.Marshal([]int{p.DataBlocks(), p.ParityBlocks()})
}

func (p *Parity) UnmarshalJSON(b []byte) error {
	var nums []uint8
	if err := json.Unmarshal(b, &nums); err != nil {
		return err
	}
	if len(nums) != 2 {
		return fmt.Errorf("expecting a list of 2 numbers for parity value, got %v", nums)
	}
	*p = MkParity(nums[0], nums[1])
	return nil
}

type Rs struct {
	r *C.struct_rs
}

type Parity uint8

func GetRs(parity Parity) *Rs {
	if parity.DataBlocks() < 2 {
		panic(fmt.Errorf("bad parity, expected at least 2 data blocks, got %v", parity))
	}
	r := &Rs{}
	r.r = C.rs_get(C.uchar(parity))
	return r
}

func (r *Rs) Parity() Parity {
	return Parity(C.rs_parity(r.r))
}

func (r *Rs) BlockSize(size int) int {
	if size < 0 {
		panic(fmt.Errorf("negative size %v", size))
	}
	return int(C.rs_block_size(r.r, C.ulong(size)))
}

func (r *Rs) SplitData(data []byte) [][]byte {
	if len(data) == 0 {
		panic("empty data")
	}
	dataBlocks := make([][]byte, r.Parity().DataBlocks())
	blockSize := r.BlockSize(len(data))
	for i := range dataBlocks {
		dataBlocks[i] = make([]byte, blockSize)
		// some blocks might be entirely empty, if the data is small and the number
		// of data blocks is large
		if i*blockSize <= len(data) {
			copy(dataBlocks[i], data[i*blockSize:])
		}
	}
	return dataBlocks
}

func (r *Rs) ComputeParity(data [][]byte) [][]byte {
	parity := make([][]byte, r.Parity().ParityBlocks())
	blockSize := len(data[0])
	for i := range parity {
		parity[i] = make([]byte, blockSize)
	}
	r.ComputeParityInto(data, parity)
	return parity
}

// Returns block size
func (r *Rs) checkBlockSize(data [][]byte, parity [][]byte) int {
	if len(data) != r.Parity().DataBlocks() {
		panic(fmt.Errorf("bad number of data blocks, expected %v, got %v", r.Parity().DataBlocks(), len(data)))
	}
	blockSize := len(data[0])
	for i := range data {
		if len(data[i]) != blockSize {
			panic(fmt.Errorf("bad block size, expected %v, got %v", blockSize, len(data[i])))
		}
	}
	if parity != nil {
		if len(parity) != r.Parity().ParityBlocks() {
			panic(fmt.Errorf("bad number of parity blocks, expected %v, got %v", r.Parity().ParityBlocks(), len(parity)))
		}
		for i := range parity {
			if len(parity[i]) != blockSize {
				panic(fmt.Errorf("bad block size, expected %v, got %v", blockSize, len(parity[i])))
			}
		}
	}
	expectedBlockSize := r.BlockSize(blockSize * len(data))
	if expectedBlockSize != blockSize {
		panic(fmt.Errorf("bad block size, expected %v, got %v", expectedBlockSize, blockSize))
	}
	return blockSize
}

func (r *Rs) ComputeParityInto(data [][]byte, parity [][]byte) {
	blockSize := r.checkBlockSize(data, parity)
	// prepare C structs
	dataPtrs := (**C.uchar)(C.malloc(C.size_t(uintptr(r.Parity().DataBlocks()) * unsafe.Sizeof((*C.uchar)(nil)))))
	for i := range data {
		C.set_ptr(dataPtrs, C.ulong(i), (*C.uchar)(&data[i][0]))
	}
	parityPtrs := (**C.uchar)(C.malloc(C.size_t(uintptr(r.Parity().ParityBlocks()) * unsafe.Sizeof((*C.uchar)(nil)))))
	for i := range parity {
		C.set_ptr(parityPtrs, C.ulong(i), (*C.uchar)(&parity[i][0]))
	}
	// go for it
	C.rs_compute_parity(r.r, C.ulong(blockSize), dataPtrs, parityPtrs)
}

func (r *Rs) Recover(
	haveBlocks []uint8,
	blocks [][]byte,
	wantBlock uint8,
) []byte {
	block := make([]byte, len(blocks[0]))
	r.RecoverInto(haveBlocks, blocks, wantBlock, block)
	return block
}

func (r *Rs) RecoverInto(
	haveBlocks []uint8,
	blocks [][]byte,
	wantBlock uint8,
	block []byte,
) {
	blockSize := r.checkBlockSize(blocks, nil)
	if len(block) != blockSize {
		panic(fmt.Errorf("bad block size, expected %v, got %v", blockSize, len(block)))
	}
	// TODO check forr sortedness etc.
	blocksPtrs := (**C.uchar)(C.malloc(C.size_t(uintptr(r.Parity().DataBlocks()) * unsafe.Sizeof((*C.uchar)(nil)))))
	for i := range blocks {
		C.set_ptr(blocksPtrs, C.ulong(i), (*C.uchar)(&blocks[i][0]))
	}
	C.rs_recover(r.r, C.ulong(blockSize), (*C.uchar)(&haveBlocks[0]), blocksPtrs, C.uchar(wantBlock), (*C.uchar)(&block[0]))
}
