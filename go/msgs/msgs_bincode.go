// Automatically generated with go run bincodegen.
// Run `go generate ./...` from the go/ directory to regenerate it.
package msgs

import (
	"fmt"
	"io"
	"xtx/eggsfs/bincode"
)

// This file specifies
type ShardRequest interface {
	bincode.Packable
	bincode.Unpackable
	ShardRequestKind() ShardMessageKind
}

type ShardResponse interface {
	bincode.Packable
	bincode.Unpackable
	ShardResponseKind() ShardMessageKind
}

type CDCRequest interface {
	bincode.Packable
	bincode.Unpackable
	CDCRequestKind() CDCMessageKind
}

type CDCResponse interface {
	bincode.Packable
	bincode.Unpackable
	CDCResponseKind() CDCMessageKind
}

type IsDirectoryInfoEntry interface {
	bincode.Packable
	bincode.Unpackable
	Tag() DirectoryInfoTag
}

func TagToDirInfoEntry(tag DirectoryInfoTag) IsDirectoryInfoEntry {
	switch tag {
	case SNAPSHOT_POLICY_TAG:
		return &SnapshotPolicy{}
	case SPAN_POLICY_TAG:
		return &SpanPolicy{}
	case BLOCK_POLICY_TAG:
		return &BlockPolicy{}
	default:
		panic(fmt.Errorf("unknown policy tag %v", tag))
	}
}

func (err ErrCode) Error() string {
	return err.String()
}

func (err *ErrCode) Pack(w io.Writer) error {
	return bincode.PackScalar(w, uint16(*err))
}
	
func (errCode *ErrCode) Unpack(r io.Reader) error {
	var c uint16
	if err := bincode.UnpackScalar(r, &c); err != nil {
		return err
	}
	*errCode = ErrCode(c)
	return nil
}	
	
func (fs *FetchedSpan) Pack(w io.Writer) error {
	if fs.Header.StorageClass == EMPTY_STORAGE {
		return fmt.Errorf("cannot have EMPTY_STORAGE in fetched span")
	}
	if err := fs.Header.Pack(w); err != nil {
		return err
	}
	switch b := fs.Body.(type) {
	case *FetchedBlocksSpan:
		if fs.Header.StorageClass == INLINE_STORAGE {
			return fmt.Errorf("got INLINE storage class with blocks body")
		}
		if err := b.Pack(w); err != nil {
			return err
		}
	case *FetchedInlineSpan:
		if fs.Header.StorageClass != INLINE_STORAGE {
			return fmt.Errorf("got non-INLINE storage (%v) with inline body", fs.Header.StorageClass)
		}
		if err := b.Pack(w); err != nil {
			return err
		}
	default:
		return fmt.Errorf("unexpected FetchedSpan body of type %T", b)
	}
	return nil
}

func (fs *FetchedSpan) Unpack(r io.Reader) error {
	if err := fs.Header.Unpack(r); err != nil {
		return err
	}
	if fs.Header.StorageClass == EMPTY_STORAGE {
		return fmt.Errorf("unexpected EMPTY_STORAGE in unpacked FetchedSpan")
	}
	if fs.Header.StorageClass == INLINE_STORAGE {
		fs.Body = &FetchedInlineSpan{}
	} else {
		fs.Body = &FetchedBlocksSpan{}
	}
	if err := fs.Body.Unpack(r); err != nil {
		return err
	}
	return nil
}
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
	BAD_PROTOCOL_VERSION ErrCode = 60
	BAD_CERTIFICATE ErrCode = 61
	BLOCK_TOO_RECENT_FOR_DELETION ErrCode = 62
	BLOCK_FETCH_OUT_OF_BOUNDS ErrCode = 63
	BAD_BLOCK_CRC ErrCode = 64
	BLOCK_TOO_BIG ErrCode = 65
	BLOCK_NOT_FOUND ErrCode = 66
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
	case 60:
		return "BAD_PROTOCOL_VERSION"
	case 61:
		return "BAD_CERTIFICATE"
	case 62:
		return "BLOCK_TOO_RECENT_FOR_DELETION"
	case 63:
		return "BLOCK_FETCH_OUT_OF_BOUNDS"
	case 64:
		return "BAD_BLOCK_CRC"
	case 65:
		return "BLOCK_TOO_BIG"
	case 66:
		return "BLOCK_NOT_FOUND"
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
	case 4:
		return "STAT_DIRECTORY"
	case 5:
		return "READ_DIR"
	case 6:
		return "CONSTRUCT_FILE"
	case 7:
		return "ADD_SPAN_INITIATE"
	case 8:
		return "ADD_SPAN_CERTIFY"
	case 9:
		return "LINK_FILE"
	case 10:
		return "SOFT_UNLINK_FILE"
	case 11:
		return "FILE_SPANS"
	case 12:
		return "SAME_DIRECTORY_RENAME"
	case 16:
		return "ADD_INLINE_SPAN"
	case 115:
		return "FULL_READ_DIR"
	case 123:
		return "MOVE_SPAN"
	case 116:
		return "REMOVE_NON_OWNED_EDGE"
	case 117:
		return "SAME_SHARD_HARD_FILE_UNLINK"
	case 3:
		return "STAT_TRANSIENT_FILE"
	case 13:
		return "SET_DIRECTORY_INFO"
	case 15:
		return "EXPIRE_TRANSIENT_FILE"
	case 112:
		return "VISIT_DIRECTORIES"
	case 113:
		return "VISIT_FILES"
	case 114:
		return "VISIT_TRANSIENT_FILES"
	case 118:
		return "REMOVE_SPAN_INITIATE"
	case 119:
		return "REMOVE_SPAN_CERTIFY"
	case 120:
		return "SWAP_BLOCKS"
	case 121:
		return "BLOCK_SERVICE_FILES"
	case 122:
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
	STAT_DIRECTORY ShardMessageKind = 0x4
	READ_DIR ShardMessageKind = 0x5
	CONSTRUCT_FILE ShardMessageKind = 0x6
	ADD_SPAN_INITIATE ShardMessageKind = 0x7
	ADD_SPAN_CERTIFY ShardMessageKind = 0x8
	LINK_FILE ShardMessageKind = 0x9
	SOFT_UNLINK_FILE ShardMessageKind = 0xA
	FILE_SPANS ShardMessageKind = 0xB
	SAME_DIRECTORY_RENAME ShardMessageKind = 0xC
	ADD_INLINE_SPAN ShardMessageKind = 0x10
	FULL_READ_DIR ShardMessageKind = 0x73
	MOVE_SPAN ShardMessageKind = 0x7B
	REMOVE_NON_OWNED_EDGE ShardMessageKind = 0x74
	SAME_SHARD_HARD_FILE_UNLINK ShardMessageKind = 0x75
	STAT_TRANSIENT_FILE ShardMessageKind = 0x3
	SET_DIRECTORY_INFO ShardMessageKind = 0xD
	EXPIRE_TRANSIENT_FILE ShardMessageKind = 0xF
	VISIT_DIRECTORIES ShardMessageKind = 0x70
	VISIT_FILES ShardMessageKind = 0x71
	VISIT_TRANSIENT_FILES ShardMessageKind = 0x72
	REMOVE_SPAN_INITIATE ShardMessageKind = 0x76
	REMOVE_SPAN_CERTIFY ShardMessageKind = 0x77
	SWAP_BLOCKS ShardMessageKind = 0x78
	BLOCK_SERVICE_FILES ShardMessageKind = 0x79
	REMOVE_INODE ShardMessageKind = 0x7A
	CREATE_DIRECTORY_INODE ShardMessageKind = 0x80
	SET_DIRECTORY_OWNER ShardMessageKind = 0x81
	REMOVE_DIRECTORY_OWNER ShardMessageKind = 0x89
	CREATE_LOCKED_CURRENT_EDGE ShardMessageKind = 0x82
	LOCK_CURRENT_EDGE ShardMessageKind = 0x83
	UNLOCK_CURRENT_EDGE ShardMessageKind = 0x84
	REMOVE_OWNED_SNAPSHOT_FILE_EDGE ShardMessageKind = 0x86
	MAKE_FILE_TRANSIENT ShardMessageKind = 0x87
)

func MkShardMessage(k string) (ShardRequest, ShardResponse, error) {
	switch {
	case k == "LOOKUP":
		return &LookupReq{}, &LookupResp{}, nil
	case k == "STAT_FILE":
		return &StatFileReq{}, &StatFileResp{}, nil
	case k == "STAT_DIRECTORY":
		return &StatDirectoryReq{}, &StatDirectoryResp{}, nil
	case k == "READ_DIR":
		return &ReadDirReq{}, &ReadDirResp{}, nil
	case k == "CONSTRUCT_FILE":
		return &ConstructFileReq{}, &ConstructFileResp{}, nil
	case k == "ADD_SPAN_INITIATE":
		return &AddSpanInitiateReq{}, &AddSpanInitiateResp{}, nil
	case k == "ADD_SPAN_CERTIFY":
		return &AddSpanCertifyReq{}, &AddSpanCertifyResp{}, nil
	case k == "LINK_FILE":
		return &LinkFileReq{}, &LinkFileResp{}, nil
	case k == "SOFT_UNLINK_FILE":
		return &SoftUnlinkFileReq{}, &SoftUnlinkFileResp{}, nil
	case k == "FILE_SPANS":
		return &FileSpansReq{}, &FileSpansResp{}, nil
	case k == "SAME_DIRECTORY_RENAME":
		return &SameDirectoryRenameReq{}, &SameDirectoryRenameResp{}, nil
	case k == "ADD_INLINE_SPAN":
		return &AddInlineSpanReq{}, &AddInlineSpanResp{}, nil
	case k == "FULL_READ_DIR":
		return &FullReadDirReq{}, &FullReadDirResp{}, nil
	case k == "MOVE_SPAN":
		return &MoveSpanReq{}, &MoveSpanResp{}, nil
	case k == "REMOVE_NON_OWNED_EDGE":
		return &RemoveNonOwnedEdgeReq{}, &RemoveNonOwnedEdgeResp{}, nil
	case k == "SAME_SHARD_HARD_FILE_UNLINK":
		return &SameShardHardFileUnlinkReq{}, &SameShardHardFileUnlinkResp{}, nil
	case k == "STAT_TRANSIENT_FILE":
		return &StatTransientFileReq{}, &StatTransientFileResp{}, nil
	case k == "SET_DIRECTORY_INFO":
		return &SetDirectoryInfoReq{}, &SetDirectoryInfoResp{}, nil
	case k == "EXPIRE_TRANSIENT_FILE":
		return &ExpireTransientFileReq{}, &ExpireTransientFileResp{}, nil
	case k == "VISIT_DIRECTORIES":
		return &VisitDirectoriesReq{}, &VisitDirectoriesResp{}, nil
	case k == "VISIT_FILES":
		return &VisitFilesReq{}, &VisitFilesResp{}, nil
	case k == "VISIT_TRANSIENT_FILES":
		return &VisitTransientFilesReq{}, &VisitTransientFilesResp{}, nil
	case k == "REMOVE_SPAN_INITIATE":
		return &RemoveSpanInitiateReq{}, &RemoveSpanInitiateResp{}, nil
	case k == "REMOVE_SPAN_CERTIFY":
		return &RemoveSpanCertifyReq{}, &RemoveSpanCertifyResp{}, nil
	case k == "SWAP_BLOCKS":
		return &SwapBlocksReq{}, &SwapBlocksResp{}, nil
	case k == "BLOCK_SERVICE_FILES":
		return &BlockServiceFilesReq{}, &BlockServiceFilesResp{}, nil
	case k == "REMOVE_INODE":
		return &RemoveInodeReq{}, &RemoveInodeResp{}, nil
	case k == "CREATE_DIRECTORY_INODE":
		return &CreateDirectoryInodeReq{}, &CreateDirectoryInodeResp{}, nil
	case k == "SET_DIRECTORY_OWNER":
		return &SetDirectoryOwnerReq{}, &SetDirectoryOwnerResp{}, nil
	case k == "REMOVE_DIRECTORY_OWNER":
		return &RemoveDirectoryOwnerReq{}, &RemoveDirectoryOwnerResp{}, nil
	case k == "CREATE_LOCKED_CURRENT_EDGE":
		return &CreateLockedCurrentEdgeReq{}, &CreateLockedCurrentEdgeResp{}, nil
	case k == "LOCK_CURRENT_EDGE":
		return &LockCurrentEdgeReq{}, &LockCurrentEdgeResp{}, nil
	case k == "UNLOCK_CURRENT_EDGE":
		return &UnlockCurrentEdgeReq{}, &UnlockCurrentEdgeResp{}, nil
	case k == "REMOVE_OWNED_SNAPSHOT_FILE_EDGE":
		return &RemoveOwnedSnapshotFileEdgeReq{}, &RemoveOwnedSnapshotFileEdgeResp{}, nil
	case k == "MAKE_FILE_TRANSIENT":
		return &MakeFileTransientReq{}, &MakeFileTransientResp{}, nil
	default:
		return nil, nil, fmt.Errorf("bad kind string %s", k)
	}
}

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

func MkCDCMessage(k string) (CDCRequest, CDCResponse, error) {
	switch {
	case k == "MAKE_DIRECTORY":
		return &MakeDirectoryReq{}, &MakeDirectoryResp{}, nil
	case k == "RENAME_FILE":
		return &RenameFileReq{}, &RenameFileResp{}, nil
	case k == "SOFT_UNLINK_DIRECTORY":
		return &SoftUnlinkDirectoryReq{}, &SoftUnlinkDirectoryResp{}, nil
	case k == "RENAME_DIRECTORY":
		return &RenameDirectoryReq{}, &RenameDirectoryResp{}, nil
	case k == "HARD_UNLINK_DIRECTORY":
		return &HardUnlinkDirectoryReq{}, &HardUnlinkDirectoryResp{}, nil
	case k == "CROSS_SHARD_HARD_UNLINK_FILE":
		return &CrossShardHardUnlinkFileReq{}, &CrossShardHardUnlinkFileResp{}, nil
	default:
		return nil, nil, fmt.Errorf("bad kind string %s", k)
	}
}

func (k ShuckleMessageKind) String() string {
	switch k {
	case 3:
		return "SHARDS"
	case 7:
		return "CDC"
	case 8:
		return "INFO"
	case 2:
		return "REGISTER_BLOCK_SERVICES"
	case 4:
		return "REGISTER_SHARD"
	case 5:
		return "ALL_BLOCK_SERVICES"
	case 6:
		return "REGISTER_CDC"
	case 9:
		return "SET_BLOCK_SERVICE_FLAGS"
	default:
		return fmt.Sprintf("ShuckleMessageKind(%d)", k)
	}
}


const (
	SHARDS ShuckleMessageKind = 0x3
	CDC ShuckleMessageKind = 0x7
	INFO ShuckleMessageKind = 0x8
	REGISTER_BLOCK_SERVICES ShuckleMessageKind = 0x2
	REGISTER_SHARD ShuckleMessageKind = 0x4
	ALL_BLOCK_SERVICES ShuckleMessageKind = 0x5
	REGISTER_CDC ShuckleMessageKind = 0x6
	SET_BLOCK_SERVICE_FLAGS ShuckleMessageKind = 0x9
)

func MkShuckleMessage(k string) (ShuckleRequest, ShuckleResponse, error) {
	switch {
	case k == "SHARDS":
		return &ShardsReq{}, &ShardsResp{}, nil
	case k == "CDC":
		return &CdcReq{}, &CdcResp{}, nil
	case k == "INFO":
		return &InfoReq{}, &InfoResp{}, nil
	case k == "REGISTER_BLOCK_SERVICES":
		return &RegisterBlockServicesReq{}, &RegisterBlockServicesResp{}, nil
	case k == "REGISTER_SHARD":
		return &RegisterShardReq{}, &RegisterShardResp{}, nil
	case k == "ALL_BLOCK_SERVICES":
		return &AllBlockServicesReq{}, &AllBlockServicesResp{}, nil
	case k == "REGISTER_CDC":
		return &RegisterCdcReq{}, &RegisterCdcResp{}, nil
	case k == "SET_BLOCK_SERVICE_FLAGS":
		return &SetBlockServiceFlagsReq{}, &SetBlockServiceFlagsResp{}, nil
	default:
		return nil, nil, fmt.Errorf("bad kind string %s", k)
	}
}

func (k BlocksMessageKind) String() string {
	switch k {
	case 2:
		return "FETCH_BLOCK"
	case 3:
		return "WRITE_BLOCK"
	case 1:
		return "ERASE_BLOCK"
	case 5:
		return "TEST_WRITE"
	default:
		return fmt.Sprintf("BlocksMessageKind(%d)", k)
	}
}


const (
	FETCH_BLOCK BlocksMessageKind = 0x2
	WRITE_BLOCK BlocksMessageKind = 0x3
	ERASE_BLOCK BlocksMessageKind = 0x1
	TEST_WRITE BlocksMessageKind = 0x5
)

func MkBlocksMessage(k string) (BlocksRequest, BlocksResponse, error) {
	switch {
	case k == "FETCH_BLOCK":
		return &FetchBlockReq{}, &FetchBlockResp{}, nil
	case k == "WRITE_BLOCK":
		return &WriteBlockReq{}, &WriteBlockResp{}, nil
	case k == "ERASE_BLOCK":
		return &EraseBlockReq{}, &EraseBlockResp{}, nil
	case k == "TEST_WRITE":
		return &TestWriteReq{}, &TestWriteResp{}, nil
	default:
		return nil, nil, fmt.Errorf("bad kind string %s", k)
	}
}

func (v *LookupReq) ShardRequestKind() ShardMessageKind {
	return LOOKUP
}

func (v *LookupReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	return nil
}

func (v *LookupReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LookupResp) ShardResponseKind() ShardMessageKind {
	return LOOKUP
}

func (v *LookupResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *LookupResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileReq) ShardRequestKind() ShardMessageKind {
	return STAT_FILE
}

func (v *StatFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileResp) ShardResponseKind() ShardMessageKind {
	return STAT_FILE
}

func (v *StatFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Mtime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Atime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Size)); err != nil {
		return err
	}
	return nil
}

func (v *StatFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Atime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Size)); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryReq) ShardRequestKind() ShardMessageKind {
	return STAT_DIRECTORY
}

func (v *StatDirectoryReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryResp) ShardResponseKind() ShardMessageKind {
	return STAT_DIRECTORY
}

func (v *StatDirectoryResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Mtime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Owner)); err != nil {
		return err
	}
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *StatDirectoryResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Owner)); err != nil {
		return err
	}
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *ReadDirReq) ShardRequestKind() ShardMessageKind {
	return READ_DIR
}

func (v *ReadDirReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.StartHash)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *ReadDirReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartHash)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *ReadDirResp) ShardResponseKind() ShardMessageKind {
	return READ_DIR
}

func (v *ReadDirResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NextHash)); err != nil {
		return err
	}
	len1 := len(v.Results)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *ReadDirResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextHash)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Results, len1)
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *ConstructFileReq) ShardRequestKind() ShardMessageKind {
	return CONSTRUCT_FILE
}

func (v *ConstructFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Type)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Note)); err != nil {
		return err
	}
	return nil
}

func (v *ConstructFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Type)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Note); err != nil {
		return err
	}
	return nil
}

func (v *ConstructFileResp) ShardResponseKind() ShardMessageKind {
	return CONSTRUCT_FILE
}

func (v *ConstructFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *ConstructFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *AddSpanInitiateReq) ShardRequestKind() ShardMessageKind {
	return ADD_SPAN_INITIATE
}

func (v *AddSpanInitiateReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	len1 := len(v.Blacklist)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Blacklist[i].Pack(w); err != nil {
			return err
		}
	}
	if err := bincode.PackScalar(w, uint8(v.Parity)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Stripes)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.CellSize)); err != nil {
		return err
	}
	len2 := len(v.Crcs)
	if err := bincode.PackLength(w, len2); err != nil {
		return err
	}
	for i := 0; i < len2; i++ {
		if err := bincode.PackScalar(w, uint32(v.Crcs[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanInitiateReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blacklist, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blacklist[i].Unpack(r); err != nil {
			return err
		}
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Parity)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Stripes)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.CellSize)); err != nil {
		return err
	}
	var len2 int
	if err := bincode.UnpackLength(r, &len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Crcs, len2)
	for i := 0; i < len2; i++ {
		if err := bincode.UnpackScalar(r, (*uint32)(&v.Crcs[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanInitiateResp) ShardResponseKind() ShardMessageKind {
	return ADD_SPAN_INITIATE
}

func (v *AddSpanInitiateResp) Pack(w io.Writer) error {
	len1 := len(v.Blocks)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanInitiateResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanCertifyReq) ShardRequestKind() ShardMessageKind {
	return ADD_SPAN_CERTIFY
}

func (v *AddSpanCertifyReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	len1 := len(v.Proofs)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanCertifyReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Proofs, len1)
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *AddSpanCertifyResp) ShardResponseKind() ShardMessageKind {
	return ADD_SPAN_CERTIFY
}

func (v *AddSpanCertifyResp) Pack(w io.Writer) error {
	return nil
}

func (v *AddSpanCertifyResp) Unpack(r io.Reader) error {
	return nil
}

func (v *LinkFileReq) ShardRequestKind() ShardMessageKind {
	return LINK_FILE
}

func (v *LinkFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	return nil
}

func (v *LinkFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LinkFileResp) ShardResponseKind() ShardMessageKind {
	return LINK_FILE
}

func (v *LinkFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *LinkFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileReq) ShardRequestKind() ShardMessageKind {
	return SOFT_UNLINK_FILE
}

func (v *SoftUnlinkFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileResp) ShardResponseKind() ShardMessageKind {
	return SOFT_UNLINK_FILE
}

func (v *SoftUnlinkFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DeleteCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DeleteCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *FileSpansReq) ShardRequestKind() ShardMessageKind {
	return FILE_SPANS
}

func (v *FileSpansReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Limit)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *FileSpansReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Limit)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *FileSpansResp) ShardResponseKind() ShardMessageKind {
	return FILE_SPANS
}

func (v *FileSpansResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NextOffset)); err != nil {
		return err
	}
	len1 := len(v.BlockServices)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Pack(w); err != nil {
			return err
		}
	}
	len2 := len(v.Spans)
	if err := bincode.PackLength(w, len2); err != nil {
		return err
	}
	for i := 0; i < len2; i++ {
		if err := v.Spans[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *FileSpansResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextOffset)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BlockServices, len1)
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Unpack(r); err != nil {
			return err
		}
	}
	var len2 int
	if err := bincode.UnpackLength(r, &len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Spans, len2)
	for i := 0; i < len2; i++ {
		if err := v.Spans[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *SameDirectoryRenameReq) ShardRequestKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME
}

func (v *SameDirectoryRenameReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.OldName)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.NewName)); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.OldName); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameResp) ShardResponseKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME
}

func (v *SameDirectoryRenameResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NewCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NewCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *AddInlineSpanReq) ShardRequestKind() ShardMessageKind {
	return ADD_INLINE_SPAN
}

func (v *AddInlineSpanReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *AddInlineSpanReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	if err := bincode.UnpackBytes(r, (*[]byte)(&v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *AddInlineSpanResp) ShardResponseKind() ShardMessageKind {
	return ADD_INLINE_SPAN
}

func (v *AddInlineSpanResp) Pack(w io.Writer) error {
	return nil
}

func (v *AddInlineSpanResp) Unpack(r io.Reader) error {
	return nil
}

func (v *FullReadDirReq) ShardRequestKind() ShardMessageKind {
	return FULL_READ_DIR
}

func (v *FullReadDirReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Flags)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.StartName)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.StartTime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Limit)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Flags)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.StartName); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartTime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Limit)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirResp) ShardResponseKind() ShardMessageKind {
	return FULL_READ_DIR
}

func (v *FullReadDirResp) Pack(w io.Writer) error {
	if err := v.Next.Pack(w); err != nil {
		return err
	}
	len1 := len(v.Results)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *FullReadDirResp) Unpack(r io.Reader) error {
	if err := v.Next.Unpack(r); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Results, len1)
	for i := 0; i < len1; i++ {
		if err := v.Results[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *MoveSpanReq) ShardRequestKind() ShardMessageKind {
	return MOVE_SPAN
}

func (v *MoveSpanReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint32(v.SpanSize)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.FileId1)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.FileId2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset2)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie2[:]); err != nil {
		return err
	}
	return nil
}

func (v *MoveSpanReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint32)(&v.SpanSize)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId1)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset2)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie2[:]); err != nil {
		return err
	}
	return nil
}

func (v *MoveSpanResp) ShardResponseKind() ShardMessageKind {
	return MOVE_SPAN
}

func (v *MoveSpanResp) Pack(w io.Writer) error {
	return nil
}

func (v *MoveSpanResp) Unpack(r io.Reader) error {
	return nil
}

func (v *RemoveNonOwnedEdgeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_NON_OWNED_EDGE
}

func (v *RemoveNonOwnedEdgeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveNonOwnedEdgeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveNonOwnedEdgeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_NON_OWNED_EDGE
}

func (v *RemoveNonOwnedEdgeResp) Pack(w io.Writer) error {
	return nil
}

func (v *RemoveNonOwnedEdgeResp) Unpack(r io.Reader) error {
	return nil
}

func (v *SameShardHardFileUnlinkReq) ShardRequestKind() ShardMessageKind {
	return SAME_SHARD_HARD_FILE_UNLINK
}

func (v *SameShardHardFileUnlinkReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SameShardHardFileUnlinkReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SameShardHardFileUnlinkResp) ShardResponseKind() ShardMessageKind {
	return SAME_SHARD_HARD_FILE_UNLINK
}

func (v *SameShardHardFileUnlinkResp) Pack(w io.Writer) error {
	return nil
}

func (v *SameShardHardFileUnlinkResp) Unpack(r io.Reader) error {
	return nil
}

func (v *StatTransientFileReq) ShardRequestKind() ShardMessageKind {
	return STAT_TRANSIENT_FILE
}

func (v *StatTransientFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatTransientFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *StatTransientFileResp) ShardResponseKind() ShardMessageKind {
	return STAT_TRANSIENT_FILE
}

func (v *StatTransientFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Mtime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Note)); err != nil {
		return err
	}
	return nil
}

func (v *StatTransientFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Note); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryInfoReq) ShardRequestKind() ShardMessageKind {
	return SET_DIRECTORY_INFO
}

func (v *SetDirectoryInfoReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryInfoReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryInfoResp) ShardResponseKind() ShardMessageKind {
	return SET_DIRECTORY_INFO
}

func (v *SetDirectoryInfoResp) Pack(w io.Writer) error {
	return nil
}

func (v *SetDirectoryInfoResp) Unpack(r io.Reader) error {
	return nil
}

func (v *ExpireTransientFileReq) ShardRequestKind() ShardMessageKind {
	return EXPIRE_TRANSIENT_FILE
}

func (v *ExpireTransientFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ExpireTransientFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ExpireTransientFileResp) ShardResponseKind() ShardMessageKind {
	return EXPIRE_TRANSIENT_FILE
}

func (v *ExpireTransientFileResp) Pack(w io.Writer) error {
	return nil
}

func (v *ExpireTransientFileResp) Unpack(r io.Reader) error {
	return nil
}

func (v *VisitDirectoriesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_DIRECTORIES
}

func (v *VisitDirectoriesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BeginId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitDirectoriesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BeginId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitDirectoriesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_DIRECTORIES
}

func (v *VisitDirectoriesResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NextId)); err != nil {
		return err
	}
	len1 := len(v.Ids)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := bincode.PackScalar(w, uint64(v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitDirectoriesResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Ids, len1)
	for i := 0; i < len1; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitFilesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_FILES
}

func (v *VisitFilesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BeginId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitFilesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BeginId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitFilesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_FILES
}

func (v *VisitFilesResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NextId)); err != nil {
		return err
	}
	len1 := len(v.Ids)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := bincode.PackScalar(w, uint64(v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitFilesResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Ids, len1)
	for i := 0; i < len1; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.Ids[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitTransientFilesReq) ShardRequestKind() ShardMessageKind {
	return VISIT_TRANSIENT_FILES
}

func (v *VisitTransientFilesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BeginId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitTransientFilesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BeginId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Mtu)); err != nil {
		return err
	}
	return nil
}

func (v *VisitTransientFilesResp) ShardResponseKind() ShardMessageKind {
	return VISIT_TRANSIENT_FILES
}

func (v *VisitTransientFilesResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NextId)); err != nil {
		return err
	}
	len1 := len(v.Files)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Files[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *VisitTransientFilesResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextId)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Files, len1)
	for i := 0; i < len1; i++ {
		if err := v.Files[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanInitiateReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_SPAN_INITIATE
}

func (v *RemoveSpanInitiateReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *RemoveSpanInitiateReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	return nil
}

func (v *RemoveSpanInitiateResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_SPAN_INITIATE
}

func (v *RemoveSpanInitiateResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	len1 := len(v.Blocks)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanInitiateResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanCertifyReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_SPAN_CERTIFY
}

func (v *RemoveSpanCertifyReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	len1 := len(v.Proofs)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanCertifyReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Proofs, len1)
	for i := 0; i < len1; i++ {
		if err := v.Proofs[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveSpanCertifyResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_SPAN_CERTIFY
}

func (v *RemoveSpanCertifyResp) Pack(w io.Writer) error {
	return nil
}

func (v *RemoveSpanCertifyResp) Unpack(r io.Reader) error {
	return nil
}

func (v *SwapBlocksReq) ShardRequestKind() ShardMessageKind {
	return SWAP_BLOCKS
}

func (v *SwapBlocksReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId1)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset1)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockId1)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.FileId2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockId2)); err != nil {
		return err
	}
	return nil
}

func (v *SwapBlocksReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId1)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset1)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId1)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId2)); err != nil {
		return err
	}
	return nil
}

func (v *SwapBlocksResp) ShardResponseKind() ShardMessageKind {
	return SWAP_BLOCKS
}

func (v *SwapBlocksResp) Pack(w io.Writer) error {
	return nil
}

func (v *SwapBlocksResp) Unpack(r io.Reader) error {
	return nil
}

func (v *BlockServiceFilesReq) ShardRequestKind() ShardMessageKind {
	return BLOCK_SERVICE_FILES
}

func (v *BlockServiceFilesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockServiceId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.StartFrom)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceFilesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartFrom)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceFilesResp) ShardResponseKind() ShardMessageKind {
	return BLOCK_SERVICE_FILES
}

func (v *BlockServiceFilesResp) Pack(w io.Writer) error {
	len1 := len(v.FileIds)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := bincode.PackScalar(w, uint64(v.FileIds[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *BlockServiceFilesResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.FileIds, len1)
	for i := 0; i < len1; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.FileIds[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *RemoveInodeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_INODE
}

func (v *RemoveInodeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveInodeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveInodeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_INODE
}

func (v *RemoveInodeResp) Pack(w io.Writer) error {
	return nil
}

func (v *RemoveInodeResp) Unpack(r io.Reader) error {
	return nil
}

func (v *CreateDirectoryInodeReq) ShardRequestKind() ShardMessageKind {
	return CREATE_DIRECTORY_INODE
}

func (v *CreateDirectoryInodeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *CreateDirectoryInodeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *CreateDirectoryInodeResp) ShardResponseKind() ShardMessageKind {
	return CREATE_DIRECTORY_INODE
}

func (v *CreateDirectoryInodeResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Mtime)); err != nil {
		return err
	}
	return nil
}

func (v *CreateDirectoryInodeResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Mtime)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryOwnerReq) ShardRequestKind() ShardMessageKind {
	return SET_DIRECTORY_OWNER
}

func (v *SetDirectoryOwnerReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryOwnerReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	return nil
}

func (v *SetDirectoryOwnerResp) ShardResponseKind() ShardMessageKind {
	return SET_DIRECTORY_OWNER
}

func (v *SetDirectoryOwnerResp) Pack(w io.Writer) error {
	return nil
}

func (v *SetDirectoryOwnerResp) Unpack(r io.Reader) error {
	return nil
}

func (v *RemoveDirectoryOwnerReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_DIRECTORY_OWNER
}

func (v *RemoveDirectoryOwnerReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *RemoveDirectoryOwnerReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *RemoveDirectoryOwnerResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_DIRECTORY_OWNER
}

func (v *RemoveDirectoryOwnerResp) Pack(w io.Writer) error {
	return nil
}

func (v *RemoveDirectoryOwnerResp) Unpack(r io.Reader) error {
	return nil
}

func (v *CreateLockedCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return CREATE_LOCKED_CURRENT_EDGE
}

func (v *CreateLockedCurrentEdgeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CreateLockedCurrentEdgeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CreateLockedCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return CREATE_LOCKED_CURRENT_EDGE
}

func (v *CreateLockedCurrentEdgeResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CreateLockedCurrentEdgeResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *LockCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return LOCK_CURRENT_EDGE
}

func (v *LockCurrentEdgeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	return nil
}

func (v *LockCurrentEdgeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	return nil
}

func (v *LockCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return LOCK_CURRENT_EDGE
}

func (v *LockCurrentEdgeResp) Pack(w io.Writer) error {
	return nil
}

func (v *LockCurrentEdgeResp) Unpack(r io.Reader) error {
	return nil
}

func (v *UnlockCurrentEdgeReq) ShardRequestKind() ShardMessageKind {
	return UNLOCK_CURRENT_EDGE
}

func (v *UnlockCurrentEdgeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, bool(v.WasMoved)); err != nil {
		return err
	}
	return nil
}

func (v *UnlockCurrentEdgeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*bool)(&v.WasMoved)); err != nil {
		return err
	}
	return nil
}

func (v *UnlockCurrentEdgeResp) ShardResponseKind() ShardMessageKind {
	return UNLOCK_CURRENT_EDGE
}

func (v *UnlockCurrentEdgeResp) Pack(w io.Writer) error {
	return nil
}

func (v *UnlockCurrentEdgeResp) Unpack(r io.Reader) error {
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_OWNED_SNAPSHOT_FILE_EDGE
}

func (v *RemoveOwnedSnapshotFileEdgeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_OWNED_SNAPSHOT_FILE_EDGE
}

func (v *RemoveOwnedSnapshotFileEdgeResp) Pack(w io.Writer) error {
	return nil
}

func (v *RemoveOwnedSnapshotFileEdgeResp) Unpack(r io.Reader) error {
	return nil
}

func (v *MakeFileTransientReq) ShardRequestKind() ShardMessageKind {
	return MAKE_FILE_TRANSIENT
}

func (v *MakeFileTransientReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Note)); err != nil {
		return err
	}
	return nil
}

func (v *MakeFileTransientReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Note); err != nil {
		return err
	}
	return nil
}

func (v *MakeFileTransientResp) ShardResponseKind() ShardMessageKind {
	return MAKE_FILE_TRANSIENT
}

func (v *MakeFileTransientResp) Pack(w io.Writer) error {
	return nil
}

func (v *MakeFileTransientResp) Unpack(r io.Reader) error {
	return nil
}

func (v *MakeDirectoryReq) CDCRequestKind() CDCMessageKind {
	return MAKE_DIRECTORY
}

func (v *MakeDirectoryReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	return nil
}

func (v *MakeDirectoryReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	return nil
}

func (v *MakeDirectoryResp) CDCResponseKind() CDCMessageKind {
	return MAKE_DIRECTORY
}

func (v *MakeDirectoryResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *MakeDirectoryResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileReq) CDCRequestKind() CDCMessageKind {
	return RENAME_FILE
}

func (v *RenameFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldOwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.OldName)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NewOwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.NewName)); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldOwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.OldName); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileResp) CDCResponseKind() CDCMessageKind {
	return RENAME_FILE
}

func (v *RenameFileResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RenameFileResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryReq) CDCRequestKind() CDCMessageKind {
	return SOFT_UNLINK_DIRECTORY
}

func (v *SoftUnlinkDirectoryReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	return nil
}

func (v *SoftUnlinkDirectoryResp) CDCResponseKind() CDCMessageKind {
	return SOFT_UNLINK_DIRECTORY
}

func (v *SoftUnlinkDirectoryResp) Pack(w io.Writer) error {
	return nil
}

func (v *SoftUnlinkDirectoryResp) Unpack(r io.Reader) error {
	return nil
}

func (v *RenameDirectoryReq) CDCRequestKind() CDCMessageKind {
	return RENAME_DIRECTORY
}

func (v *RenameDirectoryReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldOwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.OldName)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NewOwnerId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.NewName)); err != nil {
		return err
	}
	return nil
}

func (v *RenameDirectoryReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldOwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.OldName); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OldCreationTime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NewOwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.NewName); err != nil {
		return err
	}
	return nil
}

func (v *RenameDirectoryResp) CDCResponseKind() CDCMessageKind {
	return RENAME_DIRECTORY
}

func (v *RenameDirectoryResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *RenameDirectoryResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *HardUnlinkDirectoryReq) CDCRequestKind() CDCMessageKind {
	return HARD_UNLINK_DIRECTORY
}

func (v *HardUnlinkDirectoryReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DirId)); err != nil {
		return err
	}
	return nil
}

func (v *HardUnlinkDirectoryReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DirId)); err != nil {
		return err
	}
	return nil
}

func (v *HardUnlinkDirectoryResp) CDCResponseKind() CDCMessageKind {
	return HARD_UNLINK_DIRECTORY
}

func (v *HardUnlinkDirectoryResp) Pack(w io.Writer) error {
	return nil
}

func (v *HardUnlinkDirectoryResp) Unpack(r io.Reader) error {
	return nil
}

func (v *CrossShardHardUnlinkFileReq) CDCRequestKind() CDCMessageKind {
	return CROSS_SHARD_HARD_UNLINK_FILE
}

func (v *CrossShardHardUnlinkFileReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CrossShardHardUnlinkFileReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.OwnerId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CrossShardHardUnlinkFileResp) CDCResponseKind() CDCMessageKind {
	return CROSS_SHARD_HARD_UNLINK_FILE
}

func (v *CrossShardHardUnlinkFileResp) Pack(w io.Writer) error {
	return nil
}

func (v *CrossShardHardUnlinkFileResp) Unpack(r io.Reader) error {
	return nil
}

func (v *FailureDomain) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 16, v.Name[:]); err != nil {
		return err
	}
	return nil
}

func (v *FailureDomain) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 16, v.Name[:]); err != nil {
		return err
	}
	return nil
}

func (v *DirectoryInfoEntry) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Tag)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *DirectoryInfoEntry) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Tag)); err != nil {
		return err
	}
	if err := bincode.UnpackBytes(r, (*[]byte)(&v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *DirectoryInfo) Pack(w io.Writer) error {
	len1 := len(v.Entries)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *DirectoryInfo) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Entries, len1)
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *CurrentEdge) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NameHash)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *CurrentEdge) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NameHash)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *BlockInfo) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.BlockServiceIp1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.BlockServicePort1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.BlockServiceIp2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.BlockServicePort2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockServiceId)); err != nil {
		return err
	}
	if err := v.BlockServiceFailureDomain.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *BlockInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.BlockServiceIp1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.BlockServicePort1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.BlockServiceIp2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.BlockServicePort2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := v.BlockServiceFailureDomain.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *BlockProof) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *BlockProof) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *BlockService) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Flags)); err != nil {
		return err
	}
	return nil
}

func (v *BlockService) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Flags)); err != nil {
		return err
	}
	return nil
}

func (v *ShardInfo) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *ShardInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *BlockPolicyEntry) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.MinSize)); err != nil {
		return err
	}
	return nil
}

func (v *BlockPolicyEntry) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.MinSize)); err != nil {
		return err
	}
	return nil
}

func (v *SpanPolicyEntry) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint32(v.MaxSize)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Parity)); err != nil {
		return err
	}
	return nil
}

func (v *SpanPolicyEntry) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint32)(&v.MaxSize)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Parity)); err != nil {
		return err
	}
	return nil
}

func (v *StripePolicy) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint32(v.TargetStripeSize)); err != nil {
		return err
	}
	return nil
}

func (v *StripePolicy) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint32)(&v.TargetStripeSize)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedBlock) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.BlockServiceIx)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedBlock) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.BlockServiceIx)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedSpanHeader) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedSpanHeader) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedInlineSpan) Pack(w io.Writer) error {
	if err := bincode.PackBytes(w, []byte(v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedInlineSpan) Unpack(r io.Reader) error {
	if err := bincode.UnpackBytes(r, (*[]byte)(&v.Body)); err != nil {
		return err
	}
	return nil
}

func (v *FetchedBlocksSpan) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Parity)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Stripes)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.CellSize)); err != nil {
		return err
	}
	len1 := len(v.Blocks)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Pack(w); err != nil {
			return err
		}
	}
	len2 := len(v.StripesCrc)
	if err := bincode.PackLength(w, len2); err != nil {
		return err
	}
	for i := 0; i < len2; i++ {
		if err := bincode.PackScalar(w, uint32(v.StripesCrc[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *FetchedBlocksSpan) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Parity)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Stripes)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.CellSize)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks, len1)
	for i := 0; i < len1; i++ {
		if err := v.Blocks[i].Unpack(r); err != nil {
			return err
		}
	}
	var len2 int
	if err := bincode.UnpackLength(r, &len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.StripesCrc, len2)
	for i := 0; i < len2; i++ {
		if err := bincode.UnpackScalar(r, (*uint32)(&v.StripesCrc[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *BlacklistEntry) Pack(w io.Writer) error {
	if err := v.FailureDomain.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockService)); err != nil {
		return err
	}
	return nil
}

func (v *BlacklistEntry) Unpack(r io.Reader) error {
	if err := v.FailureDomain.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockService)); err != nil {
		return err
	}
	return nil
}

func (v *Edge) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, bool(v.Current)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.TargetId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NameHash)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Name)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *Edge) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*bool)(&v.Current)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.TargetId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NameHash)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Name); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirCursor) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, bool(v.Current)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.StartName)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.StartTime)); err != nil {
		return err
	}
	return nil
}

func (v *FullReadDirCursor) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*bool)(&v.Current)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.StartName); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartTime)); err != nil {
		return err
	}
	return nil
}

func (v *TransientFile) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.DeadlineTime)); err != nil {
		return err
	}
	return nil
}

func (v *TransientFile) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Cookie[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DeadlineTime)); err != nil {
		return err
	}
	return nil
}

func (v *EntryNewBlockInfo) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockServiceId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *EntryNewBlockInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceInfo) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 16, v.FailureDomain[:]); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 16, v.SecretKey[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Flags)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.CapacityBytes)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.AvailableBytes)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Blocks)); err != nil {
		return err
	}
	if err := bincode.PackBytes(w, []byte(v.Path)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 16, v.FailureDomain[:]); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 16, v.SecretKey[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Flags)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.CapacityBytes)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.AvailableBytes)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Blocks)); err != nil {
		return err
	}
	if err := bincode.UnpackString(r, &v.Path); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterShardInfo) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterShardInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	return nil
}

func (v *SpanPolicy) Pack(w io.Writer) error {
	len1 := len(v.Entries)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *SpanPolicy) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Entries, len1)
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *BlockPolicy) Pack(w io.Writer) error {
	len1 := len(v.Entries)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *BlockPolicy) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Entries, len1)
	for i := 0; i < len1; i++ {
		if err := v.Entries[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *SnapshotPolicy) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.DeleteAfterTime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.DeleteAfterVersions)); err != nil {
		return err
	}
	return nil
}

func (v *SnapshotPolicy) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.DeleteAfterTime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.DeleteAfterVersions)); err != nil {
		return err
	}
	return nil
}

func (v *ShardsReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHARDS
}

func (v *ShardsReq) Pack(w io.Writer) error {
	return nil
}

func (v *ShardsReq) Unpack(r io.Reader) error {
	return nil
}

func (v *ShardsResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHARDS
}

func (v *ShardsResp) Pack(w io.Writer) error {
	len1 := len(v.Shards)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Shards[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *ShardsResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Shards, len1)
	for i := 0; i < len1; i++ {
		if err := v.Shards[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *CdcReq) ShuckleRequestKind() ShuckleMessageKind {
	return CDC
}

func (v *CdcReq) Pack(w io.Writer) error {
	return nil
}

func (v *CdcReq) Unpack(r io.Reader) error {
	return nil
}

func (v *CdcResp) ShuckleResponseKind() ShuckleMessageKind {
	return CDC
}

func (v *CdcResp) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *CdcResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *InfoReq) ShuckleRequestKind() ShuckleMessageKind {
	return INFO
}

func (v *InfoReq) Pack(w io.Writer) error {
	return nil
}

func (v *InfoReq) Unpack(r io.Reader) error {
	return nil
}

func (v *InfoResp) ShuckleResponseKind() ShuckleMessageKind {
	return INFO
}

func (v *InfoResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint32(v.NumBlockServices)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.NumFailureDomains)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Capacity)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Available)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Blocks)); err != nil {
		return err
	}
	return nil
}

func (v *InfoResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint32)(&v.NumBlockServices)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.NumFailureDomains)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Capacity)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Available)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Blocks)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterBlockServicesReq) ShuckleRequestKind() ShuckleMessageKind {
	return REGISTER_BLOCK_SERVICES
}

func (v *RegisterBlockServicesReq) Pack(w io.Writer) error {
	len1 := len(v.BlockServices)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *RegisterBlockServicesReq) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BlockServices, len1)
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *RegisterBlockServicesResp) ShuckleResponseKind() ShuckleMessageKind {
	return REGISTER_BLOCK_SERVICES
}

func (v *RegisterBlockServicesResp) Pack(w io.Writer) error {
	return nil
}

func (v *RegisterBlockServicesResp) Unpack(r io.Reader) error {
	return nil
}

func (v *RegisterShardReq) ShuckleRequestKind() ShuckleMessageKind {
	return REGISTER_SHARD
}

func (v *RegisterShardReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Id)); err != nil {
		return err
	}
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *RegisterShardReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Id)); err != nil {
		return err
	}
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *RegisterShardResp) ShuckleResponseKind() ShuckleMessageKind {
	return REGISTER_SHARD
}

func (v *RegisterShardResp) Pack(w io.Writer) error {
	return nil
}

func (v *RegisterShardResp) Unpack(r io.Reader) error {
	return nil
}

func (v *AllBlockServicesReq) ShuckleRequestKind() ShuckleMessageKind {
	return ALL_BLOCK_SERVICES
}

func (v *AllBlockServicesReq) Pack(w io.Writer) error {
	return nil
}

func (v *AllBlockServicesReq) Unpack(r io.Reader) error {
	return nil
}

func (v *AllBlockServicesResp) ShuckleResponseKind() ShuckleMessageKind {
	return ALL_BLOCK_SERVICES
}

func (v *AllBlockServicesResp) Pack(w io.Writer) error {
	len1 := len(v.BlockServices)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *AllBlockServicesResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BlockServices, len1)
	for i := 0; i < len1; i++ {
		if err := v.BlockServices[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *RegisterCdcReq) ShuckleRequestKind() ShuckleMessageKind {
	return REGISTER_CDC
}

func (v *RegisterCdcReq) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port1)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.CurrentTransactionKind)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.CurrentTransactionStep)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.QueuedTransactions)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterCdcReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip1[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port1)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 4, v.Ip2[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.CurrentTransactionKind)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.CurrentTransactionStep)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.QueuedTransactions)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterCdcResp) ShuckleResponseKind() ShuckleMessageKind {
	return REGISTER_CDC
}

func (v *RegisterCdcResp) Pack(w io.Writer) error {
	return nil
}

func (v *RegisterCdcResp) Unpack(r io.Reader) error {
	return nil
}

func (v *SetBlockServiceFlagsReq) ShuckleRequestKind() ShuckleMessageKind {
	return SET_BLOCK_SERVICE_FLAGS
}

func (v *SetBlockServiceFlagsReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Flags)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.FlagsMask)); err != nil {
		return err
	}
	return nil
}

func (v *SetBlockServiceFlagsReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Flags)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.FlagsMask)); err != nil {
		return err
	}
	return nil
}

func (v *SetBlockServiceFlagsResp) ShuckleResponseKind() ShuckleMessageKind {
	return SET_BLOCK_SERVICE_FLAGS
}

func (v *SetBlockServiceFlagsResp) Pack(w io.Writer) error {
	return nil
}

func (v *SetBlockServiceFlagsResp) Unpack(r io.Reader) error {
	return nil
}

func (v *FetchBlockReq) BlocksRequestKind() BlocksMessageKind {
	return FETCH_BLOCK
}

func (v *FetchBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Offset)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Count)); err != nil {
		return err
	}
	return nil
}

func (v *FetchBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Offset)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Count)); err != nil {
		return err
	}
	return nil
}

func (v *FetchBlockResp) BlocksResponseKind() BlocksMessageKind {
	return FETCH_BLOCK
}

func (v *FetchBlockResp) Pack(w io.Writer) error {
	return nil
}

func (v *FetchBlockResp) Unpack(r io.Reader) error {
	return nil
}

func (v *WriteBlockReq) BlocksRequestKind() BlocksMessageKind {
	return WRITE_BLOCK
}

func (v *WriteBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *WriteBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *WriteBlockResp) BlocksResponseKind() BlocksMessageKind {
	return WRITE_BLOCK
}

func (v *WriteBlockResp) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *WriteBlockResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *EraseBlockReq) BlocksRequestKind() BlocksMessageKind {
	return ERASE_BLOCK
}

func (v *EraseBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *EraseBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 8, v.Certificate[:]); err != nil {
		return err
	}
	return nil
}

func (v *EraseBlockResp) BlocksResponseKind() BlocksMessageKind {
	return ERASE_BLOCK
}

func (v *EraseBlockResp) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *EraseBlockResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *TestWriteReq) BlocksRequestKind() BlocksMessageKind {
	return TEST_WRITE
}

func (v *TestWriteReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Size)); err != nil {
		return err
	}
	return nil
}

func (v *TestWriteReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Size)); err != nil {
		return err
	}
	return nil
}

func (v *TestWriteResp) BlocksResponseKind() BlocksMessageKind {
	return TEST_WRITE
}

func (v *TestWriteResp) Pack(w io.Writer) error {
	return nil
}

func (v *TestWriteResp) Unpack(r io.Reader) error {
	return nil
}

