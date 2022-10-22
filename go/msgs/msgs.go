// See bincodegen.go for more comments on how we split op these operations
package msgs

import (
	"time"
	"xtx/eggsfs/bincode"
)

//go:generate go run ../bincodegen

const UDP_MTU = 1472

type InodeType uint8
type InodeId uint64
type OwnedInodeId uint64 // 64th bit is used to mark whether the inode is owned.
type ShardId uint8
type Parity uint8
type StorageClass uint8
type EggsTime uint64
type BlockId uint64

const (
	DIRECTORY InodeType = 1
	FILE      InodeType = 2
	SYMLINK   InodeType = 3
)

func (id InodeId) Type() InodeType {
	return InodeType((id >> 61) & 0x03)
}

func (id InodeId) Shard() ShardId {
	return ShardId(id & 0xFF)
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

const (
	INLINE_STORAGE    StorageClass = 0
	ZERO_FILL_STORAGE StorageClass = 1
)

// Given directory inode and name, returns inode from outgoing
// current edge. Does not consider non-current directories and
// non-current edges.
type LookupReq struct {
	DirId InodeId
	Name  []byte
}

type LookupResp struct {
	TargetId     InodeId
	CreationTime EggsTime
}

// Given inode, returns size, type, last modified for files,
// last modified and parent for directories.
type StatReq struct {
	Id InodeId
}

type StatResp struct {
	Mtime       EggsTime
	SizeOrOwner uint64 // file -> size, dirs -> owner
	// The next two fields are unused for files -- we don't know
	// if a file is local by just looking at it.
	IsCurrentDirectory bool
	Opaque             []byte
}

type ReadDirReq struct {
	DirId     InodeId
	StartHash uint64
	// * all the times leading up to the creation of the directory will return
	//     an empty directory listing.
	// * all the times after the last modification will return the current directory
	//     listing (use 0xFFFFFFFFFFFFFFFF to just get the current directory listing)
	AsOfTime EggsTime //
}

type ReadDirResp struct {
	NextHash uint64
	Results  []Edge
}

// create a new transient file.
type ConstructFileReq struct {
	Type InodeType // must not be DIRECTORY
}

type ConstructFileResp struct {
	Id     InodeId
	Cookie uint64
}

type NewBlockInfo struct {
	Crc32 []byte `bincode:"fixed4"`
	Size  uint64 `bincode:"varint"`
}

// add span. the file must be transient
type AddSpanInitiateReq struct {
	FileId       InodeId
	Cookie       uint64
	ByteOffset   uint64 `bincode:"varint"`
	StorageClass StorageClass
	Parity       Parity
	Crc32        []byte `bincode:"fixed4"`
	Size         uint64 `bincode:"varint"`
	// empty if storage class not inline
	BodyBytes []byte
	// empty if storage class zero/inline
	BodyBlocks []NewBlockInfo
}

type BlockInfo struct {
	Ip      []byte `bincode:"fixed4"`
	Port    uint16
	BlockId uint64
	// certificate := MAC(b'w' + block_id + crc + size)[:8] (for creation)
	Certificate []byte `bincode:"fixed8"`
}

type AddSpanInitiateResp struct {
	// Left empty for inline/zero-filled spans
	Blocks []BlockInfo
}

// certify span. again, the file must be transient.
type AddSpanCertifyReq struct {
	FileId     InodeId
	Cookie     uint64
	ByteOffset uint64   `bincode:"varint"`
	Proofs     [][]byte `bincode:"fixed8"`
}

type AddSpanCertifyResp struct{}

// makes a transient file current. requires the inode, the
// parent dir, and the filename.
type LinkFileReq struct {
	FileId  InodeId
	Cookie  uint64
	OwnerId InodeId
	Name    []byte
}

type LinkFileResp struct{}

// turns a current outgoing edge into a snapshot owning edge.
type SoftUnlinkFileReq struct {
	OwnerId InodeId
	FileId  InodeId
	Name    []byte
}

type SoftUnlinkFileResp struct{}

type FileSpansReq struct {
	FileId     InodeId
	ByteOffset uint64 `bincode:"varint"`
}

type FetchedBlock struct {
	Ip      []byte `bincode:"fixed4"`
	Port    uint16
	BlockId BlockId
	Crc32   []byte `bincode:"fixed4"`
	Size    uint64 `bincode:"varint"`
	Flags   uint8
}

// If the storage class is zero-filled, BodyBytes and BodyBlocks are empty.
// If the storage class is inline, BodyBlocks is empty.
// If the storage class is not inline, BodyBytes is empty.
type FetchedSpan struct {
	ByteOffset   uint64 `bincode:"varint"`
	Parity       Parity
	StorageClass StorageClass
	Crc32        []byte `bincode:"fixed4"`
	Size         uint64 `bincode:"varint"`
	BodyBytes    []byte
	BodyBlocks   []FetchedBlock
}

type FileSpansResp struct {
	NextOffset uint64 `bincode:"varint"`
	Spans      []FetchedSpan
}

type SameDirectoryRenameReq struct {
	TargetId InodeId
	DirId    InodeId
	OldName  []byte
	NewName  []byte
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
	DeadlineTime EggsTime
}
type VisitTransientFilesResp struct {
	NextId InodeId
	Files  []TransientFile
}

type FullReadDirReq struct {
	DirId     InodeId
	StartHash uint64
	StartName []byte
	StartTime EggsTime
}

type Edge struct {
	TargetId     InodeId
	NameHash     uint64
	Name         []byte
	CreationTime EggsTime
}

type EdgeWithOwnership struct {
	TargetId     OwnedInodeId
	NameHash     uint64
	Name         []byte
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
	// stuff like expiration policy, storage class, etc.
	Opaque []byte
}

type CreateDirectoryINodeResp struct {
	Mtime EggsTime
}

// This is needed to remove directories -- but it can break the invariants
// between edges pointing to the dir and the owner.
type SetDirectoryOwnerReq struct {
	DirId   InodeId
	OwnerId InodeId
}

type SetDirectoryOwnerResp struct{}

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
	Name     []byte
	TargetId InodeId
	// We need this because we want idempotency (retrying this request should
	// not create spurious edges when overriding files), and we want to guarantee
	// that the current edge is newest.
	CreationTime EggsTime
}

type CreateLockedCurrentEdgeResp struct{}

type LockCurrentEdgeReq struct {
	DirId    InodeId
	Name     []byte
	TargetId InodeId
}

type LockCurrentEdgeResp struct{}

// This also lets us turn edges into snapshot.
type UnlockCurrentEdgeReq struct {
	DirId    InodeId
	Name     []byte
	TargetId InodeId
	WasMoved bool
}

type UnlockCurrentEdgeResp struct{}

type RemoveEdgesResp struct{}

type RemoveNonOwnedEdgeReq struct {
	DirId        InodeId
	TargetId     InodeId
	Name         []byte
	CreationTime EggsTime
}

type RemoveNonOwnedEdgeResp struct{}

type RemoveOwnedSnapshotFileEdgeReq struct {
	DirId        InodeId
	TargetId     InodeId
	Name         []byte
	CreationTime EggsTime
}

type RemoveOwnedSnapshotFileEdgeResp struct{}

type MakeDirectoryReq struct {
	OwnerId InodeId
	Name    []byte
}

type MakeDirectoryResp struct {
	Id InodeId
}

type RenameFileReq struct {
	TargetId   InodeId
	OldOwnerId InodeId
	OldName    []byte
	NewOwnerId InodeId
	NewName    []byte
}

type RenameFileResp struct{}

type RemoveDirectoryReq struct {
	OwnerId  InodeId
	TargetId InodeId
	Name     []byte
}

type RemoveDirectoryResp struct{}

type RenameDirectoryReq struct {
	TargetId   InodeId
	OldOwnerId InodeId
	OldName    []byte
	NewOwnerId InodeId
	NewName    []byte
}

type RenameDirectoryResp struct{}
