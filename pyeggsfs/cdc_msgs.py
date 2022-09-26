
from dataclasses import dataclass
import enum
from typing import ClassVar, Dict, Optional, Tuple, Type, Union, Set

import bincode
from common import *

class CDCRequestKind(enum.IntEnum):
    ERROR = 0

    MAKE_DIRECTORY = 1
    RENAME_FILE = 2
    RENAME_DIRECTORY = 3
    UNLINK_DIRECTORY = 4 # TODO in the kernel is rmdir, but we mean something more specific with "unlink"

    # This should be privileged, needed for GC
    HARD_UNLINK_FILE = 5
assert CDCRequestKind.ERROR == EggsError.kind

CDC_ERRORS: Dict[CDCRequestKind, Set[ErrCode]] = {
    CDCRequestKind.MAKE_DIRECTORY: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.CANNOT_OVERRIDE_NAME, ErrCode.NAME_IS_LOCKED,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCRequestKind.RENAME_FILE: {
        ErrCode.TYPE_IS_DIRECTORY, ErrCode.OLD_DIRECTORY_NOT_FOUND, ErrCode.OLD_NAME_IS_LOCKED,
        ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NEW_DIRECTORY_NOT_FOUND, ErrCode.NEW_NAME_IS_LOCKED,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCRequestKind.RENAME_DIRECTORY: {
        ErrCode.TYPE_IS_NOT_DIRECTORY, ErrCode.LOOP_IN_DIRECTORY_RENAME, ErrCode.OLD_DIRECTORY_NOT_FOUND,
        ErrCode.OLD_NAME_IS_LOCKED, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET, ErrCode.NEW_DIRECTORY_NOT_FOUND,
        ErrCode.NEW_NAME_IS_LOCKED, ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCRequestKind.UNLINK_DIRECTORY: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NAME_IS_LOCKED, ErrCode.TYPE_IS_NOT_DIRECTORY,
    },
}
@dataclass
class MakeDirReq(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.MAKE_DIRECTORY
    owner_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirReq':
        parent_id = bincode.unpack_u64(u)
        subname = bincode.unpack_bytes(u)
        return MakeDirReq(parent_id, subname)
@dataclass
class MakeDirResp(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.MAKE_DIRECTORY
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
    
    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirResp':
        id = bincode.unpack_u64(u)
        return MakeDirResp(id)

@dataclass
class RenameFileReq(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.RENAME_FILE
    target_id: int
    old_owner_id: int
    old_name: bytes
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameFileReq(target_id, old_owner_id, old_name, new_owner_id, new_name)

@dataclass
class RenameFileResp(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.RENAME_FILE

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileResp':
        return RenameFileResp()

@dataclass
class UnlinkDirectoryReq(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.UNLINK_DIRECTORY
    owner_id: int
    target_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
    
    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlinkDirectoryReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return UnlinkDirectoryReq(owner_id, target_id, name)

@dataclass
class UnlinkDirectoryResp(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.UNLINK_DIRECTORY

    def pack_into(self, b: bytearray) -> None:
        pass
    
    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlinkDirectoryResp':
        return UnlinkDirectoryResp()

@dataclass
class RenameDirectoryReq(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.RENAME_DIRECTORY
    target_id: int
    old_owner_id: int
    old_name: bytes
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameDirectoryReq(target_id, old_owner_id, old_name, new_owner_id, new_name)

@dataclass
class RenameDirectoryResp(bincode.Packable):
    kind: ClassVar[CDCRequestKind] = CDCRequestKind.RENAME_DIRECTORY

    def pack_into(self, b: bytearray) -> None:
        pass

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryResp':
        return RenameDirectoryResp()

CDCRequestBody = Union[MakeDirReq, RenameFileReq, UnlinkDirectoryReq, RenameDirectoryReq]
CDCResponseBody = Union[EggsError, MakeDirResp, RenameFileResp, UnlinkDirectoryResp, RenameDirectoryResp]

CDC_REQUESTS: Dict[CDCRequestKind, Tuple[Type[CDCRequestBody], Type[CDCResponseBody]]] = {
    CDCRequestKind.MAKE_DIRECTORY: (MakeDirReq, MakeDirResp),
    CDCRequestKind.RENAME_FILE: (RenameFileReq, RenameFileResp),
    CDCRequestKind.UNLINK_DIRECTORY: (UnlinkDirectoryReq, UnlinkDirectoryResp),
    CDCRequestKind.RENAME_DIRECTORY: (RenameDirectoryReq, RenameDirectoryResp),
}

@dataclass
class CDCRequest(bincode.Packable):
    version: int
    request_id: int
    body: CDCRequestBody

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.version, b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CDCRequest':
        ver = bincode.unpack_u8(u)
        request_id = bincode.unpack_u64(u)
        body_kind = CDCRequestKind(bincode.unpack_u8(u))
        body_type = CDC_REQUESTS[body_kind][0]
        body = body_type.unpack(u)
        return CDCRequest(ver, request_id, body)

@dataclass
class CDCResponse(bincode.Packable):
    # Second byte for the kind
    SIZE: ClassVar[int] = 8 + 1
    request_id: int
    body: CDCResponseBody

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.kind, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CDCResponse':
        request_id = bincode.unpack_u64(u)
        resp_kind = CDCRequestKind(bincode.unpack_u8(u))
        body: CDCResponseBody
        if resp_kind == CDCRequestKind.ERROR:
            body = EggsError.unpack(u)
        else:
            body = CDC_REQUESTS[resp_kind][1].unpack(u)
        return CDCResponse(request_id, body)
