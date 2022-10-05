package bincode

import (
	"bytes"
	"io"
	"testing"

	"github.com/stretchr/testify/assert"
)

type simpleStruct struct {
	x  uint16
	y  uint32
	bs []byte
}

func (s *simpleStruct) Pack(w io.Writer) error {
	PackFixed(s.x, w)
	PackFixed(s.y, w)
	err := PackBytes(s.bs, w)
	if err != nil {
		return err
	}
	return nil
}

func (s *simpleStruct) Unpack(r io.Reader) error {
	UnpackFixed(&s.x, r)
	UnpackFixed(&s.y, r)
	UnpackBytes(&s.bs, r)
	return nil
}

func TestSimpleStruct(t *testing.T) {
	ss1 := simpleStruct{x: 32, y: 10, bs: []byte("Hello")}
	bytes, err := PackToBytes(&ss1)
	assert.Nil(t, err)
	var ss2 simpleStruct
	assert.Nil(t, UnpackFromBytes(&ss2, bytes))
	assert.Equal(t, ss2, ss1)
}

func TestU61(t *testing.T) {
	for i := 0; i < 61; i++ {
		x := uint64(1) << i
		var buf bytes.Buffer
		err := PackU61Var(x, &buf)
		assert.Nil(t, err)
		var y uint64
		UnpackU61Var(&y, bytes.NewReader(buf.Bytes()))
		assert.Equal(t, y, x)
	}
}
