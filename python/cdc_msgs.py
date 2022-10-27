from dataclasses import dataclass
from typing import ClassVar, Dict, Optional, Tuple, Type, Union, Set

import bincode
from common import *
from error import *
from msgs import *

CDC_REQ_PROTOCOL_VERSION = b'CDC\0'
CDC_RESP_PROTOCOL_VERSION = b'CDC\1'

# INTERNAL_ERROR/FATAL_ERROR/TIMEOUT are implicitly included in all of these
CDC_ERRORS: Dict[CDCMessageKind, Set[ErrCode]] = {
    CDCMessageKind.MAKE_DIRECTORY: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.CANNOT_OVERRIDE_NAME, ErrCode.NAME_IS_LOCKED,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCMessageKind.RENAME_FILE: {
        ErrCode.TYPE_IS_DIRECTORY, ErrCode.OLD_DIRECTORY_NOT_FOUND, ErrCode.OLD_NAME_IS_LOCKED,
        ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NEW_DIRECTORY_NOT_FOUND, ErrCode.NEW_NAME_IS_LOCKED,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCMessageKind.RENAME_DIRECTORY: {
        ErrCode.TYPE_IS_NOT_DIRECTORY, ErrCode.LOOP_IN_DIRECTORY_RENAME, ErrCode.OLD_DIRECTORY_NOT_FOUND,
        ErrCode.OLD_NAME_IS_LOCKED, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET, ErrCode.NEW_DIRECTORY_NOT_FOUND,
        ErrCode.NEW_NAME_IS_LOCKED, ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    CDCMessageKind.SOFT_UNLINK_DIRECTORY: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NAME_IS_LOCKED, ErrCode.TYPE_IS_NOT_DIRECTORY,
    },
    CDCMessageKind.HARD_UNLINK_DIRECTORY: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.DIRECTORY_NOT_EMPTY, ErrCode.DIRECTORY_HAS_OWNER, ErrCode.CANNOT_REMOVE_ROOT_DIRECTORY,
    }
}

@dataclass
class CDCRequest(bincode.Packable):
    request_id: int
    body: CDCRequestBody

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(CDC_REQ_PROTOCOL_VERSION, len(CDC_REQ_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.KIND, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CDCRequest':
        ver = bincode.unpack_fixed(u, len(CDC_REQ_PROTOCOL_VERSION))
        assert ver == CDC_REQ_PROTOCOL_VERSION, f'Expected {CDC_REQ_PROTOCOL_VERSION}, got {ver}'
        request_id = bincode.unpack_u64(u)
        body_kind = CDCMessageKind(bincode.unpack_u8(u))
        body_type = CDC_REQUESTS[body_kind][0]
        body = body_type.unpack(u)
        return CDCRequest(request_id=request_id, body=body)

@dataclass
class CDCResponse(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = len(CDC_RESP_PROTOCOL_VERSION) + 8 + 1
    request_id: int
    body: Union[CDCResponseBody, EggsError]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(CDC_RESP_PROTOCOL_VERSION, len(CDC_RESP_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.KIND, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CDCResponse':
        ver = bincode.unpack_fixed(u, len(CDC_RESP_PROTOCOL_VERSION))
        assert ver == CDC_RESP_PROTOCOL_VERSION
        request_id = bincode.unpack_u64(u)
        resp_kind = bincode.unpack_u8(u)
        body: Union[CDCResponseBody, EggsError]
        if resp_kind == EggsError.KIND:
            body = EggsError.unpack(u)
        else:
            body = CDC_REQUESTS[CDCMessageKind(resp_kind)][1].unpack(u)
        return CDCResponse(request_id=request_id, body=body)
