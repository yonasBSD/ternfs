package lib

import (
	"encoding/binary"
	"xtx/ternfs/msgs"
)

func BlockServiceIdFromKey(secretKey [16]byte) msgs.BlockServiceId {
	// we don't really care about leaking part or all of the key -- the whole key business is
	// to defend against bugs, not malicious agents.
	//
	// That said, we prefer to encode the knowledge of how to generate block service ids once,
	// in the block service.
	//
	// Also, we remove the highest bit for the sake of SQLite, at least for now
	return msgs.BlockServiceId(binary.LittleEndian.Uint64(secretKey[:8]) & uint64(0x7FFFFFFFFFFFFFFF))
}
