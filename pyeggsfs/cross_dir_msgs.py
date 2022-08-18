
from dataclasses import dataclass
import enum
import itertools
from typing import ClassVar, Dict, Optional, Tuple, Type, Union

import bincode


class RequestKind(enum.IntEnum):
    ERROR = 0
    MK_DIR = 1
    MV_FILE = 2
    MV_DIR = 3
    RM_DIR = 4
    PURGE_REMOTE_FILE = 5


@dataclass
class MkDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MK_DIR
    parent_id: int
    subname: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_id, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MkDirReq':
        parent_id = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        return MkDirReq(parent_id, subname)


@dataclass
class MvFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MV_FILE
    source_parent_inode: int
    target_parent_inode: int
    source_name: str
    target_name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.source_parent_inode, b)
        bincode.pack_u64_into(self.target_parent_inode, b)
        bincode.pack_bytes_into(self.source_name.encode(), b)
        bincode.pack_bytes_into(self.target_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MvFileReq':
        source_parent_inode = bincode.unpack_u64(u)
        target_parent_inode = bincode.unpack_u64(u)
        source_name = bincode.unpack_bytes(u).decode()
        target_name = bincode.unpack_bytes(u).decode()
        return MvFileReq(source_parent_inode, target_parent_inode, source_name,
            target_name)


@dataclass
class MvDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MV_DIR
    source_parent_inode: int
    target_parent_inode: int
    source_name: str
    target_name: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.source_parent_inode, b)
        bincode.pack_u64_into(self.target_parent_inode, b)
        bincode.pack_bytes_into(self.source_name.encode(), b)
        bincode.pack_bytes_into(self.target_name.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MvDirReq':
        source_parent_inode = bincode.unpack_u64(u)
        target_parent_inode = bincode.unpack_u64(u)
        source_name = bincode.unpack_bytes(u).decode()
        target_name = bincode.unpack_bytes(u).decode()
        return MvDirReq(source_parent_inode, target_parent_inode, source_name,
            target_name)


@dataclass
class RmDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.RM_DIR
    parent_id: int
    subname: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_id, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RmDirReq':
        parent_id = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u).decode()
        return RmDirReq(parent_id, subname)


@dataclass
class PurgeRemoteFileReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.PURGE_REMOTE_FILE
    parent_inode: int
    name: str
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.parent_inode, b)
        bincode.pack_bytes_into(self.name.encode(), b)
        bincode.pack_u64_into(self.creation_time, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'PurgeRemoteFileReq':
        parent_inode = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u).decode()
        creation_time = bincode.unpack_u64(u)
        return PurgeRemoteFileReq(parent_inode, name, creation_time)


ReqBodyTy = Union[MkDirReq, MvFileReq, MvDirReq, RmDirReq, PurgeRemoteFileReq]


REQUESTS: Dict[RequestKind, Type[ReqBodyTy]] = {
    RequestKind.MK_DIR:            MkDirReq,
    RequestKind.MV_FILE:           MvFileReq,
    RequestKind.MV_DIR:            MvDirReq,
    RequestKind.RM_DIR:            RmDirReq,
    RequestKind.PURGE_REMOTE_FILE: PurgeRemoteFileReq
}

for kind, req in REQUESTS.items():
    assert kind == req.kind, f'{kind}, {req}'

assert all(k in REQUESTS for k in itertools.islice(RequestKind, 1, None))

@dataclass
class CrossDirRequest(bincode.Packable):
    ver: int
    request_id: int
    body: ReqBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.ver, b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossDirRequest':
        ver = bincode.unpack_u8(u)
        request_id = bincode.unpack_u64(u)
        body_kind = RequestKind(bincode.unpack_u8(u))
        body_type = REQUESTS[body_kind]
        body = body_type.unpack(u)
        return CrossDirRequest(ver, request_id, body)


class ResponseStatus(enum.IntEnum):
    OK = 0
    GENERAL_ERROR = 1
    PARENT_INVALID = 2
    NOT_FOUND = 3
    BAD_INODE_TYPE = 4
    BAD_REQUEST = 5
    CANNOT_OVERRIDE_TARGET = 6
    LOOP_DETECTED = 7


@dataclass
class CrossDirResponse(bincode.Packable):
    request_id: int
    status_code: ResponseStatus
    new_inode: Optional[int]
    text: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.status_code, b)
        if self.new_inode is None:
            bincode.pack_u8_into(0, b)
        else:
            bincode.pack_u8_into(1, b)
            bincode.pack_u64_into(self.new_inode, b)
        bincode.pack_bytes_into(self.text.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossDirResponse':
        request_id = bincode.unpack_u64(u)
        status_code = ResponseStatus(bincode.unpack_u8(u))
        has_inode = bincode.unpack_u8(u)
        new_inode = bincode.unpack_u64(u) if has_inode else None
        text = bincode.unpack_bytes(u).decode()
        return CrossDirResponse(request_id, status_code, new_inode, text)


def __tests() -> None:
    original = CrossDirRequest(ver=0, request_id=123,
        body=MkDirReq(parent_id=512, subname='hello_world'))

    packed = bincode.pack(original)

    unpacked = bincode.unpack(CrossDirRequest, packed)

    assert(original == unpacked)


if __name__ == '__main__':
    __tests()
