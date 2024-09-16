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
	case STRIPE_POLICY_TAG:
		return &StripePolicy{}
	default:
		panic(fmt.Errorf("unknown policy tag %v", tag))
	}
}

func (err EggsError) Error() string {
	return err.String()
}

func (err *EggsError) Pack(w io.Writer) error {
	return bincode.PackScalar(w, uint16(*err))
}

func (errCode *EggsError) Unpack(r io.Reader) error {
	var c uint16
	if err := bincode.UnpackScalar(r, &c); err != nil {
		return err
	}
	*errCode = EggsError(c)
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
	INTERNAL_ERROR EggsError = 10
	FATAL_ERROR EggsError = 11
	TIMEOUT EggsError = 12
	MALFORMED_REQUEST EggsError = 13
	MALFORMED_RESPONSE EggsError = 14
	NOT_AUTHORISED EggsError = 15
	UNRECOGNIZED_REQUEST EggsError = 16
	FILE_NOT_FOUND EggsError = 17
	DIRECTORY_NOT_FOUND EggsError = 18
	NAME_NOT_FOUND EggsError = 19
	EDGE_NOT_FOUND EggsError = 20
	EDGE_IS_LOCKED EggsError = 21
	TYPE_IS_DIRECTORY EggsError = 22
	TYPE_IS_NOT_DIRECTORY EggsError = 23
	BAD_COOKIE EggsError = 24
	INCONSISTENT_STORAGE_CLASS_PARITY EggsError = 25
	LAST_SPAN_STATE_NOT_CLEAN EggsError = 26
	COULD_NOT_PICK_BLOCK_SERVICES EggsError = 27
	BAD_SPAN_BODY EggsError = 28
	SPAN_NOT_FOUND EggsError = 29
	BLOCK_SERVICE_NOT_FOUND EggsError = 30
	CANNOT_CERTIFY_BLOCKLESS_SPAN EggsError = 31
	BAD_NUMBER_OF_BLOCKS_PROOFS EggsError = 32
	BAD_BLOCK_PROOF EggsError = 33
	CANNOT_OVERRIDE_NAME EggsError = 34
	NAME_IS_LOCKED EggsError = 35
	MTIME_IS_TOO_RECENT EggsError = 36
	MISMATCHING_TARGET EggsError = 37
	MISMATCHING_OWNER EggsError = 38
	MISMATCHING_CREATION_TIME EggsError = 39
	DIRECTORY_NOT_EMPTY EggsError = 40
	FILE_IS_TRANSIENT EggsError = 41
	OLD_DIRECTORY_NOT_FOUND EggsError = 42
	NEW_DIRECTORY_NOT_FOUND EggsError = 43
	LOOP_IN_DIRECTORY_RENAME EggsError = 44
	DIRECTORY_HAS_OWNER EggsError = 45
	FILE_IS_NOT_TRANSIENT EggsError = 46
	FILE_NOT_EMPTY EggsError = 47
	CANNOT_REMOVE_ROOT_DIRECTORY EggsError = 48
	FILE_EMPTY EggsError = 49
	CANNOT_REMOVE_DIRTY_SPAN EggsError = 50
	BAD_SHARD EggsError = 51
	BAD_NAME EggsError = 52
	MORE_RECENT_SNAPSHOT_EDGE EggsError = 53
	MORE_RECENT_CURRENT_EDGE EggsError = 54
	BAD_DIRECTORY_INFO EggsError = 55
	DEADLINE_NOT_PASSED EggsError = 56
	SAME_SOURCE_AND_DESTINATION EggsError = 57
	SAME_DIRECTORIES EggsError = 58
	SAME_SHARD EggsError = 59
	BAD_PROTOCOL_VERSION EggsError = 60
	BAD_CERTIFICATE EggsError = 61
	BLOCK_TOO_RECENT_FOR_DELETION EggsError = 62
	BLOCK_FETCH_OUT_OF_BOUNDS EggsError = 63
	BAD_BLOCK_CRC EggsError = 64
	BLOCK_TOO_BIG EggsError = 65
	BLOCK_NOT_FOUND EggsError = 66
	CANNOT_UNSET_DECOMMISSIONED EggsError = 67
	CANNOT_REGISTER_DECOMMISSIONED_OR_STALE EggsError = 68
	BLOCK_TOO_OLD_FOR_WRITE EggsError = 69
	BLOCK_IO_ERROR_DEVICE EggsError = 70
	BLOCK_IO_ERROR_FILE EggsError = 71
	INVALID_REPLICA EggsError = 72
	DIFFERENT_ADDRS_INFO EggsError = 73
	LEADER_PREEMPTED EggsError = 74
	LOG_ENTRY_MISSING EggsError = 75
	LOG_ENTRY_TRIMMED EggsError = 76
	LOG_ENTRY_UNRELEASED EggsError = 77
	LOG_ENTRY_RELEASED EggsError = 78
	AUTO_DECOMMISSION_FORBIDDEN EggsError = 79
	INCONSISTENT_BLOCK_SERVICE_REGISTRATION EggsError = 80
	SWAP_BLOCKS_INLINE_STORAGE EggsError = 81
	SWAP_BLOCKS_MISMATCHING_SIZE EggsError = 82
	SWAP_BLOCKS_MISMATCHING_STATE EggsError = 83
	SWAP_BLOCKS_MISMATCHING_CRC EggsError = 84
	SWAP_BLOCKS_DUPLICATE_BLOCK_SERVICE EggsError = 85
	SWAP_SPANS_INLINE_STORAGE EggsError = 86
	SWAP_SPANS_MISMATCHING_SIZE EggsError = 87
	SWAP_SPANS_NOT_CLEAN EggsError = 88
	SWAP_SPANS_MISMATCHING_CRC EggsError = 89
	SWAP_SPANS_MISMATCHING_BLOCKS EggsError = 90
	EDGE_NOT_OWNED EggsError = 91
	CANNOT_CREATE_DB_SNAPSHOT EggsError = 92
	BLOCK_SIZE_NOT_MULTIPLE_OF_PAGE_SIZE EggsError = 93
)

func (err EggsError) String() string {
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
	case 67:
		return "CANNOT_UNSET_DECOMMISSIONED"
	case 68:
		return "CANNOT_REGISTER_DECOMMISSIONED_OR_STALE"
	case 69:
		return "BLOCK_TOO_OLD_FOR_WRITE"
	case 70:
		return "BLOCK_IO_ERROR_DEVICE"
	case 71:
		return "BLOCK_IO_ERROR_FILE"
	case 72:
		return "INVALID_REPLICA"
	case 73:
		return "DIFFERENT_ADDRS_INFO"
	case 74:
		return "LEADER_PREEMPTED"
	case 75:
		return "LOG_ENTRY_MISSING"
	case 76:
		return "LOG_ENTRY_TRIMMED"
	case 77:
		return "LOG_ENTRY_UNRELEASED"
	case 78:
		return "LOG_ENTRY_RELEASED"
	case 79:
		return "AUTO_DECOMMISSION_FORBIDDEN"
	case 80:
		return "INCONSISTENT_BLOCK_SERVICE_REGISTRATION"
	case 81:
		return "SWAP_BLOCKS_INLINE_STORAGE"
	case 82:
		return "SWAP_BLOCKS_MISMATCHING_SIZE"
	case 83:
		return "SWAP_BLOCKS_MISMATCHING_STATE"
	case 84:
		return "SWAP_BLOCKS_MISMATCHING_CRC"
	case 85:
		return "SWAP_BLOCKS_DUPLICATE_BLOCK_SERVICE"
	case 86:
		return "SWAP_SPANS_INLINE_STORAGE"
	case 87:
		return "SWAP_SPANS_MISMATCHING_SIZE"
	case 88:
		return "SWAP_SPANS_NOT_CLEAN"
	case 89:
		return "SWAP_SPANS_MISMATCHING_CRC"
	case 90:
		return "SWAP_SPANS_MISMATCHING_BLOCKS"
	case 91:
		return "EDGE_NOT_OWNED"
	case 92:
		return "CANNOT_CREATE_DB_SNAPSHOT"
	case 93:
		return "BLOCK_SIZE_NOT_MULTIPLE_OF_PAGE_SIZE"
	default:
		return fmt.Sprintf("EggsError(%d)", err)
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
	case 17:
		return "SET_TIME"
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
	case 18:
		return "SHARD_SNAPSHOT"
	case 13:
		return "SET_DIRECTORY_INFO"
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
	case 124:
		return "ADD_SPAN_INITIATE_WITH_REFERENCE"
	case 125:
		return "REMOVE_ZERO_BLOCK_SERVICE_FILES"
	case 126:
		return "SWAP_SPANS"
	case 127:
		return "SAME_DIRECTORY_RENAME_SNAPSHOT"
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
	SET_TIME ShardMessageKind = 0x11
	FULL_READ_DIR ShardMessageKind = 0x73
	MOVE_SPAN ShardMessageKind = 0x7B
	REMOVE_NON_OWNED_EDGE ShardMessageKind = 0x74
	SAME_SHARD_HARD_FILE_UNLINK ShardMessageKind = 0x75
	STAT_TRANSIENT_FILE ShardMessageKind = 0x3
	SHARD_SNAPSHOT ShardMessageKind = 0x12
	SET_DIRECTORY_INFO ShardMessageKind = 0xD
	VISIT_DIRECTORIES ShardMessageKind = 0x70
	VISIT_FILES ShardMessageKind = 0x71
	VISIT_TRANSIENT_FILES ShardMessageKind = 0x72
	REMOVE_SPAN_INITIATE ShardMessageKind = 0x76
	REMOVE_SPAN_CERTIFY ShardMessageKind = 0x77
	SWAP_BLOCKS ShardMessageKind = 0x78
	BLOCK_SERVICE_FILES ShardMessageKind = 0x79
	REMOVE_INODE ShardMessageKind = 0x7A
	ADD_SPAN_INITIATE_WITH_REFERENCE ShardMessageKind = 0x7C
	REMOVE_ZERO_BLOCK_SERVICE_FILES ShardMessageKind = 0x7D
	SWAP_SPANS ShardMessageKind = 0x7E
	SAME_DIRECTORY_RENAME_SNAPSHOT ShardMessageKind = 0x7F
	CREATE_DIRECTORY_INODE ShardMessageKind = 0x80
	SET_DIRECTORY_OWNER ShardMessageKind = 0x81
	REMOVE_DIRECTORY_OWNER ShardMessageKind = 0x89
	CREATE_LOCKED_CURRENT_EDGE ShardMessageKind = 0x82
	LOCK_CURRENT_EDGE ShardMessageKind = 0x83
	UNLOCK_CURRENT_EDGE ShardMessageKind = 0x84
	REMOVE_OWNED_SNAPSHOT_FILE_EDGE ShardMessageKind = 0x86
	MAKE_FILE_TRANSIENT ShardMessageKind = 0x87
)

var AllShardMessageKind = [...]ShardMessageKind{
	LOOKUP,
	STAT_FILE,
	STAT_DIRECTORY,
	READ_DIR,
	CONSTRUCT_FILE,
	ADD_SPAN_INITIATE,
	ADD_SPAN_CERTIFY,
	LINK_FILE,
	SOFT_UNLINK_FILE,
	FILE_SPANS,
	SAME_DIRECTORY_RENAME,
	ADD_INLINE_SPAN,
	SET_TIME,
	FULL_READ_DIR,
	MOVE_SPAN,
	REMOVE_NON_OWNED_EDGE,
	SAME_SHARD_HARD_FILE_UNLINK,
	STAT_TRANSIENT_FILE,
	SHARD_SNAPSHOT,
	SET_DIRECTORY_INFO,
	VISIT_DIRECTORIES,
	VISIT_FILES,
	VISIT_TRANSIENT_FILES,
	REMOVE_SPAN_INITIATE,
	REMOVE_SPAN_CERTIFY,
	SWAP_BLOCKS,
	BLOCK_SERVICE_FILES,
	REMOVE_INODE,
	ADD_SPAN_INITIATE_WITH_REFERENCE,
	REMOVE_ZERO_BLOCK_SERVICE_FILES,
	SWAP_SPANS,
	SAME_DIRECTORY_RENAME_SNAPSHOT,
	CREATE_DIRECTORY_INODE,
	SET_DIRECTORY_OWNER,
	REMOVE_DIRECTORY_OWNER,
	CREATE_LOCKED_CURRENT_EDGE,
	LOCK_CURRENT_EDGE,
	UNLOCK_CURRENT_EDGE,
	REMOVE_OWNED_SNAPSHOT_FILE_EDGE,
	MAKE_FILE_TRANSIENT,
}

const MaxShardMessageKind ShardMessageKind = 137

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
	case k == "SET_TIME":
		return &SetTimeReq{}, &SetTimeResp{}, nil
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
	case k == "SHARD_SNAPSHOT":
		return &ShardSnapshotReq{}, &ShardSnapshotResp{}, nil
	case k == "SET_DIRECTORY_INFO":
		return &SetDirectoryInfoReq{}, &SetDirectoryInfoResp{}, nil
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
	case k == "ADD_SPAN_INITIATE_WITH_REFERENCE":
		return &AddSpanInitiateWithReferenceReq{}, &AddSpanInitiateWithReferenceResp{}, nil
	case k == "REMOVE_ZERO_BLOCK_SERVICE_FILES":
		return &RemoveZeroBlockServiceFilesReq{}, &RemoveZeroBlockServiceFilesResp{}, nil
	case k == "SWAP_SPANS":
		return &SwapSpansReq{}, &SwapSpansResp{}, nil
	case k == "SAME_DIRECTORY_RENAME_SNAPSHOT":
		return &SameDirectoryRenameSnapshotReq{}, &SameDirectoryRenameSnapshotResp{}, nil
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
	case 7:
		return "CDC_SNAPSHOT"
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
	CDC_SNAPSHOT CDCMessageKind = 0x7
)

var AllCDCMessageKind = [...]CDCMessageKind{
	MAKE_DIRECTORY,
	RENAME_FILE,
	SOFT_UNLINK_DIRECTORY,
	RENAME_DIRECTORY,
	HARD_UNLINK_DIRECTORY,
	CROSS_SHARD_HARD_UNLINK_FILE,
	CDC_SNAPSHOT,
}

const MaxCDCMessageKind CDCMessageKind = 7

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
	case k == "CDC_SNAPSHOT":
		return &CdcSnapshotReq{}, &CdcSnapshotResp{}, nil
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
	case 15:
		return "SHUCKLE"
	case 34:
		return "BLOCK_SERVICES_WITH_FLAG_CHANGE"
	case 4:
		return "REGISTER_SHARD"
	case 6:
		return "REGISTER_CDC"
	case 9:
		return "SET_BLOCK_SERVICE_FLAGS"
	case 10:
		return "BLOCK_SERVICE"
	case 12:
		return "SHARD"
	case 16:
		return "SHARD_REPLICAS"
	case 17:
		return "SHARD_BLOCK_SERVICES"
	case 19:
		return "CDC_REPLICAS"
	case 20:
		return "SHARDS_WITH_REPLICAS"
	case 21:
		return "SET_BLOCK_SERVICE_DECOMMISSIONED"
	case 22:
		return "MOVE_SHARD_LEADER"
	case 23:
		return "CLEAR_SHARD_INFO"
	case 24:
		return "REGISTER_BLOCK_SERVICES"
	case 25:
		return "CDC_WITH_REPLICAS"
	case 32:
		return "ERASE_DECOMMISSIONED_BLOCK"
	case 33:
		return "ALL_BLOCK_SERVICES"
	case 35:
		return "MOVE_CDC_LEADER"
	case 36:
		return "CLEAR_CD_CINFO"
	default:
		return fmt.Sprintf("ShuckleMessageKind(%d)", k)
	}
}


const (
	SHARDS ShuckleMessageKind = 0x3
	CDC ShuckleMessageKind = 0x7
	INFO ShuckleMessageKind = 0x8
	SHUCKLE ShuckleMessageKind = 0xF
	BLOCK_SERVICES_WITH_FLAG_CHANGE ShuckleMessageKind = 0x22
	REGISTER_SHARD ShuckleMessageKind = 0x4
	REGISTER_CDC ShuckleMessageKind = 0x6
	SET_BLOCK_SERVICE_FLAGS ShuckleMessageKind = 0x9
	BLOCK_SERVICE ShuckleMessageKind = 0xA
	SHARD ShuckleMessageKind = 0xC
	SHARD_REPLICAS ShuckleMessageKind = 0x10
	SHARD_BLOCK_SERVICES ShuckleMessageKind = 0x11
	CDC_REPLICAS ShuckleMessageKind = 0x13
	SHARDS_WITH_REPLICAS ShuckleMessageKind = 0x14
	SET_BLOCK_SERVICE_DECOMMISSIONED ShuckleMessageKind = 0x15
	MOVE_SHARD_LEADER ShuckleMessageKind = 0x16
	CLEAR_SHARD_INFO ShuckleMessageKind = 0x17
	REGISTER_BLOCK_SERVICES ShuckleMessageKind = 0x18
	CDC_WITH_REPLICAS ShuckleMessageKind = 0x19
	ERASE_DECOMMISSIONED_BLOCK ShuckleMessageKind = 0x20
	ALL_BLOCK_SERVICES ShuckleMessageKind = 0x21
	MOVE_CDC_LEADER ShuckleMessageKind = 0x23
	CLEAR_CD_CINFO ShuckleMessageKind = 0x24
)

var AllShuckleMessageKind = [...]ShuckleMessageKind{
	SHARDS,
	CDC,
	INFO,
	SHUCKLE,
	BLOCK_SERVICES_WITH_FLAG_CHANGE,
	REGISTER_SHARD,
	REGISTER_CDC,
	SET_BLOCK_SERVICE_FLAGS,
	BLOCK_SERVICE,
	SHARD,
	SHARD_REPLICAS,
	SHARD_BLOCK_SERVICES,
	CDC_REPLICAS,
	SHARDS_WITH_REPLICAS,
	SET_BLOCK_SERVICE_DECOMMISSIONED,
	MOVE_SHARD_LEADER,
	CLEAR_SHARD_INFO,
	REGISTER_BLOCK_SERVICES,
	CDC_WITH_REPLICAS,
	ERASE_DECOMMISSIONED_BLOCK,
	ALL_BLOCK_SERVICES,
	MOVE_CDC_LEADER,
	CLEAR_CD_CINFO,
}

const MaxShuckleMessageKind ShuckleMessageKind = 36

func MkShuckleMessage(k string) (ShuckleRequest, ShuckleResponse, error) {
	switch {
	case k == "SHARDS":
		return &ShardsReq{}, &ShardsResp{}, nil
	case k == "CDC":
		return &CdcReq{}, &CdcResp{}, nil
	case k == "INFO":
		return &InfoReq{}, &InfoResp{}, nil
	case k == "SHUCKLE":
		return &ShuckleReq{}, &ShuckleResp{}, nil
	case k == "BLOCK_SERVICES_WITH_FLAG_CHANGE":
		return &BlockServicesWithFlagChangeReq{}, &BlockServicesWithFlagChangeResp{}, nil
	case k == "REGISTER_SHARD":
		return &RegisterShardReq{}, &RegisterShardResp{}, nil
	case k == "REGISTER_CDC":
		return &RegisterCdcReq{}, &RegisterCdcResp{}, nil
	case k == "SET_BLOCK_SERVICE_FLAGS":
		return &SetBlockServiceFlagsReq{}, &SetBlockServiceFlagsResp{}, nil
	case k == "BLOCK_SERVICE":
		return &BlockServiceReq{}, &BlockServiceResp{}, nil
	case k == "SHARD":
		return &ShardReq{}, &ShardResp{}, nil
	case k == "SHARD_REPLICAS":
		return &ShardReplicasReq{}, &ShardReplicasResp{}, nil
	case k == "SHARD_BLOCK_SERVICES":
		return &ShardBlockServicesReq{}, &ShardBlockServicesResp{}, nil
	case k == "CDC_REPLICAS":
		return &CdcReplicasReq{}, &CdcReplicasResp{}, nil
	case k == "SHARDS_WITH_REPLICAS":
		return &ShardsWithReplicasReq{}, &ShardsWithReplicasResp{}, nil
	case k == "SET_BLOCK_SERVICE_DECOMMISSIONED":
		return &SetBlockServiceDecommissionedReq{}, &SetBlockServiceDecommissionedResp{}, nil
	case k == "MOVE_SHARD_LEADER":
		return &MoveShardLeaderReq{}, &MoveShardLeaderResp{}, nil
	case k == "CLEAR_SHARD_INFO":
		return &ClearShardInfoReq{}, &ClearShardInfoResp{}, nil
	case k == "REGISTER_BLOCK_SERVICES":
		return &RegisterBlockServicesReq{}, &RegisterBlockServicesResp{}, nil
	case k == "CDC_WITH_REPLICAS":
		return &CdcWithReplicasReq{}, &CdcWithReplicasResp{}, nil
	case k == "ERASE_DECOMMISSIONED_BLOCK":
		return &EraseDecommissionedBlockReq{}, &EraseDecommissionedBlockResp{}, nil
	case k == "ALL_BLOCK_SERVICES":
		return &AllBlockServicesReq{}, &AllBlockServicesResp{}, nil
	case k == "MOVE_CDC_LEADER":
		return &MoveCdcLeaderReq{}, &MoveCdcLeaderResp{}, nil
	case k == "CLEAR_CD_CINFO":
		return &ClearCDCInfoReq{}, &ClearCDCInfoResp{}, nil
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
	case 4:
		return "FETCH_BLOCK_WITH_CRC"
	case 1:
		return "ERASE_BLOCK"
	case 5:
		return "TEST_WRITE"
	case 6:
		return "CHECK_BLOCK"
	case 7:
		return "CONVERT_BLOCK"
	default:
		return fmt.Sprintf("BlocksMessageKind(%d)", k)
	}
}


const (
	FETCH_BLOCK BlocksMessageKind = 0x2
	WRITE_BLOCK BlocksMessageKind = 0x3
	FETCH_BLOCK_WITH_CRC BlocksMessageKind = 0x4
	ERASE_BLOCK BlocksMessageKind = 0x1
	TEST_WRITE BlocksMessageKind = 0x5
	CHECK_BLOCK BlocksMessageKind = 0x6
	CONVERT_BLOCK BlocksMessageKind = 0x7
)

var AllBlocksMessageKind = [...]BlocksMessageKind{
	FETCH_BLOCK,
	WRITE_BLOCK,
	FETCH_BLOCK_WITH_CRC,
	ERASE_BLOCK,
	TEST_WRITE,
	CHECK_BLOCK,
	CONVERT_BLOCK,
}

const MaxBlocksMessageKind BlocksMessageKind = 7

func MkBlocksMessage(k string) (BlocksRequest, BlocksResponse, error) {
	switch {
	case k == "FETCH_BLOCK":
		return &FetchBlockReq{}, &FetchBlockResp{}, nil
	case k == "WRITE_BLOCK":
		return &WriteBlockReq{}, &WriteBlockResp{}, nil
	case k == "FETCH_BLOCK_WITH_CRC":
		return &FetchBlockWithCrcReq{}, &FetchBlockWithCrcResp{}, nil
	case k == "ERASE_BLOCK":
		return &EraseBlockReq{}, &EraseBlockResp{}, nil
	case k == "TEST_WRITE":
		return &TestWriteReq{}, &TestWriteResp{}, nil
	case k == "CHECK_BLOCK":
		return &CheckBlockReq{}, &CheckBlockResp{}, nil
	case k == "CONVERT_BLOCK":
		return &ConvertBlockReq{}, &ConvertBlockResp{}, nil
	default:
		return nil, nil, fmt.Errorf("bad kind string %s", k)
	}
}

func (k LogMessageKind) String() string {
	switch k {
	case 1:
		return "LOG_WRITE"
	case 2:
		return "RELEASE"
	case 3:
		return "LOG_READ"
	case 4:
		return "NEW_LEADER"
	case 5:
		return "NEW_LEADER_CONFIRM"
	case 6:
		return "LOG_RECOVERY_READ"
	case 7:
		return "LOG_RECOVERY_WRITE"
	default:
		return fmt.Sprintf("LogMessageKind(%d)", k)
	}
}


const (
	LOG_WRITE LogMessageKind = 0x1
	RELEASE LogMessageKind = 0x2
	LOG_READ LogMessageKind = 0x3
	NEW_LEADER LogMessageKind = 0x4
	NEW_LEADER_CONFIRM LogMessageKind = 0x5
	LOG_RECOVERY_READ LogMessageKind = 0x6
	LOG_RECOVERY_WRITE LogMessageKind = 0x7
)

var AllLogMessageKind = [...]LogMessageKind{
	LOG_WRITE,
	RELEASE,
	LOG_READ,
	NEW_LEADER,
	NEW_LEADER_CONFIRM,
	LOG_RECOVERY_READ,
	LOG_RECOVERY_WRITE,
}

const MaxLogMessageKind LogMessageKind = 7

func MkLogMessage(k string) (LogRequest, LogResponse, error) {
	switch {
	case k == "LOG_WRITE":
		return &LogWriteReq{}, &LogWriteResp{}, nil
	case k == "RELEASE":
		return &ReleaseReq{}, &ReleaseResp{}, nil
	case k == "LOG_READ":
		return &LogReadReq{}, &LogReadResp{}, nil
	case k == "NEW_LEADER":
		return &NewLeaderReq{}, &NewLeaderResp{}, nil
	case k == "NEW_LEADER_CONFIRM":
		return &NewLeaderConfirmReq{}, &NewLeaderConfirmResp{}, nil
	case k == "LOG_RECOVERY_READ":
		return &LogRecoveryReadReq{}, &LogRecoveryReadResp{}, nil
	case k == "LOG_RECOVERY_WRITE":
		return &LogRecoveryWriteReq{}, &LogRecoveryWriteResp{}, nil
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

func (v *SetTimeReq) ShardRequestKind() ShardMessageKind {
	return SET_TIME
}

func (v *SetTimeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Mtime)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Atime)); err != nil {
		return err
	}
	return nil
}

func (v *SetTimeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Mtime)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Atime)); err != nil {
		return err
	}
	return nil
}

func (v *SetTimeResp) ShardResponseKind() ShardMessageKind {
	return SET_TIME
}

func (v *SetTimeResp) Pack(w io.Writer) error {
	return nil
}

func (v *SetTimeResp) Unpack(r io.Reader) error {
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

func (v *ShardSnapshotReq) ShardRequestKind() ShardMessageKind {
	return SHARD_SNAPSHOT
}

func (v *ShardSnapshotReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.SnapshotId)); err != nil {
		return err
	}
	return nil
}

func (v *ShardSnapshotReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.SnapshotId)); err != nil {
		return err
	}
	return nil
}

func (v *ShardSnapshotResp) ShardResponseKind() ShardMessageKind {
	return SHARD_SNAPSHOT
}

func (v *ShardSnapshotResp) Pack(w io.Writer) error {
	return nil
}

func (v *ShardSnapshotResp) Unpack(r io.Reader) error {
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

func (v *AddSpanInitiateWithReferenceReq) ShardRequestKind() ShardMessageKind {
	return ADD_SPAN_INITIATE_WITH_REFERENCE
}

func (v *AddSpanInitiateWithReferenceReq) Pack(w io.Writer) error {
	if err := v.Req.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Reference)); err != nil {
		return err
	}
	return nil
}

func (v *AddSpanInitiateWithReferenceReq) Unpack(r io.Reader) error {
	if err := v.Req.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Reference)); err != nil {
		return err
	}
	return nil
}

func (v *AddSpanInitiateWithReferenceResp) ShardResponseKind() ShardMessageKind {
	return ADD_SPAN_INITIATE_WITH_REFERENCE
}

func (v *AddSpanInitiateWithReferenceResp) Pack(w io.Writer) error {
	if err := v.Resp.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *AddSpanInitiateWithReferenceResp) Unpack(r io.Reader) error {
	if err := v.Resp.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *RemoveZeroBlockServiceFilesReq) ShardRequestKind() ShardMessageKind {
	return REMOVE_ZERO_BLOCK_SERVICE_FILES
}

func (v *RemoveZeroBlockServiceFilesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.StartBlockService)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.StartFile)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveZeroBlockServiceFilesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartBlockService)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.StartFile)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveZeroBlockServiceFilesResp) ShardResponseKind() ShardMessageKind {
	return REMOVE_ZERO_BLOCK_SERVICE_FILES
}

func (v *RemoveZeroBlockServiceFilesResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Removed)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NextBlockService)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.NextFile)); err != nil {
		return err
	}
	return nil
}

func (v *RemoveZeroBlockServiceFilesResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Removed)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextBlockService)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NextFile)); err != nil {
		return err
	}
	return nil
}

func (v *SwapSpansReq) ShardRequestKind() ShardMessageKind {
	return SWAP_SPANS
}

func (v *SwapSpansReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId1)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset1)); err != nil {
		return err
	}
	len1 := len(v.Blocks1)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := bincode.PackScalar(w, uint64(v.Blocks1[i])); err != nil {
			return err
		}
	}
	if err := bincode.PackScalar(w, uint64(v.FileId2)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ByteOffset2)); err != nil {
		return err
	}
	len2 := len(v.Blocks2)
	if err := bincode.PackLength(w, len2); err != nil {
		return err
	}
	for i := 0; i < len2; i++ {
		if err := bincode.PackScalar(w, uint64(v.Blocks2[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *SwapSpansReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId1)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset1)); err != nil {
		return err
	}
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks1, len1)
	for i := 0; i < len1; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.Blocks1[i])); err != nil {
			return err
		}
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId2)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ByteOffset2)); err != nil {
		return err
	}
	var len2 int
	if err := bincode.UnpackLength(r, &len2); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Blocks2, len2)
	for i := 0; i < len2; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.Blocks2[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *SwapSpansResp) ShardResponseKind() ShardMessageKind {
	return SWAP_SPANS
}

func (v *SwapSpansResp) Pack(w io.Writer) error {
	return nil
}

func (v *SwapSpansResp) Unpack(r io.Reader) error {
	return nil
}

func (v *SameDirectoryRenameSnapshotReq) ShardRequestKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME_SNAPSHOT
}

func (v *SameDirectoryRenameSnapshotReq) Pack(w io.Writer) error {
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

func (v *SameDirectoryRenameSnapshotReq) Unpack(r io.Reader) error {
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

func (v *SameDirectoryRenameSnapshotResp) ShardResponseKind() ShardMessageKind {
	return SAME_DIRECTORY_RENAME_SNAPSHOT
}

func (v *SameDirectoryRenameSnapshotResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NewCreationTime)); err != nil {
		return err
	}
	return nil
}

func (v *SameDirectoryRenameSnapshotResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NewCreationTime)); err != nil {
		return err
	}
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

func (v *CdcSnapshotReq) CDCRequestKind() CDCMessageKind {
	return CDC_SNAPSHOT
}

func (v *CdcSnapshotReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.SnapshotId)); err != nil {
		return err
	}
	return nil
}

func (v *CdcSnapshotReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.SnapshotId)); err != nil {
		return err
	}
	return nil
}

func (v *CdcSnapshotResp) CDCResponseKind() CDCMessageKind {
	return CDC_SNAPSHOT
}

func (v *CdcSnapshotResp) Pack(w io.Writer) error {
	return nil
}

func (v *CdcSnapshotResp) Unpack(r io.Reader) error {
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

func (v *AddSpanInitiateBlockInfo) Pack(w io.Writer) error {
	if err := v.BlockServiceAddrs.Pack(w); err != nil {
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

func (v *AddSpanInitiateBlockInfo) Unpack(r io.Reader) error {
	if err := v.BlockServiceAddrs.Unpack(r); err != nil {
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

func (v *RemoveSpanInitiateBlockInfo) Pack(w io.Writer) error {
	if err := v.BlockServiceAddrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockServiceId)); err != nil {
		return err
	}
	if err := v.BlockServiceFailureDomain.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.BlockServiceFlags)); err != nil {
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

func (v *RemoveSpanInitiateBlockInfo) Unpack(r io.Reader) error {
	if err := v.BlockServiceAddrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServiceId)); err != nil {
		return err
	}
	if err := v.BlockServiceFailureDomain.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.BlockServiceFlags)); err != nil {
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
	if err := v.Addrs.Pack(w); err != nil {
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
	if err := v.Addrs.Unpack(r); err != nil {
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
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *ShardInfo) Unpack(r io.Reader) error {
	if err := v.Addrs.Unpack(r); err != nil {
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
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	if err := v.FailureDomain.Pack(w); err != nil {
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
	if err := bincode.PackScalar(w, bool(v.HasFiles)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.FlagsLastChanged)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := v.FailureDomain.Unpack(r); err != nil {
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
	if err := bincode.UnpackScalar(r, (*bool)(&v.HasFiles)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FlagsLastChanged)); err != nil {
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

func (v *ShardWithReplicasInfo) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Id)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, bool(v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *ShardWithReplicasInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Id)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*bool)(&v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterBlockServiceInfo) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.StorageClass)); err != nil {
		return err
	}
	if err := v.FailureDomain.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackFixedBytes(w, 16, v.SecretKey[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Flags)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.FlagsMask)); err != nil {
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
	return nil
}

func (v *RegisterBlockServiceInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.StorageClass)); err != nil {
		return err
	}
	if err := v.FailureDomain.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackFixedBytes(r, 16, v.SecretKey[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Flags)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.FlagsMask)); err != nil {
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
	return nil
}

func (v *CdcWithReplicasInfo) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.ReplicaId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, bool(v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *CdcWithReplicasInfo) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.ReplicaId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*bool)(&v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *IpPort) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 4, v.Addrs[:]); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint16(v.Port)); err != nil {
		return err
	}
	return nil
}

func (v *IpPort) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 4, v.Addrs[:]); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Port)); err != nil {
		return err
	}
	return nil
}

func (v *AddrsInfo) Pack(w io.Writer) error {
	if err := v.Addr1.Pack(w); err != nil {
		return err
	}
	if err := v.Addr2.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *AddrsInfo) Unpack(r io.Reader) error {
	if err := v.Addr1.Unpack(r); err != nil {
		return err
	}
	if err := v.Addr2.Unpack(r); err != nil {
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
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastSeen)); err != nil {
		return err
	}
	return nil
}

func (v *CdcResp) Unpack(r io.Reader) error {
	if err := v.Addrs.Unpack(r); err != nil {
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

func (v *ShuckleReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHUCKLE
}

func (v *ShuckleReq) Pack(w io.Writer) error {
	return nil
}

func (v *ShuckleReq) Unpack(r io.Reader) error {
	return nil
}

func (v *ShuckleResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHUCKLE
}

func (v *ShuckleResp) Pack(w io.Writer) error {
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *ShuckleResp) Unpack(r io.Reader) error {
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *BlockServicesWithFlagChangeReq) ShuckleRequestKind() ShuckleMessageKind {
	return BLOCK_SERVICES_WITH_FLAG_CHANGE
}

func (v *BlockServicesWithFlagChangeReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.ChangedSince)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServicesWithFlagChangeReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ChangedSince)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServicesWithFlagChangeResp) ShuckleResponseKind() ShuckleMessageKind {
	return BLOCK_SERVICES_WITH_FLAG_CHANGE
}

func (v *BlockServicesWithFlagChangeResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.LastChange)); err != nil {
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
	return nil
}

func (v *BlockServicesWithFlagChangeResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastChange)); err != nil {
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
	return nil
}

func (v *RegisterShardReq) ShuckleRequestKind() ShuckleMessageKind {
	return REGISTER_SHARD
}

func (v *RegisterShardReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Shrid)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, bool(v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *RegisterShardReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Shrid)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*bool)(&v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
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

func (v *RegisterCdcReq) ShuckleRequestKind() ShuckleMessageKind {
	return REGISTER_CDC
}

func (v *RegisterCdcReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Replica)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, bool(v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *RegisterCdcReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Replica)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*bool)(&v.IsLeader)); err != nil {
		return err
	}
	if err := v.Addrs.Unpack(r); err != nil {
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

func (v *BlockServiceReq) ShuckleRequestKind() ShuckleMessageKind {
	return BLOCK_SERVICE
}

func (v *BlockServiceReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceResp) ShuckleResponseKind() ShuckleMessageKind {
	return BLOCK_SERVICE
}

func (v *BlockServiceResp) Pack(w io.Writer) error {
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *BlockServiceResp) Unpack(r io.Reader) error {
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *ShardReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHARD
}

func (v *ShardReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ShardReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ShardResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHARD
}

func (v *ShardResp) Pack(w io.Writer) error {
	if err := v.Info.Pack(w); err != nil {
		return err
	}
	return nil
}

func (v *ShardResp) Unpack(r io.Reader) error {
	if err := v.Info.Unpack(r); err != nil {
		return err
	}
	return nil
}

func (v *ShardReplicasReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHARD_REPLICAS
}

func (v *ShardReplicasReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ShardReplicasReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *ShardReplicasResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHARD_REPLICAS
}

func (v *ShardReplicasResp) Pack(w io.Writer) error {
	len1 := len(v.Replicas)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *ShardReplicasResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Replicas, len1)
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *ShardBlockServicesReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHARD_BLOCK_SERVICES
}

func (v *ShardBlockServicesReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.ShardId)); err != nil {
		return err
	}
	return nil
}

func (v *ShardBlockServicesReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.ShardId)); err != nil {
		return err
	}
	return nil
}

func (v *ShardBlockServicesResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHARD_BLOCK_SERVICES
}

func (v *ShardBlockServicesResp) Pack(w io.Writer) error {
	len1 := len(v.BlockServices)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := bincode.PackScalar(w, uint64(v.BlockServices[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *ShardBlockServicesResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.BlockServices, len1)
	for i := 0; i < len1; i++ {
		if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServices[i])); err != nil {
			return err
		}
	}
	return nil
}

func (v *CdcReplicasReq) ShuckleRequestKind() ShuckleMessageKind {
	return CDC_REPLICAS
}

func (v *CdcReplicasReq) Pack(w io.Writer) error {
	return nil
}

func (v *CdcReplicasReq) Unpack(r io.Reader) error {
	return nil
}

func (v *CdcReplicasResp) ShuckleResponseKind() ShuckleMessageKind {
	return CDC_REPLICAS
}

func (v *CdcReplicasResp) Pack(w io.Writer) error {
	len1 := len(v.Replicas)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *CdcReplicasResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Replicas, len1)
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *ShardsWithReplicasReq) ShuckleRequestKind() ShuckleMessageKind {
	return SHARDS_WITH_REPLICAS
}

func (v *ShardsWithReplicasReq) Pack(w io.Writer) error {
	return nil
}

func (v *ShardsWithReplicasReq) Unpack(r io.Reader) error {
	return nil
}

func (v *ShardsWithReplicasResp) ShuckleResponseKind() ShuckleMessageKind {
	return SHARDS_WITH_REPLICAS
}

func (v *ShardsWithReplicasResp) Pack(w io.Writer) error {
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

func (v *ShardsWithReplicasResp) Unpack(r io.Reader) error {
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

func (v *SetBlockServiceDecommissionedReq) ShuckleRequestKind() ShuckleMessageKind {
	return SET_BLOCK_SERVICE_DECOMMISSIONED
}

func (v *SetBlockServiceDecommissionedReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *SetBlockServiceDecommissionedReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Id)); err != nil {
		return err
	}
	return nil
}

func (v *SetBlockServiceDecommissionedResp) ShuckleResponseKind() ShuckleMessageKind {
	return SET_BLOCK_SERVICE_DECOMMISSIONED
}

func (v *SetBlockServiceDecommissionedResp) Pack(w io.Writer) error {
	return nil
}

func (v *SetBlockServiceDecommissionedResp) Unpack(r io.Reader) error {
	return nil
}

func (v *MoveShardLeaderReq) ShuckleRequestKind() ShuckleMessageKind {
	return MOVE_SHARD_LEADER
}

func (v *MoveShardLeaderReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Shrid)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *MoveShardLeaderReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Shrid)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *MoveShardLeaderResp) ShuckleResponseKind() ShuckleMessageKind {
	return MOVE_SHARD_LEADER
}

func (v *MoveShardLeaderResp) Pack(w io.Writer) error {
	return nil
}

func (v *MoveShardLeaderResp) Unpack(r io.Reader) error {
	return nil
}

func (v *ClearShardInfoReq) ShuckleRequestKind() ShuckleMessageKind {
	return CLEAR_SHARD_INFO
}

func (v *ClearShardInfoReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Shrid)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *ClearShardInfoReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Shrid)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *ClearShardInfoResp) ShuckleResponseKind() ShuckleMessageKind {
	return CLEAR_SHARD_INFO
}

func (v *ClearShardInfoResp) Pack(w io.Writer) error {
	return nil
}

func (v *ClearShardInfoResp) Unpack(r io.Reader) error {
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

func (v *CdcWithReplicasReq) ShuckleRequestKind() ShuckleMessageKind {
	return CDC_WITH_REPLICAS
}

func (v *CdcWithReplicasReq) Pack(w io.Writer) error {
	return nil
}

func (v *CdcWithReplicasReq) Unpack(r io.Reader) error {
	return nil
}

func (v *CdcWithReplicasResp) ShuckleResponseKind() ShuckleMessageKind {
	return CDC_WITH_REPLICAS
}

func (v *CdcWithReplicasResp) Pack(w io.Writer) error {
	len1 := len(v.Replicas)
	if err := bincode.PackLength(w, len1); err != nil {
		return err
	}
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Pack(w); err != nil {
			return err
		}
	}
	return nil
}

func (v *CdcWithReplicasResp) Unpack(r io.Reader) error {
	var len1 int
	if err := bincode.UnpackLength(r, &len1); err != nil {
		return err
	}
	bincode.EnsureLength(&v.Replicas, len1)
	for i := 0; i < len1; i++ {
		if err := v.Replicas[i].Unpack(r); err != nil {
			return err
		}
	}
	return nil
}

func (v *EraseDecommissionedBlockReq) ShuckleRequestKind() ShuckleMessageKind {
	return ERASE_DECOMMISSIONED_BLOCK
}

func (v *EraseDecommissionedBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockServiceId)); err != nil {
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

func (v *EraseDecommissionedBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockServiceId)); err != nil {
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

func (v *EraseDecommissionedBlockResp) ShuckleResponseKind() ShuckleMessageKind {
	return ERASE_DECOMMISSIONED_BLOCK
}

func (v *EraseDecommissionedBlockResp) Pack(w io.Writer) error {
	if err := bincode.PackFixedBytes(w, 8, v.Proof[:]); err != nil {
		return err
	}
	return nil
}

func (v *EraseDecommissionedBlockResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackFixedBytes(r, 8, v.Proof[:]); err != nil {
		return err
	}
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

func (v *MoveCdcLeaderReq) ShuckleRequestKind() ShuckleMessageKind {
	return MOVE_CDC_LEADER
}

func (v *MoveCdcLeaderReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Replica)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *MoveCdcLeaderReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Replica)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *MoveCdcLeaderResp) ShuckleResponseKind() ShuckleMessageKind {
	return MOVE_CDC_LEADER
}

func (v *MoveCdcLeaderResp) Pack(w io.Writer) error {
	return nil
}

func (v *MoveCdcLeaderResp) Unpack(r io.Reader) error {
	return nil
}

func (v *ClearCDCInfoReq) ShuckleRequestKind() ShuckleMessageKind {
	return CLEAR_CD_CINFO
}

func (v *ClearCDCInfoReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint8(v.Replica)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint8(v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *ClearCDCInfoReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Replica)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint8)(&v.Location)); err != nil {
		return err
	}
	return nil
}

func (v *ClearCDCInfoResp) ShuckleResponseKind() ShuckleMessageKind {
	return CLEAR_CD_CINFO
}

func (v *ClearCDCInfoResp) Pack(w io.Writer) error {
	return nil
}

func (v *ClearCDCInfoResp) Unpack(r io.Reader) error {
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

func (v *FetchBlockWithCrcReq) BlocksRequestKind() BlocksMessageKind {
	return FETCH_BLOCK_WITH_CRC
}

func (v *FetchBlockWithCrcReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.FileId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.BlockCrc)); err != nil {
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

func (v *FetchBlockWithCrcReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.FileId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.BlockCrc)); err != nil {
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

func (v *FetchBlockWithCrcResp) BlocksResponseKind() BlocksMessageKind {
	return FETCH_BLOCK_WITH_CRC
}

func (v *FetchBlockWithCrcResp) Pack(w io.Writer) error {
	return nil
}

func (v *FetchBlockWithCrcResp) Unpack(r io.Reader) error {
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

func (v *CheckBlockReq) BlocksRequestKind() BlocksMessageKind {
	return CHECK_BLOCK
}

func (v *CheckBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *CheckBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *CheckBlockResp) BlocksResponseKind() BlocksMessageKind {
	return CHECK_BLOCK
}

func (v *CheckBlockResp) Pack(w io.Writer) error {
	return nil
}

func (v *CheckBlockResp) Unpack(r io.Reader) error {
	return nil
}

func (v *ConvertBlockReq) BlocksRequestKind() BlocksMessageKind {
	return CONVERT_BLOCK
}

func (v *ConvertBlockReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.BlockId)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Size)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint32(v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *ConvertBlockReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.BlockId)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Size)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint32)(&v.Crc)); err != nil {
		return err
	}
	return nil
}

func (v *ConvertBlockResp) BlocksResponseKind() BlocksMessageKind {
	return CONVERT_BLOCK
}

func (v *ConvertBlockResp) Pack(w io.Writer) error {
	return nil
}

func (v *ConvertBlockResp) Unpack(r io.Reader) error {
	return nil
}

func (v *LogWriteReq) LogRequestKind() LogMessageKind {
	return LOG_WRITE
}

func (v *LogWriteReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Token)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastReleased)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Idx)); err != nil {
		return err
	}
	if err := bincode.PackBlob(w, v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogWriteReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Token)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastReleased)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Idx)); err != nil {
		return err
	}
	if err := bincode.UnpackBlob(r, &v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogWriteResp) LogResponseKind() LogMessageKind {
	return LOG_WRITE
}

func (v *LogWriteResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *LogWriteResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *ReleaseReq) LogRequestKind() LogMessageKind {
	return RELEASE
}

func (v *ReleaseReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Token)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastReleased)); err != nil {
		return err
	}
	return nil
}

func (v *ReleaseReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Token)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastReleased)); err != nil {
		return err
	}
	return nil
}

func (v *ReleaseResp) LogResponseKind() LogMessageKind {
	return RELEASE
}

func (v *ReleaseResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *ReleaseResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *LogReadReq) LogRequestKind() LogMessageKind {
	return LOG_READ
}

func (v *LogReadReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.Idx)); err != nil {
		return err
	}
	return nil
}

func (v *LogReadReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Idx)); err != nil {
		return err
	}
	return nil
}

func (v *LogReadResp) LogResponseKind() LogMessageKind {
	return LOG_READ
}

func (v *LogReadResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	if err := bincode.PackBlob(w, v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogReadResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	if err := bincode.UnpackBlob(r, &v.Value); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderReq) LogRequestKind() LogMessageKind {
	return NEW_LEADER
}

func (v *NewLeaderReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NomineeToken)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NomineeToken)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderResp) LogResponseKind() LogMessageKind {
	return NEW_LEADER
}

func (v *NewLeaderResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.LastReleased)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.LastReleased)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderConfirmReq) LogRequestKind() LogMessageKind {
	return NEW_LEADER_CONFIRM
}

func (v *NewLeaderConfirmReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.ReleasedIdx)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderConfirmReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.ReleasedIdx)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderConfirmResp) LogResponseKind() LogMessageKind {
	return NEW_LEADER_CONFIRM
}

func (v *NewLeaderConfirmResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *NewLeaderConfirmResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryReadReq) LogRequestKind() LogMessageKind {
	return LOG_RECOVERY_READ
}

func (v *LogRecoveryReadReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Idx)); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryReadReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Idx)); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryReadResp) LogResponseKind() LogMessageKind {
	return LOG_RECOVERY_READ
}

func (v *LogRecoveryReadResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	if err := bincode.PackBlob(w, v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryReadResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	if err := bincode.UnpackBlob(r, &v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryWriteReq) LogRequestKind() LogMessageKind {
	return LOG_RECOVERY_WRITE
}

func (v *LogRecoveryWriteReq) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint64(v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.PackScalar(w, uint64(v.Idx)); err != nil {
		return err
	}
	if err := bincode.PackBlob(w, v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryWriteReq) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint64)(&v.NomineeToken)); err != nil {
		return err
	}
	if err := bincode.UnpackScalar(r, (*uint64)(&v.Idx)); err != nil {
		return err
	}
	if err := bincode.UnpackBlob(r, &v.Value); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryWriteResp) LogResponseKind() LogMessageKind {
	return LOG_RECOVERY_WRITE
}

func (v *LogRecoveryWriteResp) Pack(w io.Writer) error {
	if err := bincode.PackScalar(w, uint16(v.Result)); err != nil {
		return err
	}
	return nil
}

func (v *LogRecoveryWriteResp) Unpack(r io.Reader) error {
	if err := bincode.UnpackScalar(r, (*uint16)(&v.Result)); err != nil {
		return err
	}
	return nil
}

