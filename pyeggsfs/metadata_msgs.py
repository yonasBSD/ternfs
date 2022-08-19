#!/usr/bin/env python3

from dataclasses import dataclass, field
import enum
import itertools
from typing import (ClassVar, Dict, List, NamedTuple, Optional, Tuple, Type,
    Union)

import bincode
import metadata_utils
from metadata_utils import InodeType


# one-hot encoding to allow "acceptable-types" bitset
class InodeFlags(enum.IntFlag):
    DIRECTORY = 1
    FILE = 2
    SYMLINK = 4


def inode_type_to_flag(type: InodeType) -> InodeFlags:
    return InodeFlags(1 << type)


# u8 where MSb states whether this request is privilaged
class RequestKind(enum.IntEnum):
    ERROR = 0

    # unprivilaged
    RESOLVE = 0x01
    STAT = 0x02
    LS_DIR = 0x03
    CREATE_EDEN_FILE = 0x04
    ADD_EDEN_SPAN = 0x05
    CERTIFY_EDEN_SPAN = 0x06
    LINK_EDEN_FILE = 0x07
    REPAIR_SPANS = 0x08
    EXPUNGE_SPAN = 0x09
    CERTIFY_EXPUNGE = 0x0A
    EXPUNGE_FILE = 0x0B
    DELETE_FILE = 0x0C
    FETCH_SPANS = 0x0D
    SAME_DIR_RENAME = 0x0E
    PURGE_DIRENT = 0x0F # won't work for remote owning dirents
    VISIT_INODES = 0x10
    VISIT_EDEN = 0x11
    REVERSE_BLOCK_QUERY = 0x12
    REPAIR_BLOCK = 0x13

    # privilaged (needs MAC)
    SET_PARENT = 0x81
    CREATE_UNLINKED_DIR = 0x82
    # a release must follow an inject or acquire
    INJECT_DIRENT = 0x83  # insert a new living dirent with owning == false
    ACQUIRE_DIRENT = 0x84 # if living dirent exists, set owning := false
    RELEASE_DIRENT = 0x85 # if living dirent exists, set owning := true
    PURGE_REMOTE_OWNING_DIRENT = 0x86
    MOVE_FILE_TO_EDEN = 0x87

    def is_privilaged(self) -> bool:
        return bool(self.value & 0x80)


class ResolveMode(enum.IntEnum):
    ALIVE = 0
    # returns dead entry with largest creation_ts <= given ts
    DEAD_LE = 1
    DEAD_GE = 2


@dataclass
class ResolveReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RESOLVE
    mode: ResolveMode
    parent_id: int
    creation_ts: int # ignored for ALIVE requests
    subname: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.mode, b)
        bincode.pack_u64_into(self.parent_id, b)
        bincode.pack_u64_into(self.creation_ts, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolveReq':
        mode = ResolveMode(bincode.unpack_u8(u))
        parent_id = bincode.unpack_u64(u)
        creation_ts = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        return ResolveReq(mode, parent_id, creation_ts, subname)


@dataclass
class StatReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.STAT
    inode_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatReq':
        return StatReq(bincode.unpack_u64(u))


class LsFlags(enum.IntFlag):
    NEWER_THAN_AS_OF = 1 # should we return before or after as_of time?
    INCLUDE_NOTHING_ENTRIES = 2
    USE_DEAD_MAP = 4


@dataclass
class LsDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.LS_DIR
    inode: int
    as_of: int
    continuation_key: int
    flags: LsFlags

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.as_of, b)
        bincode.pack_u64_into(self.continuation_key, b)
        bincode.pack_u8_into(self.flags, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsDirReq':
        inode = bincode.unpack_u64(u)
        as_of = bincode.unpack_u64(u)
        continuation_key = bincode.unpack_u64(u)
        flags = LsFlags(bincode.unpack_u8(u))
        return LsDirReq(inode, as_of, continuation_key, flags)


@dataclass
class CreateEdenFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_EDEN_FILE
    type: InodeType # must not be DIRECTORY

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.type, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateEdenFileReq':
        type = InodeType(bincode.unpack_u8(u))
        return CreateEdenFileReq(type)


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
class AddEdenSpanReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ADD_EDEN_SPAN
    storage_class: int
    parity_mode: int
    crc32: bytes
    size: int
    byte_offset: int
    inode: int
    cookie: int
    payload: Union[bytes, List[NewBlockInfo]]

    def pack_into(self, b: bytearray) -> None:
        assert (self.parity_mode == 0) == isinstance(self.payload, bytes)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity_mode, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.cookie, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.payload, bytes)
            bincode.pack_fixed_into(self.payload, self.size, b)
        elif isinstance(self.payload, list):
            assert metadata_utils.total_blocks(self.parity_mode) == len(self.payload)
            for info in self.payload:
                info.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddEdenSpanReq':
        storage_class = bincode.unpack_u8(u)
        parity_mode = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        byte_offset = bincode.unpack_v61(u)
        inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        payload: Union[bytes, List[NewBlockInfo]]
        if storage_class == INLINE_STORAGE:
            payload = bincode.unpack_fixed(u, size)
        elif storage_class == ZERO_FILL_STORAGE:
            payload = b''
        else:
            payload = [
                NewBlockInfo.unpack(u)
                for _ in range(metadata_utils.total_blocks(parity_mode))
            ]
        return AddEdenSpanReq(storage_class, parity_mode, crc32, size,
            byte_offset, inode, cookie, payload)


@dataclass
class CertifyEdenSpanReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EDEN_SPAN
    inode: int
    byte_offset: int
    cookie: int
    proofs: List[bytes]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for proof in self.proofs:
            bincode.pack_fixed_into(proof, 8, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyEdenSpanReq':
        inode = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        cookie = bincode.unpack_u64(u)
        num_proofs = bincode.unpack_u16(u)
        proofs = [bincode.unpack_fixed(u, 8) for _ in range(num_proofs)]
        return CertifyEdenSpanReq(inode, byte_offset, cookie, proofs)


@dataclass
class LinkEdenFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.LINK_EDEN_FILE
    eden_inode: int
    cookie: int
    parent_inode: int
    new_name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.eden_inode, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.new_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkEdenFileReq':
        eden_inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        parent_inode = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u).decode()
        return LinkEdenFileReq(eden_inode, cookie, parent_inode, new_name)


@dataclass
class RepairSpansReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REPAIR_SPANS
    source_inode: int
    source_cookie: int
    sink_inode: int
    sink_cookie: int
    target_inode: int
    target_cookie: int # 0 implies target is not in eden
    byte_offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.source_inode, b)
        bincode.pack_u64_into(self.source_cookie, b)
        bincode.pack_u64_into(self.sink_inode, b)
        bincode.pack_u64_into(self.sink_cookie, b)
        bincode.pack_u64_into(self.target_inode, b)
        bincode.pack_u64_into(self.target_cookie, b)
        bincode.pack_u64_into(self.byte_offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RepairSpansReq':
        source_inode = bincode.unpack_u64(u)
        source_cookie = bincode.unpack_u64(u)
        sink_inode = bincode.unpack_u64(u)
        sink_cookie = bincode.unpack_u64(u)
        target_inode = bincode.unpack_u64(u)
        target_cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_u64(u)
        return RepairSpansReq(source_inode, source_cookie, sink_inode,
            sink_cookie, target_inode, target_cookie, byte_offset)


@dataclass
class ExpungeEdenSpanReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_SPAN
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenSpanReq':
        inode = bincode.unpack_u64(u)
        return ExpungeEdenSpanReq(inode)


@dataclass
class CertifyExpungeReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EXPUNGE
    inode: int
    offset: int
    proofs: List[Tuple[int, bytes]]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_v61_into(self.offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for block_id, proof in self.proofs:
            bincode.pack_u64_into(block_id, b)
            bincode.pack_fixed_into(proof, 8, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyExpungeReq':
        inode = bincode.unpack_u64(u)
        offset = bincode.unpack_v61(u)
        num_proofs = bincode.unpack_u16(u)
        proofs = [
            (bincode.unpack_u64(u), bincode.unpack_fixed(u, 8))
            for _ in range(num_proofs)
        ]
        return CertifyExpungeReq(inode, offset, proofs)


@dataclass
class ExpungeEdenFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_FILE
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenFileReq':
        inode = bincode.unpack_u64(u)
        return ExpungeEdenFileReq(inode)


@dataclass
class DeleteFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.DELETE_FILE
    parent_inode: int
    name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'DeleteFileReq':
        parent_inode = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        return DeleteFileReq(parent_inode, name)


@dataclass
class FetchSpansReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.FETCH_SPANS
    inode: int
    offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_v61_into(self.offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchSpansReq':
        inode = bincode.unpack_u64(u)
        offset = bincode.unpack_v61(u)
        return FetchSpansReq(inode, offset)


@dataclass
class SameDirRenameReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SAME_DIR_RENAME
    parent_inode: int
    old_name: str
    new_name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.old_name.encode(), b)
        bincode.pack_bytes_into(self.new_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirRenameReq':
        parent_inode = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u).decode()
        new_name = bincode.unpack_bytes(u).decode()
        return SameDirRenameReq(parent_inode, old_name, new_name)


@dataclass
class PurgeDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.PURGE_DIRENT
    parent_inode: int
    name: str
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)
        bincode.pack_u64_into(self.creation_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'PurgeDirentReq':
        parent_inode = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        creation_time = bincode.unpack_u64(u)
        return PurgeDirentReq(parent_inode, name, creation_time)


@dataclass
class VisitInodesReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.VISIT_INODES
    begin_inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitInodesReq':
        begin_inode = bincode.unpack_u64(u)
        return VisitInodesReq(begin_inode)


@dataclass
class VisitEdenReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.VISIT_EDEN
    begin_inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitEdenReq':
        begin_inode = bincode.unpack_u64(u)
        return VisitEdenReq(begin_inode)


@dataclass
class ReverseBlockQueryReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REVERSE_BLOCK_QUERY
    bserver_secret: bytes
    from_block_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.bserver_secret, 16, b)
        bincode.pack_u64_into(self.from_block_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReverseBlockQueryReq':
        bserver_secret = bincode.unpack_fixed(u, 16)
        from_block_id = bincode.unpack_u64(u)
        return ReverseBlockQueryReq(bserver_secret, from_block_id)


@dataclass
class RepairBlockReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REPAIR_BLOCK
    source_inode: int
    source_cookie: int
    target_inode: int
    target_cookie: int # 0 implies target is not in eden
    byte_offset: int
    faulty_block_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.source_inode, b)
        bincode.pack_u64_into(self.source_cookie, b)
        bincode.pack_u64_into(self.target_inode, b)
        bincode.pack_u64_into(self.target_cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u64_into(self.faulty_block_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RepairBlockReq':
        source_inode = bincode.unpack_u64(u)
        source_cookie = bincode.unpack_u64(u)
        target_inode = bincode.unpack_u64(u)
        target_cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        faulty_block_id = bincode.unpack_u64(u)
        return RepairBlockReq(source_inode, source_cookie, target_inode,
            target_cookie, byte_offset, faulty_block_id)


@dataclass
class SetParentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SET_PARENT
    inode: int
    new_parent: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.new_parent, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetParentReq':
        inode = bincode.unpack_u64(u)
        new_parent = bincode.unpack_u64(u)
        return SetParentReq(inode, new_parent)


@dataclass
class CreateDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_UNLINKED_DIR
    token_inode: int
    new_parent: int
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.token_inode, b)
        bincode.pack_u64_into(self.new_parent, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirReq':
        token_inode = bincode.unpack_u64(u)
        new_parent = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return CreateDirReq(token_inode, new_parent, opaque)


@dataclass
class InjectDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.INJECT_DIRENT
    parent_inode: int
    subname: str
    child_inode: int
    creation_ts: int
    dry: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_u64_into(self.child_inode, b)
        bincode.pack_u64_into(self.creation_ts, b)
        bincode.pack_u8_into(self.dry, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'InjectDirentReq':
        parent_inode = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        child_inode = bincode.unpack_u64(u)
        creation_ts = bincode.unpack_u64(u)
        dry = bool(bincode.unpack_u8(u))
        return InjectDirentReq(parent_inode, subname, child_inode, creation_ts,
            dry)


@dataclass
class AcquireDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ACQUIRE_DIRENT
    parent_inode: int
    subname: str
    acceptable_types: InodeFlags

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_u8_into(self.acceptable_types, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AcquireDirentReq':
        parent_inode = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        acceptable_types = InodeFlags(bincode.unpack_u8(u))
        return AcquireDirentReq(parent_inode, subname, acceptable_types)


@dataclass
class ReleaseDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RELEASE_DIRENT
    parent_inode: int
    subname: str
    kill: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_u8_into(self.kill, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReleaseDirentReq':
        parent_inode = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        kill = bool(bincode.unpack_u8(u))
        return ReleaseDirentReq(parent_inode, subname, kill)


@dataclass
class PurgeRemoteOwningDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.PURGE_REMOTE_OWNING_DIRENT
    parent_inode: int
    name: str
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)
        bincode.pack_u64_into(self.creation_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'PurgeRemoteOwningDirentReq':
        parent_inode = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        creation_time = bincode.unpack_u64(u)
        return PurgeRemoteOwningDirentReq(parent_inode, name, creation_time)


@dataclass
class MoveFileToEdenReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MOVE_FILE_TO_EDEN
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MoveFileToEdenReq':
        inode = bincode.unpack_u64(u)
        return MoveFileToEdenReq(inode)


ReqBodyTy = Union[ResolveReq, StatReq, LsDirReq, CreateEdenFileReq,
    AddEdenSpanReq, CertifyEdenSpanReq, LinkEdenFileReq, RepairSpansReq,
    ExpungeEdenSpanReq, CertifyExpungeReq, ExpungeEdenFileReq, DeleteFileReq,
    FetchSpansReq, SameDirRenameReq, PurgeDirentReq, VisitInodesReq,
    VisitEdenReq, ReverseBlockQueryReq, RepairBlockReq, SetParentReq,
    CreateDirReq, InjectDirentReq, AcquireDirentReq, ReleaseDirentReq,
    PurgeRemoteOwningDirentReq, MoveFileToEdenReq]


@dataclass
class MetadataRequest(bincode.Packable):
    ver: int
    request_id: int
    body: ReqBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.body.kind, b)
        bincode.pack_u8_into(self.ver, b)
        bincode.pack_u64_into(self.request_id, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataRequest':
        kind = RequestKind(bincode.unpack_u8(u))
        ver = bincode.unpack_u8(u)
        request_id = bincode.unpack_u64(u)
        body_type = REQUESTS[kind][0]
        body = body_type.unpack(u)
        return MetadataRequest(ver, request_id, body)


class MetadataErrorKind(enum.IntEnum):
    TOO_SOON = 0
    ALREADY_EXISTS = 1
    NAME_TOO_LONG = 2
    ROCKS_DB_ERROR = 3
    NETWORK_ERROR = 4
    BINCODE_ERROR = 5
    LOGIC_ERROR = 6
    UNSUPPORTED_VERSION = 7
    NOT_FOUND = 8
    NOT_AUTHORISED = 9
    BAD_REQUEST = 10
    BAD_INODE_TYPE = 11
    TIMED_OUT = 12 # not sent, but used by client as sentinel
    EDEN_DEADLINE = 13
    SHUCKLE_ERROR = 14
    BAD_REQUEST_IDEMPOTENT_HINT = 15 # previous invocation may have succeeded


@dataclass
class MetadataError(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ERROR
    error_kind: MetadataErrorKind
    text: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.error_kind, b)
        bincode.pack_bytes_into(self.text.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataError':
        error_kind = MetadataErrorKind(bincode.unpack_u8(u))
        text = bincode.unpack_bytes(u).decode()
        return MetadataError(error_kind, text)


@dataclass
class ResolveResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RESOLVE
    inode_with_ownership: int
    creation_ts: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode_with_ownership, b)
        bincode.pack_u64_into(self.creation_ts, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolveResp':
        inode_with_ownership = bincode.unpack_u64(u)
        creation_ts = bincode.unpack_u64(u)
        return ResolveResp(inode_with_ownership, creation_ts)


@dataclass
class StatFilePayload(bincode.Packable):
    mtime: int
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatFilePayload':
        mtime = bincode.unpack_u64(u)
        size = bincode.unpack_u64(u)
        return StatFilePayload(mtime, size)


@dataclass
class StatDirPayload(bincode.Packable):
    mtime: int
    parent_inode: int # NULL_INODE => no parent
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatDirPayload':
        mtime = bincode.unpack_u64(u)
        parent_inode = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return StatDirPayload(mtime, parent_inode, opaque)


StatPayloadTy = Union[StatFilePayload, StatDirPayload]


@dataclass
class StatResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.STAT
    mtime: int
    size_or_parent: int # files => size, dirs => parent
    opaque: bytes # always empty for files

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size_or_parent, b)
        bincode.pack_fixed_into(self.opaque, len(self.opaque), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatResp':
        mtime = bincode.unpack_u64(u)
        size_or_parent = bincode.unpack_u64(u)
        opaque_len = u.bytes_remaining()
        opaque = bincode.unpack_fixed(u, opaque_len)
        return StatResp(mtime, size_or_parent, opaque)


@dataclass
class LsPayload(bincode.Packable):
    inode: int # NULL_INODE => a "nothing" entry
    hash_of_name: int
    name: str

    def calc_packed_size(self) -> int:
        ret = 8 + 8 # inode + hash_of_name
        ret += bincode.bytes_packed_size(self.name.encode())
        return ret

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.hash_of_name, b)
        bincode.pack_bytes_into(self.name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsPayload':
        inode = bincode.unpack_u64(u)
        hash_of_name = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        return LsPayload(inode, hash_of_name, name)


@dataclass
class LsDirResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.LS_DIR
    SIZE: ClassVar[int] = 8 + 2
    continuation_key: int # UINT64_MAX => no more results
    results: List[LsPayload]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.continuation_key, b)
        bincode.pack_u16_into(len(self.results), b)
        for r in self.results:
            r.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsDirResp':
        continuation_key = bincode.unpack_u64(u)
        count = bincode.unpack_u16(u)
        results = [LsPayload.unpack(u) for _ in range(count)]
        return LsDirResp(continuation_key, results)


@dataclass
class CreateEdenFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_EDEN_FILE
    inode: int
    cookie: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.cookie, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateEdenFileResp':
        inode = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        return CreateEdenFileResp(inode, cookie)


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
class AddEdenSpanResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ADD_EDEN_SPAN
    span: List[BlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(len(self.span), b)
        for block in self.span:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddEdenSpanResp':
        size = bincode.unpack_u8(u)
        span = [BlockInfo.unpack(u) for _ in range(size)]
        return AddEdenSpanResp(span)


@dataclass
class CertifyEdenSpanResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EDEN_SPAN

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyEdenSpanResp':
        return CertifyEdenSpanResp()


@dataclass
class SetParentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SET_PARENT

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetParentResp':
        return SetParentResp()


@dataclass
class CreateDirResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_UNLINKED_DIR
    inode: int
    mtime: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)
        bincode.pack_u64_into(self.mtime, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirResp':
        inode = bincode.unpack_u64(u)
        creation_ts = bincode.unpack_u64(u)
        return CreateDirResp(inode, creation_ts)


@dataclass
class InjectDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.INJECT_DIRENT
    dry: bool # echoed back (for piece of mind)

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.dry, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'InjectDirentResp':
        dry = bool(bincode.unpack_u8(u))
        return InjectDirentResp(dry)


@dataclass
class AcquireDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ACQUIRE_DIRENT
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AcquireDirentResp':
        inode = bincode.unpack_u64(u)
        return AcquireDirentResp(inode)


@dataclass
class ReleaseDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RELEASE_DIRENT
    did_exist: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.did_exist, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReleaseDirentResp':
        did_exist = bool(bincode.unpack_u8(u))
        return ReleaseDirentResp(did_exist)


@dataclass
class LinkEdenFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.LINK_EDEN_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkEdenFileResp':
        return LinkEdenFileResp()


@dataclass
class RepairSpansResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REPAIR_SPANS

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RepairSpansResp':
        return RepairSpansResp()


@dataclass
class ExpungeEdenSpanResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_SPAN
    offset: int
    span: List[BlockInfo] # empty => blockless, no certification required

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.offset, b)
        bincode.pack_u8_into(len(self.span), b)
        for block in self.span:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenSpanResp':
        offset = bincode.unpack_v61(u)
        size = bincode.unpack_u8(u)
        span = [BlockInfo.unpack(u) for _ in range(size)]
        return ExpungeEdenSpanResp(offset, span)


@dataclass
class CertifyExpungeResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EXPUNGE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyExpungeResp':
        return CertifyExpungeResp()


@dataclass
class ExpungeEdenFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenFileResp':
        return ExpungeEdenFileResp()


@dataclass
class DeleteFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.DELETE_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'DeleteFileResp':
        return DeleteFileResp()


class BlockFlags(enum.IntFlag):
    STALE = 1
    TERMINAL = 2


@dataclass
class FetchedBlock(bincode.Packable):
    ip: bytes
    port: int
    block_id: int
    crc: bytes
    size: int
    flags: BlockFlags

    def calc_packed_size(self) -> int:
        return 4 + 2 + 8 + 4 + bincode.v61_packed_size(self.size) + 1

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.crc, 4, b)
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
    file_offset: int
    parity: int
    storage_class: int # maybe not needed?
    crc: bytes
    size: int # ditto?
    payload: Union[bytes, List[FetchedBlock]]

    def calc_packed_size(self) -> int:
        ret = 1 + 1 + 4 # partity + storage_class
        ret += bincode.v61_packed_size(self.file_offset)
        ret += bincode.v61_packed_size(self.size)
        if isinstance(self.payload, bytes):
            ret += len(self.payload)
        else:
            ret += sum(block.calc_packed_size() for block in self.payload)
        return ret

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.file_offset, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.crc, 4, b)
        bincode.pack_v61_into(self.size, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.payload, bytes)
            bincode.pack_fixed_into(self.payload, self.size, b)
        elif self.storage_class == ZERO_FILL_STORAGE:
            assert self.payload == b''
        else:
            assert isinstance(self.payload, list)
            assert len(self.payload) == metadata_utils.total_blocks(self.parity)
            for block in self.payload:
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
            num_blocks = metadata_utils.total_blocks(parity)
            assert num_blocks
            payload = [FetchedBlock.unpack(u) for _ in range(num_blocks)]
        return FetchedSpan(file_offset, parity, storage_class, crc, size,
            payload)


@dataclass
class FetchSpansResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.FETCH_SPANS
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
    def unpack(u: bincode.UnpackWrapper) -> 'FetchSpansResp':
        next_offset = bincode.unpack_v61(u)
        num_spans = bincode.unpack_u16(u)
        spans = [FetchedSpan.unpack(u) for _ in range(num_spans)]
        return FetchSpansResp(next_offset, spans)


@dataclass
class SameDirRenameResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SAME_DIR_RENAME

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirRenameResp':
        return SameDirRenameResp()


@dataclass
class PurgeDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.PURGE_DIRENT

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'PurgeDirentResp':
        return PurgeDirentResp()


@dataclass
class VisitInodesResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.VISIT_INODES
    SIZE: ClassVar[int] = 8
    continuation_key: int
    inodes: List[int]

    def pack_into(self, b: bytearray) -> None:
        assert (MetadataResponse.SIZE + VisitInodesResp.SIZE
            + 8 * len(self.inodes)) <= metadata_utils.UDP_MTU
        bincode.pack_u64_into(self.continuation_key, b)
        for inode in self.inodes:
            bincode.pack_u64_into(inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitInodesResp':
        continuation_key = bincode.unpack_u64(u)
        num_inodes = u.bytes_remaining() // 8
        inodes = [bincode.unpack_u64(u) for _ in range(num_inodes)]
        return VisitInodesResp(continuation_key, inodes)


class EdenVal(NamedTuple):
    inode: int
    deadline_time: int


@dataclass
class VisitEdenResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.VISIT_EDEN
    SIZE: ClassVar[int] = 8
    continuation_key: int
    eden_vals: List[EdenVal]

    def pack_into(self, b: bytearray) -> None:
        assert (MetadataResponse.SIZE + VisitEdenResp.SIZE
            + 16 * len(self.eden_vals)) <= metadata_utils.UDP_MTU
        bincode.pack_u64_into(self.continuation_key, b)
        for eden_val in self.eden_vals:
            bincode.pack_u64_into(eden_val.inode, b)
            bincode.pack_u64_into(eden_val.deadline_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitEdenResp':
        continuation_key = bincode.unpack_u64(u)
        num_eden_vals = u.bytes_remaining() // 16
        eden_vals = [
            EdenVal(bincode.unpack_u64(u), bincode.unpack_u64(u))
            for _ in range(num_eden_vals)
        ]
        return VisitEdenResp(continuation_key, eden_vals)


@dataclass
class QueriedBlock(bincode.Packable):
    block_id: int
    file_inode: int
    byte_offset: int

    def calc_packed_size(self) -> int:
        return 16 + bincode.v61_packed_size(self.byte_offset)

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_u64_into(self.file_inode, b)
        bincode.pack_v61_into(self.byte_offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'QueriedBlock':
        block_id = bincode.unpack_u64(u)
        file_inode = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        return QueriedBlock(block_id, file_inode, byte_offset)


@dataclass
class ReverseBlockQueryResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REVERSE_BLOCK_QUERY
    SIZE: ClassVar[int] = 8 + 2
    continuation_key: int
    blocks: List[QueriedBlock]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.continuation_key, b)
        bincode.pack_u16_into(len(self.blocks), b)
        for block in self.blocks:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReverseBlockQueryResp':
        continuation_key = bincode.unpack_u64(u)
        num_blocks = bincode.unpack_u16(u)
        blocks = [
            QueriedBlock.unpack(u)
            for _ in range(num_blocks)
        ]
        return ReverseBlockQueryResp(continuation_key, blocks)


@dataclass
class RepairBlockResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.REPAIR_BLOCK

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RepairBlockResp':
        return RepairBlockResp()


@dataclass
class PurgeRemoteOwningDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.PURGE_REMOTE_OWNING_DIRENT

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'PurgeRemoteOwningDirentResp':
        return PurgeRemoteOwningDirentResp()


@dataclass
class MoveFileToEdenResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MOVE_FILE_TO_EDEN

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MoveFileToEdenResp':
        return MoveFileToEdenResp()


RespBodyTy = Union[MetadataError, ResolveResp, StatResp, LsDirResp,
    CreateEdenFileResp, AddEdenSpanResp, CertifyEdenSpanResp, LinkEdenFileResp,
    RepairSpansResp, ExpungeEdenSpanResp, CertifyExpungeResp,
    ExpungeEdenFileResp, DeleteFileResp, DeleteFileReq, FetchSpansResp,
    SameDirRenameResp, PurgeDirentResp, VisitInodesResp, VisitEdenResp,
    ReverseBlockQueryResp, RepairBlockResp, SetParentResp, CreateDirResp,
    InjectDirentResp, AcquireDirentResp, ReleaseDirentResp,
    PurgeRemoteOwningDirentResp, MoveFileToEdenResp]


REQUESTS: Dict[RequestKind, Tuple[Type[ReqBodyTy], Type[RespBodyTy]]] = {
    RequestKind.RESOLVE: (ResolveReq, ResolveResp),
    RequestKind.STAT: (StatReq, StatResp),
    RequestKind.LS_DIR: (LsDirReq, LsDirResp),
    RequestKind.CREATE_EDEN_FILE: (CreateEdenFileReq, CreateEdenFileResp),
    RequestKind.ADD_EDEN_SPAN: (AddEdenSpanReq, AddEdenSpanResp),
    RequestKind.CERTIFY_EDEN_SPAN: (CertifyEdenSpanReq, CertifyEdenSpanResp),
    RequestKind.LINK_EDEN_FILE: (LinkEdenFileReq, LinkEdenFileResp),
    RequestKind.REPAIR_SPANS: (RepairSpansReq, RepairSpansResp),
    RequestKind.EXPUNGE_SPAN: (ExpungeEdenSpanReq, ExpungeEdenSpanResp),
    RequestKind.CERTIFY_EXPUNGE: (CertifyExpungeReq, CertifyExpungeResp),
    RequestKind.EXPUNGE_FILE: (ExpungeEdenFileReq, ExpungeEdenFileResp),
    RequestKind.DELETE_FILE: (DeleteFileReq, DeleteFileResp),
    RequestKind.FETCH_SPANS: (FetchSpansReq, FetchSpansResp),
    RequestKind.SAME_DIR_RENAME: (SameDirRenameReq, SameDirRenameResp),
    RequestKind.PURGE_DIRENT: (PurgeDirentReq, PurgeDirentResp),
    RequestKind.VISIT_INODES: (VisitInodesReq, VisitInodesResp),
    RequestKind.VISIT_EDEN: (VisitEdenReq, VisitEdenResp),
    RequestKind.REVERSE_BLOCK_QUERY: (ReverseBlockQueryReq,
        ReverseBlockQueryResp),
    RequestKind.REPAIR_BLOCK: (RepairBlockReq, RepairBlockResp),
    RequestKind.SET_PARENT: (SetParentReq, SetParentResp),
    RequestKind.CREATE_UNLINKED_DIR: (CreateDirReq, CreateDirResp),
    RequestKind.INJECT_DIRENT: (InjectDirentReq, InjectDirentResp),
    RequestKind.ACQUIRE_DIRENT: (AcquireDirentReq, AcquireDirentResp),
    RequestKind.RELEASE_DIRENT: (ReleaseDirentReq, ReleaseDirentResp),
    RequestKind.PURGE_REMOTE_OWNING_DIRENT: (PurgeRemoteOwningDirentReq,
        PurgeRemoteOwningDirentResp),
    RequestKind.MOVE_FILE_TO_EDEN: (MoveFileToEdenReq, MoveFileToEdenResp),
}

for kind, (req, resp) in REQUESTS.items():
    assert req.kind == kind, f'{kind}, {req}'
    assert resp.kind == kind, f'{kind}, {resp}'

assert all(k in REQUESTS for k in itertools.islice(RequestKind, 1, None))


@dataclass
class MetadataResponse(bincode.Packable):
    SIZE: ClassVar[int] = 8 + 1
    request_id: int
    body: RespBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataResponse':
        request_id = bincode.unpack_u64(u)
        resp_kind = RequestKind(bincode.unpack_u8(u))
        body: RespBodyTy
        if resp_kind == RequestKind.ERROR:
            body = MetadataError.unpack(u)
        else:
            body = REQUESTS[resp_kind][1].unpack(u)
        return MetadataResponse(request_id, body)


def __tests() -> None:
    original = MetadataRequest(ver=0, request_id=123,
        body=ResolveReq(mode=ResolveMode.ALIVE, parent_id=512,
            creation_ts=0, subname='hello_world'))

    packed = bincode.pack(original)

    unpacked = bincode.unpack(MetadataRequest, packed)

    assert(original == unpacked)

    original2 = MetadataResponse(
        request_id=456,
        body=ResolveResp(inode_with_ownership=9001, creation_ts=1657216525)
    )

    packed2 = bincode.pack(original2)

    unpacked2 = bincode.unpack(MetadataResponse, packed2)

    assert(original2 == unpacked2)


if __name__ == '__main__':
    __tests()
