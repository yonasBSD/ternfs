package common

import (
	"time"
	"xtx/eggsfs/bincode"
)

const UDP_MTU = 1472

type InodeType uint8
type InodeId uint64
type ShardId uint8
type ErrCode uint16

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

const (
	NULL_INODE_ID InodeId = 0
	// Can't call MakeInodeId in constant
	ROOT_DIR_INODE_ID = InodeId(DIRECTORY) << 61
)

func (shard ShardId) Port() int {
	return 22272 + int(shard)
}

const CDC_PORT int = 36137

//go:generate stringer -type=ErrCode
const (
	INTERNAL_ERROR                      ErrCode = 0
	FATAL_ERROR                         ErrCode = 1
	TIMEOUT                             ErrCode = 2
	MALFORMED_REQUEST                   ErrCode = 32
	NOT_AUTHORISED                      ErrCode = 3
	UNRECOGNIZED_REQUEST                ErrCode = 4
	FILE_NOT_FOUND                      ErrCode = 5
	DIRECTORY_NOT_FOUND                 ErrCode = 6
	NAME_NOT_FOUND                      ErrCode = 7
	TYPE_IS_DIRECTORY                   ErrCode = 8
	TYPE_IS_NOT_DIRECTORY               ErrCode = 9
	BAD_COOKIE                          ErrCode = 10
	INCONSISTENT_STORAGE_CLASS_PARITY   ErrCode = 11
	LAST_SPAN_STATE_NOT_CLEAN           ErrCode = 12
	COULD_NOT_PICK_BLOCK_SERVERS        ErrCode = 13
	BAD_SPAN_BODY                       ErrCode = 14
	SPAN_NOT_FOUND                      ErrCode = 15
	BLOCK_SERVER_NOT_FOUND              ErrCode = 16
	CANNOT_CERTIFY_BLOCKLESS_SPAN       ErrCode = 17
	BAD_NUMBER_OF_BLOCKS_PROOFS         ErrCode = 18
	BAD_BLOCK_PROOF                     ErrCode = 19
	CANNOT_OVERRIDE_NAME                ErrCode = 20
	NAME_IS_LOCKED                      ErrCode = 21
	OLD_NAME_IS_LOCKED                  ErrCode = 22
	NEW_NAME_IS_LOCKED                  ErrCode = 23
	MORE_RECENT_SNAPSHOT_ALREADY_EXISTS ErrCode = 24
	MISMATCHING_TARGET                  ErrCode = 25
	MISMATCHING_OWNER                   ErrCode = 26
	DIRECTORY_NOT_EMPTY                 ErrCode = 27
	FILE_IS_TRANSIENT                   ErrCode = 28
	OLD_DIRECTORY_NOT_FOUND             ErrCode = 29
	NEW_DIRECTORY_NOT_FOUND             ErrCode = 30
	LOOP_IN_DIRECTORY_RENAME            ErrCode = 31
)

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

const EGGS_EPOCH uint64 = 1_577_836_800_000_000_000

func EggsTime() uint64 {
	return uint64(time.Now().UnixNano()) - EGGS_EPOCH
}

type Alerter interface {
	RaiseAlert(err error)
}
