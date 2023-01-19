package bincode

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"math/bits"
)

type Packable interface {
	Pack(w io.Writer) error
}

func PackScalar[V bool | uint8 | uint16 | uint32 | uint64](w io.Writer, x V) error {
	return binary.Write(w, binary.LittleEndian, x)
}

func PackVarU61(w io.Writer, x uint64) error {
	bits := bits.Len64(x)
	if bits > 61 {
		panic(fmt.Sprintf("uint64 too large for PackU61Var: %v", x))
	}
	neededBytes := ((bits + 3) + 8 - 1) / 8
	x = (x << 3) | uint64(neededBytes-1)
	first := true
	var data [8]byte
	i := 0
	for first || x > 0 {
		first = false
		data[i] = byte(x)
		x = x >> 8
		i++
	}
	_, err := w.Write(data[:i])
	return err
}

func PackBytes(w io.Writer, bs []byte) error {
	if len(bs) > 255 {
		panic(fmt.Sprintf("bytes length exceed 255: %v", len(bs)))
	}
	if err := PackScalar(w, uint8(len(bs))); err != nil {
		return err
	}
	if _, err := w.Write(bs); err != nil {
		return err
	}
	return nil
}

func PackFixedBytes(w io.Writer, l int, bs []byte) error {
	if len(bs) != l {
		panic(fmt.Sprintf("expecting fixed bytes of len %v, got %v instead", l, len(bs)))
	}
	_, err := w.Write(bs)
	return err
}

func PackLength(w io.Writer, l int) error {
	if l > math.MaxUint16 {
		panic(fmt.Sprintf("len %d exceeds max length %d", l, math.MaxUint16))
	}
	return PackScalar(w, uint16(l))
}

func UnpackScalar[V bool | uint8 | uint16 | uint32 | uint64](r io.Reader, x *V) error {
	return binary.Read(r, binary.LittleEndian, x)
}

func UnpackVarU61(r io.Reader, x *uint64) error {
	var b uint8
	if err := UnpackScalar(r, &b); err != nil {
		return err
	}
	remaining := b & (8 - 1)
	var buf [8]byte
	if _, err := io.ReadFull(r, buf[:remaining]); err != nil {
		return err
	}
	*x = uint64(b)
	for i := 0; i <= int(remaining); i++ {
		*x = *x | (uint64(buf[i]) << ((i + 1) * 8))
	}
	*x = *x >> 3
	return nil
}

// This function will discard what's in `data`, and just
// set the pointer to a slice of the backing `buf`.
func UnpackBytes(r io.Reader, data *[]byte) error {
	var l uint8
	if err := UnpackScalar(r, &l); err != nil {
		return err
	}
	*data = make([]byte, l)
	if _, err := io.ReadFull(r, *data); err != nil {
		return err
	}
	return nil
}

// The intent with this one (as opposed with `UnpackBytes`) is
// to use it with a fixedsized array, e.g.
//
//     var x [4]byte
//     buf.UnpackFixedBytes(4, x[:])
//
// Which is why it does not take a pointer like `UnpackBytes`,
// and copies the data.
func UnpackFixedBytes(r io.Reader, l int, data []byte) error {
	if len(data) != l {
		panic(fmt.Sprintf("expecting fixed bytes of len %v, got %v instead", l, len(data)))
	}
	if _, err := io.ReadFull(r, data); err != nil {
		return err
	}
	return nil
}

func UnpackString(r io.Reader, data *string) error {
	var bs []byte
	if err := UnpackBytes(r, &bs); err != nil {
		return err
	}
	*data = string(bs)
	return nil
}

func UnpackLength(r io.Reader, l *int) error {
	var l16 uint16
	if err := UnpackScalar(r, &l16); err != nil {
		return err
	}
	*l = int(l16)
	return nil
}

type Unpackable interface {
	Unpack(r io.Reader) error
}

// Useful in codegen, weirdly go does not include something like vector::resize.
//
// This does not resize exponentially, which is annoying, but better than allocating
// every time.
func EnsureLength[T any](xs *[]T, l int) {
	if *xs == nil || cap(*xs) < l {
		*xs = make([]T, l)
	} else {
		*xs = (*xs)[:l]
	}
}

func Pack(v Packable) []byte {
	buf := bytes.NewBuffer([]byte{})
	if err := v.Pack(buf); err != nil {
		panic(err)
	}
	return buf.Bytes()
}

func Unpack(data []byte, v Unpackable) error {
	buf := bytes.NewReader(data)
	if err := v.Unpack(buf); err != nil {
		return err
	}
	if buf.Len() != 0 {
		return fmt.Errorf("%v leftover bytes remaining after unpacking", buf.Len())
	}
	return nil
}
