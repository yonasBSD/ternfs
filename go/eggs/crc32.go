package eggs

import (
	"encoding/binary"
	"hash/crc32"
)

func CRC32C(data []byte) [4]byte {
	return CRC32FromU32(crc32.Checksum(data, crc32.MakeTable(crc32.Castagnoli)))
}

func CRC32ToU32(crc [4]byte) uint32 {
	return binary.LittleEndian.Uint32(crc[:])
}

func CRC32FromU32(crc uint32) (out [4]byte) {
	binary.LittleEndian.PutUint32(out[:], crc)
	return out
}
