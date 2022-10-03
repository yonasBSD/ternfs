#!/usr/bin/env python3

import argparse
import functools
import itertools
import math
import posixpath
import pprint
import socket
import struct
import sys
import time
from typing import Callable, Dict, List, Optional, Tuple, Union, TypeVar
import logging
from pathlib import Path

import bincode
import crypto
from cdc_msgs import *
from shard_msgs import *
from common import *


LOCAL_HOST = '127.0.0.1'

def send_shard_request(shard: int, req_body: ShardRequestBody, key: Optional[crypto.ExpandedKey] = None, timeout_secs: float = 2.0) -> ShardResponseBody:
    assert (key is not None) == req_body.kind.is_privileged()
    port = shard_to_port(shard)
    ver = PROTOCOL_VERSION
    request_id = eggs_time()
    target = target = (LOCAL_HOST, port)
    req = ShardRequest(ver, request_id, req_body)
    packed_req = bincode.pack(req)
    if key is not None:
        packed_req = crypto.add_mac(packed_req, key)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(timeout_secs)
        try:
            packed_resp = sock.recv(UDP_MTU)
        except socket.timeout:
            return EggsError(ErrCode.TIMEOUT)
    response = bincode.unpack(ShardResponse, packed_resp)
    assert request_id == response.request_id
    logging.debug(f'Sent shard request {req}')
    logging.debug(f'Got shard response {response}')
    if isinstance(response.body, EggsError):
        raise response.body
    return response.body

def send_cdc_request(req_body: CDCRequestBody) -> CDCResponseBody:
    request_id = eggs_time()
    target = target = (LOCAL_HOST, CDC_PORT)
    req = CDCRequest(PROTOCOL_VERSION, request_id, req_body)
    packed_req = bincode.pack(req)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(2.0)
        packed_resp = sock.recv(UDP_MTU)
    response = bincode.unpack(CDCResponse, packed_resp)
    assert request_id == response.request_id
    if isinstance(response.body, EggsError):
        print(f'Got error {response.body} for req {req_body}')
        raise response.body
    return response.body

def lookup_internal(dir_id: int, name: str) -> int:
    resp = send_shard_request(inode_id_shard(dir_id), LookupReq(dir_id=dir_id, name=name.encode('ascii')))
    assert isinstance(resp, LookupResp)
    return resp.target_id

def lookup_raw(dir_id: int, name: str):
    inode = lookup_internal(dir_id, name)
    print(f'inode id:    0x{inode:016X}')
    print(f'type:        {repr(inode_id_type(inode))}')
    print(f'shard:       {inode_id_shard(inode)}')

def stat_raw(id: int):
    resp = send_shard_request(inode_id_shard(id), StatReq(id))
    assert isinstance(resp, StatResp)
    print(f'mtime:       {eggs_time_str(resp.mtime)}')
    if inode_id_type(id) == InodeType.DIRECTORY:
        print(f'owner:       0x{resp.size_or_owner:016X}')
    else:
        print(f'size:        {resp.size_or_owner}')

def stat(path: Path):
    return stat_raw(lookup(path))

def lookup_abs_path(path: Path):
    if path.parts[0] != '/':
        raise ValueError('Path must be absolute')
    cur_inode = ROOT_DIR_INODE_ID
    for part in path.parts[1:]:
        cur_inode = lookup_internal(cur_inode, part)
    return cur_inode

def lookup(path: Path) -> int:
    inode = lookup_abs_path(path)
    print(f'inode id:    0x{inode:016X}')
    print(f'type:        {repr(inode_id_type(inode))}')
    print(f'shard:       {inode_id_shard(inode)}')
    return inode

def mkdir_raw(owner_id: int, name: str) -> None:
    send_cdc_request(MakeDirReq(owner_id=owner_id, name=name.encode('ascii')))

def mkdir(path: Path) -> None:
    owner_id = lookup_abs_path(path.parent)
    # before talking to the cross-dir co-ordinator, check whether this
    # subdirectory already exists.
    # this ensures we're not wasting the rename co-ordinator's time
    try:
        lookup_internal(owner_id, path.name)
        raise EggsError(ErrCode.CANNOT_OVERRIDE_NAME)
    except EggsError as err:
        if err.error_code != ErrCode.DIRECTORY_NOT_FOUND:
            raise err
    return mkdir_raw(owner_id, path.name)

def rm_file_raw(owner_id: int, file_id: int, name: str) -> None:
    send_shard_request(inode_id_shard(owner_id), SoftUnlinkFileReq(owner_id, file_id, name.encode('ascii')))

def rm_file(path: Path) -> None:
    dir_id = lookup_abs_path(path.parent)
    file_id = lookup_internal(dir_id, path.name)
    rm_file_raw(dir_id, file_id, path.name)

def rm_dir_raw(owner_id: int, dir_id: int, name: str) -> None:
    send_cdc_request(UnlinkDirectoryReq(owner_id=owner_id, target_id=dir_id, name=name.encode('ascii')))

def rm_dir(path: Path) -> None:
    owner_id = lookup_abs_path(path.parent)
    dir_id = lookup_internal(owner_id, path.name)
    rm_dir_raw(owner_id, dir_id, path.name)

def same_dir_mv(target_id: int, dir_id: int, old: str, new: str) -> None:
    send_shard_request(inode_id_shard(dir_id), SameDirectoryRenameReq(dir_id=dir_id, target_id=target_id, old_name=old.encode('ascii'), new_name=new.encode('ascii')))

def mv_raw(target_id: int, old_owner_id: int, old_name: str, new_owner_id: int, new_name: str):
    if inode_id_type(target_id) == InodeType.FILE:
        send_cdc_request(RenameFileReq(target_id, old_owner_id, old_name.encode('ascii'), new_owner_id, new_name.encode('ascii')))
    else:
        send_cdc_request(RenameDirectoryReq(target_id, old_owner_id, old_name.encode('ascii'), new_owner_id, new_name.encode('ascii')))

def mv(a: Path, b: Path) -> None:
    owner_a = lookup_abs_path(a.parent)
    a_id = lookup_internal(owner_a, a.name)
    owner_b = lookup_abs_path(b.parent)
    if owner_a == owner_b:
        same_dir_mv(dir_id=owner_a, target_id=a_id, old=a.name, new=b.name)
    elif inode_id_type(a_id) == InodeType.FILE:
        send_cdc_request(RenameFileReq(a_id, owner_a, a.name.encode('ascii'), owner_b, b.name.encode('ascii')))
    else:
        send_cdc_request(RenameDirectoryReq(a_id, owner_a, a.name.encode('ascii'), owner_b, b.name.encode('ascii')))

def readdir(id: int):
    continuation_key = 0
    while True:
        resp = send_shard_request(inode_id_shard(id), ReadDirReq(id, continuation_key))
        assert isinstance(resp, ReadDirResp)
        for result in resp.results:
            s = result.name.decode("ascii") + ("/" if inode_id_type(result.target_id) == InodeType.DIRECTORY else "")
            print(f'{s: <20} 0x{result.target_id:016X}')
        continuation_key = resp.continuation_key
        if continuation_key == 0:
            break

def ls(dir: Path) -> None:
    id = lookup(dir)
    print('')
    readdir(id)

# Writes block, returns proof
def write_block(*, block: BlockInfo, data: bytes, crc32: bytes) -> bytes:
    header = struct.pack('<cQ4sI8s', b'w', block.block_id, crc32, len(data), block.certificate)
    with socket.create_connection((socket.inet_ntoa(block.ip), block.port)) as conn:
        conn.send(header + data)
        resp = conn.recv(17)
        assert len(resp) == 17
    proof: bytes
    rkind, rblock_id, proof = struct.unpack('<cQ8s', resp)
    if rkind != b'W':
        raise RuntimeError(f'Unexpected response kind {rkind} {resp!r}')
    if rblock_id != block.block_id:
        raise RuntimeError(f'Bad block id, expected {block.block_id} got {rblock_id}')
    return proof

def read_block(block: FetchedBlock) -> bytes:
    ip = socket.inet_ntoa(block.ip)
    msg = struct.pack('<cQ', b'f', block.block_id)
    with socket.create_connection((ip, block.port)) as conn:
        conn.send(msg)
        reply = conn.recv(5)
        assert len(reply) == 5
        kind, ret_block_sz = struct.unpack('<cI', reply)
        if kind != b'F':
            raise Exception(f'Bad reply {reply!r}')
        if ret_block_sz != block.size:
            raise Exception(f'Block {block.block_id} inconsistent size: metadata says {block.size}, block service says {ret_block_sz}')
        # TODO check crc32
        data = b''
        while len(data) < block.size:
            data += conn.recv(block.size - len(data))
        return data

# No mirroring for now, 4k blocks, just so that we create many blocks.
PARITY = create_parity_mode(1, 0)
STORAGE_CLASS = 2

def create_file(name: Path, blob: bytes):
    dir_id = lookup_abs_path(name.parent)
    shard = inode_id_shard(dir_id)
    transient_file = send_shard_request(shard, ConstructFileReq(InodeType.FILE))
    assert isinstance(transient_file, ConstructFileResp)
    file_id = transient_file.id
    cookie = transient_file.cookie
    for ix in range(0, len(blob), 1<<12):
        data = blob[ix:ix+(1<<12)]
        crc32 = crypto.crc32c(data)
        size = len(data)
        span = send_shard_request(
            shard,
            AddSpanInitiateReq(
                file_id=file_id,
                cookie=cookie,
                byte_offset=ix,
                storage_class=STORAGE_CLASS,
                parity=PARITY,
                crc32=crc32,
                size=size,
                body=[NewBlockInfo(crc32, size)]
            )
        )
        assert isinstance(span, AddSpanInitiateResp)
        assert len(span.blocks) == 1
        block = span.blocks[0]
        block_proof = write_block(block=block, data=data, crc32=crc32)
        send_shard_request(
            shard,
            AddSpanCertifyReq(
                file_id=file_id,
                cookie=cookie,
                byte_offset=ix,
                proofs=[block_proof],
            )
        )
    send_shard_request(shard, LinkFileReq(file_id, cookie, dir_id, name.name.encode('ascii')))


def create_file_from_str(name: Path, str: str):
    return create_file(name, str.encode('utf-8'))

def create_file_from_file(name: Path, file: Path):
    with open(file, mode='rb') as f:
        return create_file(name, f.read())

def cat(name: Path):
    file_id = lookup_abs_path(name)
    byte_offset = 0
    while True:
        resp = send_shard_request(inode_id_shard(file_id), FileSpansReq(file_id=file_id, byte_offset=byte_offset))
        assert isinstance(resp, FileSpansResp)
        for span in resp.spans:
            assert span.parity == PARITY
            assert span.storage_class == STORAGE_CLASS
            assert isinstance(span.body, list)
            assert len(span.body) == 1
            sys.stdout.buffer.write(read_block(span.body[0]))
        byte_offset = resp.next_offset
        if byte_offset == 0:
            break
    sys.stdout.flush()
            

def transient_files():
    for shard in range(256):
        begin_id = 0
        while True:
            resp = send_shard_request(shard, VisitTransientFilesReq(begin_id))
            assert isinstance(resp, VisitTransientFilesResp)
            for f in resp.files:
                print(f'inode id:    0x{f.id:016X}')
                print(f'type:        {repr(inode_id_type(f.id))}')
                print(f'shard:       {shard}')
            begin_id = resp.next_id
            if begin_id == 0:
                break

# From command line commands, to top-level function and types of args
HUMAN_COMMANDS: Dict[str, Tuple[Callable, List[Type]]] = {
    'lookup': (lookup, [Path]),
    'mkdir': (mkdir, [Path]),
    'stat': (stat, [Path]),
    'ls': (ls, [Path]),
    'echo_into': (create_file_from_str, [Path, str]),
    'transient_files': (transient_files, []),
    'cat': (cat, [Path]),
    'copy_into': (create_file_from_file, [Path, Path]),
    'rm_file': (rm_file, [Path]),
    'rm_dir': (rm_dir, [Path]),
    'mv': (mv, [Path, Path]),
}

RAW_COMMANDS: Dict[str, Tuple[Callable, List[Type]]] = {
    'lookup': (lookup_raw, [int, str]),
    'mkdir': (mkdir_raw, [int, str]),
    'stat': (stat_raw, [int]),
    'readdir': (readdir, [int]),
    'transient_files': (transient_files, []),
    'rm_file': (rm_file_raw, [int, int, str]),
    'rm_dir': (rm_dir_raw, [int, int, str]),
    'same_dir_mv': (same_dir_mv, [int, int, str, str]),
    'mv': (mv_raw, [int, int, str, int, str]),
}

def main() -> None:
    global VERBOSE
    parser = argparse.ArgumentParser(description='A basic eggsfs client for doing simple ops')
    parser.add_argument('command', choices=(HUMAN_COMMANDS.keys()|RAW_COMMANDS.keys()))
    parser.add_argument('-r', '--raw', action='store_true', help='Use operations taking the same arguments as the shard_msg/cdc_msg, rather than human friendly versions.')
    parser.add_argument('arguments', nargs='*')
    config = parser.parse_args(sys.argv[1:])

    fun, args_types = (RAW_COMMANDS if config.raw else HUMAN_COMMANDS)[config.command]
    assert len(args_types) == len(config.arguments), f'Expected {len(args_types)} arguments, got {len(config.arguments)}.'
    args = map(lambda x: int(x[0], 0) if x[1] == int else x[1](x[0]), zip(config.arguments, args_types))
    fun(*args)


if __name__ == '__main__':
    main()
