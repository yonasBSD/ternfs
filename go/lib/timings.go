package lib

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type Timings struct {
	histo Histogram
	// Actual data
	startedAt time.Time
	bins      []uint64
}

type HistogramBin struct {
	UpperBound time.Duration
	Count      uint64
}

func (t *Timings) Histogram() []HistogramBin {
	bins := make([]HistogramBin, len(t.bins))
	for i, upperBound := range t.histo.Bins() {
		bins[i].Count = t.bins[i]
		bins[i].UpperBound = time.Duration(upperBound)
	}
	return bins
}

func NewTimings(bins int, firstUpperBound time.Duration, growth float64) *Timings {
	if firstUpperBound < 1 {
		panic(fmt.Errorf("non-positive first upper bound %d", firstUpperBound))
	}
	timings := Timings{
		bins:  make([]uint64, bins),
		histo: *NewHistogram(bins, uint64(firstUpperBound), growth),
	}
	timings.Reset()
	return &timings
}

func (t *Timings) Reset() {
	for i := range t.bins {
		atomic.StoreUint64(&t.bins[i], 0)
	}
	t.startedAt = time.Now()
}

func (t *Timings) Add(d time.Duration) {
	inanos := d.Nanoseconds()
	if inanos < 0 {
		return
	}
	atomic.AddUint64(&t.bins[t.histo.WhichBin(uint64(d))], 1)
}

// In these aggregates we're conservative (pick the upper bound)

func (t *Timings) TotalTime() time.Duration {
	d := time.Duration(0)
	for _, bin := range t.Histogram() {
		d += bin.UpperBound * time.Duration(bin.Count)
	}
	return d
}

func (t *Timings) Count() uint64 {
	x := uint64(0)
	for _, bin := range t.Histogram() {
		x += bin.Count
	}
	return x
}

func (t *Timings) Mean() time.Duration {
	bins := t.Histogram()
	totalCount := uint64(0)
	for _, bin := range bins {
		totalCount += bin.Count
	}
	x := float64(0)
	for _, bin := range bins {
		x += float64(bin.UpperBound) * (float64(bin.Count) / float64(totalCount))
	}
	return time.Duration(x)
}

func (t *Timings) P(target float64) time.Duration {
	bins := t.Histogram()
	totalCount := uint64(0)
	for _, bin := range bins {
		totalCount += bin.Count
	}
	p := float64(0)
	for _, bin := range bins {
		p += float64(bin.Count) / float64(totalCount)
		if p >= target {
			return bin.UpperBound
		}
	}
	panic("impossible")
}

func (t *Timings) Median() time.Duration {
	return t.P(0.5)
}

func (t *Timings) ToStats(statTime msgs.EggsTime, prefix string) []msgs.Stat {
	stats := make([]msgs.Stat, 0, 1)
	// hist
	hist := t.Histogram()
	histBuf := bytes.NewBuffer(make([]byte, 0, 8+len(hist)*8*2))
	binary.Write(histBuf, binary.LittleEndian, time.Since(t.startedAt))
	for _, bin := range hist {
		binary.Write(histBuf, binary.LittleEndian, bin.UpperBound.Nanoseconds())
		binary.Write(histBuf, binary.LittleEndian, bin.Count)
	}
	stats = append(stats, msgs.Stat{
		Name:  prefix + ".latency",
		Time:  statTime,
		Value: histBuf.Bytes(),
	})
	return stats
}

func TimingsToStats[K interface {
	~uint8
	fmt.Stringer
}](prefix string, timings map[K]*Timings) []msgs.Stat {
	stats := make([]msgs.Stat, 0, len(timings)*2) // count, histogram
	now := msgs.Now()
	for k, t := range timings {
		stats = append(stats, t.ToStats(now, fmt.Sprintf("%s.%s", prefix, k.String()))...)
	}
	return stats
}
