package wyhash

// #cgo CFLAGS: -O3
// #include "../../cpp/wyhash/wyhash.h"
import "C"

type Rand struct {
	state uint64
}

func New(seed uint64) *Rand {
	rand := Rand{state: seed}
	return &rand
}

func (r *Rand) Uint64() uint64 {
	return uint64(C.wyhash64((*C.ulong)(&r.state)))
}

func (r *Rand) Uint32() uint32 {
	return uint32(r.Uint64())
}

func (r *Rand) Float64() float64 {
	return float64(r.Uint64()&((1<<53)-1)) / float64(uint64(1<<53))
}

func (r *Rand) Read(p []byte) (int, error) {
	if len(p) == 0 {
		return 0, nil
	}
	C.wyhash64_bytes((*C.ulong)(&r.state), (*C.uchar)(&p[0]), C.ulong(len(p)))
	return len(p), nil
}
