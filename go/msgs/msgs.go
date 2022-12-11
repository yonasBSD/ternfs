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
type InodeIdExtra uint64 // 64th bit is used to mark whether the inode is owned.
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

func (id BlockServiceId) String() string {
	return fmt.Sprintf("0x%X", uint64(id))
}

func (id BlockId) String() string {
	return fmt.Sprintf("0x%X", uint64(id))
}

func MakeInodeId(typ InodeType, shard ShardId, id uint64) InodeId {
	return (InodeId(typ) << 61) | (InodeId(id) << 8) | InodeId(shard)
}

func (id InodeIdExtra) Id() InodeId {
	return InodeId(uint64(id) & ^(uint64(1) << 63))
}

func (id InodeIdExtra) Extra() bool {
	if (uint64(id) >> 63) == 0 {
		return false
	} else {
		return true
	}
}

func (id InodeIdExtra) String() string {
	if id.Extra() {
		return fmt.Sprintf("[X]%v", id.Id())
	} else {
		return fmt.Sprintf("[ ]%v", id.Id())
	}
}

func MakeInodeIdExtra(id InodeId, owned bool) InodeIdExtra {
	x := uint64(id)
	if owned {
		x = x | (1 << 63)
	}
	return InodeIdExtra(x)
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

func MkParity(dataBlocks uint8, parityBlocks uint8) Parity {
	if dataBlocks == 0 || dataBlocks >= 16 {
		panic(fmt.Errorf("bad data blocks %v", dataBlocks))
	}
	if parityBlocks >= 16 {
		panic(fmt.Errorf("bad parity blocks %v", parityBlocks))
	}
	return Parity(dataBlocks | (parityBlocks << 4))
}

func (parity Parity) DataBlocks() int {
	return int(parity) & 0x0F
}
func (parity Parity) ParityBlocks() int {
	return int(parity) >> 4
}

// --------------------------------------------------------------------
// Shard requests/responses

const (
	EMPTY_STORAGE  StorageClass = 0
	INLINE_STORAGE StorageClass = 1
)

// Given directory inode and name, returns inode from outgoing
// current edge. Does not consider non-current directories and
// non-current edges.
//
// Returns DIRECTORY_NOT_FOUND if the directory is snapshot.
type LookupReq struct {
	DirId InodeId
	Name  string
}

type LookupResp struct {
	TargetId     InodeId
	CreationTime EggsTime
}

// Does not consider transient files. Might return snapshot files:
// we don't really have a way of knowing if a file is snapshot just by
// looking at it, unlike directories.
type StatFileReq struct {
	Id InodeId
}

type StatFileResp struct {
	Mtime EggsTime
	Size  uint64
}

type StatTransientFileReq struct {
	Id InodeId
}

type StatTransientFileResp struct {
	Mtime EggsTime
	Size  uint64
	Note  []byte
}

// Considers all directories, also snapshot/transient ones. Remember that
// a transient directory is just a snapshot directory with no outgoing
// edges.
//
// The caller can detect if the directory is snapshot (unlike with files),
// and avoid calling `ReadDirReq`, which won't work.1
type StatDirectoryReq struct {
	Id InodeId
}

type SetDirectoryInfo struct {
	// If `Inherited = true`, `Body` should be empty, and vice-versa.
	//
	// Note that while we keep the directory info opaque in all the
	// shard/cdc APIs, we do define the relevant data structure here,
	// since we need them among other things to perform GC
	// (see `DirectoryInfoBody`).
	Inherited bool
	Body      []byte
}

type StatDirectoryResp struct {
	Mtime EggsTime
	Owner InodeId // if NULL_INODE_ID, the directory is currently snapshot
	// If this is empty, then the client should traverse upwards and
	// ask the owner to find out what the info is (this might require
	// multiple hops).
	//
	// If `Owner = NULL_INODE_ID`, then `DirectoryInfo` will not be empty.
	Info []byte
}

// Does not consider snaphsot/transient directories, unlike `StatDirectoryReq`.
// This is since clients almost never want this, and when they do they're almost
// certainly better served by FullReadDirReq.
type ReadDirReq struct {
	DirId     InodeId
	StartHash uint64
}

type CurrentEdge struct {
	TargetId     InodeId
	NameHash     uint64
	Name         string
	CreationTime EggsTime
}

// Names with the same hash will never straddle two `ReadDirResp`s, assuming the
// directory contents don't change in the meantime.
type ReadDirResp struct {
	NextHash uint64
	Results  []CurrentEdge
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
	Cookie [8]byte
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
// The empty storage class (EMPTY_STORAGE) is only used for heartbeats -- it never produces
// spans in the file. So all spans you can read will be with storage class > 0.
//
// The inline storage class (INLINE_STORAGE) allows small files (< 256 bytes) to be stored
// directly in the metadata.
type AddSpanInitiateReq struct {
	FileId       InodeId
	Cookie       [8]byte
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
	BlockId          BlockId
	// certificate := MAC(b'w' + block_id + crc + size)[:8] (for creation)
	Certificate [8]byte
}

type AddSpanInitiateResp struct {
	// Left empty for inline/zero-filled spans
	Blocks []BlockInfo
}

type BlockProof struct {
	BlockId BlockId
	Proof   [8]byte
}

// certify span. again, the file must be transient.
type AddSpanCertifyReq struct {
	FileId     InodeId
	Cookie     [8]byte
	ByteOffset uint64 `bincode:"varint"`
	Proofs     []BlockProof
}

type AddSpanCertifyResp struct{}

type RemoveSpanInitiateReq struct {
	FileId InodeId
	Cookie [8]byte
}

type RemoveSpanInitiateResp struct {
	ByteOffset uint64 `bincode:"varint"`
	// If empty, this is a blockless span, and no certification is required
	Blocks []BlockInfo
}

type RemoveSpanCertifyReq struct {
	FileId     InodeId
	Cookie     [8]byte
	ByteOffset uint64 `bincode:"varint"`
	Proofs     []BlockProof
}

type RemoveSpanCertifyResp struct{}

// Makes a transient file current. Requires the inode, the
// parent dir, and the filename.
//
// If the file is not transient, but does exist, this will return
// normally. This is not strictly necessary but simplifies writing
// the clients (since they) need to retry.
//
// An expired transient file is considered gone.
type LinkFileReq struct {
	FileId  InodeId
	Cookie  [8]byte
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

// Starts from the first span with byte offset <= than the provided
// ByteOffset (this is so that you can just start reading a file
// somewhere without hunting for the right span).
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

// We intentionally do not allow to only visit the "past deadline" files, to not
// have possibly long-running requests on the server (because they need to traverse
// many files before finding the correct one). Instead we let the GC process spend
// time doing that (or we could just stop visiting when we encounter a not-expired
// file, since they are in increasing id order, and therefore the new one come last)
type VisitTransientFilesReq struct {
	BeginId InodeId
}

type TransientFile struct {
	Id           InodeId
	Cookie       [8]byte
	DeadlineTime EggsTime
}

// Shall this be unsafe/private? We can freely get the cookie.
type VisitTransientFilesResp struct {
	NextId InodeId
	Files  []TransientFile
}

type FullReadDirCursor struct {
	Current   bool
	StartHash uint64
	StartName string
	StartTime EggsTime // must be 0 if Current=true
}

// This streams all the edges, first the snapshot ones, then the current ones,
// and does so regardless whether the directory has been removed or not.
//
// Starts from the first edge >= than the cursor. The snapshot edges come first.
type FullReadDirReq struct {
	DirId  InodeId
	Cursor FullReadDirCursor
}

type Edge struct {
	Current bool
	// if current, the extra bit tells us whether the edge is locked. this
	// shouldn't really be useful to any client, but FullReadDir is sort of
	// an internal request anyway.
	//
	// if not current, the extra bit tells us whether the edge is owned.
	// this is needed for GC.
	TargetId     InodeIdExtra
	NameHash     uint64
	Name         string
	CreationTime EggsTime
}

type FullReadDirResp struct {
	Next    FullReadDirCursor // default value if we're done
	Results []Edge
}

// Creates a directory with a given parent and given inode id. Unsafe because
// we can create directories with a certain parent while the paren't isn't
// pointing at them (or isn't even a valid inode). We'd break the "no directory leaks"
// invariant or the "null dir owner <-> not current" invariant.
type CreateDirectoryInodeReq struct {
	Id      InodeId
	OwnerId InodeId
	Info    SetDirectoryInfo
}

type CreateDirectoryInodeResp struct {
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
	// We need this since we're removing the OwnerId. This field must
	// be non-empty.
	Info []byte
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
	Info SetDirectoryInfo
}

type SetDirectoryInfoResp struct{}

// TODO this works with transient files, but don't require a cookie -- it's a bit
// inconsistent.
type SwapBlocksReq struct {
	FileId1     InodeId
	ByteOffset1 uint64
	BlockId1    BlockId
	FileId2     InodeId
	ByteOffset2 uint64
	BlockId2    BlockId
}

type SwapBlocksResp struct{}

type BlockServiceFilesReq struct {
	BlockServiceId BlockServiceId
}

type BlockServiceFilesResp struct {
	FileIds []InodeId
}

type DirectoryEmptyReq struct {
}

// --------------------------------------------------------------------
// CDC requests/responses

type MakeDirectoryReq struct {
	OwnerId InodeId
	Name    string
	Info    SetDirectoryInfo
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
// directory info

type SpanPolicy struct {
	MaxSize      uint64
	StorageClass StorageClass
	Parity       Parity
}

// See SnapshotPolicy for the meaning of `DeleteAfterTime` and
// `DeleteAfterVersions`
type DirectoryInfoBody struct {
	// We store a version number for this serialized data structure
	// since it is opaque to the server and therefore we might want
	// to evolve it separatedly. Right now it's 1 for this data structure.
	Version             uint8
	DeleteAfterTime     uint64 // nanoseconds
	DeleteAfterVersions uint8
	// Sorted by MaxSize. There's always an implicit policy for inline
	// spans (max size 255). Which means that the first `MaxSize`
	// must be > 255.
	SpanPolicies []SpanPolicy
}

// --------------------------------------------------------------------
// shard log entries
//
// these are only used internally, but we define them here for codegen
// simplicity.
//
// We only define the individual entries here, the framing is specified
// in c++. The framing, amongst other things, includes the log entry time
// and the index, so we don't also store it here.

type ConstructFileEntry struct {
	Type         InodeType
	DeadlineTime EggsTime
	Note         string
}

type LinkFileEntry struct {
	FileId  InodeId
	OwnerId InodeId
	Name    string
}

type SameDirectoryRenameEntry struct {
	TargetId InodeId
	DirId    InodeId
	OldName  string
	NewName  string
}

type SoftUnlinkFileEntry struct {
	OwnerId InodeId
	FileId  InodeId
	Name    string
}

type CreateDirectoryInodeEntry struct {
	Id      InodeId
	OwnerId InodeId
	Info    SetDirectoryInfo
}

type CreateLockedCurrentEdgeEntry struct {
	DirId        InodeId
	Name         string
	TargetId     InodeId
	CreationTime EggsTime
}

type UnlockCurrentEdgeEntry struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
	WasMoved bool
}

type LockCurrentEdgeEntry struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
}

type RemoveDirectoryOwnerEntry struct {
	DirId InodeId
	Info  []byte
}

type RemoveInodeEntry struct {
	Id InodeId
}

type SetDirectoryOwnerEntry struct {
	DirId   InodeId
	OwnerId InodeId // must be a directory (no NULL_INODE_ID!)
}

type SetDirectoryInfoEntry struct {
	DirId InodeId
	Info  SetDirectoryInfo
}

type RemoveNonOwnedEdgeEntry struct {
	DirId        InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type IntraShardHardFileUnlinkEntry struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type RemoveSpanInitiateEntry struct {
	FileId InodeId
}

type EntryBlockService struct {
	Id            uint64
	Ip            [4]byte
	Port          uint16
	StorageClass  uint8
	FailureDomain [16]byte
	SecretKey     [16]byte
}

type UpdateBlockServicesEntry struct {
	BlockServices []EntryBlockService
}

type EntryNewBlockInfo struct {
	BlockServiceId uint64
	Crc32          [4]byte
}

type AddSpanInitiateEntry struct {
	FileId       InodeId
	ByteOffset   uint64
	StorageClass StorageClass
	Parity       Parity
	Crc32        [4]byte
	Size         uint32
	BlockSize    uint32
	// empty unless StorageClass == INLINE_STORAGE
	BodyBytes []byte
	// empty unless StorageClass not in (EMPTY_STORAGE, INLINE_STORAGE)
	BodyBlocks []EntryNewBlockInfo
}

type AddSpanCertifyEntry struct {
	FileId     InodeId
	ByteOffset uint64
	Proofs     []BlockProof
}
