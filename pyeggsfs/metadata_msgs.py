#!/usr/bin/env python3

from dataclasses import dataclass, field
import enum
import itertools
from typing import (ClassVar, Dict, List, NamedTuple, Optional, Tuple, Type,
    Union)

import bincode
import metadata_utils


# one-hot encoding to allow "acceptable-types" bitset
class InodeType(enum.IntEnum):
    DIRECTORY = 1
    FILE = 2
    SYMLINK = 4


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

    # privilaged (needs MAC)
    SET_PARENT = 0x81
    CREATE_UNLINKED_DIR = 0x82
    # a release must follow an inject or acquire
    INJECT_DIRENT = 0x83  # insert a new living dirent with owning == false
    ACQUIRE_DIRENT = 0x84 # if living dirent exists, set owning := false
    RELEASE_DIRENT = 0x85 # if living dirent exists, set owning := true

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
        bincode.pack_unsigned_into(self.mode, b)
        bincode.pack_unsigned_into(self.parent_id, b)
        bincode.pack_unsigned_into(self.creation_ts, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolveReq':
        mode = ResolveMode(bincode.unpack_unsigned(u))
        parent_id = bincode.unpack_unsigned(u)
        creation_ts = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        return ResolveReq(mode, parent_id, creation_ts, subname)


@dataclass
class StatReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.STAT
    inode_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatReq':
        return StatReq(bincode.unpack_unsigned(u))


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
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_u64_into(self.as_of, b)
        bincode.pack_unsigned_into(self.continuation_key, b)
        bincode.pack_unsigned_into(self.flags, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsDirReq':
        inode = bincode.unpack_unsigned(u)
        as_of = bincode.unpack_u64(u)
        continuation_key = bincode.unpack_unsigned(u)
        flags = LsFlags(bincode.unpack_unsigned(u))
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
        assert len(self.crc32) == 4
        bincode.pack_fixed_into(self.crc32, b)
        bincode.pack_unsigned_into(self.size, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'NewBlockInfo':
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_unsigned(u)
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
        assert len(self.crc32) == 4
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity_mode, b)
        bincode.pack_fixed_into(self.crc32, b)
        bincode.pack_unsigned_into(self.size, b)
        bincode.pack_unsigned_into(self.byte_offset, b)
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_u64_into(self.cookie, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.payload, bytes)
            assert len(self.payload) == self.size
            bincode.pack_fixed_into(self.payload, b)
        elif isinstance(self.payload, list):
            assert metadata_utils.total_blocks(self.parity_mode) == len(self.payload)
            for info in self.payload:
                info.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddEdenSpanReq':
        storage_class = bincode.unpack_u8(u)
        parity_mode = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_unsigned(u)
        byte_offset = bincode.unpack_unsigned(u)
        inode = bincode.unpack_unsigned(u)
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
        assert all(len(proof) == 8 for proof in self.proofs)
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.byte_offset, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_unsigned_into(len(self.proofs), b)
        for proof in self.proofs:
            bincode.pack_fixed_into(proof, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyEdenSpanReq':
        inode = bincode.unpack_unsigned(u)
        byte_offset = bincode.unpack_unsigned(u)
        cookie = bincode.unpack_u64(u)
        num_proofs = bincode.unpack_unsigned(u)
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
        bincode.pack_unsigned_into(self.eden_inode, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.new_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkEdenFileReq':
        eden_inode = bincode.unpack_unsigned(u)
        cookie = bincode.unpack_u64(u)
        parent_inode = bincode.unpack_unsigned(u)
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
        bincode.pack_unsigned_into(self.source_inode, b)
        bincode.pack_u64_into(self.source_cookie, b)
        bincode.pack_unsigned_into(self.sink_inode, b)
        bincode.pack_u64_into(self.sink_cookie, b)
        bincode.pack_unsigned_into(self.target_inode, b)
        bincode.pack_u64_into(self.target_cookie, b)
        bincode.pack_unsigned_into(self.byte_offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RepairSpansReq':
        source_inode = bincode.unpack_unsigned(u)
        source_cookie = bincode.unpack_u64(u)
        sink_inode = bincode.unpack_unsigned(u)
        sink_cookie = bincode.unpack_u64(u)
        target_inode = bincode.unpack_unsigned(u)
        target_cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_unsigned(u)
        return RepairSpansReq(source_inode, source_cookie, sink_inode,
            sink_cookie, target_inode, target_cookie, byte_offset)


@dataclass
class ExpungeEdenSpanReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_SPAN
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenSpanReq':
        inode = bincode.unpack_unsigned(u)
        return ExpungeEdenSpanReq(inode)


@dataclass
class CertifyExpungeReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EXPUNGE
    inode: int
    offset: int
    proofs: List[Tuple[int, bytes]]

    def pack_into(self, b: bytearray) -> None:
        assert all(len(proof) == 8 for proof in self.proofs)
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.offset, b)
        bincode.pack_unsigned_into(len(self.proofs), b)
        for block_id, proof in self.proofs:
            bincode.pack_u32_into(block_id, b)
            bincode.pack_fixed_into(proof, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyExpungeReq':
        inode = bincode.unpack_unsigned(u)
        offset = bincode.unpack_unsigned(u)
        num_proofs = bincode.unpack_unsigned(u)
        proofs = [
            (bincode.unpack_u32(u), bincode.unpack_fixed(u, 8))
            for _ in range(num_proofs)
        ]
        return CertifyExpungeReq(inode, offset, proofs)


@dataclass
class ExpungeEdenFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_FILE
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenFileReq':
        inode = bincode.unpack_unsigned(u)
        return ExpungeEdenFileReq(inode)


@dataclass
class DeleteFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.DELETE_FILE
    parent_inode: int
    name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'DeleteFileReq':
        parent_inode = bincode.unpack_unsigned(u)
        name = bincode.unpack_bytes(u).decode()
        return DeleteFileReq(parent_inode, name)


@dataclass
class FetchSpansReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.FETCH_SPANS
    inode: int
    offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchSpansReq':
        inode = bincode.unpack_unsigned(u)
        offset = bincode.unpack_unsigned(u)
        return FetchSpansReq(inode, offset)


@dataclass
class SameDirRenameReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SAME_DIR_RENAME
    parent_inode: int
    old_name: str
    new_name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.old_name.encode(), b)
        bincode.pack_bytes_into(self.new_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirRenameReq':
        parent_inode = bincode.unpack_unsigned(u)
        old_name = bincode.unpack_bytes(u).decode()
        new_name = bincode.unpack_bytes(u).decode()
        return SameDirRenameReq(parent_inode, old_name, new_name)


@dataclass
class SetParentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.SET_PARENT
    inode: int
    new_parent: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.new_parent, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetParentReq':
        inode = bincode.unpack_unsigned(u)
        new_parent = bincode.unpack_unsigned(u)
        return SetParentReq(inode, new_parent)


@dataclass
class CreateDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_UNLINKED_DIR
    token_inode: int
    new_parent: int
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.token_inode, b)
        bincode.pack_unsigned_into(self.new_parent, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirReq':
        token_inode = bincode.unpack_unsigned(u)
        new_parent = bincode.unpack_unsigned(u)
        opaque = bincode.unpack_bytes(u)
        return CreateDirReq(token_inode, new_parent, opaque)


@dataclass
class InjectDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.INJECT_DIRENT
    parent_inode: int
    subname: str
    child_inode: int
    child_type: InodeType
    creation_ts: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_unsigned_into(self.child_inode, b)
        bincode.pack_u8_into(self.child_type, b)
        bincode.pack_unsigned_into(self.creation_ts, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'InjectDirentReq':
        parent_inode = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        child_inode = bincode.unpack_unsigned(u)
        child_type = InodeType(bincode.unpack_u8(u))
        creation_ts = bincode.unpack_unsigned(u)
        return InjectDirentReq(parent_inode, subname, child_inode, child_type,
            creation_ts)


@dataclass
class AcquireDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ACQUIRE_DIRENT
    parent_inode: int
    subname: str
    acceptable_types: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_u8_into(self.acceptable_types, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AcquireDirentReq':
        parent_inode = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        acceptable_types = bincode.unpack_u8(u)
        return AcquireDirentReq(parent_inode, subname, acceptable_types)


@dataclass
class ReleaseDirentReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RELEASE_DIRENT
    parent_inode: int
    subname: str
    kill: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.subname.encode(), b)
        bincode.pack_u8_into(self.kill, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReleaseDirentReq':
        parent_inode = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        kill = bool(bincode.unpack_u8(u))
        return ReleaseDirentReq(parent_inode, subname, kill)


ReqBodyTy = Union[ResolveReq, StatReq, LsDirReq, CreateEdenFileReq,
    AddEdenSpanReq, CertifyEdenSpanReq, LinkEdenFileReq, RepairSpansReq,
    ExpungeEdenSpanReq, CertifyExpungeReq, ExpungeEdenFileReq, DeleteFileReq,
    FetchSpansReq, SameDirRenameReq, SetParentReq, CreateDirReq,
    InjectDirentReq, AcquireDirentReq, ReleaseDirentReq]


@dataclass
class MetadataRequest(bincode.Packable):
    ver: int
    request_id: int
    body: ReqBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.body.kind, b)
        bincode.pack_unsigned_into(self.ver, b)
        bincode.pack_u64_into(self.request_id, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataRequest':
        kind = RequestKind(bincode.unpack_u8(u))
        ver = bincode.unpack_unsigned(u)
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
        bincode.pack_unsigned_into(self.error_kind, b)
        bincode.pack_bytes_into(self.text.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataError':
        error_kind = MetadataErrorKind(bincode.unpack_unsigned(u))
        text = bincode.unpack_bytes(u).decode()
        return MetadataError(error_kind, text)


@dataclass
class ResolvedInode(bincode.Packable):
    id: int
    inode_type: InodeType
    creation_ts: int
    is_owning: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.id, b)
        bincode.pack_u8_into(self.inode_type, b)
        bincode.pack_unsigned_into(self.creation_ts, b)
        bincode.pack_unsigned_into(self.is_owning, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolvedInode':
        id = bincode.unpack_unsigned(u)
        inode_type = InodeType(bincode.unpack_u8(u))
        creation_ts = bincode.unpack_unsigned(u)
        is_owning = bool(bincode.unpack_unsigned(u))
        return ResolvedInode(id, inode_type, creation_ts, is_owning)


@dataclass
class ResolveResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RESOLVE
    f: Optional[ResolvedInode]

    def pack_into(self, b: bytearray) -> None:
        if self.f is None:
            bincode.pack_unsigned_into(0, b)
        else:
            bincode.pack_unsigned_into(1, b)
            self.f.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolveResp':
        option_kind = bincode.unpack_unsigned(u)
        return ResolveResp(ResolvedInode.unpack(u) if option_kind else None)


@dataclass
class StatFilePayload(bincode.Packable):
    mtime: int
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.mtime, b)
        bincode.pack_unsigned_into(self.size, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatFilePayload':
        mtime = bincode.unpack_unsigned(u)
        size = bincode.unpack_unsigned(u)
        return StatFilePayload(mtime, size)


@dataclass
class StatDirPayload(bincode.Packable):
    mtime: int
    parent_inode: int # NULL_INODE => no parent
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.mtime, b)
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.opaque, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatDirPayload':
        mtime = bincode.unpack_unsigned(u)
        parent_inode = bincode.unpack_unsigned(u)
        opaque = bincode.unpack_bytes(u)
        return StatDirPayload(mtime, parent_inode, opaque)


StatPayloadTy = Union[StatFilePayload, StatDirPayload]


@dataclass
class StatResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.STAT
    inode_type: InodeType
    payload: StatPayloadTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.inode_type, b)
        self.payload.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatResp':
        file_type = InodeType(bincode.unpack_u8(u))
        payload: StatPayloadTy
        if file_type == InodeType.DIRECTORY:
            payload = StatDirPayload.unpack(u)
        else:
            payload = StatFilePayload.unpack(u)
        return StatResp(file_type, payload)


@dataclass
class LsPayload(bincode.Packable):
    inode: int # NULL_INODE => a "nothing" entry
    hash_of_name: int
    name: str
    inode_type: InodeType

    def calc_packed_size(self) -> int:
        ret = 8 + 1 # sizeof(inode) + sizeof(len(name))
        ret += bincode.varint_packed_size(self.inode)
        ret += len(self.name.encode())
        ret += bincode.varint_packed_size(self.inode_type)
        return ret

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_u64_into(self.hash_of_name, b)
        bincode.pack_bytes_into(self.name.encode(), b)
        bincode.pack_u8_into(self.inode_type, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsPayload':
        inode = bincode.unpack_unsigned(u)
        hash_of_name = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        inode_type = InodeType(bincode.unpack_u8(u))
        return LsPayload(inode, hash_of_name, name, inode_type)


@dataclass
class LsDirResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.LS_DIR
    SIZE: ClassVar[int] = 8 + 1
    continuation_key: int # UINT64_MAX => no more results
    results: List[LsPayload]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.continuation_key, b)
        bincode.pack_unsigned_into(len(self.results), b)
        for r in self.results:
            r.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsDirResp':
        continuation_key = bincode.unpack_u64(u)
        count = bincode.unpack_unsigned(u)
        results = [LsPayload.unpack(u) for _ in range(count)]
        return LsDirResp(continuation_key, results)


@dataclass
class CreateEdenFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CREATE_EDEN_FILE
    inode: int
    cookie: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_u64_into(self.cookie, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateEdenFileResp':
        inode = bincode.unpack_unsigned(u)
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
        assert len(self.ip) == 4
        assert len(self.certificate) == 8, self.certificate
        bincode.pack_fixed_into(self.ip, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_unsigned_into(self.block_id, b)
        bincode.pack_fixed_into(self.certificate, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockInfo':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_unsigned(u)
        certificate = bincode.unpack_fixed(u, 8)
        return BlockInfo(ip, port, block_id, certificate)


@dataclass
class AddEdenSpanResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ADD_EDEN_SPAN
    span: List[BlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(len(self.span), b)
        for block in self.span:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddEdenSpanResp':
        size = bincode.unpack_unsigned(u)
        span = [BlockInfo.unpack(u) for _ in range(size)]
        return AddEdenSpanResp(span)


@dataclass
class CertifyEdenSpanResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EDEN_SPAN
    # echoed back
    inode: int
    byte_offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.byte_offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyEdenSpanResp':
        inode = bincode.unpack_unsigned(u)
        byte_offset = bincode.unpack_unsigned(u)
        return CertifyEdenSpanResp(inode, byte_offset)


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
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.mtime, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirResp':
        inode = bincode.unpack_unsigned(u)
        creation_ts = bincode.unpack_unsigned(u)
        return CreateDirResp(inode, creation_ts)


@dataclass
class InjectDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.INJECT_DIRENT

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'InjectDirentResp':
        return InjectDirentResp()


@dataclass
class AcquireDirentResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ACQUIRE_DIRENT
    inode: int
    inode_type: InodeType

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_u8_into(self.inode_type, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AcquireDirentResp':
        inode = bincode.unpack_unsigned(u)
        inode_type = InodeType(bincode.unpack_u8(u))
        return AcquireDirentResp(inode, inode_type)


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
    inode: int
    offset: int
    span: List[BlockInfo] # empty => blockless, no certification required

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.offset, b)
        bincode.pack_unsigned_into(len(self.span), b)
        for block in self.span:
            block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenSpanResp':
        inode = bincode.unpack_unsigned(u)
        offset = bincode.unpack_unsigned(u)
        size = bincode.unpack_unsigned(u)
        span = [BlockInfo.unpack(u) for _ in range(size)]
        return ExpungeEdenSpanResp(inode, offset, span)


@dataclass
class CertifyExpungeResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.CERTIFY_EXPUNGE
    inode: int
    offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)
        bincode.pack_unsigned_into(self.offset, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CertifyExpungeResp':
        inode = bincode.unpack_unsigned(u)
        offset = bincode.unpack_unsigned(u)
        return CertifyExpungeResp(inode, offset)


@dataclass
class ExpungeEdenFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.EXPUNGE_FILE
    inode: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpungeEdenFileResp':
        inode = bincode.unpack_unsigned(u)
        return ExpungeEdenFileResp(inode)


@dataclass
class DeleteFileResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.DELETE_FILE
    parent_inode: int
    name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'DeleteFileResp':
        parent_inode = bincode.unpack_unsigned(u)
        name = bincode.unpack_bytes(u).decode()
        return DeleteFileResp(parent_inode, name)


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
        return 4 + 2 + 8 + 4 + bincode.varint_packed_size(self.size) + 1

    def pack_into(self, b: bytearray) -> None:
        assert len(self.ip) == 4
        assert len(self.crc) == 4
        bincode.pack_fixed_into(self.ip, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.crc, b)
        bincode.pack_unsigned_into(self.size, b)
        bincode.pack_u8_into(self.flags, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedBlock':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_u64(u)
        crc = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_unsigned(u)
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
        ret += bincode.varint_packed_size(self.file_offset)
        ret += bincode.varint_packed_size(self.size)
        if isinstance(self.payload, bytes):
            ret += bincode.varbytes_packed_size(self.payload)
        else:
            ret += sum(block.calc_packed_size() for block in self.payload)
        return ret

    def pack_into(self, b: bytearray) -> None:
        assert len(self.crc) == 4
        bincode.pack_unsigned_into(self.file_offset, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.crc, b)
        bincode.pack_unsigned_into(self.size, b)
        if self.storage_class == INLINE_STORAGE:
            assert isinstance(self.payload, bytes)
            assert self.size == len(self.payload)
            bincode.pack_fixed_into(self.payload, b)
        elif self.storage_class == ZERO_FILL_STORAGE:
            assert self.payload == b''
        else:
            assert isinstance(self.payload, list)
            assert len(self.payload) == metadata_utils.total_blocks(self.parity)
            for block in self.payload:
                block.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedSpan':
        file_offset = bincode.unpack_unsigned(u)
        parity = bincode.unpack_u8(u)
        storage_class = bincode.unpack_u8(u)
        crc = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_unsigned(u)
        payload: Union[bytes, List[FetchedBlock]]
        if storage_class == INLINE_STORAGE:
            payload = bincode.unpack_fixed(u, size)
        elif storage_class == ZERO_FILL_STORAGE:
            payload = b''
        else:
            num_blocks = metadata_utils.total_blocks(parity)
            assert num_blocks
            payload = [FetchedBlock.unpack(u) for _ in range(num_blocks)]
        return FetchedSpan(file_offset, parity, storage_class, crc, size, payload)


@dataclass
class FetchSpansResp(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.FETCH_SPANS
    SIZE_UPPER_BOUND: ClassVar[int] = 9 + 3
    next_offset: int # 0 => no more spans (0 more efficient than UINT64_MAX)
    spans: List[FetchedSpan]

    def pack_into(self, b: bytearray) -> None:
        assert len(self.spans) < 2**16
        bincode.pack_unsigned_into(self.next_offset, b)
        bincode.pack_unsigned_into(len(self.spans), b)
        for span in self.spans:
            span.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchSpansResp':
        next_offset = bincode.unpack_unsigned(u)
        num_spans = bincode.unpack_unsigned(u)
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



RespBodyTy = Union[MetadataError, ResolveResp, StatResp, LsDirResp,
    CreateEdenFileResp, AddEdenSpanResp, CertifyEdenSpanResp, LinkEdenFileResp,
    RepairSpansResp, ExpungeEdenSpanResp, CertifyExpungeResp,
    ExpungeEdenFileResp, DeleteFileResp, DeleteFileReq, FetchSpansResp,
    SameDirRenameResp, SetParentResp, CreateDirResp, InjectDirentResp,
    AcquireDirentResp, ReleaseDirentResp]


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
    RequestKind.SET_PARENT: (SetParentReq, SetParentResp),
    RequestKind.CREATE_UNLINKED_DIR: (CreateDirReq, CreateDirResp),
    RequestKind.INJECT_DIRENT: (InjectDirentReq, InjectDirentResp),
    RequestKind.ACQUIRE_DIRENT: (AcquireDirentReq, AcquireDirentResp),
    RequestKind.RELEASE_DIRENT: (ReleaseDirentReq, ReleaseDirentResp),
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
        body=ResolveResp(f=ResolvedInode(
            id=9001, inode_type=InodeType.FILE, creation_ts=1657216525,
            is_owning=True)))

    packed2 = bincode.pack(original2)

    unpacked2 = bincode.unpack(MetadataResponse, packed2)

    assert(original2 == unpacked2)


if __name__ == '__main__':
    __tests()
