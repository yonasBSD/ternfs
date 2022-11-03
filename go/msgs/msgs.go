// See bincodegen.go for more comments on how we split op these operations
package msgs

import (
	"fmt"
	"time"
	"xtx/eggsfs/bincode"
)

//go:generate go run ../bincodegen

// --------------------------------------------------------------------
// Common types and utilities

const UDP_MTU = 1472

type InodeType uint8
type InodeId uint64
type OwnedInodeId uint64 // 64th bit is used to mark whether the inode is owned.
type ShardId uint8
type Parity uint8
type StorageClass uint8
type EggsTime uint64
type BlockId uint64
type BlockServiceId uint64

// These four below are the magic number to identify UDP packets. After a three-letter
// string identifying the service we have a version number. The idea is that when the
// version number increases you increase both req and resp. Different for req and resp
// so that middleware can understand packets just by looking at their contents.

// >>> format(struct.unpack('<I', b'SHA\0')[0], 'x')
// '414853'
const SHARD_REQ_PROTOCOL_VERSION uint32 = 0x414853

// >>> format(struct.unpack('<I', b'SHA\1')[0], 'x')
// '1414853'
const SHARD_RESP_PROTOCOL_VERSION uint32 = 0x1414853

// >>> format(struct.unpack('<I', b'CDC\0')[0], 'x')
// '434443'
const CDC_REQ_PROTOCOL_VERSION uint32 = 0x434443

// >>> format(struct.unpack('<I', b'CDC\1')[0], 'x')
// '1434443'
const CDC_RESP_PROTOCOL_VERSION uint32 = 0x1434443

// For CDC/SHARD we use 0 as an error kind
const ERROR_KIND uint8 = 0

const (
	DIRECTORY InodeType = 1
	FILE      InodeType = 2
	SYMLINK   InodeType = 3
)

func (typ InodeType) String() string {
	switch typ {
	case DIRECTORY:
		return "DIRECTORY"
	case FILE:
		return "FILE"
	case SYMLINK:
		return "SYMLINK"
	default:
		return fmt.Sprintf("InodeType(%d)", uint8(typ))
	}
}

func (id InodeId) Type() InodeType {
	typ := InodeType((id >> 61) & 0x03)
	if !(typ == DIRECTORY || typ == FILE || typ == SYMLINK) {
		panic(fmt.Errorf("bad inode type %v -- are you calling Type() on NULL_INODE_ID?", typ))
	}
	return typ
}

func (id InodeId) Shard() ShardId {
	return ShardId(id & 0xFF)
}

func (id InodeId) String() string {
	return fmt.Sprintf("0x%X", uint64(id))
}

func MakeInodeId(typ InodeType, shard ShardId, id uint64) InodeId {
	return (InodeId(typ) << 61) | (InodeId(id) << 8) | InodeId(shard)
}

func (id OwnedInodeId) Id() InodeId {
	return InodeId(uint64(id) & ^(uint64(1) << 63))
}

func (id OwnedInodeId) Owned() bool {
	if (uint64(id) >> 63) == 0 {
		return false
	} else {
		return true
	}
}

func (id OwnedInodeId) String() string {
	if id.Owned() {
		return fmt.Sprintf("[O]%v", id.Id())
	} else {
		return fmt.Sprintf("[ ]%v", id.Id())
	}
}

func MakeOwnedInodeId(id InodeId, owned bool) OwnedInodeId {
	x := uint64(id)
	if owned {
		x = x | (1 << 63)
	}
	return OwnedInodeId(x)
}

const (
	NULL_INODE_ID InodeId = 0
	// Can't call MakeInodeId in constant
	ROOT_DIR_INODE_ID = InodeId(DIRECTORY) << 61
)

func (shard ShardId) Port() int {
	return 22272 + int(shard)
}

const CDC_PORT int = 36137

const EGGS_EPOCH uint64 = 1_577_836_800_000_000_000

func MakeEggsTime(t time.Time) EggsTime {
	return EggsTime(uint64(t.UnixNano()) - EGGS_EPOCH)
}

func Now() EggsTime {
	return MakeEggsTime(time.Now())
}

func (t EggsTime) Time() time.Time {
	return time.Unix(0, int64(uint64(t)+EGGS_EPOCH))
}

func (t EggsTime) String() string {
	return t.Time().Format(time.RFC3339Nano)
}

type ErrCode uint16

func (err ErrCode) Error() string {
	return err.String()
}

func (err *ErrCode) Pack(buf *bincode.Buf) {
	buf.PackU16(uint16(*err))
}

func (errCode *ErrCode) Unpack(buf *bincode.Buf) error {
	var c uint16
	if err := buf.UnpackU16(&c); err != nil {
		return err
	}
	*errCode = ErrCode(c)
	return nil
}

type ShardMessageKind uint8

type CDCMessageKind uint8

const ERROR uint8 = 0

// --------------------------------------------------------------------
// Shard requests/responses

const (
	ZERO_STORAGE   StorageClass = 0
	INLINE_STORAGE StorageClass = 1
)

// Given directory inode and name, returns inode from outgoing
// current edge. Does not consider non-current directories and
// non-current edges.
type LookupReq struct {
	DirId InodeId
	Name  string
}

type LookupResp struct {
	TargetId     InodeId
	CreationTime EggsTime
}

type StatFileReq struct {
	Id InodeId
}

type StatFileResp struct {
	Mtime     EggsTime
	Size      uint64
	Transient bool
	Note      []byte // empty if not transient
}

type StatDirectoryReq struct {
	Id InodeId
}

type StatDirectoryResp struct {
	Mtime EggsTime
	Owner InodeId // if NULL_INODE_ID, the directory is currently soft unlinked
	Info  DirectoryInfo
}

type ReadDirReq struct {
	DirId     InodeId
	StartHash uint64
	// * all the times leading up to the creation of the directory will return
	//     an empty directory listing.
	// * all the times after the last modification will return the current directory
	//     listing (use 0xFFFFFFFFFFFFFFFF to just get the current directory listing)
	AsOfTime EggsTime
}

type ReadDirResp struct {
	NextHash uint64
	Results  []Edge
}

// create a new transient file.
type ConstructFileReq struct {
	Type InodeType // must not be DIRECTORY
	// this can be the future file name or anyway something that gives a pointer
	// to what this transient wile will be or was (if we're destructing).
	Note string
}

type ConstructFileResp struct {
	Id     InodeId
	Cookie uint64
}

type NewBlockInfo struct {
	Crc32 [4]byte
}

type BlockServiceBlacklist struct {
	Ip   [4]byte
	Port uint16
	Id   BlockServiceId
}

// Add span. The file must be transient.
//
// Generally speaking, the num_data_blocks*BlockSize == Size. However, there are two
// exceptions.
//
// * If the erasure code we use dictates the data to be a multiple of a certain number
//     of bytes, then the sum of the data block sizes might be larger than the span size
//     to ensure that, in which case the excess data must be zero and can be discarded.
// * The span size can be greater than the sum of data blocks, in which case trailing
//     zeros are added. This is to support cheap creation of gaps in the file.
//
// The zero storage class (ZERO_STORAGE) is not strictly needed -- we could just use
// a span size with no data blocks at all. But it's still nice to distinguish it explicitly
// -- otherwise you'd have to specify some dummy storage class for such spans, and you would
// not be able to check that there _is_ some data when you expect it to.
//
// The inline storage class allows small files (< 256 bytes) to be stored directly
// in the metadata.
type AddSpanInitiateReq struct {
	FileId       InodeId
	Cookie       uint64
	ByteOffset   uint64 `bincode:"varint"`
	StorageClass StorageClass
	// A single element of these matches if all the nonzero elements in it match.
	// This is useful when the kernel knows it cannot communicate with a certain block
	// service (because it noticed that it is broken before shuckle/shard did, or
	// because of some transient network problem, or...).
	Blacklist []BlockServiceBlacklist
	Parity    Parity
	Crc32     [4]byte
	Size      uint64 `bincode:"varint"`
	BlockSize uint64 `bincode:"varint"`
	// empty if storage class not inline
	BodyBytes []byte
	// empty if storage class zero/inline
	BodyBlocks []NewBlockInfo
}

type BlockInfo struct {
	BlockServiceIp   [4]byte
	BlockServicePort uint16
	BlockServiceId   BlockServiceId
	BlockId          uint64
	// certificate := MAC(b'w' + block_id + crc + size)[:8] (for creation)
	Certificate [8]byte
}

type AddSpanInitiateResp struct {
	// Left empty for inline/zero-filled spans
	Blocks []BlockInfo
}

type BlockProof struct {
	BlockId uint64
	Proof   [8]byte
}

// certify span. again, the file must be transient.
type AddSpanCertifyReq struct {
	FileId     InodeId
	Cookie     uint64
	ByteOffset uint64 `bincode:"varint"`
	Proofs     []BlockProof
}

type AddSpanCertifyResp struct{}

type RemoveSpanInitiateReq struct {
	FileId InodeId
	Cookie uint64
}

type RemoveSpanInitiateResp struct {
	ByteOffset uint64 `bincode:"varint"`
	// If empty, this is a blockless span, and no certification is required
	Blocks []BlockInfo
}

type RemoveSpanCertifyReq struct {
	FileId     InodeId
	Cookie     uint64
	ByteOffset uint64 `bincode:"varint"`
	Proofs     []BlockProof
}

type RemoveSpanCertifyResp struct{}

// makes a transient file current. requires the inode, the
// parent dir, and the filename.
type LinkFileReq struct {
	FileId  InodeId
	Cookie  uint64
	OwnerId InodeId
	Name    string
}

type LinkFileResp struct{}

// turns a current outgoing edge into a snapshot owning edge.
type SoftUnlinkFileReq struct {
	OwnerId InodeId
	FileId  InodeId
	Name    string
}

type SoftUnlinkFileResp struct{}

type FileSpansReq struct {
	FileId     InodeId
	ByteOffset uint64 `bincode:"varint"`
}

type FetchedBlock struct {
	BlockServiceIx uint8 // Index into `BlockServices`
	BlockId        BlockId
	Crc32          [4]byte
}

// If the storage class is zero-filled, BodyBytes and BodyBlocks are empty.
// If the storage class is inline, BodyBlocks is empty.
// If the storage class is not inline, BodyBytes is empty.
type FetchedSpan struct {
	ByteOffset   uint64 `bincode:"varint"`
	Parity       Parity
	StorageClass StorageClass
	Crc32        [4]byte
	Size         uint64 `bincode:"varint"`
	// See comment for AddSpanInitiateReq for explanations regarding span
	// vs block size, and also body bytes vs. body blocks
	BlockSize  uint64 `bincode:"varint"`
	BodyBytes  []byte
	BodyBlocks []FetchedBlock
}

type BlockService struct {
	Ip   [4]byte
	Port uint16
	// The BlockServiceId is stable (derived from the secret key, which is stored on the
	// block service id).
	//
	// The ip/port are not, and in fact might be shared by multiple block services.
	Id    BlockServiceId
	Flags uint8
}

type FileSpansResp struct {
	NextOffset    uint64 `bincode:"varint"`
	BlockServices []BlockService
	Spans         []FetchedSpan
}

type SameDirectoryRenameReq struct {
	TargetId InodeId
	DirId    InodeId
	OldName  string
	NewName  string
}

type SameDirectoryRenameResp struct{}

type VisitDirectoriesReq struct {
	BeginId InodeId
}

type VisitDirectoriesResp struct {
	NextId InodeId
	Ids    []InodeId
}

type VisitFilesReq struct {
	BeginId InodeId
}

type VisitFilesResp struct {
	NextId InodeId
	Ids    []InodeId
}

type VisitTransientFilesReq struct {
	BeginId InodeId
}

type TransientFile struct {
	Id           InodeId
	Cookie       uint64
	DeadlineTime EggsTime
	Note         string
}

// Shall this be unsafe/private? We can freely get the cookie.
type VisitTransientFilesResp struct {
	NextId InodeId
	Files  []TransientFile
}

type FullReadDirReq struct {
	DirId     InodeId
	StartHash uint64
	StartName string
	StartTime EggsTime
}

type Edge struct {
	TargetId     InodeId
	NameHash     uint64
	Name         string
	CreationTime EggsTime
}

type EdgeWithOwnership struct {
	TargetId     OwnedInodeId
	NameHash     uint64
	Name         string
	CreationTime EggsTime
}

type FullReadDirResp struct {
	Finished bool
	Results  []EdgeWithOwnership
}

// Creates a directory with a given parent and given inode id. Unsafe because
// we can create directories with a certain parent while the paren't isn't
// pointing at them (or isn't even a valid inode). We'd break the "no directory leaks"
// invariant or the "null dir owner <-> not current" invariant.
type CreateDirectoryINodeReq struct {
	Id      InodeId
	OwnerId InodeId
	// If Inherit is set here, the rest of the fields will be ignored, and the
	// directory info will be filled in with the parent info.
	Info DirectoryInfo
}

type CreateDirectoryINodeResp struct {
	Mtime EggsTime
}

// This is needed to move directories -- but it can break the invariants
// between edges pointing to the dir and the owner.
type SetDirectoryOwnerReq struct {
	DirId   InodeId
	OwnerId InodeId // must not be a directory (no NULL_INODE_ID!)
}

type SetDirectoryOwnerResp struct{}

// This is needed to remove directories -- but again, it can break invariants.
type RemoveDirectoryOwnerReq struct {
	DirId InodeId
	// The current (concrete) info of the directory owner.
	Info DirectoryInfoBody
}

type RemoveDirectoryOwnerResp struct{}

type RemoveEdgesReq struct {
	DirId InodeId
	Edges []Edge
}

// These is generally needed when we need to move/create things cross-shard, but
// is unsafe for various reasons:
// * W must remember to unlock the edge, otherwise it'll be locked forever.
// * We must make sure to not end up with multiple owners for the target.
// TODO add comment about how creating an unlocked current edge is no good
// if we want to retry things safely. We might create the edge without realizing
// that we did (e.g. timeouts), and somebody might move it away in the meantime (with
// some shard-local operation).
type CreateLockedCurrentEdgeReq struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
	// We need this because we want idempotency (retrying this request should
	// not create spurious edges when overriding files), and we want to guarantee
	// that the current edge is newest.
	CreationTime EggsTime
}

type CreateLockedCurrentEdgeResp struct{}

type LockCurrentEdgeReq struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
}

type LockCurrentEdgeResp struct{}

// This also lets us turn edges into snapshot.
type UnlockCurrentEdgeReq struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
	WasMoved bool
}

type UnlockCurrentEdgeResp struct{}

type RemoveEdgesResp struct{}

type RemoveNonOwnedEdgeReq struct {
	DirId        InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type RemoveNonOwnedEdgeResp struct{}

// Will remove the snapshot, owned edge; and move the file to transient in one
// go.
type IntraShardHardFileUnlinkReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type IntraShardHardFileUnlinkResp struct{}

// This is needed to implemente inter-shard hard file unlinking, and it is unsafe, since
// we must make sure that the owned file is made transient in its shard.
type RemoveOwnedSnapshotFileEdgeReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type RemoveOwnedSnapshotFileEdgeResp struct{}

// This is required to implemented inter-shard had file unlinking, and it is unsafe
// since it lets us make a file which is owned by a directory transient.
type MakeFileTransientReq struct {
	Id   InodeId
	Note string
}

type MakeFileTransientResp struct{}

type SetDirectoryInfoReq struct {
	Id   InodeId
	Info DirectoryInfo
}

type SetDirectoryInfoResp struct{}

// --------------------------------------------------------------------
// CDC requests/responses

type MakeDirectoryReq struct {
	OwnerId InodeId
	Name    string
	Info    DirectoryInfo
}

type MakeDirectoryResp struct {
	Id InodeId
}

type RenameFileReq struct {
	TargetId   InodeId
	OldOwnerId InodeId
	OldName    string
	NewOwnerId InodeId
	NewName    string
}

type RenameFileResp struct{}

type SoftUnlinkDirectoryReq struct {
	OwnerId  InodeId
	TargetId InodeId
	Name     string
}

type SoftUnlinkDirectoryResp struct{}

type RenameDirectoryReq struct {
	TargetId   InodeId
	OldOwnerId InodeId
	OldName    string
	NewOwnerId InodeId
	NewName    string
}

type RenameDirectoryResp struct{}

// This operation is safe for files: we can check that it has no spans,
// and that it is transient.
//
// For directories however it is not safe. In theory, we could check
// if the directory had no edges (or only non-owning edges), but we might
// be in the middle of a CDC transaction that might be rolled back eventually.
type RemoveInodeReq struct {
	Id InodeId
}

type RemoveInodeResp struct{}

type HardUnlinkDirectoryReq struct {
	DirId InodeId
}

type HardUnlinkDirectoryResp struct{}

type HardUnlinkFileReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type HardUnlinkFileResp struct{}

// --------------------------------------------------------------------
// directory data

type SpanPolicy struct {
	MaxSize      uint64
	StorageClass StorageClass
	Parity       Parity
}

// See SnapshotPolicy for the meaning of `DeleteAfterTime` and
// `DeleteAfterVersions`
type DirectoryInfoBody struct {
	DeleteAfterTime     uint64 // nanoseconds
	DeleteAfterVersions uint8
	// Sorted by MaxSize. There's always an implicit policy for inline
	// spans (max size 255).
	SpanPolicies []SpanPolicy
}

type DirectoryInfo struct {
	// This flag tells us whether the directory was set to inherit the
	// directory info.
	Inherited bool
	// Here list is used as optional. If `Inherited`` is `false`, then
	// the length must be one. If it is `true`, then the length should
	// be zero, unless the directory owner is none, in which case it
	// should be one.
	//
	// We need to materialize the directory info for soft unlinked directories
	// with inherited infos, otherwise we won't be able to easily get the
	// directory info (since we do not have a reverse directory lookup).
	//
	// For consumers, the right way to interpret this is to just look at
	// the body. If it's present, then it should be used. If it's _not_
	// present, then the directory should be traversed upwards. It is a
	// server bug for directories with no owners (soft unlinked or ROOT)
	// to have no body.
	Body []DirectoryInfoBody
}
