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
from typing import Callable, Collection, Dict, List, NamedTuple, Optional, Tuple

import bincode
import crypto
import cross_dir_msgs
import metadata_msgs
from metadata_msgs import ResolveMode
import metadata_utils
from metadata_utils import InodeType


LOCAL_HOST = '127.0.0.1'
PROTOCOL_VER = 0
VERBOSE = False


def send_request(req_body: metadata_msgs.ReqBodyTy, shard: int,
    key: Optional[crypto.ExpandedKey] = None, timeout_secs: float = 2.0
    ) -> metadata_msgs.RespBodyTy:

    assert (key is not None) == req_body.kind.is_privilaged()
    port = metadata_utils.shard_to_port(shard)
    ver = PROTOCOL_VER
    request_id = int(time.time())
    target = target = (LOCAL_HOST, port)
    req = metadata_msgs.MetadataRequest(ver, request_id, req_body)
    packed_req = bincode.pack(req)
    if key is not None:
        packed_req = crypto.add_mac(packed_req, key)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(timeout_secs)
        try:
            packed_resp = sock.recv(metadata_utils.UDP_MTU)
        except socket.timeout:
            return metadata_msgs.MetadataError(
                metadata_msgs.MetadataErrorKind.TIMED_OUT,
                f'timed out after {timeout_secs} secs'
            )
    response = bincode.unpack(metadata_msgs.MetadataResponse, packed_resp)
    if request_id != response.request_id:
        raise Exception('Bad request_id from server:'
            f' expected {request_id} got {response.request_id}')
    if VERBOSE:
        print('>', req)
        print('<', response, end='\n\n')
    return response.body


def cross_dir_request(req_body: cross_dir_msgs.ReqBodyTy
    ) -> cross_dir_msgs.CrossDirResponse:

    ver = PROTOCOL_VER
    request_id = int(time.time())
    target = target = (LOCAL_HOST, metadata_utils.CROSS_DIR_PORT)
    req = cross_dir_msgs.CrossDirRequest(ver, request_id, req_body)
    packed_req = bincode.pack(req)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(2.0)
        packed_resp = sock.recv(metadata_utils.UDP_MTU)
    response = bincode.unpack(cross_dir_msgs.CrossDirResponse, packed_resp)
    if request_id != response.request_id:
        raise Exception('Bad request_id from server:'
            f' expected {request_id} got {response.request_id}')
    return response


def resolve(parent_id: int, subname: str, creation_ts: int = 0,
    mode: ResolveMode = ResolveMode.ALIVE) -> Optional[int]:

    req_body = metadata_msgs.ResolveReq(
        mode=mode,
        parent_id=parent_id,
        creation_ts=creation_ts,
        subname=subname,
    )
    resp_body = send_request(req_body, metadata_utils.shard_from_inode(parent_id))
    if not isinstance(resp_body, metadata_msgs.ResolveResp):
        assert isinstance(resp_body, metadata_msgs.MetadataError)
        if resp_body.error_kind == metadata_msgs.MetadataErrorKind.NOT_FOUND:
            return None
        raise Exception(f'Bad response from server:'
            f' expected ResolveResp got {resp_body}')
    return metadata_utils.strip_ownership_bit(resp_body.inode_with_ownership)


def stat(inode: int) -> Optional[metadata_msgs.StatResp]:
    req_body = metadata_msgs.StatReq(
        inode,
    )
    resp_body = send_request(req_body, metadata_utils.shard_from_inode(inode))
    if not isinstance(resp_body, metadata_msgs.StatResp):
        if (isinstance(resp_body, metadata_msgs.MetadataError)
            and resp_body.error_kind == metadata_msgs.MetadataErrorKind.NOT_FOUND):
            return None
        raise Exception(f'Bad response from stat: {resp_body}')
    return resp_body


def resolve_abs_path(path: str) -> int:

    bits = path.split('/')
    if bits[0] != "":
        raise ValueError('Path must start with /')

    # start at root
    cur_inode = metadata_utils.ROOT_INODE

    if path == '/':
        return cur_inode

    for i, bit in enumerate(bits[1:]):
        if metadata_utils.type_from_inode(cur_inode) != InodeType.DIRECTORY:
            filename = '/'.join(bits[:i+1])
            raise ValueError(f"'{filename}' is not a directory")

        next_inode = resolve(cur_inode, bit)

        if next_inode is None:
            subdirname = '/'.join(bits[:i+2])
            raise ValueError(f'No such file or directory {subdirname}')

        cur_inode = next_inode

    return cur_inode


def do_resolve(mode: metadata_msgs.ResolveMode, parent_inode: int,
    name: str, arg2: str) -> None:
    creation_ts = int(arg2 or '0')
    inode: Optional[int]
    if name == '':
        if mode == metadata_msgs.ResolveMode.ALIVE:
            inode = parent_inode
        else:
            inode = None
    else:
        inode = resolve(parent_inode, name, creation_ts, mode)
    print(inode)


def do_mkdir(parent_inode: int, name: str, arg2: str) -> None:
    if arg2 != '':
        raise ValueError('No arg2 for mkdir')

    # before talking to the cross-dir co-ordinator, check whether this
    # subdirectory already exists
    # this ensures we're not wasting the rename co-ordinator's time

    inode = resolve(parent_inode, name)
    if inode is not None:
        # if this inode is a directory, return success (ensures idempotency)
        if metadata_utils.type_from_inode(inode) == InodeType.DIRECTORY:
            print(f'Created successfully: {inode}')
            return
        raise Exception(f'Target is not a directory: {inode}')

    body = cross_dir_msgs.MkDirReq(parent_inode, name)
    response = cross_dir_request(body)
    print(response)


def do_rmdir(parent_inode: int, name: str, arg2: str) -> None:
    if arg2 != '':
        raise ValueError('No arg2 for rmdir')

    body = cross_dir_msgs.RmDirReq(parent_inode, name)
    response = cross_dir_request(body)
    print(response)


def do_stat(parent_inode: int, name: str, arg2: str) -> None:
    if arg2 != '':
        raise ValueError('No arg2 for stat')

    inode: Optional[int]
    if parent_inode == metadata_utils.ROOT_INODE and name == '':
        inode = metadata_utils.ROOT_INODE
    else:
        inode = resolve(parent_inode, name)
    if inode is None:
        raise Exception(f'resolve ({parent_inode}, "{name}") returned None')

    resp = stat(inode)
    if resp is None:
        print('Not found')
        return
    print(f'inode=0x{inode:016X}')
    type = metadata_utils.type_from_inode(inode)
    print(f'type={type.name}')
    print(f'mtime={resp.mtime}')
    if type == InodeType.DIRECTORY:
        print(f'parent=0x{resp.size_or_parent:016X}')
        print(f'opaque={resp.opaque!r}')
    else:
        print(f'size={resp.size_or_parent}')


def do_rm(parent_inode: int, name: str, arg2: str) -> None:
    if arg2 != '':
        raise ValueError('No arg2 for rm')

    resp = send_request(
        metadata_msgs.DeleteFileReq(
            parent_inode,
            name
        ),
        metadata_utils.shard_from_inode(parent_inode)
    )
    print(resp)

def same_dir_mv(parent_inode: int, old_name: str, new_name: str) -> None:
    resp = send_request(
        metadata_msgs.SameDirRenameReq(
            parent_inode,
            old_name,
            new_name
        ),
        metadata_utils.shard_from_inode(parent_inode)
    )
    print(resp)

def cross_dir_mv(old_parent: int, new_parent: int, old_name: str, new_name: str
    ) -> None:
    resolved = resolve(old_parent, old_name)
    if resolved is None:
        raise Exception(f'Failed to resolve ({old_parent}, {old_name})')

    if metadata_utils.type_from_inode(resolved) == InodeType.DIRECTORY:
        resp = cross_dir_request(
            cross_dir_msgs.MvDirReq(old_parent, new_parent, old_name, new_name)
        )
    else:
        resp = cross_dir_request(
            cross_dir_msgs.MvFileReq(old_parent, new_parent, old_name, new_name)
        )
    print(resp)

def do_mv(old_parent_inode: int, old_name: str, arg2: str) -> None:
    if not posixpath.isabs(arg2):
        raise ValueError('Only abs paths supported')
    new_parent_path = posixpath.dirname(arg2)
    new_name = posixpath.basename(arg2)

    new_parent_inode = resolve_abs_path(new_parent_path)
    if old_parent_inode == new_parent_inode:
        same_dir_mv(old_parent_inode, old_name, new_name)
    else:
        cross_dir_mv(old_parent_inode, new_parent_inode, old_name,
            new_name)

def do_ls(flags: metadata_msgs.LsFlags, parent_inode: int, name: str,
    arg2: str) -> None:
    creation_ts = int(arg2 or '0')

    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if metadata_utils.type_from_inode(inode) != InodeType.DIRECTORY:
        raise Exception('Target not a directory')

    continuation_key = 0
    shard = metadata_utils.shard_from_inode(inode)
    all_results: List[str] = []
    while True: # do..while(continuation_key != 0)
        resp = send_request(
            metadata_msgs.LsDirReq(
                inode,
                creation_ts,
                continuation_key,
                flags,
            ),
            shard
        )
        if not isinstance(resp, metadata_msgs.LsDirResp):
            raise Exception(f'Bad response: {resp}')
        all_results.extend(
            '[deleted]' if r.inode == metadata_utils.NULL_INODE else r.name
            for r in resp.results
        )
        continuation_key = resp.continuation_key
        if continuation_key == 0:
            break
    all_results.sort()
    pprint.pprint(all_results)


def do_cat(parent_inode: int, name: str, arg2: str) -> None:
    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if metadata_utils.type_from_inode(inode) == InodeType.DIRECTORY:
        raise Exception('Target is a directory')

    shard = metadata_utils.shard_from_inode(inode)
    next_offset = 0
    while True: # do..while(next_offset != 0)
        resp = send_request(
            metadata_msgs.FetchSpansReq(inode, next_offset), shard)
        if not isinstance(resp, metadata_msgs.FetchSpansResp):
            raise Exception(str(resp))
        next_offset = resp.next_offset
        for span in resp.spans:
            num_data_blocks = metadata_utils.num_data_blocks(span.parity)
            if num_data_blocks == 0:
                assert isinstance(span.payload, bytes)
                if span.storage_class == metadata_msgs.INLINE_STORAGE:
                    data = span.payload
                elif span.storage_class == metadata_msgs.ZERO_FILL_STORAGE:
                    data = b'\x00' * span.size
                else:
                    assert False
            else:
                assert isinstance(span.payload, list)
                data = bytearray(span.size)
                view = memoryview(data)
                data_idx = 0
                for block in span.payload[:num_data_blocks]:
                    ip = socket.inet_ntoa(block.ip)
                    msg = struct.pack('<cQ', b'f', block.block_id)
                    with socket.create_connection((ip, block.port)) as conn:
                        conn.send(msg)
                        reply = conn.recv(5)
                        if len(reply) != 5:
                            raise Exception('Runt reply')
                        kind, ret_block_sz = struct.unpack('<cI', reply)
                        if kind != b'F':
                            raise Exception(f'Bad reply {reply!r}')
                        if ret_block_sz != block.size:
                            raise Exception(
                                f'Block {block.block_id} inconsistent size:'
                                f' metadata says {block.size}'
                                f' block service says {ret_block_sz}')
                        next_block_idx = data_idx + ret_block_sz
                        while data_idx < next_block_idx:
                            data_idx += conn.recv_into(view[data_idx:], 0)
                        assert data_idx == next_block_idx
            print(data.decode(), end='')
        if next_offset == 0:
            break

def do_create_eden_file(parent_inode: int, name: str, arg2: str) -> None:

    if arg2 != '':
        raise ValueError('No arg2 for create eden')

    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if metadata_utils.type_from_inode(inode) != InodeType.DIRECTORY:
        raise Exception('Target not a directory')

    resp = send_request(
        metadata_msgs.CreateEdenFileReq(InodeType.FILE),
        metadata_utils.shard_from_inode(inode)
    )
    print(resp)


def do_extend_eden_deadline(parent_inode: int, name: str, arg2: str) -> None:

    if arg2 == '':
        raise ValueError('Need arg2 (cookie) for extend_eden_deadline')

    eden_inode = int(name)
    cookie = int(arg2)

    resp = send_request(
        metadata_msgs.AddEdenSpanReq(metadata_msgs.ZERO_FILL_STORAGE, 0,
            b'\x00' * 4, 0, 0, eden_inode, cookie, b''),
        metadata_utils.shard_from_inode(eden_inode)
    )
    print(resp)


# returns the offset of the expunged span
def expunge_one_span(eden_inode: int) -> int:
    shard = metadata_utils.shard_from_inode(eden_inode)

    resp = send_request(
        metadata_msgs.ExpungeEdenSpanReq(
            eden_inode
        ),
        shard
    )
    if not isinstance(resp, metadata_msgs.ExpungeEdenSpanResp):
        raise Exception(str(resp))
    offset = resp.offset
    if resp.span:
        proofs: List[Tuple[int, bytes]] = []
        for block_info in resp.span:
            payload = struct.pack('<cQ8s', b'e', block_info.block_id,
                block_info.certificate)
            ip = socket.inet_ntoa(block_info.ip)
            with socket.create_connection((ip, block_info.port)) as conn:
                conn.send(payload)
                block_server_reply = conn.recv(1024)
            if len(block_server_reply) != 17:
                raise RuntimeError(f'Bad response len {block_server_reply!r}')
            rkind, rblock_id, proof = struct.unpack('<cQ8s', block_server_reply)
            if rkind != b'E':
                raise RuntimeError(f'Unexpected response kind {rkind} {block_server_reply!r}')
            if rblock_id != block_info.block_id:
                raise RuntimeError(f'Bad block id, expected {block_info.block_id} got {rblock_id}')
            proofs.append((block_info.block_id, proof))
        resp = send_request(
            metadata_msgs.CertifyExpungeReq(
                eden_inode,
                offset,
                proofs
            ),
            shard
        )
    return offset


def do_expunge_span(parent_inode: int, name: str,
    arg2: str) -> None:
    eden_inode = int(arg2)
    print(expunge_one_span(eden_inode))


def do_expunge_file(parent_inode: int, name: str,
    arg2: str) -> None:
    eden_inode = int(arg2)
    offset = expunge_one_span(eden_inode)
    while offset != 0:
        offset = expunge_one_span(eden_inode)
    resp = send_request(
        metadata_msgs.ExpungeEdenFileReq(eden_inode),
        metadata_utils.shard_from_inode(eden_inode)
    )
    print(resp)


def do_create_test_file(parent_inode: int, name: str,
    arg2: str) -> None:

    second_block_sc = 2
    if arg2 != '':
        second_block_sc = int(arg2)
        assert 2 <= second_block_sc <= 255

    if name == '':
        raise ValueError('Cannot use empty name')

    shard = metadata_utils.shard_from_inode(parent_inode)

    resp = send_request(
        metadata_msgs.CreateEdenFileReq(InodeType.FILE),
        shard
    )

    if not isinstance(resp, metadata_msgs.CreateEdenFileResp):
        raise RuntimeError(f'{resp}')

    new_inode_id = resp.inode
    cookie = resp.cookie

    class Block(NamedTuple):
        data: bytes
        crc: bytes

    def convert_to_blocks(data: bytes, parity_mode: int) -> List[Block]:
        data_blocks = metadata_utils.num_data_blocks(parity_mode)
        parity_blocks = metadata_utils.num_parity_blocks(parity_mode)
        total_blocks = data_blocks + parity_blocks
        if data_blocks == 0:
            raise Exception('Need at least one data block')

        blocks = []
        if data_blocks == 1: # 'MIRRORING'
            blocks = [Block(data, crypto.crc32c(data))] * total_blocks
        elif parity_blocks == 1: # 'RAID-5'
            block_size = math.ceil(len(data) / data_blocks)
            def xor_bytes(x: bytes, y: bytes) -> bytes:
                return bytes(
                    a ^ b for a, b in itertools.zip_longest(x, y, fillvalue=0)
                )
            parity = b''
            for block_offset in range(0, len(data), block_size):
                block_data = data[block_offset:block_offset+block_size]
                parity = xor_bytes(parity, block_data)
                zero_padding = block_size - len(block_data)
                crc = crypto.crc32c(block_data + zero_padding * b'\0')
                blocks.append(Block(block_data, crc))
            assert len(parity) == block_size
            blocks.append(Block(parity, crypto.crc32c(parity)))
        else:
            raise NotImplementedError(
                f'Parity mode {data_blocks}+{parity_blocks} not implemented'
            )

        return blocks

    def add_blockless_span(data: bytes, byte_offset: int
        ) -> metadata_msgs.AddEdenSpanResp:
        if all(x == 0 for x in data):
            storage_class = metadata_msgs.ZERO_FILL_STORAGE
            parity_mode = 0
            payload = b''
        else:
            storage_class = metadata_msgs.INLINE_STORAGE
            parity_mode = 0
            payload = data
        ret = send_request(
            metadata_msgs.AddEdenSpanReq(
                storage_class,
                parity_mode,
                crypto.crc32c(data),
                len(data),
                byte_offset,
                new_inode_id,
                cookie,
                payload
            ),
            shard
        )
        if not isinstance(ret, metadata_msgs.AddEdenSpanResp):
            raise Exception(f'Bad response: {ret}')
        return ret

    def add_span(blocks: Collection[Block], crc: bytes, byte_offset: int,
        storage_class: int, parity_mode: int) -> metadata_msgs.AddEdenSpanResp:
        assert len(blocks) == metadata_utils.total_blocks(parity_mode)
        payload = [
            metadata_msgs.NewBlockInfo(b.crc, len(b.data)) for b in blocks
        ]
        ret = send_request(
            metadata_msgs.AddEdenSpanReq(
                storage_class,
                parity_mode,
                crc,
                len(data),
                byte_offset,
                new_inode_id,
                cookie,
                payload
            ),
            shard
        )
        if not isinstance(ret, metadata_msgs.AddEdenSpanResp):
            raise Exception(f'Bad response: {ret}')
        return ret

    # writes block, returns proof
    def write_block(block: Block, addr: Tuple[str, int], block_id: int,
        certificate: bytes) -> bytes:
        header = struct.pack('<cQ4sI8s', b'w', block_id, block.crc, len(block.data),
            certificate)
        with socket.create_connection(addr) as conn:
            conn.send(header + block.data)
            resp = conn.recv(1024)
        if len(resp) != 17:
            raise RuntimeError(f'Bad response len {resp!r}')
        proof: bytes
        rkind, rblock_id, proof = struct.unpack('<cQ8s', resp)
        if rkind != b'W':
            raise RuntimeError(f'Unexpected response kind {rkind} {resp!r}')
        if rblock_id != block_id:
            raise RuntimeError(f'Bad block id, expected {block_id} got {rblock_id}')
        return proof

    cur_offset = 0
    for data, mode in [(b'hello,', 0), (b' world', metadata_utils.create_parity_mode(3, 1)), (b'\x00' * 10, 0)]:
        if mode == 0:
            resp = add_blockless_span(data, cur_offset)
            if not isinstance(resp, metadata_msgs.AddEdenSpanResp):
                raise RuntimeError(f'{resp}')
        else:
            blocks = convert_to_blocks(data, mode)
            crc = crypto.crc32c(data)
            resp = add_span(blocks, crc, cur_offset, 2, mode)
            write_proofs = [
                write_block(block, (socket.inet_ntoa(binfo.ip), binfo.port),
                    binfo.block_id, binfo.certificate)
                for block, binfo in zip(blocks, resp.span)
            ]
            resp = send_request(
                metadata_msgs.CertifyEdenSpanReq(
                    new_inode_id,
                    cur_offset,
                    cookie,
                    write_proofs
                ),
                shard
            )
            if not isinstance(resp, metadata_msgs.CertifyEdenSpanResp):
                raise RuntimeError(f'{resp}')

        cur_offset += len(data)

    # a beautiful file with all the required bytes is in eden
    # now just need to link it
    resp = send_request(
        metadata_msgs.LinkEdenFileReq(
            new_inode_id,
            cookie,
            parent_inode,
            name
        ),
        shard
    )
    print(resp)


def do_visit_inodes(parent_inode: int, name: str, arg2: str) -> None:
    continuation_key = int(arg2)
    shard = metadata_utils.shard_from_inode(continuation_key)
    ret = []
    while True: # do..while(continuation_key != 0)
        resp = send_request(
            metadata_msgs.VisitInodesReq(continuation_key),
            shard
        )
        assert isinstance(resp, metadata_msgs.VisitInodesResp)
        ret.extend(resp.inodes)
        continuation_key = resp.continuation_key
        if continuation_key == 0:
            break
    pprint.pprint(ret)


def do_visit_eden(parent_inode: int, name: str, arg2: str) -> None:
    continuation_key = int(arg2)
    shard = metadata_utils.shard_from_inode(continuation_key)
    ret = []
    while True: # do..while(continuation_key != 0)
        resp = send_request(
            metadata_msgs.VisitEdenReq(continuation_key),
            shard
        )
        assert isinstance(resp, metadata_msgs.VisitEdenResp)
        ret.extend(resp.eden_vals)
        continuation_key = resp.continuation_key
        if continuation_key == 0:
            break
    pprint.pprint(ret)


def do_reverse_block_query(parent_inode: int, name: str, arg2: str) -> None:
    bserver = bytes.fromhex(arg2)
    ret = []
    for shard in range(2):#256):
        continuation_key = 0
        while True: # do..while(continuation_key != 0)
            resp = send_request(
                metadata_msgs.ReverseBlockQueryReq(bserver, continuation_key),
                shard
            )
            assert isinstance(resp, metadata_msgs.ReverseBlockQueryResp)
            ret.extend(resp.blocks)
            continuation_key = resp.continuation_key
            if continuation_key == 0:
                break
    pprint.pprint(ret)


def do_playground(parent_inode: int, name: str,
    arg2: str) -> None:
    # source = (1537, 6600462696362219263)
    # sink = (1025, 16789667482989277050)
    # target = (1281, 14025209132833556664)
    # resp = send_request(
    #     metadata_msgs.RepairSpansReq(
    #         source[0],
    #         source[1],
    #         sink[0],
    #         sink[1],
    #         target[0],
    #         target[1],
    #         0
    #     ),
    #     1
    # )
    # resp = send_request(
    #     metadata_msgs.PurgeDirentReq(
    #         1,
    #         'a',
    #         82998250604871378,
    #     ),
    #     1
    # )
    # resp = cross_dir_request(
    #     cross_dir_msgs.PurgeRemoteFileReq(
    #         1,
    #         'test.txt',
    #         82912756340648967,
    #     ),
    # )
    resp = send_request(
        metadata_msgs.RepairBlockReq(
            5633,
            18018732710616102787,
            5121,
            0,
            6,
            83065400081668609,
        ),
        metadata_utils.shard_from_inode(5633)
    )
    print(resp)


requests: Dict[str, Callable[[int, str, str], None]] = {
    'resolve': functools.partial(do_resolve, metadata_msgs.ResolveMode.ALIVE),
    'resolve_dead_le': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_LE),
    'resolve_dead_ge': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_GE),
    'mkdir': do_mkdir,
    'rmdir': do_rmdir,
    'stat': do_stat,
    'rm': do_rm,
    'mv': do_mv,
    'ls': functools.partial(do_ls,
        metadata_msgs.LsFlags(0)),
    'ls_dead_le': functools.partial(do_ls,
        metadata_msgs.LsFlags.USE_DEAD_MAP),
    'ls_dead_ge': functools.partial(do_ls,
        metadata_msgs.LsFlags.NEWER_THAN_AS_OF
        | metadata_msgs.LsFlags.USE_DEAD_MAP),
    'ls_dead_all_le': functools.partial(do_ls,
        metadata_msgs.LsFlags.INCLUDE_NOTHING_ENTRIES
        | metadata_msgs.LsFlags.USE_DEAD_MAP),
    'ls_dead_all_ge': functools.partial(do_ls,
        metadata_msgs.LsFlags.INCLUDE_NOTHING_ENTRIES
        | metadata_msgs.LsFlags.NEWER_THAN_AS_OF
        | metadata_msgs.LsFlags.USE_DEAD_MAP),
    'cat': do_cat,
    'create_eden_file': do_create_eden_file,
    'extend_eden_deadline': do_extend_eden_deadline,
    'expunge_span': do_expunge_span,
    'expunge_file': do_expunge_file,
    'create_test_file': do_create_test_file,
    'visit_inodes': do_visit_inodes,
    'visit_eden': do_visit_eden,
    'reverse_block_query': do_reverse_block_query,
    'playground': do_playground,
}


def main() -> None:
    global VERBOSE
    parser = argparse.ArgumentParser(
        description='A basic eggsfs client for doing simple ops')
    parser.add_argument('request', choices=requests)
    parser.add_argument('path')
    parser.add_argument('arg2', nargs='?', default='')
    parser.add_argument('--verbose', action='store_true')
    config = parser.parse_args(sys.argv[1:])

    VERBOSE = config.verbose

    if not posixpath.isabs(config.path):
        raise ValueError('Only abs paths supported')
    parent_path = posixpath.dirname(config.path)
    basename = posixpath.basename(config.path)

    parent_inode = resolve_abs_path(parent_path)

    request_doer = requests[config.request]
    request_doer(parent_inode, basename, config.arg2)


if __name__ == '__main__':
    main()
