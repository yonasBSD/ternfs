// Automatically generated with go run bincodegen.
// Run `go generate ./...` from the go/ directory to regenerate it.
package msgs

import "fmt"
import "xtx/eggsfs/bincode"

const (
	INTERNAL_ERROR ErrCode = 10
	FATAL_ERROR ErrCode = 11
	TIMEOUT ErrCode = 12
	MALFORMED_REQUEST ErrCode = 13
	MALFORMED_RESPONSE ErrCode = 14
	NOT_AUTHORISED ErrCode = 15
	UNRECOGNIZED_REQUEST ErrCode = 16
	FILE_NOT_FOUND ErrCode = 17
	DIRECTORY_NOT_FOUND ErrCode = 18
	NAME_NOT_FOUND ErrCode = 19
	EDGE_NOT_FOUND ErrCode = 20
	EDGE_IS_LOCKED ErrCode = 21
	TYPE_IS_DIRECTORY ErrCode = 22
	TYPE_IS_NOT_DIRECTORY ErrCode = 23
	BAD_COOKIE ErrCode = 24
	INCONSISTENT_STORAGE_CLASS_PARITY ErrCode = 25
	LAST_SPAN_STATE_NOT_CLEAN ErrCode = 26
	COULD_NOT_PICK_BLOCK_SERVICES ErrCode = 27
	BAD_SPAN_BODY ErrCode = 28
	SPAN_NOT_FOUND ErrCode = 29
	BLOCK_SERVICE_NOT_FOUND ErrCode = 30
	CANNOT_CERTIFY_BLOCKLESS_SPAN ErrCode = 31
	BAD_NUMBER_OF_BLOCKS_PROOFS ErrCode = 32
	BAD_BLOCK_PROOF ErrCode = 33
	CANNOT_OVERRIDE_NAME ErrCode = 34
	NAME_IS_LOCKED ErrCode = 35
	MTIME_IS_TOO_RECENT ErrCode = 36
	MISMATCHING_TARGET ErrCode = 37
	MISMATCHING_OWNER ErrCode = 38
	MISMATCHING_CREATION_TIME ErrCode = 39
	DIRECTORY_NOT_EMPTY ErrCode = 40
	FILE_IS_TRANSIENT ErrCode = 41
	OLD_DIRECTORY_NOT_FOUND ErrCode = 42
	NEW_DIRECTORY_NOT_FOUND ErrCode = 43
	LOOP_IN_DIRECTORY_RENAME ErrCode = 44
	DIRECTORY_HAS_OWNER ErrCode = 45
	FILE_IS_NOT_TRANSIENT ErrCode = 46
	FILE_NOT_EMPTY ErrCode = 47
	CANNOT_REMOVE_ROOT_DIRECTORY ErrCode = 48
	FILE_EMPTY ErrCode = 49
	CANNOT_REMOVE_DIRTY_SPAN ErrCode = 50
	BAD_SHARD ErrCode = 51
	BAD_NAME ErrCode = 52
	MORE_RECENT_SNAPSHOT_EDGE ErrCode = 53
	MORE_RECENT_CURRENT_EDGE ErrCode = 54
	BAD_DIRECTORY_INFO ErrCode = 55
	DEADLINE_NOT_PASSED ErrCode = 56
	SAME_SOURCE_AND_DESTINATION ErrCode = 57
	SAME_DIRECTORIES ErrCode = 58
	SAME_SHARD ErrCode = 59
)

func (err ErrCode) String() string {
	switch err {
	case 10:
		return "INTERNAL_ERROR"
	case 11:
		return "FATAL_ERROR"
	case 12:
		return "TIMEOUT"
	case 13:
		return "MALFORMED_REQUEST"
	case 14:
		return "MALFORMED_RESPONSE"
	case 15:
		return "NOT_AUTHORISED"
	case 16:
		return "UNRECOGNIZED_REQUEST"
	case 17:
		return "FILE_NOT_FOUND"
	case 18:
		return "DIRECTORY_NOT_FOUND"
	case 19:
		return "NAME_NOT_FOUND"
	case 20:
		return "EDGE_NOT_FOUND"
	case 21:
		return "EDGE_IS_LOCKED"
	case 22:
		return "TYPE_IS_DIRECTORY"
	case 23:
		return "TYPE_IS_NOT_DIRECTORY"
	case 24:
		return "BAD_COOKIE"
	case 25:
		return "INCONSISTENT_STORAGE_CLASS_PARITY"
	case 26:
		return "LAST_SPAN_STATE_NOT_CLEAN"
	case 27:
		return "COULD_NOT_PICK_BLOCK_SERVICES"
	case 28:
		return "BAD_SPAN_BODY"
	case 29:
		return "SPAN_NOT_FOUND"
	case 30:
		return "BLOCK_SERVICE_NOT_FOUND"
	case 31:
		return "CANNOT_CERTIFY_BLOCKLESS_SPAN"
	case 32:
		return "BAD_NUMBER_OF_BLOCKS_PROOFS"
	case 33:
		return "BAD_BLOCK_PROOF"
	case 34:
		return "CANNOT_OVERRIDE_NAME"
	case 35:
		return "NAME_IS_LOCKED"
	case 36:
		return "MTIME_IS_TOO_RECENT"
	case 37:
		return "MISMATCHING_TARGET"
	case 38:
		return "MISMATCHING_OWNER"
	case 39:
		return "MISMATCHING_CREATION_TIME"
	case 40:
		return "DIRECTORY_NOT_EMPTY"
	case 41:
		return "FILE_IS_TRANSIENT"
	case 42:
		return "OLD_DIRECTORY_NOT_FOUND"
	case 43:
		return "NEW_DIRECTORY_NOT_FOUND"
	case 44:
		return "LOOP_IN_DIRECTORY_RENAME"
	case 45:
		return "DIRECTORY_HAS_OWNER"
	case 46:
		return "FILE_IS_NOT_TRANSIENT"
	case 47:
		return "FILE_NOT_EMPTY"
	case 48:
		return "CANNOT_REMOVE_ROOT_DIRECTORY"
	case 49:
		return "FILE_EMPTY"
	case 50:
		return "CANNOT_REMOVE_DIRTY_SPAN"
	case 51:
		return "BAD_SHARD"
	case 52:
		return "BAD_NAME"
	case 53:
		return "MORE_RECENT_SNAPSHOT_EDGE"
	case 54:
		return "MORE_RECENT_CURRENT_EDGE"
	case 55:
		return "BAD_DIRECTORY_INFO"
	case 56:
		return "DEADLINE_NOT_PASSED"
	case 57:
		return "SAME_SOURCE_AND_DESTINATION"
	case 58:
		return "SAME_DIRECTORIES"
	case 59:
		return "SAME_SHARD"
	default:
		return fmt.Sprintf("ErrCode(%d)", err)
	}
}

func (k ShardMessageKind) String() string {
	switch k {
	case 1:
		return "LOOKUP"
	case 2:
		return "STAT_FILE"
	case 10:
		return "STAT_TRANSIENT_FILE"
	case 8:
		return "STAT_DIRECTORY"
	case 3:
		return "READ_DIR"
	case 4:
		return "CONSTRUCT_FILE"
	case 5:
		return "ADD_SPAN_INITIATE"
	case 6:
		return "ADD_SPAN_CERTIFY"
	case 7:
		return "LINK_FILE"
	case 12:
		return "SOFT_UNLINK_FILE"
	case 13:
		return "FILE_SPANS"
	case 14:
		return "SAME_DIRECTORY_RENAME"
	case 15:
		return "SET_DIRECTORY_INFO"
	case 9:
		return "SNAPSHOT_LOOKUP"
	case 21:
		return "VISIT_DIRECTORIES"
	case 32:
		return "VISIT_FILES"
	case 22:
		return "VISIT_TRANSIENT_FILES"
	case 33:
		return "FULL_READ_DIR"
	case 23:
		return "REMOVE_NON_OWNED_EDGE"
	case 24:
		return "SAME_SHARD_HARD_FILE_UNLINK"
	case 25:
		return "REMOVE_SPAN_INITIATE"
	case 26:
		return "REMOVE_SPAN_CERTIFY"
	case 34:
		return "SWAP_BLOCKS"
	case 35:
		return "BLOCK_SERVICE_FILES"
	case 36:
		return "REMOVE_INODE"
	case 128:
		return "CREATE_DIRECTORY_INODE"
	case 129:
		return "SET_DIRECTORY_OWNER"
	case 137:
		return "REMOVE_DIRECTORY_OWNER"
	case 130:
		return "CREATE_LOCKED_CURRENT_EDGE"
	case 131:
		return "LOCK_CURRENT_EDGE"
	case 132:
		return "UNLOCK_CURRENT_EDGE"
	case 134:
		return "REMOVE_OWNED_SNAPSHOT_FILE_EDGE"
	case 135:
		return "MAKE_FILE_TRANSIENT"
	default:
		return fmt.Sprintf("ShardMessageKind(%d)", k)
	}
}


const (
	LOOKUP ShardMessageKind = 0x1
	STAT_FILE ShardMessageKind = 0x2
	STAT_TRANSIENT_FILE ShardMessageKind = 0xA
	STAT_DIRECTORY ShardMessageKind = 0x8
	READ_DIR ShardMessageKind = 0x3
	CONSTRUCT_FILE ShardMessageKind = 0x4
	ADD_SPAN_INITIATE ShardMessageKind = 0x5
	ADD_SPAN_CERTIFY ShardMessageKind = 0x6
	LINK_FILE ShardMessageKind = 0x7
	SOFT_UNLINK_FILE ShardMessageKind = 0xC
	FILE_SPANS ShardMessageKind = 0xD
	SAME_DIRECTORY_RENAME ShardMessageKind = 0xE
	SET_DIRECTORY_INFO ShardMessageKind = 0xF
	SNAPSHOT_LOOKUP ShardMessageKind = 0x9
	VISIT_DIRECTORIES ShardMessageKind = 0x15
	VISIT_FILES ShardMessageKind = 0x20
	VISIT_TRANSIENT_FILES ShardMessageKind = 0x16
	FULL_READ_DIR ShardMessageKind = 0x21
	REMOVE_NON_OWNED_EDGE ShardMessageKind = 0x17
	SAME_SHARD_HARD_FILE_UNLINK ShardMessageKind = 0x18
	REMOVE_SPAN_INITIATE ShardMessageKind = 0x19
	REMOVE_SPAN_CERTIFY ShardMessageKind = 0x1A
	SWAP_BLOCKS ShardMessageKind = 0x22
	BLOCK_SERVICE_FILES ShardMessageKind = 0x23
	REMOVE_INODE ShardMessageKind = 0x24
	CREATE_DIRECTORY_INODE ShardMessageKind = 0x80
	SET_DIRECTORY_OWNER ShardMessageKind = 0x81
	REMOVE_DIRECTORY_OWNER ShardMessageKind = 0x89
	CREATE_LOCKED_CURRENT_EDGE ShardMessageKind = 0x82
	LOCK_CURRENT_EDGE ShardMessageKind = 0x83
	UNLOCK_CURRENT_EDGE ShardMessageKind = 0x84
	REMOVE_OWNED_SNAPSHOT_FILE_EDGE ShardMessageKind = 0x86
	MAKE_FILE_TRANSIENT ShardMessageKind = 0x87
)

func (k CDCMessageKind) String() string {
	switch k {
	case 1:
		return "MAKE_DIRECTORY"
	case 2:
		return "RENAME_FILE"
	case 3:
		return "SOFT_UNLINK_DIRECTORY"
	case 4:
		return "RENAME_DIRECTORY"
	case 5:
		return "HARD_UNLINK_DIRECTORY"
	case 6:
		return "CROSS_SHARD_HARD_UNLINK_FILE"
	default:
		return fmt.Sprintf("CDCMessageKind(%d)", k)
	}
}


const (
	MAKE_DIRECTORY CDCMessageKind = 0x1
	RENAME_FILE CDCMessageKind = 0x2
	SOFT_UNLINK_DIRECTORY CDCMessageKind = 0x3
	RENAME_DIRECTORY CDCMessageKind = 0x4
	HARD_UNLINK_DIRECTORY CDCMessageKind = 0x5
	CROSS_SHARD_HARD_UNLINK_FILE CDCMessageKind = 0x6
)

func (v *LookupReq) ShardRequestKind() ShardMessageKind {
	return LOOKUP
}

func (v *LookupReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
}

func (v *LookupReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LookupResp) ShardResponseKind() ShardMessageKind {
	return LOOKUP
}

func (v *LookupResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *LookupResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileReq) ShardRequestKind() ShardMessageKind {
	return STAT_FILE
}

func (v *StatFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *StatFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileResp) ShardResponseKind() ShardMessageKind {
	return STAT_FILE
}

func (v *StatFileResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Mtime))
	buf.PackU64(uint64(v.Size))
}

func (v *StatFileResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.Size)); err != nil {
		return err
	}
	return nil
}

func (v *StatTransientFileReq) ShardRequestKind() ShardMessageKind {
	return STAT_TRANSIENT_FILE
}

func (v *StatTransientFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *StatTransientFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatTransientFileResp) ShardResponseKind() ShardMessageKind {
	return STAT_TRANSIENT_FILE
}

func (v *StatTransientFileResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Mtime))
	buf.PackU64(uint64(v.Size))
	buf.PackBytes([]byte(v.Note))
}

func (v *StatTransientFileResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.Size)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Note); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryReq) ShardRequestKind() ShardMessageKind {
	return STAT_DIRECTORY
}

func (v *StatDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *StatDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryResp) ShardResponseKind() ShardMessageKind {
	return STAT_DIRECTORY
}

func (v *StatDirectoryResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Mtime))
	buf.PackU64(uint64(v.Owner))
	buf.PackBytes([]byte(v.Info))
}

func (v *StatDirectoryResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.Owner)); err != nil {
		return err
	}
	if err := buf.UnpackBytes((*[]byte)(&v.Info)); err != nil {
		return err
	}
	return nil
}

func (v *ReadDirReq) ShardRequestKind() ShardMessageKind {
	return READ_DIR
}

func (v *ReadDirReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackU64(uint64(v.StartHash))
}

func (v *ReadDirReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.StartHash)); err != nil {
		return err
	}
	return nil
}

func (v *ReadDirResp) ShardResponseKind() ShardMessageKind {
	return READ_DIR
}

func (v *ReadDirResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NextHash))
	len1 := len(v.Results)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Results[i].Pack(buf)
	}
}

func (v *ReadDirResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NextHash)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Results, len1)
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *ConstructFileReq) ShardRequestKind() ShardMessageKind {
	return CONSTRUCT_FILE
}

func (v *ConstructFileReq) Pack(buf *bincode.Buf) {
	buf.PackU8(uint8(v.Type))
	buf.PackBytes([]byte(v.Note))
}

func (v *ConstructFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU8((*uint8)(&v.Type)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Note); err != nil {
		return err
	}
	return nil
}

func (v *ConstructFileResp) ShardResponseKind() ShardMessageKind {
	return CONSTRUCT_FILE
}

func (v *ConstructFileResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackFixedBytes(8, v.Cookie[:])
}

func (v *ConstructFileResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *AddSpanInitiateReq) ShardRequestKind() ShardMessageKind {
	return ADD_SPAN_INITIATE
}

func (v *AddSpanInitiateReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackFixedBytes(8, v.Cookie[:])
	buf.PackVarU61(uint64(v.ByteOffset))
	buf.PackU8(uint8(v.StorageClass))
	len1 := len(v.Blacklist)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Blacklist[i].Pack(buf)
	}
	buf.PackU8(uint8(v.Parity))
	buf.PackFixedBytes(4, v.Crc32[:])
	buf.PackVarU61(uint64(v.Size))
	buf.PackVarU61(uint64(v.BlockSize))
	buf.PackBytes([]byte(v.BodyBytes))
	len2 := len(v.BodyBlocks)
	buf.PackLength(len2)
	for i := 0; i < len2; i++ {
		v.BodyBlocks[i].Pack(buf)
	}
}

func (v *AddSpanInitiateReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blacklist, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blacklist[i].Unpack(buf); err != nil {
			return err
		}
	}
	if err := buf.UnpackU8((*uint8)(&v.Parity)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(4, v.Crc32[:]); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.Size)); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.BlockSize)); err != nil {
		return err
	}
	if err := buf.UnpackBytes((*[]byte)(&v.BodyBytes)); err != nil {
		return err
	}
	var len2 int
	if err := buf.UnpackLength(&len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BodyBlocks, len2)
	for i := 0; i < len2; i++ {
		if err := v.BodyBlocks[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanInitiateResp) ShardResponseKind() ShardMessageKind {
	return ADD_SPAN_INITIATE
}

func (v *AddSpanInitiateResp) Pack(buf *bincode.Buf) {
	len1 := len(v.Blocks)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Blocks[i].Pack(buf)
	}
}

func (v *AddSpanInitiateResp) Unpack(buf *bincode.Buf) error {
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanCertifyReq) ShardRequestKind() ShardMessageKind {
	return ADD_SPAN_CERTIFY
}

func (v *AddSpanCertifyReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackFixedBytes(8, v.Cookie[:])
	buf.PackVarU61(uint64(v.ByteOffset))
	len1 := len(v.Proofs)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Proofs[i].Pack(buf)
	}
}

func (v *AddSpanCertifyReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Proofs, len1)
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanCertifyResp) ShardResponseKind() ShardMessageKind {
	return ADD_SPAN_CERTIFY
}

func (v *AddSpanCertifyResp) Pack(buf *bincode.Buf) {
}

func (v *AddSpanCertifyResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *LinkFileReq) ShardRequestKind() ShardMessageKind {
	return LINK_FILE
}

func (v *LinkFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackFixedBytes(8, v.Cookie[:])
	buf.PackU64(uint64(v.OwnerId))
	buf.PackBytes([]byte(v.Name))
}

func (v *LinkFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LinkFileResp) ShardResponseKind() ShardMessageKind {
	return LINK_FILE
}

func (v *LinkFileResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.CreationTime))
}

func (v *LinkFileResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileReq) ShardRequestKind() ShardMessageKind {
	return SOFT_UNLINK_FILE
}

func (v *SoftUnlinkFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.FileId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *SoftUnlinkFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileResp) ShardResponseKind() ShardMessageKind {
	return SOFT_UNLINK_FILE
}

func (v *SoftUnlinkFileResp) Pack(buf *bincode.Buf) {
}

func (v *SoftUnlinkFileResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *FileSpansReq) ShardRequestKind() ShardMessageKind {
	return FILE_SPANS
}

func (v *FileSpansReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackVarU61(uint64(v.ByteOffset))
}

func (v *FileSpansReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	return nil
}

func (v *FileSpansResp) ShardResponseKind() ShardMessageKind {
	return FILE_SPANS
}

func (v *FileSpansResp) Pack(buf *bincode.Buf) {
	buf.PackVarU61(uint64(v.NextOffset))
	len1 := len(v.BlockServices)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.BlockServices[i].Pack(buf)
	}
	len2 := len(v.Spans)
	buf.PackLength(len2)
	for i := 0; i < len2; i++ {
		v.Spans[i].Pack(buf)
	}
}

func (v *FileSpansResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackVarU61((*uint64)(&v.NextOffset)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BlockServices, len1)
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Unpack(buf); err != nil {
			return err
		}
	}
	var len2 int
	if err := buf.UnpackLength(&len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Spans, len2)
	for i := 0; i < len2; i++ {
		if err := v.Spans[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *SameDirectoryRenameReq) ShardRequestKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME
}

func (v *SameDirectoryRenameReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.OldName))
	buf.PackU64(uint64(v.OldCreationTime))
	buf.PackBytes([]byte(v.NewName))
}

func (v *SameDirectoryRenameReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.OldName); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameResp) ShardResponseKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME
}

func (v *SameDirectoryRenameResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NewCreationTime))
}

func (v *SameDirectoryRenameResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NewCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryInfoReq) ShardRequestKind() ShardMessageKind {
	return SET_DIRECTORY_INFO
}

func (v *SetDirectoryInfoReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	v.Info.Pack(buf)
}

func (v *SetDirectoryInfoReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := v.Info.Unpack(buf); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryInfoResp) ShardResponseKind() ShardMessageKind {
	return SET_DIRECTORY_INFO
}

func (v *SetDirectoryInfoResp) Pack(buf *bincode.Buf) {
}

func (v *SetDirectoryInfoResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *SnapshotLookupReq) ShardRequestKind() ShardMessageKind {
	return SNAPSHOT_LOOKUP
}

func (v *SnapshotLookupReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.StartFrom))
}

func (v *SnapshotLookupReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.StartFrom)); err != nil {
		return err
	}
	return nil
}

func (v *SnapshotLookupResp) ShardResponseKind() ShardMessageKind {
	return SNAPSHOT_LOOKUP
}

func (v *SnapshotLookupResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NextTime))
	len1 := len(v.Edges)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Edges[i].Pack(buf)
	}
}

func (v *SnapshotLookupResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NextTime)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Edges, len1)
	for i := 0; i < len1; i++ {
		if err := v.Edges[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitDirectoriesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_DIRECTORIES
}

func (v *VisitDirectoriesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BeginId))
}

func (v *VisitDirectoriesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BeginId)); err != nil {
		return err
	}
	return nil
}

func (v *VisitDirectoriesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_DIRECTORIES
}

func (v *VisitDirectoriesResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NextId))
	len1 := len(v.Ids)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		buf.PackU64(uint64(v.Ids[i]))
	}
}

func (v *VisitDirectoriesResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Ids, len1)
	for i := 0; i < len1; i++ {
		if err := buf.UnpackU64((*uint64)(&v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitFilesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_FILES
}

func (v *VisitFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BeginId))
}

func (v *VisitFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BeginId)); err != nil {
		return err
	}
	return nil
}

func (v *VisitFilesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_FILES
}

func (v *VisitFilesResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NextId))
	len1 := len(v.Ids)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		buf.PackU64(uint64(v.Ids[i]))
	}
}

func (v *VisitFilesResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Ids, len1)
	for i := 0; i < len1; i++ {
		if err := buf.UnpackU64((*uint64)(&v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitTransientFilesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_TRANSIENT_FILES
}

func (v *VisitTransientFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BeginId))
}

func (v *VisitTransientFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BeginId)); err != nil {
		return err
	}
	return nil
}

func (v *VisitTransientFilesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_TRANSIENT_FILES
}

func (v *VisitTransientFilesResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.NextId))
	len1 := len(v.Files)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Files[i].Pack(buf)
	}
}

func (v *VisitTransientFilesResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Files, len1)
	for i := 0; i < len1; i++ {
		if err := v.Files[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *FullReadDirReq) ShardRequestKind() ShardMessageKind {
	return FULL_READ_DIR
}

func (v *FullReadDirReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	v.Cursor.Pack(buf)
}

func (v *FullReadDirReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := v.Cursor.Unpack(buf); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirResp) ShardResponseKind() ShardMessageKind {
	return FULL_READ_DIR
}

func (v *FullReadDirResp) Pack(buf *bincode.Buf) {
	v.Next.Pack(buf)
	len1 := len(v.Results)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Results[i].Pack(buf)
	}
}

func (v *FullReadDirResp) Unpack(buf *bincode.Buf) error {
	if err := v.Next.Unpack(buf); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Results, len1)
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveNonOwnedEdgeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_NON_OWNED_EDGE
}

func (v *RemoveNonOwnedEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *RemoveNonOwnedEdgeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveNonOwnedEdgeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_NON_OWNED_EDGE
}

func (v *RemoveNonOwnedEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveNonOwnedEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *SameShardHardFileUnlinkReq) ShardRequestKind() ShardMessageKind {
	return SAME_SHARD_HARD_FILE_UNLINK
}

func (v *SameShardHardFileUnlinkReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *SameShardHardFileUnlinkReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SameShardHardFileUnlinkResp) ShardResponseKind() ShardMessageKind {
	return SAME_SHARD_HARD_FILE_UNLINK
}

func (v *SameShardHardFileUnlinkResp) Pack(buf *bincode.Buf) {
}

func (v *SameShardHardFileUnlinkResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *RemoveSpanInitiateReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_SPAN_INITIATE
}

func (v *RemoveSpanInitiateReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackFixedBytes(8, v.Cookie[:])
}

func (v *RemoveSpanInitiateReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *RemoveSpanInitiateResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_SPAN_INITIATE
}

func (v *RemoveSpanInitiateResp) Pack(buf *bincode.Buf) {
	buf.PackVarU61(uint64(v.ByteOffset))
	len1 := len(v.Blocks)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Blocks[i].Pack(buf)
	}
}

func (v *RemoveSpanInitiateResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanCertifyReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_SPAN_CERTIFY
}

func (v *RemoveSpanCertifyReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId))
	buf.PackFixedBytes(8, v.Cookie[:])
	buf.PackVarU61(uint64(v.ByteOffset))
	len1 := len(v.Proofs)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.Proofs[i].Pack(buf)
	}
}

func (v *RemoveSpanCertifyReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Proofs, len1)
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanCertifyResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_SPAN_CERTIFY
}

func (v *RemoveSpanCertifyResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveSpanCertifyResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *SwapBlocksReq) ShardRequestKind() ShardMessageKind {
	return SWAP_BLOCKS
}

func (v *SwapBlocksReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.FileId1))
	buf.PackU64(uint64(v.ByteOffset1))
	buf.PackU64(uint64(v.BlockId1))
	buf.PackU64(uint64(v.FileId2))
	buf.PackU64(uint64(v.ByteOffset2))
	buf.PackU64(uint64(v.BlockId2))
}

func (v *SwapBlocksReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.FileId1)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.ByteOffset1)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.BlockId1)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.FileId2)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.ByteOffset2)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.BlockId2)); err != nil {
		return err
	}
	return nil
}

func (v *SwapBlocksResp) ShardResponseKind() ShardMessageKind {
	return SWAP_BLOCKS
}

func (v *SwapBlocksResp) Pack(buf *bincode.Buf) {
}

func (v *SwapBlocksResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *BlockServiceFilesReq) ShardRequestKind() ShardMessageKind {
	return BLOCK_SERVICE_FILES
}

func (v *BlockServiceFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BlockServiceId))
	buf.PackU64(uint64(v.StartFrom))
}

func (v *BlockServiceFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.StartFrom)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceFilesResp) ShardResponseKind() ShardMessageKind {
	return BLOCK_SERVICE_FILES
}

func (v *BlockServiceFilesResp) Pack(buf *bincode.Buf) {
	len1 := len(v.FileIds)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		buf.PackU64(uint64(v.FileIds[i]))
	}
}

func (v *BlockServiceFilesResp) Unpack(buf *bincode.Buf) error {
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.FileIds, len1)
	for i := 0; i < len1; i++ {
		if err := buf.UnpackU64((*uint64)(&v.FileIds[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveInodeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_INODE
}

func (v *RemoveInodeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *RemoveInodeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveInodeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_INODE
}

func (v *RemoveInodeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveInodeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *CreateDirectoryInodeReq) ShardRequestKind() ShardMessageKind {
	return CREATE_DIRECTORY_INODE
}

func (v *CreateDirectoryInodeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackU64(uint64(v.OwnerId))
	v.Info.Pack(buf)
}

func (v *CreateDirectoryInodeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := v.Info.Unpack(buf); err != nil {
		return err
	}
	return nil
}

func (v *CreateDirectoryInodeResp) ShardResponseKind() ShardMessageKind {
	return CREATE_DIRECTORY_INODE
}

func (v *CreateDirectoryInodeResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Mtime))
}

func (v *CreateDirectoryInodeResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Mtime)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryOwnerReq) ShardRequestKind() ShardMessageKind {
	return SET_DIRECTORY_OWNER
}

func (v *SetDirectoryOwnerReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackU64(uint64(v.OwnerId))
}

func (v *SetDirectoryOwnerReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryOwnerResp) ShardResponseKind() ShardMessageKind {
	return SET_DIRECTORY_OWNER
}

func (v *SetDirectoryOwnerResp) Pack(buf *bincode.Buf) {
}

func (v *SetDirectoryOwnerResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *RemoveDirectoryOwnerReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_DIRECTORY_OWNER
}

func (v *RemoveDirectoryOwnerReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Info))
}

func (v *RemoveDirectoryOwnerReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackBytes((*[]byte)(&v.Info)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveDirectoryOwnerResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_DIRECTORY_OWNER
}

func (v *RemoveDirectoryOwnerResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveDirectoryOwnerResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *CreateLockedCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return CREATE_LOCKED_CURRENT_EDGE
}

func (v *CreateLockedCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.TargetId))
}

func (v *CreateLockedCurrentEdgeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	return nil
}

func (v *CreateLockedCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return CREATE_LOCKED_CURRENT_EDGE
}

func (v *CreateLockedCurrentEdgeResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.CreationTime))
}

func (v *CreateLockedCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *LockCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return LOCK_CURRENT_EDGE
}

func (v *LockCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.CreationTime))
	buf.PackBytes([]byte(v.Name))
}

func (v *LockCurrentEdgeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LockCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return LOCK_CURRENT_EDGE
}

func (v *LockCurrentEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *LockCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *UnlockCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return UNLOCK_CURRENT_EDGE
}

func (v *UnlockCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBool(bool(v.WasMoved))
}

func (v *UnlockCurrentEdgeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackBool((*bool)(&v.WasMoved)); err != nil {
		return err
	}
	return nil
}

func (v *UnlockCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return UNLOCK_CURRENT_EDGE
}

func (v *UnlockCurrentEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *UnlockCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_OWNED_SNAPSHOT_FILE_EDGE
}

func (v *RemoveOwnedSnapshotFileEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *RemoveOwnedSnapshotFileEdgeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_OWNED_SNAPSHOT_FILE_EDGE
}

func (v *RemoveOwnedSnapshotFileEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveOwnedSnapshotFileEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *MakeFileTransientReq) ShardRequestKind() ShardMessageKind {
	return MAKE_FILE_TRANSIENT
}

func (v *MakeFileTransientReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackBytes([]byte(v.Note))
}

func (v *MakeFileTransientReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Note); err != nil {
		return err
	}
	return nil
}

func (v *MakeFileTransientResp) ShardResponseKind() ShardMessageKind {
	return MAKE_FILE_TRANSIENT
}

func (v *MakeFileTransientResp) Pack(buf *bincode.Buf) {
}

func (v *MakeFileTransientResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *MakeDirectoryReq) CDCRequestKind() CDCMessageKind {
	return MAKE_DIRECTORY
}

func (v *MakeDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackBytes([]byte(v.Name))
	v.Info.Pack(buf)
}

func (v *MakeDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := v.Info.Unpack(buf); err != nil {
		return err
	}
	return nil
}

func (v *MakeDirectoryResp) CDCResponseKind() CDCMessageKind {
	return MAKE_DIRECTORY
}

func (v *MakeDirectoryResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *MakeDirectoryResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileReq) CDCRequestKind() CDCMessageKind {
	return RENAME_FILE
}

func (v *RenameFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.OldOwnerId))
	buf.PackBytes([]byte(v.OldName))
	buf.PackU64(uint64(v.OldCreationTime))
	buf.PackU64(uint64(v.NewOwnerId))
	buf.PackBytes([]byte(v.NewName))
}

func (v *RenameFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OldOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.OldName); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileResp) CDCResponseKind() CDCMessageKind {
	return RENAME_FILE
}

func (v *RenameFileResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.CreationTime))
}

func (v *RenameFileResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryReq) CDCRequestKind() CDCMessageKind {
	return SOFT_UNLINK_DIRECTORY
}

func (v *SoftUnlinkDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.CreationTime))
	buf.PackBytes([]byte(v.Name))
}

func (v *SoftUnlinkDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryResp) CDCResponseKind() CDCMessageKind {
	return SOFT_UNLINK_DIRECTORY
}

func (v *SoftUnlinkDirectoryResp) Pack(buf *bincode.Buf) {
}

func (v *SoftUnlinkDirectoryResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *RenameDirectoryReq) CDCRequestKind() CDCMessageKind {
	return RENAME_DIRECTORY
}

func (v *RenameDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.OldOwnerId))
	buf.PackBytes([]byte(v.OldName))
	buf.PackU64(uint64(v.OldCreationTime))
	buf.PackU64(uint64(v.NewOwnerId))
	buf.PackBytes([]byte(v.NewName))
}

func (v *RenameDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OldOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.OldName); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameDirectoryResp) CDCResponseKind() CDCMessageKind {
	return RENAME_DIRECTORY
}

func (v *RenameDirectoryResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.CreationTime))
}

func (v *RenameDirectoryResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *HardUnlinkDirectoryReq) CDCRequestKind() CDCMessageKind {
	return HARD_UNLINK_DIRECTORY
}

func (v *HardUnlinkDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
}

func (v *HardUnlinkDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.DirId)); err != nil {
		return err
	}
	return nil
}

func (v *HardUnlinkDirectoryResp) CDCResponseKind() CDCMessageKind {
	return HARD_UNLINK_DIRECTORY
}

func (v *HardUnlinkDirectoryResp) Pack(buf *bincode.Buf) {
}

func (v *HardUnlinkDirectoryResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *CrossShardHardUnlinkFileReq) CDCRequestKind() CDCMessageKind {
	return CROSS_SHARD_HARD_UNLINK_FILE
}

func (v *CrossShardHardUnlinkFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *CrossShardHardUnlinkFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CrossShardHardUnlinkFileResp) CDCResponseKind() CDCMessageKind {
	return CROSS_SHARD_HARD_UNLINK_FILE
}

func (v *CrossShardHardUnlinkFileResp) Pack(buf *bincode.Buf) {
}

func (v *CrossShardHardUnlinkFileResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *TransientFile) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackFixedBytes(8, v.Cookie[:])
	buf.PackU64(uint64(v.DeadlineTime))
}

func (v *TransientFile) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Cookie[:]); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.DeadlineTime)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedBlock) Pack(buf *bincode.Buf) {
	buf.PackU8(uint8(v.BlockServiceIx))
	buf.PackU64(uint64(v.BlockId))
	buf.PackFixedBytes(4, v.Crc32[:])
}

func (v *FetchedBlock) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU8((*uint8)(&v.BlockServiceIx)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(4, v.Crc32[:]); err != nil {
		return err
	}
	return nil
}

func (v *CurrentEdge) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.NameHash))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *CurrentEdge) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.NameHash)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *Edge) Pack(buf *bincode.Buf) {
	buf.PackBool(bool(v.Current))
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.NameHash))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *Edge) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackBool((*bool)(&v.Current)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.NameHash)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedSpan) Pack(buf *bincode.Buf) {
	buf.PackVarU61(uint64(v.ByteOffset))
	buf.PackU8(uint8(v.Parity))
	buf.PackU8(uint8(v.StorageClass))
	buf.PackFixedBytes(4, v.Crc32[:])
	buf.PackVarU61(uint64(v.Size))
	buf.PackVarU61(uint64(v.BlockSize))
	buf.PackBytes([]byte(v.BodyBytes))
	len1 := len(v.BodyBlocks)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.BodyBlocks[i].Pack(buf)
	}
}

func (v *FetchedSpan) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackVarU61((*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.Parity)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(4, v.Crc32[:]); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.Size)); err != nil {
		return err
	}
	if err := buf.UnpackVarU61((*uint64)(&v.BlockSize)); err != nil {
		return err
	}
	if err := buf.UnpackBytes((*[]byte)(&v.BodyBytes)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BodyBlocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.BodyBlocks[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *BlockInfo) Pack(buf *bincode.Buf) {
	buf.PackFixedBytes(4, v.BlockServiceIp[:])
	buf.PackU16(uint16(v.BlockServicePort))
	buf.PackU64(uint64(v.BlockServiceId))
	buf.PackU64(uint64(v.BlockId))
	buf.PackFixedBytes(8, v.Certificate[:])
}

func (v *BlockInfo) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackFixedBytes(4, v.BlockServiceIp[:]); err != nil {
		return err
	}
	if err := buf.UnpackU16((*uint16)(&v.BlockServicePort)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *NewBlockInfo) Pack(buf *bincode.Buf) {
	buf.PackFixedBytes(4, v.Crc32[:])
}

func (v *NewBlockInfo) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackFixedBytes(4, v.Crc32[:]); err != nil {
		return err
	}
	return nil
}

func (v *BlockProof) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BlockId))
	buf.PackFixedBytes(8, v.Proof[:])
}

func (v *BlockProof) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *SpanPolicy) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.MaxSize))
	buf.PackU8(uint8(v.StorageClass))
	buf.PackU8(uint8(v.Parity))
}

func (v *SpanPolicy) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.MaxSize)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.Parity)); err != nil {
		return err
	}
	return nil
}

func (v *DirectoryInfoBody) Pack(buf *bincode.Buf) {
	buf.PackU8(uint8(v.Version))
	buf.PackU64(uint64(v.DeleteAfterTime))
	buf.PackU16(uint16(v.DeleteAfterVersions))
	len1 := len(v.SpanPolicies)
	buf.PackLength(len1)
	for i := 0; i < len1; i++ {
		v.SpanPolicies[i].Pack(buf)
	}
}

func (v *DirectoryInfoBody) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU8((*uint8)(&v.Version)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.DeleteAfterTime)); err != nil {
		return err
	}
	if err := buf.UnpackU16((*uint16)(&v.DeleteAfterVersions)); err != nil {
		return err
	}
	var len1 int
	if err := buf.UnpackLength(&len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.SpanPolicies, len1)
	for i := 0; i < len1; i++ {
		if err := v.SpanPolicies[i].Unpack(buf); err != nil {
			return err
		}
	}
	return nil
}

func (v *SetDirectoryInfo) Pack(buf *bincode.Buf) {
	buf.PackBool(bool(v.Inherited))
	buf.PackBytes([]byte(v.Body))
}

func (v *SetDirectoryInfo) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackBool((*bool)(&v.Inherited)); err != nil {
		return err
	}
	if err := buf.UnpackBytes((*[]byte)(&v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceBlacklist) Pack(buf *bincode.Buf) {
	buf.PackFixedBytes(4, v.Ip[:])
	buf.PackU16(uint16(v.Port))
	buf.PackU64(uint64(v.Id))
}

func (v *BlockServiceBlacklist) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackFixedBytes(4, v.Ip[:]); err != nil {
		return err
	}
	if err := buf.UnpackU16((*uint16)(&v.Port)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *BlockService) Pack(buf *bincode.Buf) {
	buf.PackFixedBytes(4, v.Ip[:])
	buf.PackU16(uint16(v.Port))
	buf.PackU64(uint64(v.Id))
	buf.PackU8(uint8(v.Flags))
}

func (v *BlockService) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackFixedBytes(4, v.Ip[:]); err != nil {
		return err
	}
	if err := buf.UnpackU16((*uint16)(&v.Port)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.Flags)); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirCursor) Pack(buf *bincode.Buf) {
	buf.PackBool(bool(v.Current))
	buf.PackU64(uint64(v.StartHash))
	buf.PackBytes([]byte(v.StartName))
	buf.PackU64(uint64(v.StartTime))
}

func (v *FullReadDirCursor) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackBool((*bool)(&v.Current)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.StartHash)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.StartName); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.StartTime)); err != nil {
		return err
	}
	return nil
}

func (v *EntryBlockService) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
	buf.PackFixedBytes(4, v.Ip[:])
	buf.PackU16(uint16(v.Port))
	buf.PackU8(uint8(v.StorageClass))
	buf.PackFixedBytes(16, v.FailureDomain[:])
	buf.PackFixedBytes(16, v.SecretKey[:])
}

func (v *EntryBlockService) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(4, v.Ip[:]); err != nil {
		return err
	}
	if err := buf.UnpackU16((*uint16)(&v.Port)); err != nil {
		return err
	}
	if err := buf.UnpackU8((*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(16, v.FailureDomain[:]); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(16, v.SecretKey[:]); err != nil {
		return err
	}
	return nil
}

func (v *EntryNewBlockInfo) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BlockServiceId))
	buf.PackFixedBytes(4, v.Crc32[:])
}

func (v *EntryNewBlockInfo) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := buf.UnpackFixedBytes(4, v.Crc32[:]); err != nil {
		return err
	}
	return nil
}

func (v *SnapshotLookupEdge) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *SnapshotLookupEdge) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

