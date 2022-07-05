#!/usr/bin/env python3

import enum
from typing import NamedTuple, Optional, Union

import bincode


class RequestKind(enum.IntEnum):
    # values derrived from metadata_msgs.rs
    MK_DIR = 10
    RESOLVE = 13


class ResponseKind(enum.IntEnum):
    MK_DIR = 0
    RESOLVE = 1


class ResolveReq(NamedTuple):
    parent_id: int
    subname: str
    ts: int


class MkDirReq(NamedTuple):
    parent_id: int
    subname: str


ReqBodyTy = Union[ResolveReq, MkDirReq]


class MetadataRequest(NamedTuple):
    ver: int
    request_id: int
    body: ReqBodyTy


def pack_request(r: MetadataRequest) -> bytearray:
    b = bytearray()
    bincode.pack_unsigned_into(r.ver, b)
    bincode.pack_unsigned_into(r.request_id, b)
    if isinstance(r.body, ResolveReq):
        bincode.pack_unsigned_into(RequestKind.RESOLVE, b)
        bincode.pack_unsigned_into(r.body.parent_id, b)
        bincode.pack_bytes_into(r.body.subname.encode(), b)
        bincode.pack_unsigned_into(r.body.ts, b)
    else:
        raise ValueError(f'Unrecognised body: {r.body}')
    return b


def unpack_request(b: bytes) -> MetadataRequest:
    u = bincode.UnpackWrapper(b)
    ver = bincode.unpack_unsigned(u)
    request_id = bincode.unpack_unsigned(u)
    kind = bincode.unpack_unsigned(u)
    if kind == RequestKind.RESOLVE:
        parent_id = bincode.unpack_unsigned(u)
        subname_len = bincode.unpack_unsigned(u)
        subname = u.read()[:subname_len].decode()
        u.advance(subname_len)
        ts = bincode.unpack_unsigned(u)
        body = ResolveReq(parent_id, subname, ts)
    else:
        raise ValueError(f'Unrecognised kind: {kind}')
    if u.idx != len(b):
        raise ValueError(f'Extra bytes found after request: {b[u.idx:]!r}')
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


class MetadataError(NamedTuple):
    kind: MetadataErrorKind
    text: str


class ResolvedInode(NamedTuple):
    id: int
    creation_time: int
    deletion_time: int
    is_file: bool


class ResolveResp(NamedTuple):
    f: Optional[ResolvedInode]


RespBodyTy = Union[MetadataError, ResolveResp]


class MetadataResponse(NamedTuple):
    request_id: int
    body: RespBodyTy


def pack_response(r: MetadataResponse) -> bytearray:
    b = bytearray()
    bincode.pack_unsigned_into(r.request_id, b)
    if isinstance(r.body, MetadataError):
        bincode.pack_unsigned_into(1, b) # Result::Err
        bincode.pack_unsigned_into(r.body.kind, b)
        bincode.pack_bytes_into(r.body.text.encode(), b)
        return b
    bincode.pack_unsigned_into(0, b) # Result::Ok
    if isinstance(r.body, ResolveResp):
        bincode.pack_unsigned_into(ResponseKind.RESOLVE, b)
        if r.body.f is None:
            bincode.pack_unsigned_into(0, b) # Option::None
        else:
            bincode.pack_unsigned_into(1, b) # Option::Some
            bincode.pack_unsigned_into(r.body.f.id, b)
            bincode.pack_unsigned_into(r.body.f.creation_time, b)
            bincode.pack_unsigned_into(r.body.f.deletion_time, b)
            bincode.pack_unsigned_into(r.body.f.is_file, b)
    else:
        raise ValueError(f'Unrecognised body: {r.body}')
    return b


def unpack_response(b: bytes) -> MetadataResponse:
    u = bincode.UnpackWrapper(b)
    request_id = bincode.unpack_unsigned(u)
    result_kind = bincode.unpack_unsigned(u)
    body: RespBodyTy
    if result_kind == 0:
        # Result::Ok
        response_kind = bincode.unpack_unsigned(u)
        if response_kind == ResponseKind.RESOLVE:
            option_kind = bincode.unpack_unsigned(u)
            if option_kind == 0:
                # Option::None
                body = ResolveResp(None)
            else:
                # Option::Some
                id = bincode.unpack_unsigned(u)
                creation_time = bincode.unpack_unsigned(u)
                deletion_time = bincode.unpack_unsigned(u)
                is_file = bool(bincode.unpack_unsigned(u))
                body = ResolveResp(ResolvedInode(
                    id,
                    creation_time,
                    deletion_time,
                    is_file,
                ))
        else:
            raise ValueError(f'Unrecognised response kind: {response_kind}')
    else:
        # Result::Err
        error_kind = MetadataErrorKind(bincode.unpack_unsigned(u))
        text = bincode.unpack_bytes(u).decode()
        body = MetadataError(error_kind, text)
    if u.idx != len(b):
        raise ValueError(f'Extra bytes found after request: {b[u.idx:]!r}')
    return MetadataResponse(request_id, body)


def __tests() -> None:
    packed = bytes([0, 123, 13, 251, 0, 2, 11, 104, 101, 108, 108, 111, 95, 119,
        111, 114, 108, 100, 251, 57, 48])

    unpacked = MetadataRequest(ver=0, request_id=123,
        body=ResolveReq(parent_id=512, subname='hello_world', ts=12345))

    unpack_res = unpack_request(packed)

    assert(unpack_res == unpacked)

    pack_res = pack_request(unpacked)

    assert(pack_res == packed)

    packed2 = bytes([251, 200, 1, 0, 1, 1, 251, 41, 35, 251, 164, 9, 251, 251,
        13, 1])

    unpacked2 = MetadataResponse(
        request_id=456,
        body=ResolveResp(f=ResolvedInode(
            id=9001, creation_time=2468, deletion_time=3579, is_file=True)))

    unpack_res2 = unpack_response(packed2)

    assert(unpack_res2 == unpacked2)

    pack_res2 = pack_response(unpacked2)

    assert(pack_res2 == packed2)


if __name__ == '__main__':
    __tests()
