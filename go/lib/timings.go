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

type Timings struct {
	// parameters
	growth              float64 // the growth factor for each histogram bin, must be > 1
	invLogGrowth        float64 // 1/ln(factor)
	firstUpperBound     uint64  // the upper bound of the first bin of the histogram
	growthDivUpperBound float64 // growth/firstUpperBound
	// Actual data. Everything time related is in nanos.
	hist []uint64
	// To compute mean/variance
	count      uint64
	sum        uint64
	sumSquares uint64
}

func (t *Timings) Mean() time.Duration {
	if t.count == 0 {
		return 0
	}
	return time.Duration(t.sum / t.count)
}

func (t *Timings) Stddev() time.Duration {
	if t.count == 0 {
		return 0
	}
	mean := t.sum / t.count
	return time.Duration(math.Sqrt(float64((t.sumSquares / t.count) - mean*mean)))
}

func (t *Timings) TotalTime() time.Duration {
	return time.Duration(t.sum)
}

func (t *Timings) TotalCount() uint64 {
	return t.count
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
		count:               0,
		sum:                 0,
		sumSquares:          0,
	}
	return &timings
}

func (t *Timings) Add(d time.Duration) {
	inanos := d.Nanoseconds()
	if inanos < 0 { // error?
		return
	}
	nanos := uint64(inanos)
	// bin = floor(log_growth(t*growth/firstUpperBound))
	//     = floor(log(t*growthDivUpperBound) * invLogGrowth)
	bin := int(math.Log(float64(nanos)*t.growthDivUpperBound) * t.invLogGrowth)
	if bin < 0 {
		bin = 0
	}
	if bin >= len(t.hist) {
		bin = len(t.hist) - 1
	}
	atomic.AddUint64(&t.count, 1)
	atomic.AddUint64(&t.sum, nanos)
	atomic.AddUint64(&t.sumSquares, nanos*nanos)
	atomic.AddUint64(&t.hist[bin], 1)
}

func (t *Timings) ToStats(time msgs.EggsTime, prefix string) []msgs.Stat {
	stats := make([]msgs.Stat, 0, 3)
	// mean
	var meanBuf [8]byte
	binary.LittleEndian.PutUint64(meanBuf[:], uint64(t.Mean().Nanoseconds()))
	stats = append(stats, msgs.Stat{
		Name:  prefix + ".mean",
		Time:  time,
		Value: meanBuf[:],
	})
	// stddev
	var stddevBuf [8]byte
	binary.LittleEndian.PutUint64(stddevBuf[:], uint64(t.Stddev().Nanoseconds()))
	stats = append(stats, msgs.Stat{
		Name:  prefix + ".stddev",
		Time:  time,
		Value: stddevBuf[:],
	})
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
