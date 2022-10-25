package bincode

import (
	"encoding/binary"
	"fmt"
	"math"
	"math/bits"
)

type Buf []byte

type Packable interface {
	Pack(buf *Buf)
}

func (buf *Buf) PackU8(x uint8) {
	(*buf)[0] = x
	*buf = (*buf)[1:]
}
func (buf *Buf) PackBool(x bool) {
	if x {
		(*buf)[0] = 1
	} else {
		(*buf)[0] = 0
	}
	*buf = (*buf)[1:]
}
func (buf *Buf) PackU16(x uint16) {
	binary.LittleEndian.PutUint16(*buf, x)
	*buf = (*buf)[2:]
}
func (buf *Buf) PackU32(x uint32) {
	binary.LittleEndian.PutUint32(*buf, x)
	*buf = (*buf)[4:]
}
func (buf *Buf) PackU64(x uint64) {
	binary.LittleEndian.PutUint64(*buf, x)
	*buf = (*buf)[8:]
}

func (buf *Buf) PackVarU61(x uint64) {
	bits := 64 - bits.LeadingZeros64(x)
	if bits > 61 {
		panic(fmt.Sprintf("uint64 too large for PackU61Var: %v", x))
	}
	neededBytes := ((bits + 3) + 8 - 1) / 8
	x = (x << 3) | uint64(neededBytes-1)
	first := true
	for first || x > 0 {
		first = false
		(*buf)[0] = byte(x)
		x = x >> 8
		(*buf) = (*buf)[1:]
	}
}

func (buf *Buf) PackBytes(bs []byte) {
	if len(bs) > 255 {
		panic(fmt.Sprintf("bytes length exceed 255: %v", len(bs)))
	}
	buf.PackU8(uint8(len(bs)))
	if copy(*buf, bs) != len(bs) {
		panic("not enough space for bytes")
	}
	*buf = (*buf)[len(bs):]
}

func (buf *Buf) PackFixedBytes(l int, bs []byte) {
	if len(bs) != l {
		panic(fmt.Sprintf("expecting fixed bytes of len %v, got %v instead", l, len(bs)))
	}
	if copy(*buf, bs) != l {
		panic("not enough space for fixed bytes")
	}
	*buf = (*buf)[l:]
}

func (buf *Buf) PackLength(l int) {
	if l > math.MaxUint16 {
		panic(fmt.Sprintf("len %d exceeds max length %d", l, math.MaxUint16))
	}
	buf.PackU16(uint16(l))
}

// Writes into `out`. Returns how much was written.
//
// Will panic if `out` is not big enough.
func PackToBytes(out []byte, v Packable) int {
	buf := Buf(out)
	v.Pack(&buf)
	return len(out) - len(buf)
}

// A variant of `PackToBytes` which automatically updates the pointer
// to the appropriately sized slice
func PackIntoBytes(out *[]byte, v Packable) {
	written := PackToBytes(*out, v)
	*out = (*out)[:written]
}

func (buf *Buf) hasBytes(x int) error {
	if len(*buf) < x {
		return fmt.Errorf("ran out of space (needed %v, got %v)", x, len(*buf))
	}
	return nil
}

func (buf *Buf) UnpackU8(x *uint8) error {
	if err := buf.hasBytes(1); err != nil {
		return err
	}
	*x = (*buf)[0]
	(*buf) = (*buf)[1:]
	return nil
}
func (buf *Buf) UnpackBool(x *bool) error {
	var y uint8
	if err := buf.UnpackU8(&y); err != nil {
		return err
	}
	if y != 0 && y != 1 {
		return fmt.Errorf("expected 0 and 1 for bool, got %d", y)
	}
	if y == 0 {
		*x = false
	} else {
		*x = true
	}
	return nil
}
func (buf *Buf) UnpackU16(x *uint16) error {
	if err := buf.hasBytes(2); err != nil {
		return err
	}
	*x = binary.LittleEndian.Uint16(*buf)
	(*buf) = (*buf)[2:]
	return nil
}
func (buf *Buf) UnpackU32(x *uint32) error {
	if err := buf.hasBytes(4); err != nil {
		return err
	}
	*x = binary.LittleEndian.Uint32(*buf)
	(*buf) = (*buf)[4:]
	return nil
}
func (buf *Buf) UnpackU64(x *uint64) error {
	if err := buf.hasBytes(8); err != nil {
		return err
	}
	*x = binary.LittleEndian.Uint64(*buf)
	(*buf) = (*buf)[8:]
	return nil
}

func (buf *Buf) UnpackVarU61(x *uint64) error {
	var b uint8
	if err := buf.UnpackU8(&b); err != nil {
		return err
	}
	remaining := b & (8 - 1)
	if err := buf.hasBytes(int(remaining)); err != nil {
		return err
	}
	*x = uint64(b)
	for i := 1; i <= int(remaining); i++ {
		if err := buf.UnpackU8(&b); err != nil {
			return err
		}
		*x = *x | (uint64(b) << (i * 8))
	}
	*x = *x >> 3
	return nil
}

func (buf *Buf) UnpackBytes(data *[]byte) error {
	var l uint8
	if err := buf.UnpackU8(&l); err != nil {
		return err
	}
	if err := buf.hasBytes(int(l)); err != nil {
		return err
	}
	*data = make([]byte, l)
	copy(*data, *buf)
	*buf = (*buf)[l:]
	return nil
}

func (buf *Buf) UnpackString(data *string) error {
	var bs []byte
	if err := buf.UnpackBytes(&bs); err != nil {
		return err
	}
	*data = string(bs)
	return nil
}

func (buf *Buf) UnpackFixedBytes(l int, data []byte) error {
	if len(data) != l {
		panic(fmt.Sprintf("expecting fixed bytes of len %v, got %v instead", l, len(data)))
	}
	if err := buf.hasBytes(l); err != nil {
		return err
	}
	copy(data, *buf)
	*buf = (*buf)[l:]
	return nil
}

func (buf *Buf) UnpackLength(l *int) error {
	var l16 uint16
	if err := buf.UnpackU16(&l16); err != nil {
		return err
	}
	*l = int(l16)
	return nil
}

// Useful in codegen, weirdly go does not include something like vector::resize.
// This does not resize exponentially, which is annoying, but better than allocating
// every time.
func EnsureLength[T any](xs *[]T, l int) {
	if *xs == nil || cap(*xs) < l {
		*xs = make([]T, l)
	} else {
		*xs = (*xs)[:l]
	}
}

type Unpackable interface {
	Unpack(buf *Buf) error
}

// Note that this function errors if we don't consume all the input.
func UnpackFromBytes(v Unpackable, data []byte) error {
	buf := Buf(data)
	if err := v.Unpack(&buf); err != nil {
		return err
	}
	if len(buf) != 0 {
		return fmt.Errorf("%v leftover bytes remaining after unpacking", len(buf))
	}
	return nil
}

type Bincodable interface {
	Packable
	Unpackable
}
