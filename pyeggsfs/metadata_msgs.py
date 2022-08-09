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

    # privilaged (needs MAC)
    SET_PARENT = 0x81
    CREATE_UNLINKED_DIR = 0x82

    # a release must follow and inject or acquire
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
        bincode.pack_unsigned_into(self.as_of, b)
        bincode.pack_unsigned_into(self.continuation_key, b)
        bincode.pack_unsigned_into(self.flags, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LsDirReq':
        inode = bincode.unpack_unsigned(u)
        as_of = bincode.unpack_unsigned(u)
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
    crc32: int
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u32_into(self.crc32, b)
        bincode.pack_unsigned_into(self.size, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'NewBlockInfo':
        crc32 = bincode.unpack_u32(u)
        size = bincode.unpack_unsigned(u)
        return NewBlockInfo(crc32, size)


INLINE_STORAGE = 0
ZERO_FILL_STORAGE = 1


@dataclass
class AddEdenSpanReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.ADD_EDEN_SPAN
    storage_class: int
    parity_mode: int
    crc32: int
    size: int
    byte_offset: int
    inode: int
    cookie: int
    payload: Union[bytes, List[NewBlockInfo]]

    def pack_into(self, b: bytearray) -> None:
        assert (self.parity_mode == 0) == isinstance(self.payload, bytes)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity_mode, b)
        bincode.pack_u32_into(self.crc32, b)
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
        crc32 = bincode.unpack_u32(u)
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
    AddEdenSpanReq, SetParentReq, CreateDirReq, InjectDirentReq,
    AcquireDirentReq, ReleaseDirentReq]


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
    EDEN_TIME_EXPIRED = 13
    SHUCKLE_ERROR = 14


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
    # certificate := MAC(b'w' + block_id + crc + size)[:8]
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


RespBodyTy = Union[MetadataError, ResolveResp, StatResp, LsDirResp,
    CreateEdenFileResp, AddEdenSpanResp, SetParentResp, CreateDirResp,
    InjectDirentResp, AcquireDirentResp, ReleaseDirentResp]


REQUESTS: Dict[RequestKind, Tuple[Type[ReqBodyTy], Type[RespBodyTy]]] = {
    RequestKind.RESOLVE: (ResolveReq, ResolveResp),
    RequestKind.STAT: (StatReq, StatResp),
    RequestKind.LS_DIR: (LsDirReq, LsDirResp),
    RequestKind.CREATE_EDEN_FILE: (CreateEdenFileReq, CreateEdenFileResp),
    RequestKind.ADD_EDEN_SPAN: (AddEdenSpanReq, AddEdenSpanResp),
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
