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
	TYPE_IS_DIRECTORY ErrCode = 20
	TYPE_IS_NOT_DIRECTORY ErrCode = 21
	BAD_COOKIE ErrCode = 22
	INCONSISTENT_STORAGE_CLASS_PARITY ErrCode = 23
	LAST_SPAN_STATE_NOT_CLEAN ErrCode = 24
	COULD_NOT_PICK_BLOCK_SERVICES ErrCode = 25
	BAD_SPAN_BODY ErrCode = 26
	SPAN_NOT_FOUND ErrCode = 27
	BLOCK_SERVICE_NOT_FOUND ErrCode = 28
	CANNOT_CERTIFY_BLOCKLESS_SPAN ErrCode = 29
	BAD_NUMBER_OF_BLOCKS_PROOFS ErrCode = 30
	BAD_BLOCK_PROOF ErrCode = 31
	CANNOT_OVERRIDE_NAME ErrCode = 32
	NAME_IS_LOCKED ErrCode = 33
	OLD_NAME_IS_LOCKED ErrCode = 34
	NEW_NAME_IS_LOCKED ErrCode = 35
	MTIME_IS_TOO_RECENT ErrCode = 36
	MISMATCHING_TARGET ErrCode = 37
	MISMATCHING_OWNER ErrCode = 38
	DIRECTORY_NOT_EMPTY ErrCode = 39
	FILE_IS_TRANSIENT ErrCode = 40
	OLD_DIRECTORY_NOT_FOUND ErrCode = 41
	NEW_DIRECTORY_NOT_FOUND ErrCode = 42
	LOOP_IN_DIRECTORY_RENAME ErrCode = 43
	EDGE_NOT_FOUND ErrCode = 44
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
	CREATION_TIME_TOO_RECENT ErrCode = 56
	DEADLINE_NOT_PASSED ErrCode = 57
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
		return "TYPE_IS_DIRECTORY"
	case 21:
		return "TYPE_IS_NOT_DIRECTORY"
	case 22:
		return "BAD_COOKIE"
	case 23:
		return "INCONSISTENT_STORAGE_CLASS_PARITY"
	case 24:
		return "LAST_SPAN_STATE_NOT_CLEAN"
	case 25:
		return "COULD_NOT_PICK_BLOCK_SERVICES"
	case 26:
		return "BAD_SPAN_BODY"
	case 27:
		return "SPAN_NOT_FOUND"
	case 28:
		return "BLOCK_SERVICE_NOT_FOUND"
	case 29:
		return "CANNOT_CERTIFY_BLOCKLESS_SPAN"
	case 30:
		return "BAD_NUMBER_OF_BLOCKS_PROOFS"
	case 31:
		return "BAD_BLOCK_PROOF"
	case 32:
		return "CANNOT_OVERRIDE_NAME"
	case 33:
		return "NAME_IS_LOCKED"
	case 34:
		return "OLD_NAME_IS_LOCKED"
	case 35:
		return "NEW_NAME_IS_LOCKED"
	case 36:
		return "MTIME_IS_TOO_RECENT"
	case 37:
		return "MISMATCHING_TARGET"
	case 38:
		return "MISMATCHING_OWNER"
	case 39:
		return "DIRECTORY_NOT_EMPTY"
	case 40:
		return "FILE_IS_TRANSIENT"
	case 41:
		return "OLD_DIRECTORY_NOT_FOUND"
	case 42:
		return "NEW_DIRECTORY_NOT_FOUND"
	case 43:
		return "LOOP_IN_DIRECTORY_RENAME"
	case 44:
		return "EDGE_NOT_FOUND"
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
		return "CREATION_TIME_TOO_RECENT"
	case 57:
		return "DEADLINE_NOT_PASSED"
	default:
		return fmt.Sprintf("ErrCode(%d)", err)
	}
}

func GetShardMessageKind(body any) ShardMessageKind {
	switch body.(type) {
	case ErrCode:
		return 0
	case *LookupReq, *LookupResp:
		return LOOKUP
	case *StatFileReq, *StatFileResp:
		return STAT_FILE
	case *StatTransientFileReq, *StatTransientFileResp:
		return STAT_TRANSIENT_FILE
	case *StatDirectoryReq, *StatDirectoryResp:
		return STAT_DIRECTORY
	case *ReadDirReq, *ReadDirResp:
		return READ_DIR
	case *ConstructFileReq, *ConstructFileResp:
		return CONSTRUCT_FILE
	case *AddSpanInitiateReq, *AddSpanInitiateResp:
		return ADD_SPAN_INITIATE
	case *AddSpanCertifyReq, *AddSpanCertifyResp:
		return ADD_SPAN_CERTIFY
	case *LinkFileReq, *LinkFileResp:
		return LINK_FILE
	case *SoftUnlinkFileReq, *SoftUnlinkFileResp:
		return SOFT_UNLINK_FILE
	case *FileSpansReq, *FileSpansResp:
		return FILE_SPANS
	case *SameDirectoryRenameReq, *SameDirectoryRenameResp:
		return SAME_DIRECTORY_RENAME
	case *SetDirectoryInfoReq, *SetDirectoryInfoResp:
		return SET_DIRECTORY_INFO
	case *VisitDirectoriesReq, *VisitDirectoriesResp:
		return VISIT_DIRECTORIES
	case *VisitFilesReq, *VisitFilesResp:
		return VISIT_FILES
	case *VisitTransientFilesReq, *VisitTransientFilesResp:
		return VISIT_TRANSIENT_FILES
	case *FullReadDirReq, *FullReadDirResp:
		return FULL_READ_DIR
	case *RemoveNonOwnedEdgeReq, *RemoveNonOwnedEdgeResp:
		return REMOVE_NON_OWNED_EDGE
	case *IntraShardHardFileUnlinkReq, *IntraShardHardFileUnlinkResp:
		return INTRA_SHARD_HARD_FILE_UNLINK
	case *RemoveSpanInitiateReq, *RemoveSpanInitiateResp:
		return REMOVE_SPAN_INITIATE
	case *RemoveSpanCertifyReq, *RemoveSpanCertifyResp:
		return REMOVE_SPAN_CERTIFY
	case *SwapBlocksReq, *SwapBlocksResp:
		return SWAP_BLOCKS
	case *BlockServiceFilesReq, *BlockServiceFilesResp:
		return BLOCK_SERVICE_FILES
	case *RemoveInodeReq, *RemoveInodeResp:
		return REMOVE_INODE
	case *CreateDirectoryInodeReq, *CreateDirectoryInodeResp:
		return CREATE_DIRECTORY_INODE
	case *SetDirectoryOwnerReq, *SetDirectoryOwnerResp:
		return SET_DIRECTORY_OWNER
	case *RemoveDirectoryOwnerReq, *RemoveDirectoryOwnerResp:
		return REMOVE_DIRECTORY_OWNER
	case *CreateLockedCurrentEdgeReq, *CreateLockedCurrentEdgeResp:
		return CREATE_LOCKED_CURRENT_EDGE
	case *LockCurrentEdgeReq, *LockCurrentEdgeResp:
		return LOCK_CURRENT_EDGE
	case *UnlockCurrentEdgeReq, *UnlockCurrentEdgeResp:
		return UNLOCK_CURRENT_EDGE
	case *RemoveOwnedSnapshotFileEdgeReq, *RemoveOwnedSnapshotFileEdgeResp:
		return REMOVE_OWNED_SNAPSHOT_FILE_EDGE
	case *MakeFileTransientReq, *MakeFileTransientResp:
		return MAKE_FILE_TRANSIENT
	default:
		panic(fmt.Sprintf("bad shard req/resp body %T", body))
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
	VISIT_DIRECTORIES ShardMessageKind = 0x15
	VISIT_FILES ShardMessageKind = 0x20
	VISIT_TRANSIENT_FILES ShardMessageKind = 0x16
	FULL_READ_DIR ShardMessageKind = 0x21
	REMOVE_NON_OWNED_EDGE ShardMessageKind = 0x17
	INTRA_SHARD_HARD_FILE_UNLINK ShardMessageKind = 0x18
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

func GetCDCMessageKind(body any) CDCMessageKind {
	switch body.(type) {
	case ErrCode:
		return 0
	case *MakeDirectoryReq, *MakeDirectoryResp:
		return MAKE_DIRECTORY
	case *RenameFileReq, *RenameFileResp:
		return RENAME_FILE
	case *SoftUnlinkDirectoryReq, *SoftUnlinkDirectoryResp:
		return SOFT_UNLINK_DIRECTORY
	case *RenameDirectoryReq, *RenameDirectoryResp:
		return RENAME_DIRECTORY
	case *HardUnlinkDirectoryReq, *HardUnlinkDirectoryResp:
		return HARD_UNLINK_DIRECTORY
	case *HardUnlinkFileReq, *HardUnlinkFileResp:
		return HARD_UNLINK_FILE
	default:
		panic(fmt.Sprintf("bad shard req/resp body %T", body))
	}
}


const (
	MAKE_DIRECTORY CDCMessageKind = 0x1
	RENAME_FILE CDCMessageKind = 0x2
	SOFT_UNLINK_DIRECTORY CDCMessageKind = 0x3
	RENAME_DIRECTORY CDCMessageKind = 0x4
	HARD_UNLINK_DIRECTORY CDCMessageKind = 0x5
	HARD_UNLINK_FILE CDCMessageKind = 0x6
)

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

func (v *StatFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *StatFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
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

func (v *StatTransientFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *StatTransientFileReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
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
	if err := buf.UnpackBytes((*[]byte)(&v.Note)); err != nil {
		return err
	}
	return nil
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

func (v *AddSpanCertifyResp) Pack(buf *bincode.Buf) {
}

func (v *AddSpanCertifyResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *LinkFileResp) Pack(buf *bincode.Buf) {
}

func (v *LinkFileResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *SoftUnlinkFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.FileId))
	buf.PackBytes([]byte(v.Name))
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
	return nil
}

func (v *SoftUnlinkFileResp) Pack(buf *bincode.Buf) {
}

func (v *SoftUnlinkFileResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *SameDirectoryRenameReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.OldName))
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
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameResp) Pack(buf *bincode.Buf) {
}

func (v *SameDirectoryRenameResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *SetDirectoryInfoResp) Pack(buf *bincode.Buf) {
}

func (v *SetDirectoryInfoResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *VisitFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BeginId))
}

func (v *VisitFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BeginId)); err != nil {
		return err
	}
	return nil
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

func (v *VisitTransientFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BeginId))
}

func (v *VisitTransientFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BeginId)); err != nil {
		return err
	}
	return nil
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

func (v *RemoveNonOwnedEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveNonOwnedEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *IntraShardHardFileUnlinkReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *IntraShardHardFileUnlinkReq) Unpack(buf *bincode.Buf) error {
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

func (v *IntraShardHardFileUnlinkResp) Pack(buf *bincode.Buf) {
}

func (v *IntraShardHardFileUnlinkResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *RemoveSpanCertifyResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveSpanCertifyResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *SwapBlocksResp) Pack(buf *bincode.Buf) {
}

func (v *SwapBlocksResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *BlockServiceFilesReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.BlockServiceId))
}

func (v *BlockServiceFilesReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	return nil
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

func (v *RemoveInodeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *RemoveInodeReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveInodeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveInodeResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *CreateDirectoryInodeResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Mtime))
}

func (v *CreateDirectoryInodeResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Mtime)); err != nil {
		return err
	}
	return nil
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

func (v *SetDirectoryOwnerResp) Pack(buf *bincode.Buf) {
}

func (v *SetDirectoryOwnerResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *RemoveDirectoryOwnerResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveDirectoryOwnerResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *CreateLockedCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.CreationTime))
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
	if err := buf.UnpackU64((*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CreateLockedCurrentEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *CreateLockedCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *LockCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.TargetId))
}

func (v *LockCurrentEdgeReq) Unpack(buf *bincode.Buf) error {
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

func (v *LockCurrentEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *LockCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *UnlockCurrentEdgeReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.DirId))
	buf.PackBytes([]byte(v.Name))
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
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackBool((*bool)(&v.WasMoved)); err != nil {
		return err
	}
	return nil
}

func (v *UnlockCurrentEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *UnlockCurrentEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *RemoveOwnedSnapshotFileEdgeResp) Pack(buf *bincode.Buf) {
}

func (v *RemoveOwnedSnapshotFileEdgeResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *MakeFileTransientResp) Pack(buf *bincode.Buf) {
}

func (v *MakeFileTransientResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *MakeDirectoryResp) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.Id))
}

func (v *MakeDirectoryResp) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.OldOwnerId))
	buf.PackBytes([]byte(v.OldName))
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
	if err := buf.UnpackU64((*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileResp) Pack(buf *bincode.Buf) {
}

func (v *RenameFileResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *SoftUnlinkDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
}

func (v *SoftUnlinkDirectoryReq) Unpack(buf *bincode.Buf) error {
	if err := buf.UnpackU64((*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackU64((*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.Name); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryResp) Pack(buf *bincode.Buf) {
}

func (v *SoftUnlinkDirectoryResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *RenameDirectoryReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.TargetId))
	buf.PackU64(uint64(v.OldOwnerId))
	buf.PackBytes([]byte(v.OldName))
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
	if err := buf.UnpackU64((*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := buf.UnpackString(&v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameDirectoryResp) Pack(buf *bincode.Buf) {
}

func (v *RenameDirectoryResp) Unpack(buf *bincode.Buf) error {
	return nil
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

func (v *HardUnlinkDirectoryResp) Pack(buf *bincode.Buf) {
}

func (v *HardUnlinkDirectoryResp) Unpack(buf *bincode.Buf) error {
	return nil
}

func (v *HardUnlinkFileReq) Pack(buf *bincode.Buf) {
	buf.PackU64(uint64(v.OwnerId))
	buf.PackU64(uint64(v.TargetId))
	buf.PackBytes([]byte(v.Name))
	buf.PackU64(uint64(v.CreationTime))
}

func (v *HardUnlinkFileReq) Unpack(buf *bincode.Buf) error {
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

func (v *HardUnlinkFileResp) Pack(buf *bincode.Buf) {
}

func (v *HardUnlinkFileResp) Unpack(buf *bincode.Buf) error {
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
	buf.PackU8(uint8(v.DeleteAfterVersions))
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
	if err := buf.UnpackU8((*uint8)(&v.DeleteAfterVersions)); err != nil {
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

