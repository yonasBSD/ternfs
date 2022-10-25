package cbcmac

import (
	"crypto/cipher"
	"fmt"
)

// these functions are safe so long as no message is a prefix of another message
// this can be achieved with fixed length messages, length prefixing or (length determining) type prefixing
func zeroPad(data []byte) []byte {
	n := (len(data) + 15) / 16
	return append(data, make([]byte, n*16-len(data))...)
}

// Intended to be used with AES-128
func CBCMAC(cipher cipher.Block, data []byte) [8]byte {
	if cipher.BlockSize() != 16 {
		panic(fmt.Errorf("expecting block size 16, got %d", cipher.BlockSize()))
	}
	data = zeroPad(data)
	block := make([]byte, 16)
	for i := 0; i < len(data); i += 16 {
		// TODO subtle.XORbytes should be available in newer versions of go
		for j := 0; j < 16; j++ {
			block[j] ^= data[i+j]
		}
		cipher.Encrypt(block, block)
	}
	var mac [8]byte
	copy(mac[:], block[:8])
	return mac
}
