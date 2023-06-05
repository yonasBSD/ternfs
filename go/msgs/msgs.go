// See bincodegen.go for more comments on how we split op these operations
package msgs

import (
	"encoding/json"
	"fmt"
	"io"
	"strconv"
	"strings"
	"time"
	"xtx/eggsfs/bincode"
	"xtx/eggsfs/rs"
)

//go:generate go run ../bincodegen

// --------------------------------------------------------------------
// Common types and utilities

const UDP_MTU = 1472

type InodeType uint8
type InodeId uint64
type InodeIdExtra uint64 // 64th bit is used to mark whether the inode is owned.
type ShardId uint8
type StorageClass uint8
type EggsTime uint64
type BlockId uint64
type BlockServiceId uint64
type BlockServiceFlags uint8
type Crc uint32

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

// >>> format(struct.unpack('<I', b'SHU\0')[0], 'x')
// '554853'
const SHUCKLE_REQ_PROTOCOL_VERSION uint32 = 0x554853

// >>> format(struct.unpack('<I', b'SHU\1')[0], 'x')
// '1554853'
const SHUCKLE_RESP_PROTOCOL_VERSION uint32 = 0x1554853

// >>> format(struct.unpack('<I', b'BLO\0')[0], 'x')
// '4f4c42'
const BLOCKS_REQ_PROTOCOL_VERSION uint32 = 0x4f4c42

// >>> format(struct.unpack('<I', b'BLO\1')[0], 'x')
// '14f4c42'
const BLOCKS_RESP_PROTOCOL_VERSION uint32 = 0x14f4c42

// For CDC/SHARD we use 0 as an error kind
const ERROR_KIND uint8 = 0

const (
	EGGSFS_BLOCK_SERVICE_EMPTY          BlockServiceFlags = 0x0
	EGGSFS_BLOCK_SERVICE_STALE          BlockServiceFlags = 0x1
	EGGSFS_BLOCK_SERVICE_NO_READ        BlockServiceFlags = 0x2
	EGGSFS_BLOCK_SERVICE_NO_WRITE       BlockServiceFlags = 0x4
	EGGSFS_BLOCK_SERVICE_DECOMMISSIONED BlockServiceFlags = 0x8
)

func BlockServiceFlagFromName(n string) (BlockServiceFlags, error) {
	switch n {
	case "EMPTY":
		return EGGSFS_BLOCK_SERVICE_EMPTY, nil
	case "STALE":
		return EGGSFS_BLOCK_SERVICE_STALE, nil
	case "NO_READ":
		return EGGSFS_BLOCK_SERVICE_NO_READ, nil
	case "NO_WRITE":
		return EGGSFS_BLOCK_SERVICE_NO_WRITE, nil
	case "DECOMMISSIONED":
		return EGGSFS_BLOCK_SERVICE_DECOMMISSIONED, nil
	default:
		panic(fmt.Errorf("unknown blockservice flag %s", n))
	}
}

func (flags BlockServiceFlags) String() string {
	var ret []string
	if flags&EGGSFS_BLOCK_SERVICE_STALE != 0 {
		ret = append(ret, "STALE")
	}
	if flags&EGGSFS_BLOCK_SERVICE_NO_READ != 0 {
		ret = append(ret, "NO_READ")
	}
	if flags&EGGSFS_BLOCK_SERVICE_NO_WRITE != 0 {
		ret = append(ret, "NO_WRITE")
	}
	if flags&EGGSFS_BLOCK_SERVICE_DECOMMISSIONED != 0 {
		ret = append(ret, "DECOMMISSIONED")
	}
	return strings.Join(ret, ",")
}

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
	return fmt.Sprintf("0x%x", uint64(id))
}

func (id InodeId) MarshalJSON() ([]byte, error) {
	return []byte(fmt.Sprintf("%q", id.String())), nil
}

func (id *InodeId) UnmarshalJSON(b []byte) error {
	var ids string
	if err := json.Unmarshal(b, &ids); err != nil {
		return err
	}
	if ids == "ROOT" {
		*id = ROOT_DIR_INODE_ID
		return nil
	}
	idu, err := strconv.ParseUint(ids, 0, 63)
	if err != nil {
		return err
	}
	*id = InodeId(idu)
	return nil
}

func (sclass *StorageClass) UnmarshalJSON(b []byte) error {
	var s string
	if err := json.Unmarshal(b, &s); err != nil {
		return err
	}
	*sclass = StorageClassFromString(s)
	return nil
}

func (id BlockServiceId) String() string {
	return fmt.Sprintf("0x%016x", uint64(id))
}

func (id BlockId) String() string {
	return fmt.Sprintf("0x%016x", uint64(id))
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

type ShardMessageKind uint8

type CDCMessageKind uint8

type ShuckleMessageKind uint8

type BlocksMessageKind uint8

const ERROR uint8 = 0

const (
	EMPTY_STORAGE  StorageClass = 0
	INLINE_STORAGE StorageClass = 1
	HDD_STORAGE    StorageClass = 2
	FLASH_STORAGE  StorageClass = 3
)

func StorageClassFromString(s string) StorageClass {
	switch s {
	case "EMPTY":
		return EMPTY_STORAGE
	case "INLINE":
		return INLINE_STORAGE
	case "HDD":
		return HDD_STORAGE
	case "FLASH":
		return FLASH_STORAGE
	default:
		panic(fmt.Errorf("bad storage class string '%v'", s))
	}
}

func (s StorageClass) String() string {
	switch s {
	case EMPTY_STORAGE:
		return "EMPTY"
	case INLINE_STORAGE:
		return "INLINE"
	case HDD_STORAGE:
		return "HDD"
	case FLASH_STORAGE:
		return "FLASH"
	default:
		return fmt.Sprintf("StorageClass(%v)", uint8(s))
	}
}

func (crc Crc) String() string {
	return fmt.Sprintf("%08x", uint32(crc))
}

func (crc *Crc) Unpack(r io.Reader) error {
	return bincode.UnpackScalar(r, (*uint32)(crc))
}

// --------------------------------------------------------------------
// Shard requests/responses

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

// This request is only needed to recover from error resulting from repeated
// calls to things moving edges (e.g. SameDirectoryRenameReq & friends).
//
// TODO this and the response are very ad-hoc, it'd possibly be nicer to fold
// it in FullReadDir
type SnapshotLookupReq struct {
	DirId     InodeId
	Name      string
	StartFrom EggsTime
}

type SnapshotLookupEdge struct {
	// If the extra bit is set, it's owned.
	TargetId     InodeIdExtra
	CreationTime EggsTime
}

type SnapshotLookupResp struct {
	NextTime EggsTime // 0 for done
	Edges    []SnapshotLookupEdge
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
	Note  string
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

type DirectoryInfoEntry struct {
	Tag  DirectoryInfoTag
	Body []byte
}

// The directory info is a bunch of tagged entries. If we want a tag
// for a directory, if we don't have it in the current directory we
// need to traverse upwards until we find it.
type DirectoryInfo struct {
	Entries []DirectoryInfoEntry
}

type StatDirectoryResp struct {
	Mtime EggsTime
	Owner InodeId // if NULL_INODE_ID, the directory is currently snapshot
	Info  DirectoryInfo
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

type AddInlineSpanReq struct {
	FileId       InodeId
	Cookie       [8]byte
	StorageClass StorageClass // Either EMPTY_STORAGE or INLINE_STORAGE
	ByteOffset   uint64
	Size         uint32
	Crc          Crc
	Body         []byte
}

type AddInlineSpanResp struct{}

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
// The storage class must not be EMPTY_STORAGE or INLINE_STORAGE.
type AddSpanInitiateReq struct {
	FileId       InodeId
	Cookie       [8]byte
	ByteOffset   uint64
	Size         uint32
	Crc          Crc
	StorageClass StorageClass
	// A single element of these matches if all the nonzero elements in it match.
	// This is useful when the kernel knows it cannot communicate with a certain block
	// service (because it noticed that it is broken before shuckle/shard did, or
	// because of some transient network problem, or...).
	Blacklist []BlockServiceId
	Parity    rs.Parity
	Stripes   uint8 // [1, 15]
	CellSize  uint32
	// Stripes x Parity.Blocks() array with the CRCs for every cell -- row-major.
	Crcs []Crc
}

type BlockInfo struct {
	BlockServiceIp1   [4]byte
	BlockServicePort1 uint16
	BlockServiceIp2   [4]byte
	BlockServicePort2 uint16
	BlockServiceId    BlockServiceId
	BlockId           BlockId
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
	ByteOffset uint64
	Proofs     []BlockProof
}

type AddSpanCertifyResp struct{}

type RemoveSpanInitiateReq struct {
	FileId InodeId
	Cookie [8]byte
}

type RemoveSpanInitiateResp struct {
	ByteOffset uint64
	// If empty, this is a blockless span, and no certification is required
	Blocks []BlockInfo
}

type RemoveSpanCertifyReq struct {
	FileId     InodeId
	Cookie     [8]byte
	ByteOffset uint64
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

type LinkFileResp struct {
	CreationTime EggsTime
}

// turns a current outgoing edge into a snapshot owning edge.
type SoftUnlinkFileReq struct {
	OwnerId InodeId
	FileId  InodeId
	Name    string
	// See comment in `SameDirectoryRenameReq` for an idication of why
	// we have this here even if it's not strictly needed.
	CreationTime EggsTime
}

type SoftUnlinkFileResp struct{}

// Starts from the first span with byte offset <= than the provided
// ByteOffset (this is so that you can just start reading a file
// somewhere without hunting for the right span).
type FileSpansReq struct {
	FileId     InodeId
	ByteOffset uint64
	// if 0, no limit.
	Limit uint32
}

type FetchedBlock struct {
	BlockServiceIx uint8 // Index into `BlockServices`
	BlockId        BlockId
	Crc            Crc
}

type FetchedInlineSpan struct {
	Body []byte
}

func (f *FetchedInlineSpan) String() string {
	return fmt.Sprintf("%v", f.Body)
}

type FetchedBlocksSpan struct {
	Parity     rs.Parity
	Stripes    uint8 // [0, 16)
	CellSize   uint32
	Blocks     []FetchedBlock
	StripesCrc []Crc
}

func (f *FetchedBlocksSpan) String() string {
	return fmt.Sprintf("%+v", *f)
}

type IsFetchedSpanBody interface {
	bincode.Packable
	bincode.Unpackable
}

type FetchedSpanHeader struct {
	ByteOffset   uint64
	Size         uint32
	Crc          Crc
	StorageClass StorageClass
}

// If StorageClass is `INLINE_STORAGE`, then the `Body` will be `FetchedInlineSpan`.
// Otherwise it'll be `FetchedBlockSpan`.
type FetchedSpan struct {
	Header FetchedSpanHeader
	Body   IsFetchedSpanBody
}

type BlockService struct {
	Ip1   [4]byte
	Port1 uint16
	Ip2   [4]byte
	Port2 uint16
	// The BlockServiceId is stable (derived from the secret key, which is stored on the
	// block service id).
	//
	// The ip/port are not, and in fact might be shared by multiple block services.
	Id    BlockServiceId
	Flags uint8
}

type FileSpansResp struct {
	NextOffset    uint64
	BlockServices []BlockService
	Spans         []FetchedSpan
}

type SameDirectoryRenameReq struct {
	TargetId InodeId
	DirId    InodeId
	OldName  string
	// This request is a bit annoying in the presence of packet
	// loss. Consider this scenario: a client performs a
	// `SameDirectoryRenameReq`, which goes through, but the
	// response is dropped.
	//
	// In this case the client must retry but genuine failures
	// (for example because the file does not exist) are indistinguishable
	// from failures due to the previous request going through.
	//
	// For this reason we include the creation time here (even if we
	// don't strictly needed because current edges are uniquely
	// identified by name) so that the shard can implement heuristics
	// to let likely repeated calls through in the name of idempotency.
	OldCreationTime EggsTime
	NewName         string
}

type SameDirectoryRenameResp struct {
	NewCreationTime EggsTime
}

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
	Info    DirectoryInfo
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
	// We need the full directory info when removing a directory since we won't
	// be able to inherit it anymore.
	Info DirectoryInfo
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
// TODO also add comment regarding that locking edges is safe only because
// we coordinate things from the CDC
type CreateLockedCurrentEdgeReq struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
}

type CreateLockedCurrentEdgeResp struct {
	CreationTime EggsTime
}

type LockCurrentEdgeReq struct {
	DirId        InodeId
	TargetId     InodeId
	CreationTime EggsTime
	Name         string
}

type LockCurrentEdgeResp struct{}

// This also lets us turn edges into snapshot, through `WasMoved`.
type UnlockCurrentEdgeReq struct {
	DirId        InodeId
	Name         string
	CreationTime EggsTime
	TargetId     InodeId
	// Turn the current edge into a snapshot edge, and create a deletion
	// edge with the same name.
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
type SameShardHardFileUnlinkReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type SameShardHardFileUnlinkResp struct{}

// This is needed to implement inter-shard hard file unlinking, and it is unsafe, since
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

// TODO this works with transient files, but doesn't require a cookie -- it's a bit
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
	// Not strictly needed, since the migration process usually fetches some file ids
	// and then purges all blocks with that block service id. So we can just keep asking
	// from the beginning. However, it can be useful due to the problems listed
	// here <https://github.com/facebook/rocksdb/wiki/Implement-Queue-Service-Using-RocksDB>.
	StartFrom InodeId
}

type BlockServiceFilesResp struct {
	FileIds []InodeId
}

type ExpireTransientFileReq struct {
	Id InodeId
}

type ExpireTransientFileResp struct{}

// --------------------------------------------------------------------
// CDC requests/responses

type MakeDirectoryReq struct {
	OwnerId InodeId
	Name    string
}

type MakeDirectoryResp struct {
	Id           InodeId
	CreationTime EggsTime
}

type RenameFileReq struct {
	TargetId        InodeId
	OldOwnerId      InodeId
	OldName         string
	OldCreationTime EggsTime
	NewOwnerId      InodeId
	NewName         string
}

type RenameFileResp struct {
	CreationTime EggsTime
}

type SoftUnlinkDirectoryReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	CreationTime EggsTime
	Name         string
}

type SoftUnlinkDirectoryResp struct{}

type RenameDirectoryReq struct {
	TargetId        InodeId
	OldOwnerId      InodeId
	OldName         string
	OldCreationTime EggsTime
	NewOwnerId      InodeId
	NewName         string
}

type RenameDirectoryResp struct {
	CreationTime EggsTime
}

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

type CrossShardHardUnlinkFileReq struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type CrossShardHardUnlinkFileResp struct{}

// --------------------------------------------------------------------
// directory info

type DirectoryInfoTag uint8

const SNAPSHOT_POLICY_TAG DirectoryInfoTag = 1
const SPAN_POLICY_TAG DirectoryInfoTag = 2
const BLOCK_POLICY_TAG DirectoryInfoTag = 3
const STRIPE_POLICY_TAG DirectoryInfoTag = 4

func (d DirectoryInfoTag) String() string {
	switch d {
	case SNAPSHOT_POLICY_TAG:
		return "SNAPSHOT"
	case SPAN_POLICY_TAG:
		return "SPAN"
	case BLOCK_POLICY_TAG:
		return "BLOCK"
	case STRIPE_POLICY_TAG:
		return "STRIPE"
	default:
		return fmt.Sprintf("DirectoryInfoTag(%v)", uint8(d))
	}
}

func DirInfoTagFromName(s string) DirectoryInfoTag {
	switch s {
	case "SNAPSHOT":
		return SNAPSHOT_POLICY_TAG
	case "SPAN":
		return SPAN_POLICY_TAG
	case "BLOCK":
		return BLOCK_POLICY_TAG
	case "STRIPE":
		return STRIPE_POLICY_TAG
	default:
		panic(fmt.Errorf("bad dir info tag string '%v'", s))
	}
}

// MSB: whether this policy is active or not. After: nanoseconds.
type DeleteAfterTime uint64

func (dat DeleteAfterTime) Active() bool {
	return (uint64(dat) >> 63) != 0
}

func (dat DeleteAfterTime) String() string {
	if dat.Active() {
		return fmt.Sprintf("ActiveDeleteAfterTime(%v)", dat.Time())
	} else {
		return "InactiveDeleteAfterTime()"
	}
}

func (dat DeleteAfterTime) Time() time.Duration {
	return time.Duration(uint64(dat) & ^(uint64(1) << 63))
}

func InactiveDeleteAfterTime() DeleteAfterTime {
	return 0
}

func ActiveDeleteAfterTime(duration time.Duration) DeleteAfterTime {
	if duration.Nanoseconds() < 0 {
		panic(fmt.Errorf("negative duration in DeleteAfterTime: %v", duration))
	}
	return DeleteAfterTime((uint64(1) << 63) | uint64(duration.Nanoseconds()))
}

// MSB: whether this policy is active or not. After: number of versions.
type DeleteAfterVersions uint16

func (dav DeleteAfterVersions) Active() bool {
	return (uint16(dav) >> 15) != 0
}

func (dav DeleteAfterVersions) Versions() uint16 {
	return uint16(dav) & ^(uint16(1) << 15)
}

func (dav DeleteAfterVersions) String() string {
	if dav.Active() {
		return fmt.Sprintf("ActiveDeleteAfterVersions(%v)", dav.Versions())
	} else {
		return "InactiveDeleteAfterVersions()"
	}
}

func InactiveDeleteAfterVersions() DeleteAfterVersions {
	return 0
}

func ActiveDeleteAfterVersions(versions int16) DeleteAfterVersions {
	if versions < 0 {
		panic(fmt.Errorf("negative versions: %v", versions))
	}
	return DeleteAfterVersions((uint16(1) << 15) | uint16(versions))
}

// If multiple policies are present, the file will be deleted if
// any of the policies are not respected.
//
// If neither policies are active, then snapshots will be kept forever.
//
// Also note that you can use either policy to delete all snapshots, by
// setting either to zero.
type SnapshotPolicy struct {
	DeleteAfterTime     DeleteAfterTime
	DeleteAfterVersions DeleteAfterVersions
}

func (p *SnapshotPolicy) Tag() DirectoryInfoTag {
	return SNAPSHOT_POLICY_TAG
}

func (p *SnapshotPolicy) UnmarshalJSON(b []byte) error {
	var spec struct {
		DeleteAfterVersions *uint16
		DeleteAfterTime     *string
	}
	if err := json.Unmarshal(b, &spec); err != nil {
		return err
	}
	if spec.DeleteAfterVersions != nil {
		p.DeleteAfterVersions = ActiveDeleteAfterVersions(int16(*spec.DeleteAfterVersions))
	} else {
		p.DeleteAfterVersions = InactiveDeleteAfterVersions()
	}
	if spec.DeleteAfterTime != nil {
		dur, err := time.ParseDuration(*spec.DeleteAfterTime)
		if err != nil {
			return err
		}
		p.DeleteAfterTime = ActiveDeleteAfterTime(dur)
	} else {
		p.DeleteAfterTime = InactiveDeleteAfterTime()
	}
	return nil
}

// 100MiB
const MAX_BLOCK_SIZE uint32 = 100 << 20

type BlockPolicyEntry struct {
	StorageClass StorageClass
	// The minimum amount of block size we want in this storage class. For spinning
	// disks, a function of seek latency and sustained data rate.
	MinSize uint32
}

// Sorted by MinSize.
type BlockPolicy struct {
	Entries []BlockPolicyEntry
}

func (p *BlockPolicy) Tag() DirectoryInfoTag {
	return BLOCK_POLICY_TAG
}

func (p *BlockPolicy) Pick(size uint32) (entry *BlockPolicyEntry) {
	if size > MAX_BLOCK_SIZE {
		panic(fmt.Errorf("size %v is greater than overall maximum size %v", size, MAX_BLOCK_SIZE))
	}
	for i := len(p.Entries) - 1; i >= 0; i-- {
		if size > p.Entries[i].MinSize {
			return &p.Entries[i]
		}
	}
	return &p.Entries[0]
}

type SpanPolicyEntry struct {
	MaxSize uint32
	Parity  rs.Parity
}

type SpanPolicy struct {
	Entries []SpanPolicyEntry
}

func (p *SpanPolicy) Pick(size uint32) (entry *SpanPolicyEntry) {
	if p.Entries[len(p.Entries)-1].MaxSize < size {
		panic(fmt.Errorf("size %v is greater than overall maximum size %v", size, p.Entries[len(p.Entries)-1].MaxSize))
	}
	for i := len(p.Entries) - 2; i >= 0; i-- {
		if size > p.Entries[i].MaxSize {
			return &p.Entries[i+1]
		}
	}
	return &p.Entries[0]
}

func (p *SpanPolicy) Tag() DirectoryInfoTag {
	return SPAN_POLICY_TAG
}

// This will in essence determine how much data we need to read from the block
// services before being able to return to the client.
type StripePolicy struct {
	TargetStripeSize uint32
}

func (p *StripePolicy) Tag() DirectoryInfoTag {
	return STRIPE_POLICY_TAG
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
	DirId           InodeId
	TargetId        InodeId
	OldName         string
	OldCreationTime EggsTime
	NewName         string
}

type SoftUnlinkFileEntry struct {
	OwnerId      InodeId
	FileId       InodeId
	Name         string
	CreationTime EggsTime
}

type CreateDirectoryInodeEntry struct {
	Id      InodeId
	OwnerId InodeId
	Info    DirectoryInfo
}

type CreateLockedCurrentEdgeEntry struct {
	DirId    InodeId
	Name     string
	TargetId InodeId
}

type UnlockCurrentEdgeEntry struct {
	DirId InodeId
	Name  string
	// Here the `CreationTime` is currently not strictly needed, since we have the
	// locking mechanism + CDC synchronization anyway, which offer stronger guarantees
	// which means we never need heuristics for this. But we include it for consistency
	// and to better detect bugs.
	CreationTime EggsTime
	TargetId     InodeId
	WasMoved     bool
}

type LockCurrentEdgeEntry struct {
	DirId        InodeId
	Name         string
	CreationTime EggsTime
	TargetId     InodeId
}

type RemoveDirectoryOwnerEntry struct {
	DirId InodeId
	Info  DirectoryInfo
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
	Info  DirectoryInfo
}

type RemoveNonOwnedEdgeEntry struct {
	DirId        InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type SameShardHardFileUnlinkEntry struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type RemoveSpanInitiateEntry struct {
	FileId InodeId
}

type AddInlineSpanEntry struct {
	FileId       InodeId
	StorageClass StorageClass // Either EMPTY_STORAGE or INLINE_STORAGE
	ByteOffset   uint64
	Size         uint32
	Crc          Crc
	Body         []byte
}

type BlockServiceInfo struct {
	Id             BlockServiceId
	Ip1            [4]byte
	Port1          uint16
	Ip2            [4]byte
	Port2          uint16
	StorageClass   StorageClass
	FailureDomain  [16]byte
	SecretKey      [16]byte
	Flags          BlockServiceFlags
	CapacityBytes  uint64
	AvailableBytes uint64
	Blocks         uint64 // how many blocks we have
	Path           string
	LastSeen       EggsTime
}

type UpdateBlockServicesEntry struct {
	BlockServices []BlockServiceInfo
}

type EntryNewBlockInfo struct {
	BlockServiceId BlockServiceId
	Crc            Crc
}

type AddSpanInitiateEntry struct {
	FileId       InodeId
	ByteOffset   uint64
	Size         uint32
	Crc          Crc
	StorageClass StorageClass
	Parity       rs.Parity
	Stripes      uint8 // [1, 16]
	CellSize     uint32
	BodyBlocks   []EntryNewBlockInfo
	BodyStripes  []Crc // the CRCs
}

type AddSpanCertifyEntry struct {
	FileId     InodeId
	ByteOffset uint64
	Proofs     []BlockProof
}

type MakeFileTransientEntry struct {
	Id   InodeId
	Note string
}

type RemoveSpanCertifyEntry struct {
	FileId     InodeId
	ByteOffset uint64
	Proofs     []BlockProof
}

type RemoveOwnedSnapshotFileEdgeEntry struct {
	OwnerId      InodeId
	TargetId     InodeId
	Name         string
	CreationTime EggsTime
}

type SwapBlocksEntry struct {
	FileId1     InodeId
	ByteOffset1 uint64
	BlockId1    BlockId
	FileId2     InodeId
	ByteOffset2 uint64
	BlockId2    BlockId
}

type ExpireTransientFileEntry struct {
	Id InodeId
}

// --------------------------------------------------------------------
// shuckle requests/responses

type ShuckleRequest interface {
	bincode.Packable
	bincode.Unpackable
	ShuckleRequestKind() ShuckleMessageKind
}

type ShuckleResponse interface {
	bincode.Packable
	bincode.Unpackable
	ShuckleResponseKind() ShuckleMessageKind
}

type BlockServicesForShardResp struct {
	BlockServices []BlockServiceInfo
}

type SetBlockServiceFlagsReq struct {
	Id        BlockServiceId
	Flags     BlockServiceFlags
	FlagsMask uint8
}

type SetBlockServiceFlagsResp struct{}

type AllBlockServicesReq struct{}

type AllBlockServicesResp struct {
	BlockServices []BlockServiceInfo
}

type RegisterBlockServicesReq struct {
	BlockServices []BlockServiceInfo
}

type RegisterBlockServicesResp struct{}

type ShardsReq struct{}

type ShardInfo struct {
	Ip1      [4]byte
	Port1    uint16
	Ip2      [4]byte
	Port2    uint16
	LastSeen EggsTime
}

type ShardsResp struct {
	// Always 256 length. If we don't have info for some shards, the ShardInfo
	// is zeroed.
	Shards []ShardInfo
}

type RegisterShardInfo struct {
	Ip1   [4]byte
	Port1 uint16
	Ip2   [4]byte
	Port2 uint16
}

type RegisterShardReq struct {
	Id   ShardId
	Info RegisterShardInfo
}

type RegisterShardResp struct{}

type RegisterCdcReq struct {
	Ip1                    [4]byte
	Port1                  uint16
	Ip2                    [4]byte
	Port2                  uint16
	CurrentTransactionKind CDCMessageKind // if 0, nothing is executing
	CurrentTransactionStep uint8
	QueuedTransactions     uint64
}

type RegisterCdcResp struct{}

type CdcReq struct{}

type CdcResp struct {
	Ip1      [4]byte
	Port1    uint16
	Ip2      [4]byte
	Port2    uint16
	LastSeen EggsTime
}

type InfoReq struct{}

type InfoResp struct {
	NumBlockServices  uint32
	NumFailureDomains uint32
	Capacity          uint64
	Available         uint64
	Blocks            uint64
}

// --------------------------------------------------------------------
// block service requests/responses

type BlocksRequest interface {
	bincode.Packable
	bincode.Unpackable
	BlocksRequestKind() BlocksMessageKind
}

type BlocksResponse interface {
	bincode.Packable
	bincode.Unpackable
	BlocksResponseKind() BlocksMessageKind
}

type EraseBlockReq struct {
	BlockId     BlockId
	Certificate [8]byte
}

type EraseBlockResp struct {
	Proof [8]byte
}

type FetchBlockReq struct {
	BlockId BlockId
	Offset  uint32
	Count   uint32
}

// Followed by data
type FetchBlockResp struct{}

type WriteBlockReq struct {
	BlockId     BlockId
	Crc         Crc
	Size        uint32
	Certificate [8]byte
}

// This does _not_ include the proof: that comes after the write.
// However, we want to get a go-ahead before starting to write.
type WriteBlockResp struct{}

type BlockWrittenReq struct{}

type BlockWrittenResp struct {
	Proof [8]byte
}

type TestWriteReq struct {
	Size uint64
}

type TestWriteResp struct{}
