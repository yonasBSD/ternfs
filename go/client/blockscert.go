package client

import (
	"bytes"
	"crypto/cipher"
	"encoding/binary"
	"xtx/eggsfs/lib"
	"xtx/eggsfs/msgs"
)

func BlockWriteCertificate(cipher cipher.Block, blockServiceId msgs.BlockServiceId, req *msgs.WriteBlockReq) [8]byte {
	w := bytes.NewBuffer([]byte{})
	binary.Write(w, binary.LittleEndian, uint64(blockServiceId))
	w.Write([]byte{'w'})
	binary.Write(w, binary.LittleEndian, uint64(req.BlockId))
	binary.Write(w, binary.LittleEndian, uint32(req.Crc))
	binary.Write(w, binary.LittleEndian, uint32(req.Size))
	return lib.CBCMAC(cipher, w.Bytes())
}

func CheckBlockWriteCertificate(cipher cipher.Block, blockServiceId msgs.BlockServiceId, req *msgs.WriteBlockReq) ([8]byte, bool) {
	expectedMac := BlockWriteCertificate(cipher, blockServiceId, req)
	return expectedMac, expectedMac == req.Certificate
}

func CheckBlockEraseCertificate(blockServiceId msgs.BlockServiceId, cipher cipher.Block, req *msgs.EraseBlockReq) ([8]byte, bool) {
	// compute mac
	w := bytes.NewBuffer([]byte{})
	binary.Write(w, binary.LittleEndian, uint64(blockServiceId))
	w.Write([]byte{'e'})
	binary.Write(w, binary.LittleEndian, uint64(req.BlockId))
	expectedMac := lib.CBCMAC(cipher, w.Bytes())
	return expectedMac, expectedMac == req.Certificate
}
