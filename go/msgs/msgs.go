package msgs

import "xtx/eggsfs/bincode"

//go:generate go run ../bincodegen

const (
	INLINE_STORAGE    uint8 = 0
	ZERO_FILL_STORAGE uint8 = 1
)

type VisitInodesReq struct {
	BeginId uint64
}

type VisitInodesResp struct {
	NextId uint64
	Ids    []uint64
}

type VisitTransientFilesReq struct {
	BeginId uint64
}

type TransientFile struct {
	Id           uint64
	DeadlineTime uint64
}
type VisitTransientFilesResp struct {
	NextId uint64
	Files  []TransientFile
}

type FileSpansReq struct {
	FileId     uint64
	ByteOffset uint64 `bincode:"varint"`
}

type FetchedBlock struct {
	Ip      []byte `bincode:"fixed4"`
	Port    uint16
	BlockId uint64
	Crc32   uint32
	Size    uint64 `bincode:"varint"`
	Flags   uint8
}

type FetchedSpanHeader struct {
	ByteOffset   uint64 `bincode:"varint"`
	Parity       uint8
	StorageClass uint8
	Crc32        uint32
	Size         uint64 `bincode:"varint"`
}

// If the storage class is zero-filled, we get no body.
// If the storage class is inline, we get BodyBytes.
// If the storage class is not inline, we get BodyBlocks.
type FetchedSpan struct {
	Header     FetchedSpanHeader
	BodyBytes  []byte
	BodyBlocks []FetchedBlock
}

func (span *FetchedSpan) Pack(buf *bincode.Buf) {
	span.Header.Pack(buf)
	switch span.Header.StorageClass {
	case ZERO_FILL_STORAGE:
		if span.BodyBytes != nil || span.BodyBlocks != nil {
			panic("unexpected body for zero-filled span!")
		}
	case INLINE_STORAGE:
		if span.BodyBlocks != nil {
			panic("unexpected blocks for inline span!")
		}
		buf.PackBytes(span.BodyBytes)
	default:
		if span.BodyBytes != nil {
			panic("unexpected bytes for non-inline span!")
		}
		buf.PackLength(len(span.BodyBlocks))
		for _, block := range span.BodyBlocks {
			block.Pack(buf)
		}
	}
}

func (span *FetchedSpan) Unpack(buf *bincode.Buf) error {
	if err := span.Header.Unpack(buf); err != nil {
		return err
	}
	switch span.Header.StorageClass {
	case ZERO_FILL_STORAGE:
		span.BodyBlocks = nil
		span.BodyBytes = nil
	case INLINE_STORAGE:
		if err := buf.UnpackBytes(&span.BodyBytes); err != nil {
			return err
		}
		span.BodyBlocks = nil
	default:
		span.BodyBytes = nil
		var l int
		if err := buf.UnpackLength(&l); err != nil {
			return err
		}
		span.BodyBlocks = make([]FetchedBlock, l)
		for _, block := range span.BodyBlocks {
			if err := block.Unpack(buf); err != nil {
				return err
			}
		}
	}
	return nil
}

type FileSpansResp struct {
	NextOffset uint64 `bincode:"varint"`
	Spans      []FetchedSpan
}
