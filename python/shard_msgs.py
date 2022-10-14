#!/usr/bin/env python3

from dataclasses import dataclass, field, replace
import enum
import itertools
from typing import ClassVar, Dict, List, NamedTuple, Optional, Tuple, Type, Union, Set

import bincode
from common import *
import crypto

SHARD_PROTOCOL_VERSION = b'SHA\0'

# u8 where MSb states whether this request is privileged
class ShardRequestKind(enum.IntEnum):
    ERROR = 0

    # unprivileged

    # given directory inode and name, returns inode from outgoing
    # current edge.
    LOOKUP = 0x01
    # given inode, returns size, type, last modified for files,
    # last modified and parent for directories.
    # Errors: NOT_FOUND
    STAT = 0x02
    # Errors: NOT_FOUND
    READ_DIR = 0x03
    # create a new transient file. takes in desired path of file
    # for debugging purposes
    CONSTRUCT_FILE = 0x04
    # add span. the file must be transient
    ADD_SPAN_INITIATE = 0x05
    # certify span. again, the file must be transient.
    ADD_SPAN_CERTIFY = 0x06
    # makes a transient file current. requires the inode, the
    # parent dir, and the filename.
    # DIRECTORY_NOT_FOUND and FILE_NOT_FOUND
    LINK_FILE = 0x07
    # turns a current outgoing edge into a snapshot owning edge. requires parent directory
    # and file name
    SOFT_UNLINK_FILE = 0x0C
    # gets the file spans
    FILE_SPANS = 0x0D
    # renames an object within a single directory.
    SAME_DIRECTORY_RENAME = 0x0E

    # Private operations. These are safe operations, but we don't want the FS client itself
    # to perform them. TODO make privileged?

    # Takes directory inode, name, and creation time.
    # Returns an error if the edge is anything but snapshot-not-owning. These edges
    # can be freely collected -- they are dead weak references.
    REMOVE_SNAPSHOT_NON_OWNING_EDGE = 0x10
    # destructs an empty transient file
    DESTRUCT_FILE = 0x11
    # safe but we don't want clients to do this.
    DESTRUCT_SPAN_INITIATE = 0x12
    DESTRUCT_SPAN_CERTIFY = 0x13
    # This handles the case where a snapshot-owning edge (which must be to a file)
    # is intra-shard. In this case we can atomically remove it and make
    # the file transient.
    HARD_UNLINK_FILE_WITHIN_SHARD = 0x14
    VISIT_DIRECTORIES = 0x15
    VISIT_FILES = 0x20 # this is used solely for debugging
    VISIT_TRANSIENT_FILES = 0x16
    REVERSE_BLOCK_QUERY = 0x17
    REPAIR_BLOCK = 0x18
    REPAIR_SPANS = 0x19

    # privileged (needs MAC)

    # Creates a directory with a given parent and given inode id. Unsafe because
    # we can create directories with a certain parent while the paren't isn't
    # pointing at them (or isn't even a valid inode). We'd break the "no directory leaks"
    # invariant or the "null dir owner <-> not current" invariant.
    CREATE_DIRECTORY_INODE = 0x81
    # This is needed to remove directories -- but it can break the invariants
    # between edges pointing to the dir and the owner.
    SET_DIRECTORY_OWNER = 0x91
    # These is generally needed when we need to move/create things cross-shard, but
    # is unsafe for various reasons:
    # * W must remember to unlock the edge, otherwise it'll be locked forever.
    # * We must make sure to not end up with multiple owners for the target.
    # TODO add comment about how creating an unlocked current edge is no good
    # if we want to retry things safely. We might create without realizing the
    # edge, and somebody might move it away in the meantime (with some shard-local
    # operation).
    CREATE_LOCKED_CURRENT_EDGE = 0x82
    LOCK_CURRENT_EDGE = 0x83
    UNLOCK_CURRENT_EDGE = 0x84
    # This is needed for inter-shard file hard unlinking. However it is unsafe because
    # it can break the "no file leaks" invariant -- the target file might be orphaned.
    REMOVE_SNAPSHOT_OWNING_EDGE = 0x85
    # This is also needed for inter-shard file hard unlinking. However it is unsafe because
    # we might break the "unique ownership" invariant.
    CREATE_TRANSIENT_EDGE = 0x86

    def is_privileged(self) -> bool:
        return bool(self.value & 0x80)

assert ShardRequestKind.ERROR == EggsError.kind

# INTERNAL_ERROR/FATAL_ERROR/TIMEOUT are implicitly included in all of these
SHARD_ERRORS: Dict[ShardRequestKind, Set[ErrCode]] = {
    ShardRequestKind.LOOKUP: {ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND},
    ShardRequestKind.STAT: {ErrCode.DIRECTORY_NOT_FOUND, ErrCode.FILE_NOT_FOUND},
    ShardRequestKind.READ_DIR: {ErrCode.DIRECTORY_NOT_FOUND},
    ShardRequestKind.CONSTRUCT_FILE: {ErrCode.TYPE_IS_DIRECTORY},
    ShardRequestKind.ADD_SPAN_INITIATE: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.INCONSISTENT_STORAGE_CLASS_PARITY,
        ErrCode.LAST_SPAN_STATE_NOT_CLEAN, ErrCode.COULD_NOT_PICK_BLOCK_SERVERS,
        ErrCode.BAD_SPAN_BODY, ErrCode.SPAN_NOT_FOUND, ErrCode.BLOCK_SERVER_NOT_FOUND,
    },
    ShardRequestKind.ADD_SPAN_CERTIFY: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.CANNOT_CERTIFY_BLOCKLESS_SPAN,
        ErrCode.BAD_NUMBER_OF_BLOCKS_PROOFS, ErrCode.BLOCK_SERVER_NOT_FOUND, ErrCode.BAD_BLOCK_PROOF,
    },
    ShardRequestKind.LINK_FILE: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.DIRECTORY_NOT_FOUND,
        ErrCode.LAST_SPAN_STATE_NOT_CLEAN, ErrCode.CANNOT_OVERRIDE_NAME, ErrCode.NAME_IS_LOCKED,
        # This should be incredibly rare barring bad snapshot edges
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    ShardRequestKind.SOFT_UNLINK_FILE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NAME_IS_LOCKED, ErrCode.TYPE_IS_DIRECTORY,
    },
    ShardRequestKind.FILE_SPANS: {
        ErrCode.FILE_NOT_FOUND, ErrCode.FILE_IS_TRANSIENT,
    },
    ShardRequestKind.SAME_DIRECTORY_RENAME: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS, ErrCode.NAME_IS_LOCKED, ErrCode.CANNOT_OVERRIDE_NAME,
    },
    ShardRequestKind.CREATE_DIRECTORY_INODE: {
        ErrCode.TYPE_IS_NOT_DIRECTORY, ErrCode.MISMATCHING_OWNER,
    },
    ShardRequestKind.SET_DIRECTORY_OWNER: {
        ErrCode.DIRECTORY_NOT_EMPTY, ErrCode.DIRECTORY_NOT_FOUND,
    },
    ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_IS_LOCKED, ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    ShardRequestKind.LOCK_CURRENT_EDGE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.MISMATCHING_TARGET, ErrCode.NAME_NOT_FOUND,
    }
}

@dataclass
class CreateDirectoryINodeReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CREATE_DIRECTORY_INODE
    id: int
    owner_id: int
    opaque: bytes # TODO what's this for?

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryINodeReq':
        inode = bincode.unpack_u64(u)
        parent_inode = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return CreateDirectoryINodeReq(id=inode, owner_id=parent_inode, opaque=opaque)
@dataclass
class CreateDirectoryINodeResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CREATE_DIRECTORY_INODE
    mtime: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryINodeResp':
        mtime = bincode.unpack_u64(u)
        return CreateDirectoryINodeResp(mtime)

@dataclass
class StatReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.STAT
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatReq':
        return StatReq(bincode.unpack_u64(u))
@dataclass
class StatResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.STAT
    mtime: int
    size_or_owner: int # files => size, dirs => owner
    opaque: bytes # always empty for files

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size_or_owner, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatResp':
        mtime = bincode.unpack_u64(u)
        size_or_owner = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return StatResp(mtime, size_or_owner, opaque)

@dataclass
class CreateLockedCurrentEdgeReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE
    dir_id: int
    name: bytes
    target_id: int
    # We need this because we want idempotency (retrying this request should
    # not create spurious edges when overriding files), and we want to guarantee
    # that the current edge is newest.
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeReq':
        parent_inode = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u)
        child_inode = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        return CreateLockedCurrentEdgeReq(parent_inode, subname, child_inode, creation_time)
@dataclass
class CreateLockedCurrentEdgeResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeResp':
        return CreateLockedCurrentEdgeResp()

@dataclass
class ReadDirReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.READ_DIR
    dir_id: int
    start_hash: int
    # * all the times leading up to the creation of the directory will return an empty directory listing.
    # * all the times after the last modification will return the current directory listing (use
    #      0xFFFFFFFFFFFFFFFF to just get the current directory listing)
    as_of_time: int = 0xFFFFFFFFFFFFFFFF

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.as_of_time, b)
        bincode.pack_u64_into(self.start_hash, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirReq':
        inode = bincode.unpack_u64(u)
        as_of = bincode.unpack_u64(u)
        continuation_key = bincode.unpack_u64(u)
        return ReadDirReq(inode, as_of_time=as_of, start_hash=continuation_key)

@dataclass
class ReadDirPayload(bincode.Packable):
    target_id: int
    name_hash: int
    name: bytes

    def calc_packed_size(self) -> int:
        ret = 8 + 8 # inode + hash_of_name
        ret += bincode.bytes_packed_size(self.name)
        return ret

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.name_hash, b)
        bincode.pack_bytes_into(self.name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirPayload':
        inode = bincode.unpack_u64(u)
        hash_of_name = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return ReadDirPayload(inode, hash_of_name, name)
@dataclass
class ReadDirResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.READ_DIR
    SIZE: ClassVar[int] = 8 + 2 # key + len of results
    next_hash: int # 0 => no more results
    results: List[ReadDirPayload]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_hash, b)
        bincode.pack_u16_into(len(self.results), b)
        for r in self.results:
            r.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirResp':
        continuation_key = bincode.unpack_u64(u)
        count = bincode.unpack_u16(u)
        results = [ReadDirPayload.unpack(u) for _ in range(count)]
        return ReadDirResp(continuation_key, results)

@dataclass
class ConstructFileReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CONSTRUCT_FILE
    type: InodeType # must not be DIRECTORY

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.type, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ConstructFileReq':
        type = InodeType(bincode.unpack_u8(u))
        return ConstructFileReq(type)
@dataclass
class ConstructFileResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.CONSTRUCT_FILE
    id: int
    cookie: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.cookie, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ConstructFileResp':
        inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        return ConstructFileResp(inode, cookie)

@dataclass
class VisitTransientFilesReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_TRANSIENT_FILES
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitTransientFilesReq':
        begin_inode = bincode.unpack_u64(u)
        return VisitTransientFilesReq(begin_inode)
@dataclass
class TransientFile:
    SIZE: ClassVar[int] = 16
    id: int
    deadline_time: int
@dataclass
class VisitTransientFilesResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_TRANSIENT_FILES
    SIZE: ClassVar[int] = 8 + 2 # 2 for the files length
    next_id: int
    files: List[TransientFile]

    def pack_into(self, b: bytearray) -> None:
        assert (ShardResponse.SIZE + VisitTransientFilesResp.SIZE + TransientFile.SIZE * len(self.files)) <= UDP_MTU
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.files), b)
        for file in self.files:
            bincode.pack_u64_into(file.id, b)
            bincode.pack_u64_into(file.deadline_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitTransientFilesResp':
        continuation_key = bincode.unpack_u64(u)
        num_files = bincode.unpack_u16(u)
        eden_vals = [
            TransientFile(bincode.unpack_u64(u), bincode.unpack_u64(u))
            for _ in range(num_files)
        ]
        return VisitTransientFilesResp(continuation_key, eden_vals)

class BlockFlags(enum.IntFlag):
    STALE = 1
    TERMINAL = 2

@dataclass
class NewBlockInfo(bincode.Packable):
    crc32: bytes
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'NewBlockInfo':
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        return NewBlockInfo(crc32, size)

INLINE_STORAGE = 0
ZERO_FILL_STORAGE = 1

@dataclass
class AddSpanInitiateReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.ADD_SPAN_INITIATE
    file_id: int
    cookie: int
    byte_offset: int
    storage_class: int
    parity: int
    crc32: bytes
    size: int
    body: Union[bytes, List[NewBlockInfo]]

    def pack_into(self, b: bytearray) -> None:
        assert (self.storage_class in (INLINE_STORAGE, ZERO_FILL_STORAGE)) == isinstance(self.body, bytes)
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.body, bytes)
            bincode.pack_fixed_into(self.body, self.size, b)
        elif self.storage_class == ZERO_FILL_STORAGE:
            assert self.body == b''
            bincode.pack_fixed_into(b'', 0, b)
        elif isinstance(self.body, list):
            assert num_total_blocks(self.parity) == len(self.body)
            for info in self.body:
                info.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanInitiateReq':
        inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        storage_class = bincode.unpack_u8(u)
        parity_mode = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        payload: Union[bytes, List[NewBlockInfo]]
        if storage_class == INLINE_STORAGE:
            payload = bincode.unpack_fixed(u, size)
        elif storage_class == ZERO_FILL_STORAGE:
            payload = bincode.unpack_fixed(u, 0)
        else:
            payload = [
                NewBlockInfo.unpack(u)
                for _ in range(num_total_blocks(parity_mode))
            ]
        return AddSpanInitiateReq(inode, cookie, byte_offset, storage_class, parity_mode, crc32, size, payload)

@dataclass
class BlockInfo(bincode.Packable):
    ip: bytes
    port: int
    block_id: int
    # certificate := MAC(b'w' + block_id + crc + size)[:8] (for creation)
    certificate: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.certificate, 8, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockInfo':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_u64(u)
        certificate = bincode.unpack_fixed(u, 8)
        return BlockInfo(ip, port, block_id, certificate)


@dataclass
class AddSpanInitiateResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.ADD_SPAN_INITIATE
    # left empty for INLINE/ZERO_FILL spans
    blocks: List[BlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(len(self.blocks), b)
        for block in self.blocks:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanInitiateResp':
        size = bincode.unpack_u8(u)
        span = [BlockInfo.unpack(u) for _ in range(size)]
        return AddSpanInitiateResp(span)

@dataclass
class AddSpanCertifyReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.ADD_SPAN_CERTIFY
    file_id: int
    cookie: int
    byte_offset: int
    proofs: List[bytes]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for proof in self.proofs:
            bincode.pack_fixed_into(proof, 8, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanCertifyReq':
        inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        num_proofs = bincode.unpack_u16(u)
        proofs = [bincode.unpack_fixed(u, 8) for _ in range(num_proofs)]
        return AddSpanCertifyReq(inode, cookie, byte_offset, proofs)

@dataclass
class AddSpanCertifyResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.ADD_SPAN_CERTIFY

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanCertifyResp':
        return AddSpanCertifyResp()



@dataclass
class LinkFileReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LINK_FILE
    file_id: int
    cookie: int
    owner_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        owner_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return LinkFileReq(file_id, cookie, owner_id, name)
@dataclass
class LinkFileResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LINK_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileResp':
        return LinkFileResp()

@dataclass
class FileSpansReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.FILE_SPANS
    file_id: int
    byte_offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_v61_into(self.byte_offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FileSpansReq':
        inode = bincode.unpack_u64(u)
        offset = bincode.unpack_v61(u)
        return FileSpansReq(inode, offset)

@dataclass
class FetchedBlock(bincode.Packable):
    ip: bytes
    port: int
    block_id: int
    crc32: bytes
    size: int
    flags: BlockFlags

    def calc_packed_size(self) -> int:
        return 4 + 2 + 8 + 4 + bincode.v61_packed_size(self.size) + 1

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_u8_into(self.flags, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedBlock':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_u64(u)
        crc = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        flags = BlockFlags(bincode.unpack_u8(u))
        return FetchedBlock(ip, port, block_id, crc, size, flags)

@dataclass
class FetchedSpan(bincode.Packable):
    byte_offset: int
    parity: int
    storage_class: int # TODO maybe not needed?
    crc32: bytes
    size: int # TODO maybe not needed either?
    body: Union[bytes, List[FetchedBlock]]

    def calc_packed_size(self) -> int:
        ret = 1 + 1 + 4 # partity + storage_class
        ret += bincode.v61_packed_size(self.byte_offset)
        ret += bincode.v61_packed_size(self.size)
        if isinstance(self.body, bytes):
            ret += len(self.body)
        else:
            ret += sum(block.calc_packed_size() for block in self.body)
        return ret

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.body, bytes)
            bincode.pack_fixed_into(self.body, self.size, b)
        elif self.storage_class == ZERO_FILL_STORAGE:
            assert self.body == b''
        else:
            assert isinstance(self.body, list)
            assert len(self.body) == num_total_blocks(self.parity)
            for block in self.body:
                block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedSpan':
        file_offset = bincode.unpack_v61(u)
        parity = bincode.unpack_u8(u)
        storage_class = bincode.unpack_u8(u)
        crc = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        payload: Union[bytes, List[FetchedBlock]]
        if storage_class == INLINE_STORAGE:
            payload = bincode.unpack_fixed(u, size)
        elif storage_class == ZERO_FILL_STORAGE:
            payload = b''
        else:
            num_blocks = num_total_blocks(parity)
            assert num_blocks
            payload = [FetchedBlock.unpack(u) for _ in range(num_blocks)]
        return FetchedSpan(file_offset, parity, storage_class, crc, size,
            payload)

@dataclass
class FileSpansResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.FILE_SPANS
    SIZE_UPPER_BOUND: ClassVar[int] = 8 + 2
    next_offset: int # 0 => no more spans (0 more efficient than UINT64_MAX)
    spans: List[FetchedSpan]

    def pack_into(self, b: bytearray) -> None:
        assert len(self.spans) < 2**16
        bincode.pack_v61_into(self.next_offset, b)
        bincode.pack_u16_into(len(self.spans), b)
        for span in self.spans:
            span.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FileSpansResp':
        next_offset = bincode.unpack_v61(u)
        num_spans = bincode.unpack_u16(u)
        spans = [FetchedSpan.unpack(u) for _ in range(num_spans)]
        return FileSpansResp(next_offset, spans)

@dataclass
class SameDirectoryRenameReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SAME_DIRECTORY_RENAME
    target_id: int
    dir_id: int
    old_name: bytes
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_bytes_into(self.new_name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameReq':
        target_id = bincode.unpack_u64(u)
        dir_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_name = bincode.unpack_bytes(u)
        return SameDirectoryRenameReq(dir_id=dir_id, target_id=target_id, old_name=old_name, new_name=new_name)

@dataclass
class SameDirectoryRenameResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SAME_DIRECTORY_RENAME

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameResp':
        return SameDirectoryRenameResp()

@dataclass
class SoftUnlinkFileReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SOFT_UNLINK_FILE
    owner_id: int
    file_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_bytes_into(self.name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkFileReq':
        parent_inode = bincode.unpack_u64(u)
        file_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return SoftUnlinkFileReq(parent_inode, file_id, name)

@dataclass
class SoftUnlinkFileResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SOFT_UNLINK_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkFileResp':
        return SoftUnlinkFileResp()

@dataclass
class SetDirectoryOwnerReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SET_DIRECTORY_OWNER
    dir_id: int
    owner_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.owner_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryOwnerReq':
        dir_id = bincode.unpack_u64(u)
        owner_id = bincode.unpack_u64(u)
        return SetDirectoryOwnerReq(dir_id, owner_id)
@dataclass
class SetDirectoryOwnerResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.SET_DIRECTORY_OWNER

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryOwnerResp':
        return SetDirectoryOwnerResp()

@dataclass
class VisitDirectoriesReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_DIRECTORIES
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitDirectoriesReq':
        begin_inode = bincode.unpack_u64(u)
        return VisitDirectoriesReq(begin_inode)

@dataclass
class VisitDirectoriesResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_DIRECTORIES
    SIZE: ClassVar[int] = 8 + 2 # 2 for the ids length
    next_id: int
    ids: List[int]

    def pack_into(self, b: bytearray) -> None:
        assert (ShardResponse.SIZE + VisitDirectoriesResp.SIZE + 8 * len(self.ids)) <= UDP_MTU
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.ids), b)
        for inode in self.ids:
            bincode.pack_u64_into(inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitDirectoriesResp':
        continuation_key = bincode.unpack_u64(u)
        num_inodes = bincode.unpack_u16(u)
        inodes = [bincode.unpack_u64(u) for _ in range(num_inodes)]
        return VisitDirectoriesResp(continuation_key, inodes)

@dataclass
class VisitFilesReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_FILES
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitFilesReq':
        begin_inode = bincode.unpack_u64(u)
        return VisitFilesReq(begin_inode)

@dataclass
class VisitFilesResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.VISIT_FILES
    SIZE: ClassVar[int] = 8 + 2 # 2 for the ids length
    next_id: int
    ids: List[int]

    def pack_into(self, b: bytearray) -> None:
        assert (ShardResponse.SIZE + VisitFilesResp.SIZE + 8 * len(self.ids)) <= UDP_MTU
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.ids), b)
        for inode in self.ids:
            bincode.pack_u64_into(inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitFilesResp':
        continuation_key = bincode.unpack_u64(u)
        num_inodes = bincode.unpack_u16(u)
        inodes = [bincode.unpack_u64(u) for _ in range(num_inodes)]
        return VisitFilesResp(continuation_key, inodes)


@dataclass
class LockCurrentEdgeReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LOCK_CURRENT_EDGE
    dir_id: int
    name: bytes
    target_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        return LockCurrentEdgeReq(dir_id, name, target_id)

@dataclass
class LockCurrentEdgeResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LOCK_CURRENT_EDGE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LockCurrentEdgeResp':
        return LockCurrentEdgeResp()

@dataclass
class UnlockCurrentEdgeReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.UNLOCK_CURRENT_EDGE
    dir_id: int
    name: bytes
    target_id: int
    was_moved: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u8_into(self.was_moved, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        soft_unlink = bool(bincode.unpack_u8(u))
        return UnlockCurrentEdgeReq(dir_id, name, target_id, soft_unlink)
@dataclass
class UnlockCurrentEdgeResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.UNLOCK_CURRENT_EDGE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlockCurrentEdgeResp':
        return UnlockCurrentEdgeResp()

@dataclass
class LookupReq(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LOOKUP
    dir_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LookupReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return LookupReq(dir_id, name)

@dataclass
class LookupResp(bincode.Packable):
    kind: ClassVar[ShardRequestKind] = ShardRequestKind.LOOKUP
    target_id: int
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LookupResp':
        inode_with_ownership = bincode.unpack_u64(u)
        creation_ts = bincode.unpack_u64(u)
        return LookupResp(inode_with_ownership, creation_ts)




ShardRequestBody = Union[
    CreateDirectoryINodeReq, StatReq, CreateLockedCurrentEdgeReq, ReadDirReq, ConstructFileReq, VisitTransientFilesReq, AddSpanInitiateReq,
    LinkFileReq, AddSpanCertifyReq, FileSpansReq, SameDirectoryRenameReq, SoftUnlinkFileReq, SetDirectoryOwnerReq,
    VisitDirectoriesReq, LockCurrentEdgeReq, UnlockCurrentEdgeReq, LookupReq,
]
ShardResponseBody = Union[
    EggsError,
    CreateDirectoryINodeResp, StatResp, CreateLockedCurrentEdgeResp, ReadDirResp, ConstructFileResp,
    VisitTransientFilesResp, AddSpanInitiateResp, LinkFileResp, AddSpanCertifyResp, FileSpansResp, SameDirectoryRenameResp,
    SoftUnlinkFileResp, SetDirectoryOwnerResp, VisitDirectoriesResp, LockCurrentEdgeResp, UnlockCurrentEdgeResp, LookupResp,
]

SHARD_REQUESTS: Dict[ShardRequestKind, Tuple[Type[ShardRequestBody], Type[ShardResponseBody]]] = {
    ShardRequestKind.CREATE_DIRECTORY_INODE: (CreateDirectoryINodeReq, CreateDirectoryINodeResp),
    ShardRequestKind.STAT: (StatReq, StatResp),
    ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE: (CreateLockedCurrentEdgeReq, CreateLockedCurrentEdgeResp),
    ShardRequestKind.READ_DIR: (ReadDirReq, ReadDirResp),
    ShardRequestKind.CONSTRUCT_FILE: (ConstructFileReq, ConstructFileResp),
    ShardRequestKind.VISIT_TRANSIENT_FILES: (VisitTransientFilesReq, VisitTransientFilesResp),
    ShardRequestKind.ADD_SPAN_INITIATE: (AddSpanInitiateReq, AddSpanInitiateResp),
    ShardRequestKind.LINK_FILE: (LinkFileReq, LinkFileResp),
    ShardRequestKind.ADD_SPAN_CERTIFY: (AddSpanCertifyReq, AddSpanCertifyResp),
    ShardRequestKind.FILE_SPANS: (FileSpansReq, FileSpansResp),
    ShardRequestKind.SAME_DIRECTORY_RENAME: (SameDirectoryRenameReq, SameDirectoryRenameResp),
    ShardRequestKind.SOFT_UNLINK_FILE: (SoftUnlinkFileReq, SoftUnlinkFileResp),
    ShardRequestKind.SET_DIRECTORY_OWNER: (SetDirectoryOwnerReq, SetDirectoryOwnerResp),
    ShardRequestKind.VISIT_DIRECTORIES: (VisitDirectoriesReq, VisitDirectoriesResp),
    ShardRequestKind.VISIT_FILES: (VisitFilesReq, VisitFilesResp),
    ShardRequestKind.LOCK_CURRENT_EDGE: (LockCurrentEdgeReq, LockCurrentEdgeResp),
    ShardRequestKind.UNLOCK_CURRENT_EDGE: (UnlockCurrentEdgeReq, UnlockCurrentEdgeResp),
    ShardRequestKind.LOOKUP: (LookupReq, LookupResp),
}

@dataclass
class ShardRequest:
    request_id: int
    body: ShardRequestBody

    def pack(self, cdc_key: Optional[crypto.ExpandedKey] = None) -> bytes:
        b = bytearray()
        bincode.pack_fixed_into(SHARD_PROTOCOL_VERSION, len(SHARD_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)
        if self.body.kind.is_privileged():
            assert cdc_key
            bincode.pack_fixed_into(crypto.compute_mac(bytes(b), cdc_key), 8, b)
        return bytes(b)

@dataclass
class UnpackedShardRequest:
    # this one is copied here so that if we cannot decode the rest
    # we can still reply.
    request_id: int
    # If something went wrong while decoding past the req id, you'll get an
    # error instead.
    request: Union[EggsError, ShardRequest]

    @staticmethod
    def unpack(bs: bytes, cdc_key: Optional[crypto.ExpandedKey] = None) -> 'UnpackedShardRequest':
        u = bincode.UnpackWrapper(bs)
        ver = bincode.unpack_fixed(u, len(SHARD_PROTOCOL_VERSION))
        assert ver == SHARD_PROTOCOL_VERSION, f'Expected shard protocol version {repr(SHARD_PROTOCOL_VERSION)}, but got {repr(ver)} instead.'
        request_id = bincode.unpack_u64(u)
        # We've made it so far, now we can at least
        # return something
        req = UnpackedShardRequest(
            request_id=request_id,
            request=None,
        )
        try:
            kind = ShardRequestKind(bincode.unpack_u8(u))
            body_type = SHARD_REQUESTS[kind][0]
            body = body_type.unpack(u)
        except Exception:
            # TODO it would be good to distinguish between actual
            # decode errors and internal exceptions here.
            return replace(req, request=EggsError(ErrCode.MALFORMED_REQUEST))
        if kind.is_privileged():
            assert cdc_key
            req_bytes = u.data[:u.idx]
            mac = bincode.unpack_fixed(u, 8)
            if crypto.compute_mac(req_bytes, cdc_key) != mac:
                return replace(req, request=EggsError(ErrCode.NOT_AUTHORISED))
        if u.idx != len(bs):
            return replace(req, request=EggsError(ErrCode.MALFORMED_REQUEST))
        return replace(req, request=ShardRequest(request_id=request_id, body=body))

@dataclass
class ShardResponse(bincode.Packable):
    SIZE: ClassVar[int] = len(SHARD_PROTOCOL_VERSION) + 8 + 1
    request_id: int
    body: ShardResponseBody

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(SHARD_PROTOCOL_VERSION, len(SHARD_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ShardResponse':
        ver = bincode.unpack_fixed(u, len(SHARD_PROTOCOL_VERSION))
        assert ver == SHARD_PROTOCOL_VERSION
        request_id = bincode.unpack_u64(u)
        resp_kind = ShardRequestKind(bincode.unpack_u8(u))
        if resp_kind == ShardRequestKind.ERROR:
            body = EggsError.unpack(u)
        else:
            body = SHARD_REQUESTS[resp_kind][1].unpack(u)
        return ShardResponse(request_id, body)
