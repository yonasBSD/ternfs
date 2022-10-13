// Automatically generated with go run bincodegen.
// Run `go generate ./...` from the go/ directory to regenerate it.
package msgs

import "xtx/eggsfs/bincode"

func (v *VisitInodesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(v.BeginId)
}

func (v *VisitInodesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.BeginId); err != nil {
		return err
	}
	return nil
}

func (v *VisitInodesResp) Pack(buf *bincode.Buf) {
	buf.PackU64(v.NextId)
	len1 := len(v.Ids)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		buf.PackU64(v.Ids[i])
	}
}

func (v *VisitInodesResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.NextId); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := buf.UnpackU64(&v.Ids[i]); err != nil {
			return err
		}
	}
	return nil
}

func (v *TransientFile) Pack(buf *bincode.Buf) {
	buf.PackU64(v.Id)
	buf.PackU64(v.DeadlineTime)
}

func (v *TransientFile) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.Id); err != nil {
		return err
	}
	if err := buf.UnpackU64(&v.DeadlineTime); err != nil {
		return err
	}
	return nil
}

func (v *VisitTransientFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(v.BeginId)
}

func (v *VisitTransientFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.BeginId); err != nil {
		return err
	}
	return nil
}

func (v *VisitTransientFilesResp) Pack(buf *bincode.Buf) {
	buf.PackU64(v.NextId)
	len1 := len(v.Files)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Files[i].Pack(buf)
	}
}

func (v *VisitTransientFilesResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.NextId); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Files[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *FileSpansReq) Pack(buf *bincode.Buf) {
	buf.PackU64(v.FileId)
	buf.PackVarU61(v.ByteOffset)
}

func (v *FileSpansReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64(&v.FileId); err != nil {
		return err
	}
	if err := buf.UnpackVarU61(&v.ByteOffset); err != nil {
		return err
	}
	return nil
}

func (v *FetchedBlock) Pack(buf *bincode.Buf) {
	buf.PackFixedBytes(4, v.Ip)
	buf.PackU16(v.Port)
	buf.PackU64(v.BlockId)
	buf.PackU32(v.Crc32)
	buf.PackVarU61(v.Size)
	buf.PackU8(v.Flags)
}

func (v *FetchedBlock) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackFixedBytes(4, &v.Ip); err != nil {
		return err
	}
	if err := buf.UnpackU16(&v.Port); err != nil {
		return err
	}
	if err := buf.UnpackU64(&v.BlockId); err != nil {
		return err
	}
	if err := buf.UnpackU32(&v.Crc32); err != nil {
		return err
	}
	if err := buf.UnpackVarU61(&v.Size); err != nil {
		return err
	}
	if err := buf.UnpackU8(&v.Flags); err != nil {
		return err
	}
	return nil
}

func (v *FetchedSpanHeader) Pack(buf *bincode.Buf) {
	buf.PackVarU61(v.ByteOffset)
	buf.PackU8(v.Parity)
	buf.PackU8(v.StorageClass)
	buf.PackU32(v.Crc32)
	buf.PackVarU61(v.Size)
}

func (v *FetchedSpanHeader) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackVarU61(&v.ByteOffset); err != nil {
		return err
	}
	if err := buf.UnpackU8(&v.Parity); err != nil {
		return err
	}
	if err := buf.UnpackU8(&v.StorageClass); err != nil {
		return err
	}
	if err := buf.UnpackU32(&v.Crc32); err != nil {
		return err
	}
	if err := buf.UnpackVarU61(&v.Size); err != nil {
		return err
	}
	return nil
}

func (v *FileSpansResp) Pack(buf *bincode.Buf) {
	buf.PackVarU61(v.NextOffset)
	len1 := len(v.Spans)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Spans[i].Pack(buf)
	}
}

func (v *FileSpansResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackVarU61(&v.NextOffset); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Spans[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

