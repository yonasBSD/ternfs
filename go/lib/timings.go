package lib

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type Histogram struct {
	// parameters
	growth              float64 // the growth factor for each histogram bin, must be > 1
	invLogGrowth        float64 // 1/ln(factor)
	firstUpperBound     uint64  // the upper bound of the first bin of the histogram
	growthDivUpperBound float64 // growth/firstUpperBound
	// Actual data
	startedAt time.Time
	bins      []uint64
}

type HistogramBin struct {
	UpperBound time.Duration
	Count      uint64
}

func (t *Histogram) Histogram() []HistogramBin {
	bins := make([]HistogramBin, len(t.bins))
	upperBound := float64(t.firstUpperBound)
	for i := 0; i < len(t.bins); i++ {
		bins[i].Count = t.bins[i]
		bins[i].UpperBound = time.Duration(upperBound)
		upperBound *= t.growth
	}
	return bins
}

func HewHistogram(bins int, firstUpperBound time.Duration, growth float64) *Histogram {
	if bins < 1 {
		panic(fmt.Errorf("non-positive bins %d", bins))
	}
	if firstUpperBound < 1 {
		panic(fmt.Errorf("non-positive first upper bound %d", firstUpperBound))
	}
	if growth <= 1.0 {
		panic(fmt.Errorf("growth %v <= 1.0", growth))
	}
	timings := Histogram{
		bins:                make([]uint64, bins),
		growth:              growth,
		invLogGrowth:        1.0 / math.Log(growth),
		firstUpperBound:     uint64(firstUpperBound.Nanoseconds()),
		growthDivUpperBound: growth / float64(firstUpperBound.Nanoseconds()),
	}
	timings.Reset()
	return &timings
}

func (t *Histogram) Reset() {
	for i := range t.bins {
		atomic.StoreUint64(&t.bins[i], 0)
	}
	t.startedAt = time.Now()
}

func (t *Histogram) Add(d time.Duration) {
	inanos := d.Nanoseconds()
	if inanos < 0 {
		return
	}
	nanos := uint64(inanos)
	{
		// bin = floor(log_growth(t*growth/firstUpperBound))
		//     = floor(log(t*growthDivUpperBound) * invLogGrowth)
		bin := int(math.Log(float64(nanos)*t.growthDivUpperBound) * t.invLogGrowth)
		if bin < 0 {
			bin = 0
		}
		if bin >= len(t.bins) {
			bin = len(t.bins) - 1
		}
		atomic.AddUint64(&t.bins[bin], 1)
	}
}

// In these aggregates we're conservative (pick the upper bound)

func (t *Histogram) TotalTime() time.Duration {
	d := time.Duration(0)
	for _, bin := range t.Histogram() {
		d += bin.UpperBound * time.Duration(bin.Count)
	}
	return d
}

func (t *Histogram) Count() uint64 {
	x := uint64(0)
	for _, bin := range t.Histogram() {
		x += bin.Count
	}
	return x
}

func (t *Histogram) Mean() time.Duration {
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

func (t *Histogram) Median() time.Duration {
	bins := t.Histogram()
	totalCount := uint64(0)
	for _, bin := range bins {
		totalCount += bin.Count
	}
	p := float64(0)
	for _, bin := range bins {
		p += float64(bin.Count) / float64(totalCount)
		if p >= 0.5 {
			return bin.UpperBound
		}
	}
	panic("impossible")
}

func (t *Histogram) ToStats(statTime msgs.EggsTime, prefix string) []msgs.Stat {
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
}](prefix string, timings map[K]*Histogram) []msgs.Stat {
	stats := make([]msgs.Stat, 0, len(timings)*2) // count, histogram
	now := msgs.Now()
	for k, t := range timings {
		stats = append(stats, t.ToStats(now, fmt.Sprintf("%s.%s", prefix, k.String()))...)
	}
	return stats
}
