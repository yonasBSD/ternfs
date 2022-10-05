package bincode

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"math/bits"
)

func PackFixed[V uint8 | uint16 | uint32 | uint64](x V, w io.Writer) error {
	return binary.Write(w, binary.LittleEndian, x)
}

func UnpackFixed[V uint8 | uint16 | uint32 | uint64](x *V, r io.Reader) error {
	return binary.Read(r, binary.LittleEndian, x)
}

func PackU61Var(x uint64, w io.Writer) error {
	bits := 64 - bits.LeadingZeros64(x)
	if bits > 61 {
		return fmt.Errorf("uint64 too large for PackU61Var: %v", x)
	}
	neededBytes := ((bits + 3) + 8 - 1) / 8
	x = (x << 3) | uint64(neededBytes-1)
	bs := make([]byte, 8)
	binary.LittleEndian.PutUint64(bs, x)
	_, err := w.Write(bs[:neededBytes])
	return err
}

func UnpackU61Var(x *uint64, r io.Reader) error {
	bs := make([]byte, 8)
	_, err := io.ReadAtLeast(r, bs[:1], 1)
	if err != nil {
		return err
	}
	remaining := bs[0] & (8 - 1)
	bs[0] = bs[0] & ^uint8(8-1)
	_, err = io.ReadAtLeast(r, bs[1:], int(remaining))
	if err != nil {
		return err
	}
	err = binary.Read(bytes.NewReader(bs), binary.LittleEndian, x)
	if err != nil {
		return err
	}
	*x = *x >> 3
	return nil
}

func PackBytes(bs []byte, w io.Writer) error {
	if len(bs) > 255 {
		return fmt.Errorf("Bytes exceed maximum length of 255 (%v)", len(bs))
	}
	_, err := w.Write([]byte{uint8(len(bs))})
	if err != nil {
		return err
	}
	_, err = w.Write(bs)
	return err
}

func UnpackBytes(bs *[]byte, r io.Reader) error {
	var l uint8
	err := UnpackFixed(&l, r)
	if err != nil {
		return err
	}
	*bs = make([]byte, l)
	_, err = io.ReadAtLeast(r, *bs, int(l))
	return err
}

type Unpackable interface {
	Unpack(io.Reader) error
}

type Packable interface {
	Pack(io.Writer) error
}

func PackToBytes(v Packable) ([]byte, error) {
	var buf bytes.Buffer
	err := v.Pack(&buf)
	if err != nil {
		return nil, err
	}
	return buf.Bytes(), nil
}

func UnpackFromBytes(v Unpackable, data []byte) error {
	return v.Unpack(bytes.NewReader(data))
}
