from dataclasses import dataclass
from typing import ClassVar
import errno

from msgs import *

@dataclass
class EggsError(bincode.Packable, Exception):
    KIND: ClassVar[int] = 0
    error_code: ErrCode
    # info: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u16_into(self.error_code, b)
        # bincode.pack_bytes_into(self.info, b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'EggsError':
        error_kind = ErrCode(bincode.unpack_u16(u))
        # info = bincode.unpack_bytes(u)
        # return EggsError(error_kind, info)
        return EggsError(error_kind)

ERR_CODE_TO_ERRNO: Dict[ErrCode, int] = {
    ErrCode.INTERNAL_ERROR: errno.EIO,
    ErrCode.FATAL_ERROR: errno.EIO,
    ErrCode.TIMEOUT: errno.EIO,
    ErrCode.NOT_AUTHORISED: errno.EACCES,
    ErrCode.UNRECOGNIZED_REQUEST: errno.EIO,
    ErrCode.FILE_NOT_FOUND: errno.ENOENT,
    ErrCode.DIRECTORY_NOT_FOUND: errno.ENOENT,
    ErrCode.NAME_NOT_FOUND: errno.ENOENT,
    ErrCode.TYPE_IS_DIRECTORY: errno.EISDIR,
    ErrCode.TYPE_IS_NOT_DIRECTORY: errno.ENOTDIR,
    ErrCode.BAD_COOKIE: errno.EACCES,
    ErrCode.INCONSISTENT_STORAGE_CLASS_PARITY: errno.EINVAL,
    ErrCode.LAST_SPAN_STATE_NOT_CLEAN: errno.EBUSY, # reasonable?
    ErrCode.COULD_NOT_PICK_BLOCK_SERVICES: errno.EIO,
    ErrCode.BAD_SPAN_BODY: errno.EINVAL,
    ErrCode.SPAN_NOT_FOUND: errno.EINVAL,
    ErrCode.BLOCK_SERVICE_NOT_FOUND: errno.EIO,
    ErrCode.CANNOT_CERTIFY_BLOCKLESS_SPAN: errno.EINVAL,
    ErrCode.BAD_NUMBER_OF_BLOCKS_PROOFS: errno.EINVAL,
    ErrCode.BAD_BLOCK_PROOF: errno.EINVAL,
    ErrCode.CANNOT_OVERRIDE_NAME: errno.EEXIST,
    ErrCode.NAME_IS_LOCKED: errno.EEXIST,
    ErrCode.OLD_NAME_IS_LOCKED: errno.EBUSY,
    ErrCode.NEW_NAME_IS_LOCKED: errno.EBUSY,
    ErrCode.MTIME_IS_TOO_RECENT: errno.EBUSY, # reasonable?
    ErrCode.MISMATCHING_TARGET: errno.EINVAL,
    ErrCode.MISMATCHING_OWNER: errno.EINVAL,
    ErrCode.DIRECTORY_NOT_EMPTY: errno.ENOTEMPTY,
    ErrCode.FILE_IS_TRANSIENT: errno.EBUSY, # reasonable?
    ErrCode.OLD_DIRECTORY_NOT_FOUND: errno.ENOENT,
    ErrCode.NEW_DIRECTORY_NOT_FOUND: errno.ENOENT,
    ErrCode.LOOP_IN_DIRECTORY_RENAME: errno.ELOOP,
}
