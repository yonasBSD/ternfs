package lib

import (
	"math/bits"
	"sync/atomic"
	"time"
)

const BUCKETS = 33

type Timings struct {
	counts     [BUCKETS]uint64
	totalNanos uint64
}

func (t *Timings) Add(d time.Duration) {
	nanos := d.Nanoseconds()
	bucket := (64 - bits.LeadingZeros64(uint64(nanos))) - 5
	if bucket < 0 {
		bucket = 0
	}
	if bucket >= BUCKETS {
		bucket = BUCKETS - 1
	}
	atomic.AddUint64(&t.totalNanos, uint64(nanos))
	atomic.AddUint64(&t.counts[bucket], 1)
}

func (t *Timings) Buckets() int {
	return BUCKETS
}

func (t *Timings) Bucket(i int) (lowerBound time.Duration, count uint64, upperBound time.Duration) {
	count = atomic.LoadUint64(&t.counts[i])
	upperBound = time.Duration(uint64(1) << (i + 5))
	if i == 0 {
		lowerBound = time.Duration(0)
	} else {
		lowerBound = time.Duration(uint64(1) << (i + 4))
	}
	return lowerBound, count, upperBound
}

func (t *Timings) TotalCount() uint64 {
	count := uint64(0)
	for i := range t.counts {
		count += atomic.LoadUint64(&t.counts[i])
	}
	return count
}

func (t *Timings) TotalTime() time.Duration {
	return time.Duration(atomic.LoadUint64(&t.totalNanos))
}
