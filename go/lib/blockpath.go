package lib

import (
	"crypto/sha1"
	"fmt"
	"path"
	"xtx/eggsfs/msgs"
)

// ripped out of eggsblocks binary to test block corruption
func BlockIdToPath(basePath string, blockId msgs.BlockId) string {
	hex := fmt.Sprintf("%016x", uint64(blockId))
	// We want to split the blocks in dirs to avoid trouble with high number of
	// files in single directory (e.g. birthday paradox stuff). However the block
	// id is very much _not_ uniformly distributed (it's the time). So we use
	// the first byte of the SHA1 of the filename of the block id.
	h := sha1.New()
	h.Write([]byte(hex))
	dir := fmt.Sprintf("%02x", h.Sum(nil)[0])
	return path.Join(path.Join(basePath, dir), hex)
}
