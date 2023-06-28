package lib

import "sync"

// An extremely dumb buffer pool. The assumption is that this is to be used in
// cases where there is a fixed upper bound for the buffers, and most requests
// are going to be for that upper bound.
type BufPool sync.Pool

func NewBufPool() *BufPool {
	pool := BufPool(sync.Pool{
		New: func() any {
			buf := []byte{}
			return &buf
		},
	})
	return &pool
}

// This does _not_ zero the memory in the bufs -- i.e. there might
// be garbage in it.
func (pool *BufPool) Get(l int) *[]byte {
	buf := (*sync.Pool)(pool).Get().(*[]byte)
	if cap(*buf) >= l {
		*buf = (*buf)[:l]
	} else {
		*buf = (*buf)[:cap(*buf)]
		*buf = append(*buf, make([]byte, l-len(*buf))...)
	}
	return buf
}

// Calling `Put` twice will result in the buffer being in the pool twice!
// TODO possibly wrap the buffer into a struct and prevent that mechanically
// by marking the put thing as dirty.
func (pool *BufPool) Put(buf *[]byte) {
	if buf == nil {
		return
	}
	(*sync.Pool)(pool).Put(buf)
}
