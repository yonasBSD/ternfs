package bincode

import (
	"testing"
	"unsafe"

	"github.com/stretchr/testify/assert"
)

type simpleStruct struct {
	x  uint16
	y  uint32
	bs []byte
}

func (s *simpleStruct) Pack(buf *Buf) {
	buf.PackU16(s.x)
	buf.PackU32(s.y)
	buf.PackBytes(s.bs)
}

func (s *simpleStruct) Unpack(buf *Buf) error {
	if err := buf.UnpackU16(&s.x); err != nil {
		return err
	}
	if err := buf.UnpackU32(&s.y); err != nil {
		return err
	}
	if err := buf.UnpackBytes(&s.bs); err != nil {
		return err
	}
	return nil
}

func TestSimpleStruct(t *testing.T) {
	ss1 := simpleStruct{x: 32, y: 10, bs: []byte("Hello")}
	bytes := make([]byte, 100)
	bytes = PackToBytes(bytes, &ss1)
	var ss2 simpleStruct
	assert.Nil(t, UnpackFromBytes(&ss2, bytes))
	assert.Equal(t, ss2, ss1)
}

func TestU61(t *testing.T) {
	buf := make([]byte, 8)
	var x, y uint64
	for i := 0; i < 61; i++ {
		x = uint64(1) << i
		bbuf := Buf(buf)
		bbuf.PackVarU61(x)
		bbuf = Buf(buf)
		err := bbuf.UnpackVarU61(&y)
		assert.Nil(t, err)
		assert.Equal(t, y, x)
	}
}

func packU(buf *Buf, x any) {
	switch v := x.(type) {
	case uint8:
		buf.PackU8(v)
	case uint16:
		buf.PackU16(v)
	case uint32:
		buf.PackU32(v)
	case uint64:
		buf.PackU64(v)
	default:
		panic("Not a U")
	}
}

func unpackU(buf *Buf, x any) error {
	var err error
	switch v := x.(type) {
	case *uint8:
		err = buf.UnpackU8(v)
	case *uint16:
		err = buf.UnpackU16(v)
	case *uint32:
		err = buf.UnpackU32(v)
	case *uint64:
		err = buf.UnpackU64(v)
	default:
		panic("Not a *U")
	}
	return err
}

func testU[V uint8 | uint16 | uint32 | uint64](t *testing.T) {
	bits := int(unsafe.Sizeof(V(0))) * 8
	buf := make([]byte, 8)
	var x, y V
	for i := 0; i < bits; i++ {
		x = V(1) << i
		bbuf := Buf(buf)
		packU(&bbuf, x)
		bbuf = Buf(buf)
		err := unpackU(&bbuf, &y)
		assert.Nil(t, err)
		assert.Equal(t, y, x)
	}
}
func TestU(t *testing.T) {
	testU[uint8](t)
	testU[uint16](t)
	testU[uint32](t)
	testU[uint64](t)
}
