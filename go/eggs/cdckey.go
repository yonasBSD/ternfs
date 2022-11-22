package eggs

import (
	"crypto/aes"
	"crypto/cipher"
	"fmt"
)

func CDCKey() cipher.Block {
	cipher, err := aes.NewCipher([]byte("\xa1\x11\x1c\xf0\xf6+\xba\x02%\xd2f\xe7\xa6\x94\x86\xfe"))
	if err != nil {
		panic(fmt.Errorf("could not create AES-128 key: %w", err))
	}
	return cipher
}
