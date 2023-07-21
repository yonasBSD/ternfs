package lib

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"sync"
	"sync/atomic"
	"time"
	"xtx/eggsfs/msgs"
)

type Timings struct {
	// parameters
	growth              float64 // the growth factor for each histogram bin, must be > 1
	invLogGrowth        float64 // 1/ln(factor)
	firstUpperBound     uint64  // the upper bound of the first bin of the histogram
	growthDivUpperBound float64 // growth/firstUpperBound
	// Actual data
	hist  []uint64
	total uint64
	// To compute mean/variance
	mu     sync.Mutex
	count  int64
	meanMs float64
	m2MsSq float64
}

func (t *Timings) Mean() time.Duration {
	return time.Duration(t.meanMs * 1e6)
}

func (t *Timings) Stddev() time.Duration {
	if t.count == 0 {
		return 0
	}
	return time.Duration(1e6 * float64(math.Sqrt(float64(t.m2MsSq/float64(t.count)))))
}

func (t *Timings) TotalTime() time.Duration {
	return time.Duration(t.total)
}

func (t *Timings) TotalCount() uint64 {
	return uint64(t.count)
}

type HistogramBin struct {
	UpperBound time.Duration
	Count      uint64
}

func (t *Timings) Histogram() []HistogramBin {
	bins := make([]HistogramBin, len(t.hist))
	upperBound := float64(t.firstUpperBound)
	for i := 0; i < len(t.hist); i++ {
		bins[i].Count = t.hist[i]
		bins[i].UpperBound = time.Duration(upperBound)
		upperBound *= t.growth
	}
	return bins
}

func NewTimings(bins int, firstUpperBound time.Duration, growth float64) *Timings {
	if bins < 1 {
		panic(fmt.Errorf("non-positive bins %d", bins))
	}
	if firstUpperBound < 1 {
		panic(fmt.Errorf("non-positive first upper bound %d", firstUpperBound))
	}
	if growth <= 1.0 {
		panic(fmt.Errorf("growth %v <= 1.0", growth))
	}
	timings := Timings{
		growth:              growth,
		invLogGrowth:        1.0 / math.Log(growth),
		firstUpperBound:     uint64(firstUpperBound.Nanoseconds()),
		growthDivUpperBound: growth / float64(firstUpperBound.Nanoseconds()),
		hist:                make([]uint64, bins),
	}
	return &timings
}

func (t *Timings) Add(d time.Duration) {
	inanos := d.Nanoseconds()
	if inanos < 0 { // error?
		return
	}
	nanos := uint64(inanos)
	// update bin/count
	{
		atomic.AddUint64(&t.total, nanos)
		// bin = floor(log_growth(t*growth/firstUpperBound))
		//     = floor(log(t*growthDivUpperBound) * invLogGrowth)
		bin := int(math.Log(float64(nanos)*t.growthDivUpperBound) * t.invLogGrowth)
		if bin < 0 {
			bin = 0
		}
		if bin >= len(t.hist) {
			bin = len(t.hist) - 1
		}
		atomic.AddUint64(&t.hist[bin], 1)
	}
	// update mean/stddev, see
	// <https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Welford's_online_algorithm>
	{
		t.mu.Lock()
		t.count++
		nanosMs := float64(inanos/1000000) + float64(inanos%1000000)/1e6
		delta := nanosMs - t.meanMs
		t.meanMs += delta / float64(t.count)
		delta2 := nanosMs - t.meanMs
		t.m2MsSq += delta * delta2
		t.mu.Unlock()
	}
}

func (t *Timings) ToStats(time msgs.EggsTime, prefix string) []msgs.Stat {
	stats := make([]msgs.Stat, 0, 2)
	// count/mean/stddev
	t.mu.Lock()
	var countBuf [8 * 3]byte
	binary.LittleEndian.PutUint64(countBuf[8*0:8*1], t.TotalCount())
	binary.LittleEndian.PutUint64(countBuf[8*1:8*2], uint64(t.Mean().Nanoseconds()))
	binary.LittleEndian.PutUint64(countBuf[8*2:8*3], uint64(t.Stddev().Nanoseconds()))
	stats = append(stats, msgs.Stat{
		Name:  prefix + ".count",
		Time:  time,
		Value: countBuf[:],
	})
	t.mu.Unlock()
	// hist
	hist := t.Histogram()
	histBuf := bytes.NewBuffer(make([]byte, 0, len(hist)*8*2))
	for _, bin := range hist {
		binary.Write(histBuf, binary.LittleEndian, bin.UpperBound.Nanoseconds())
		binary.Write(histBuf, binary.LittleEndian, bin.Count)
	}
	stats = append(stats, msgs.Stat{
		Name:  prefix + ".histogram",
		Time:  time,
		Value: histBuf.Bytes(),
	})
	return stats
}

func UnpackHistogram(data []byte) ([]HistogramBin, error) {
	if len(data)%(8+8) != 0 {
		return nil, fmt.Errorf("bad size %v, expected a multiple of %v", len(data), 8+8)
	}
	l := len(data) / 16
	bins := make([]HistogramBin, l)
	r := bytes.NewReader(data)
	for i := 0; i < l; i++ {
		var upperBound uint64
		if err := binary.Read(r, binary.LittleEndian, &upperBound); err != nil {
			return nil, err
		}
		bins[i].UpperBound = time.Duration(upperBound)
		if err := binary.Read(r, binary.LittleEndian, &bins[i].Count); err != nil {
			return nil, err
		}
	}
	return bins, nil
}

func TimingsToStats[K interface {
	~uint8
	fmt.Stringer
}](prefix string, allKeys []K, timings []Timings) []msgs.Stat {
	stats := make([]msgs.Stat, 0, len(allKeys)*3) // mean, stddev, histogram
	now := msgs.Now()
	for _, k := range allKeys {
		stats = append(stats, timings[int(k)].ToStats(now, fmt.Sprintf("%s.%s", prefix, k.String()))...)
	}
	return stats
}
