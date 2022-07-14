
from dataclasses import dataclass
import enum
from typing import ClassVar, Dict, Optional, Tuple, Type, Union

import bincode


class RequestKind(enum.IntEnum):
    ERROR = 0
    MK_DIR = 1
    MV_FILE = 2
    MV_DIR = 3


@dataclass
class MkDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MK_DIR
    parent_id: int
    subname: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.parent_id, b)
        bincode.pack_bytes_into(self.subname.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MkDirReq':
        parent_id = bincode.unpack_unsigned(u)
        subname = bincode.unpack_bytes(u).decode()
        return MkDirReq(parent_id, subname)


@dataclass
class MvDirReq(bincode.Packable):
    kind: ClassVar[RequestKind] = RequestKind.MV_DIR
    parent_id: int
    subname: str

    def pack_into(self, b: bytearray) -> None:
        raise NotImplementedError()

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MvDirReq':
        raise NotImplementedError()


ReqBodyTy = Union[MkDirReq, MvDirReq]


REQUESTS: Dict[RequestKind, Type[ReqBodyTy]] = {
    RequestKind.MK_DIR: MkDirReq,
    RequestKind.MV_DIR: MvDirReq,
}


@dataclass
class CrossDirRequest(bincode.Packable):
    ver: int
    request_id: int
    body: ReqBodyTy

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.ver, b)
        bincode.pack_unsigned_into(self.request_id, b)
        bincode.pack_unsigned_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossDirRequest':
        ver = bincode.unpack_unsigned(u)
        request_id = bincode.unpack_unsigned(u)
        body_kind = RequestKind(bincode.unpack_unsigned(u))
        body_type = REQUESTS[body_kind]
        body = body_type.unpack(u)
        return CrossDirRequest(ver, request_id, body)


class ResponseStatus(enum.IntEnum):
    OK = 0
    GENERAL_ERROR = 1
    PARENT_INVALID = 2


@dataclass
class CrossDirResponse(bincode.Packable):
    request_id: int
    status_code: ResponseStatus
    new_inode: Optional[int]
    text: str

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_unsigned_into(self.request_id, b)
        bincode.pack_unsigned_into(self.status_code, b)
        if self.new_inode is None:
            bincode.pack_unsigned_into(0, b)
        else:
            bincode.pack_unsigned_into(1, b)
            bincode.pack_unsigned_into(self.new_inode, b)
        bincode.pack_bytes_into(self.text.encode(), b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossDirResponse':
        request_id = bincode.unpack_unsigned(u)
        status_code = ResponseStatus(bincode.unpack_unsigned(u))
        has_inode = bincode.unpack_unsigned(u)
        new_inode = bincode.unpack_unsigned(u) if has_inode else None
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
