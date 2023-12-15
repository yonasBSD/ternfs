package lib

import (
	"fmt"
	"sync/atomic"
	"time"
)

type RateLimit struct {
	available uint64
	max       uint64
	wake      chan struct{}
	ticker    time.Ticker
	terminate chan struct{}
}

type RateLimitOpts struct {
	Interval time.Duration
	Amount   uint64
	Buffer   uint64
}

func NewRateLimit(opts0 *RateLimitOpts) *RateLimit {
	opts := *opts0
	if opts.Amount < 1 || opts.Interval < 1 || opts.Buffer < 1 {
		panic(fmt.Errorf("bad options: %+v", opts))
	}
	rl := &RateLimit{
		available: opts.Amount,
		max:       opts.Amount * opts.Buffer,
		wake:      make(chan struct{}),
		ticker:    *time.NewTicker(opts.Interval),
		terminate: make(chan struct{}),
	}
	go func() {
		for {
			select {
			case <-rl.terminate:
				return
			case <-rl.ticker.C:
				for {
					avail := atomic.LoadUint64(&rl.available)
					newAvail := avail + opts.Amount
					if newAvail > rl.max {
						newAvail = rl.max
					}
					if atomic.CompareAndSwapUint64(&rl.available, avail, newAvail) {
						break
					}
				}
				select {
				case rl.wake <- struct{}{}:
				default:
				}
			}
		}
	}()
	return rl
}

func (rl *RateLimit) Close() {
	rl.ticker.Stop()
	rl.terminate <- struct{}{}
}

func (rl *RateLimit) Acquire() {
	// The below is racy (we might miss the wakeup), but we don't
	// care, we'll get the next wakeup. We don't care about pauses
	// so much here. We'd really like futex.
	for {
		for {
			avail := atomic.LoadUint64(&rl.available)
			if avail > 0 {
				if atomic.CompareAndSwapUint64(&rl.available, avail, avail-1) {
					return
				}
			} else {
				break
			}
		}
		// we need to wait for the thing to be replenished
		<-rl.wake
		// wake other waiters, if any
		select {
		case rl.wake <- struct{}{}:
		default:
		}
	}
}
