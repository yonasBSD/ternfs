#!/usr/bin/env python3

from dataclasses import dataclass
import enum
from typing import ClassVar, Dict, Optional, Tuple, Type, Union

import bincode


class InodeType(enum.IntEnum):
    DIRECTORY = 0
    FILE = 1
    SYMLINK = 2


class RequestKind(enum.IntEnum):
    ERROR = 0
    RESOLVE = 1
    STAT = 2


@dataclass
class ResolveReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RESOLVE
    parent_id: int
    subname: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_id, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolveReq':
        parent_id = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        return ResolveReq(parent_id, subname)


@dataclass
class StatReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.STAT
    inode_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.inode_id, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatReq':
        return StatReq(bincode.unpack_unsigned(u))


ReqBodyTy = Union[ResolveReq, StatReq]


@dataclass
class MetadataRequest(bincode.Packable):
    ver: int
    request_id: int
    body: ReqBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.ver, b)
        bincode.pack_unsigned_into(self.request_id, b)
        bincode.pack_unsigned_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataRequest':
        ver = bincode.unpack_unsigned(u)
        request_id = bincode.unpack_unsigned(u)
        body_kind = RequestKind(bincode.unpack_unsigned(u))
        body_type = REQUESTS[body_kind][0]
        body = body_type.unpack(u)
        return MetadataRequest(ver, request_id, body)


class MetadataErrorKind(enum.IntEnum):
    TOO_SOON = 0
    INODE_ALREADY_EXISTS = 1
    NAME_TOO_LONG = 2
    ROCKS_DB_ERROR = 3
    NETWORK_ERROR = 4
    BINCODE_ERROR = 5
    LOGIC_ERROR = 6
    UNSUPPORTED_VERSION = 7
    NOT_FOUND = 8


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

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.id, b)
        bincode.pack_unsigned_into(self.inode_type, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ResolvedInode':
        id = bincode.unpack_unsigned(u)
        inode_type = InodeType(bincode.unpack_unsigned(u))
        return ResolvedInode(id, inode_type)


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
        bincode.pack_unsigned_into(self.inode_type, b)
        self.payload.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatResp':
        file_type = InodeType(bincode.unpack_unsigned(u))
        payload: StatPayloadTy
        if file_type == InodeType.DIRECTORY:
            payload = StatDirPayload.unpack(u)
        else:
            payload = StatFilePayload.unpack(u)
        return StatResp(file_type, payload)


RespBodyTy = Union[MetadataError, ResolveResp, StatResp]


REQUESTS: Dict[RequestKind, Tuple[Type[ReqBodyTy], Type[RespBodyTy]]] = {
    RequestKind.RESOLVE: (ResolveReq, ResolveResp),
    RequestKind.STAT: (StatReq, StatResp),
}


@dataclass
class MetadataResponse(bincode.Packable):
    request_id: int
    body: RespBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.request_id, b)
        bincode.pack_unsigned_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MetadataResponse':
        request_id = bincode.unpack_unsigned(u)
        resp_kind = RequestKind(bincode.unpack_unsigned(u))
        body: RespBodyTy
        if resp_kind == RequestKind.ERROR:
            body = MetadataError.unpack(u)
        else:
            body = REQUESTS[resp_kind][1].unpack(u)
        return MetadataResponse(request_id, body)


def __tests() -> None:
    original = MetadataRequest(ver=0, request_id=123,
        body=ResolveReq(parent_id=512, subname='hello_world'))

    packed = bincode.pack(original)

    unpacked = bincode.unpack(MetadataRequest, packed)

    assert(original == unpacked)

    original2 = MetadataResponse(
        request_id=456,
        body=ResolveResp(f=ResolvedInode(
            id=9001, inode_type=InodeType.FILE)))

    packed2 = bincode.pack(original2)

    unpacked2 = bincode.unpack(MetadataResponse, packed2)

    assert(original2 == unpacked2)


if __name__ == '__main__':
    __tests()
