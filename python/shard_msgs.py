#!/usr/bin/env python3

from dataclasses import dataclass, field, replace
import enum
import itertools
from typing import ClassVar, Dict, List, NamedTuple, Optional, Tuple, Type, Union, Set
import traceback

import bincode
import crypto
from common import *
from error import *
from msgs import *

SHARD_PROTOCOL_VERSION = b'SHA\0'

INLINE_STORAGE = 0
ZERO_FILL_STORAGE = 1

class BlockFlags(enum.IntFlag):
    STALE = 1
    TERMINAL = 2

def ReadDirReqNow(dir_id: int, start_hash: int) -> ReadDirReq:
    return ReadDirReq(dir_id, start_hash, as_of_time=0xFFFFFFFFFFFFFFFF)

# INTERNAL_ERROR/FATAL_ERROR/TIMEOUT are implicitly included in all of these
SHARD_ERRORS: Dict[ShardMessageKind, Set[ErrCode]] = {
    ShardMessageKind.LOOKUP: {ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND},
    ShardMessageKind.STAT: {ErrCode.DIRECTORY_NOT_FOUND, ErrCode.FILE_NOT_FOUND},
    ShardMessageKind.READ_DIR: {ErrCode.DIRECTORY_NOT_FOUND},
    ShardMessageKind.CONSTRUCT_FILE: {ErrCode.TYPE_IS_DIRECTORY},
    ShardMessageKind.ADD_SPAN_INITIATE: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.INCONSISTENT_STORAGE_CLASS_PARITY,
        ErrCode.LAST_SPAN_STATE_NOT_CLEAN, ErrCode.COULD_NOT_PICK_BLOCK_SERVERS,
        ErrCode.BAD_SPAN_BODY, ErrCode.SPAN_NOT_FOUND, ErrCode.BLOCK_SERVER_NOT_FOUND,
    },
    ShardMessageKind.ADD_SPAN_CERTIFY: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.CANNOT_CERTIFY_BLOCKLESS_SPAN,
        ErrCode.BAD_NUMBER_OF_BLOCKS_PROOFS, ErrCode.BLOCK_SERVER_NOT_FOUND, ErrCode.BAD_BLOCK_PROOF,
    },
    ShardMessageKind.LINK_FILE: {
        ErrCode.FILE_NOT_FOUND, ErrCode.BAD_COOKIE, ErrCode.DIRECTORY_NOT_FOUND,
        ErrCode.LAST_SPAN_STATE_NOT_CLEAN, ErrCode.CANNOT_OVERRIDE_NAME, ErrCode.NAME_IS_LOCKED,
        # This should be incredibly rare barring bad snapshot edges
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    ShardMessageKind.SOFT_UNLINK_FILE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.NAME_IS_LOCKED, ErrCode.TYPE_IS_DIRECTORY,
    },
    ShardMessageKind.FILE_SPANS: {
        ErrCode.FILE_NOT_FOUND, ErrCode.FILE_IS_TRANSIENT,
    },
    ShardMessageKind.SAME_DIRECTORY_RENAME: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_NOT_FOUND, ErrCode.MISMATCHING_TARGET,
        ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS, ErrCode.NAME_IS_LOCKED, ErrCode.CANNOT_OVERRIDE_NAME,
    },
    ShardMessageKind.CREATE_DIRECTORY_INODE: {
        ErrCode.TYPE_IS_NOT_DIRECTORY, ErrCode.MISMATCHING_OWNER,
    },
    ShardMessageKind.SET_DIRECTORY_OWNER: {
        ErrCode.DIRECTORY_NOT_EMPTY, ErrCode.DIRECTORY_NOT_FOUND,
    },
    ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.NAME_IS_LOCKED, ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
    },
    ShardMessageKind.LOCK_CURRENT_EDGE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.MISMATCHING_TARGET, ErrCode.NAME_NOT_FOUND,
    },
    ShardMessageKind.REMOVE_NON_OWNED_EDGE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.EDGE_NOT_FOUND,
    },
    ShardMessageKind.REMOVE_OWNED_SNAPSHOT_FILE_EDGE: {
        ErrCode.TYPE_IS_DIRECTORY, ErrCode.DIRECTORY_NOT_FOUND, ErrCode.EDGE_NOT_FOUND,
    },
    ShardMessageKind.REMOVE_INODE: {
        ErrCode.DIRECTORY_NOT_FOUND, ErrCode.DIRECTORY_NOT_EMPTY, ErrCode.DIRECTORY_HAS_OWNER, ErrCode.CANNOT_REMOVE_ROOT_DIRECTORY,
        ErrCode.FILE_NOT_FOUND, ErrCode.FILE_NOT_EMPTY, ErrCode.FILE_IS_NOT_TRANSIENT,
    },
}

def kind_is_privileged(k: ShardMessageKind) -> bool:
    return bool(k & 0x80)

assert FileSpansResp.STATIC_SIZE == 2
FileSpansResp_SIZE_UPPER_BOUND = 2 + 8

@dataclass
class ShardRequest:
    request_id: int
    body: ShardRequestBody

    def pack(self, cdc_key: Optional[crypto.ExpandedKey] = None) -> bytes:
        b = bytearray()
        bincode.pack_fixed_into(SHARD_PROTOCOL_VERSION, len(SHARD_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.KIND, b)
        self.body.pack_into(b)
        if kind_is_privileged(self.body.KIND):
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
            request=None, # type: ignore
        )
        try:
            kind = ShardMessageKind(bincode.unpack_u8(u))
            body_type = SHARD_REQUESTS[kind][0]
            body = body_type.unpack(u)
        except Exception as err:
            # TODO it would be good to distinguish between actual
            # decode errors and internal exceptions here.
            return replace(req, request=EggsError(ErrCode.MALFORMED_REQUEST))
        if kind_is_privileged(kind):
            assert cdc_key
            # Do not return spurious NOT_AUTHORISED
            if u.idx + 8 != len(bs):
                return replace(req, request=EggsError(ErrCode.MALFORMED_REQUEST))
            req_bytes = u.data[:u.idx]
            mac = bincode.unpack_fixed(u, 8)
            if crypto.compute_mac(req_bytes, cdc_key) != mac:
                return replace(req, request=EggsError(ErrCode.NOT_AUTHORISED))
        if u.idx != len(bs):
            return replace(req, request=EggsError(ErrCode.MALFORMED_REQUEST))
        return replace(req, request=ShardRequest(request_id=request_id, body=body))

@dataclass
class ShardResponse(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = len(SHARD_PROTOCOL_VERSION) + 8 + 1
    request_id: int
    body: Union[EggsError, ShardResponseBody]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(SHARD_PROTOCOL_VERSION, len(SHARD_PROTOCOL_VERSION), b)
        bincode.pack_u64_into(self.request_id, b)
        bincode.pack_u8_into(self.body.KIND, b)
        self.body.pack_into(b)

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ShardResponse':
        ver = bincode.unpack_fixed(u, len(SHARD_PROTOCOL_VERSION))
        assert ver == SHARD_PROTOCOL_VERSION
        request_id = bincode.unpack_u64(u)
        resp_kind = bincode.unpack_u8(u)
        body: Union[EggsError, ShardResponseBody]
        if resp_kind == EggsError.KIND:
            body = EggsError.unpack(u)
        else:
            body = SHARD_REQUESTS[ShardMessageKind(resp_kind)][1].unpack(u)
        return ShardResponse(request_id, body)
