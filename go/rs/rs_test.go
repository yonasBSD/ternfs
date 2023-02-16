package rs

import (
	"math/rand"
	"sort"
	"testing"

	"github.com/stretchr/testify/assert"
)

func TestGet(t *testing.T) {
	parity := MkParity(10, 4)
	rs := GetRs(parity)
	assert.Equal(t, parity, rs.Parity())
}

func TestComputeParity(t *testing.T) {
	rand := rand.New(rand.NewSource(0))
	for i := 0; i < 16*16*100; i++ {
		numData := int(2 + rand.Uint32()%(16-2))
		numParity := int(1 + rand.Uint32()%(16-1))
		data := make([]byte, 1+int(rand.Uint32()%1000))
		rand.Read(data)
		rs := GetRs(MkParity(uint8(numData), uint8(numParity)))
		dataBlocks := rs.SplitData(data)
		parityBlocks := rs.ComputeParity(dataBlocks)
		allBlocks := append(append([][]byte{}, dataBlocks...), parityBlocks...)
		assert.Equal(t, rs.Parity().DataBlocks(), len(dataBlocks))
		assert.Equal(t, rs.Parity().ParityBlocks(), len(parityBlocks))
		// Verify that the concatenation is the original data
		concatData := []byte{}
		for _, dataBlock := range dataBlocks {
			concatData = append(concatData, dataBlock...)
		}
		assert.Equal(t, data, concatData[:len(data)])
		for _, b := range concatData[len(data):] {
			assert.Equal(t, uint8(0), b)
		}
		// Verify that the first parity block is the XOR of all the data blocks
		expectedParity0 := make([]byte, len(dataBlocks[0]))
		for _, dataBlock := range dataBlocks {
			for i, b := range dataBlock {
				expectedParity0[i] ^= b
			}
		}
		assert.Equal(t, expectedParity0, parityBlocks[0])
		// Restore a random block
		{
			haveBlocks := make([]uint8, numData+numParity)
			for i := range haveBlocks {
				haveBlocks[i] = uint8(i)
			}
			for i := 0; i < numData+1; i++ {
				if i == numParity {
					break
				}
				j := i + 1 + int(rand.Uint32())%(len(haveBlocks)-i-1)
				haveBlocks[i], haveBlocks[j] = haveBlocks[j], haveBlocks[i]
			}
			wantBlock := haveBlocks[numData]
			haveBlocks = haveBlocks[:numData]
			sort.Slice(haveBlocks, func(i, j int) bool { return haveBlocks[i] < haveBlocks[j] })
			have := make([][]byte, numData)
			for i := range have {
				have[i] = allBlocks[haveBlocks[i]]
			}
			recoveredBlock := rs.Recover(haveBlocks, have, wantBlock)
			assert.Equal(t, allBlocks[wantBlock], recoveredBlock)
		}
	}
}
