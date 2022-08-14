#!/usr/bin/env python3

import argparse
import functools
import posixpath
import pprint
import socket
import struct
import sys
import time
from typing import Callable, Dict, List, Optional, Tuple, Union

import bincode
import crypto
import cross_dir_msgs
import metadata_msgs
from metadata_msgs import ResolvedInode, ResolveMode
import metadata_utils


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
    mode: ResolveMode = ResolveMode.ALIVE) -> Optional[ResolvedInode]:

    req_body = metadata_msgs.ResolveReq(
        mode=mode,
        parent_id=parent_id,
        creation_ts=creation_ts,
        subname=subname,
    )
    resp_body = send_request(req_body, metadata_utils.shard_from_inode(parent_id))
    if not isinstance(resp_body, metadata_msgs.ResolveResp):
        raise Exception(f'Bad response from server:'
            f' expected ResolveResp got {resp_body}')
    return resp_body.f


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


def resolve_abs_path(path: str, ts: int) -> ResolvedInode:

    bits = path.split('/')
    if bits[0] != "":
        raise ValueError('Path must start with /')

    # start at root
    cur_inode = ResolvedInode(
        id=metadata_utils.ROOT_INODE,
        inode_type=metadata_msgs.InodeType.DIRECTORY,
        creation_ts=0,
        is_owning=True,
    )

    if path == '/':
        return cur_inode

    for i, bit in enumerate(bits[1:]):
        if cur_inode.inode_type != metadata_msgs.InodeType.DIRECTORY:
            filename = '/'.join(bits[:i+1])
            raise ValueError(f"'{filename}' is not a directory")

        next_inode = resolve(cur_inode.id, bit)

        if next_inode is None:
            subdirname = '/'.join(bits[:i+2])
            raise ValueError(f'No such file or directory {subdirname}')

        cur_inode = next_inode

    return cur_inode


def do_resolve(mode: metadata_msgs.ResolveMode, parent_inode: ResolvedInode,
    name: str, creation_ts: int) -> None:
    inode: Optional[ResolvedInode]
    if name == '':
        if mode == metadata_msgs.ResolveMode.ALIVE:
            inode = parent_inode
        else:
            inode = None
    else:
        inode = resolve(parent_inode.id, name, creation_ts, mode)
    print(inode)


def do_mkdir(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:
    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for mkdir')

    # before talking to the cross-dir co-ordinator, check whether this
    # subdirectory already exists
    # this ensures we're not wasting the rename co-ordinator's time

    inode = resolve(parent_inode.id, name)
    if inode is not None:
        # if this inode is a directory, return success (ensures idempotency)
        if inode.inode_type == metadata_msgs.InodeType.DIRECTORY:
            print(f'Created successfully: {inode}')
            return
        raise Exception(f'Target is not a directory: {inode}')

    body = cross_dir_msgs.MkDirReq(parent_inode.id, name)
    response = cross_dir_request(body)
    print(response)


def do_rmdir(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:
    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for rmdir')

    body = cross_dir_msgs.RmDirReq(parent_inode.id, name)
    response = cross_dir_request(body)
    print(response)


def do_stat(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:
    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for stat')

    inode: Optional[ResolvedInode]
    if parent_inode.id == metadata_utils.ROOT_INODE and name == '':
        inode = ResolvedInode(
            id=metadata_utils.ROOT_INODE,
            inode_type=metadata_msgs.InodeType.DIRECTORY,
            creation_ts=0,
            is_owning=True)
    else:
        inode = resolve(parent_inode.id, name)
    if inode is None:
        raise Exception(f'resolve ({parent_inode.id}, "{name}") returned None')

    resp = stat(inode.id)
    if resp is not None and resp.inode_type != inode.inode_type:
        raise Exception('Bad inode type:'
            f' expected {inode.inode_type} got {resp.inode_type}')

    print(resp)


def do_rm(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:
    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for stat')

    resp = send_request(
        metadata_msgs.DeleteFileReq(
            parent_inode.id,
            name
        ),
        metadata_utils.shard_from_inode(parent_inode.id)
    )
    print(resp)


def do_ls(flags: metadata_msgs.LsFlags, parent_inode: ResolvedInode, name: str,
    creation_ts: int) -> None:

    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode.id, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if inode.inode_type != metadata_msgs.InodeType.DIRECTORY:
        raise Exception('Target not a directory')

    continuation_key = 0
    shard = metadata_utils.shard_from_inode(inode.id)
    all_results: List[str] = []
    while continuation_key != 0xFFFF_FFFF_FFFF_FFFF:
        resp = send_request(
            metadata_msgs.LsDirReq(
                inode.id,
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
    all_results.sort()
    pprint.pprint(all_results)


def do_cat(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:
    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode.id, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if inode.inode_type == metadata_msgs.InodeType.DIRECTORY:
        raise Exception('Target is a directory')

    shard = metadata_utils.shard_from_inode(inode.id)
    next_offset = 0
    while True:
        resp = send_request(
            metadata_msgs.FetchSpansReq(inode.id, next_offset), shard)
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
        if next_offset == 0: # python lacks do..while loops
            break

def do_create_eden_file(parent_inode: ResolvedInode, name: str,
    creation_ts: int) -> None:

    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for create eden')

    if name == '':
        inode = parent_inode
    else:
        maybe_inode = resolve(parent_inode.id, name)
        if maybe_inode is None:
            raise Exception('Target not found')
        inode = maybe_inode

    if inode.inode_type != metadata_msgs.InodeType.DIRECTORY:
        raise Exception('Target not a directory')

    resp = send_request(
        metadata_msgs.CreateEdenFileReq(metadata_msgs.InodeType.FILE),
        metadata_utils.shard_from_inode(inode.id)
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


def do_expunge_span(parent_inode: ResolvedInode, name: str,
    eden_inode: int) -> None:
    print(expunge_one_span(eden_inode))


def do_expunge_file(parent_inode: ResolvedInode, name: str,
    eden_inode: int) -> None:
    offset = expunge_one_span(eden_inode)
    while offset != 0:
        offset = expunge_one_span(eden_inode)
    resp = send_request(
        metadata_msgs.ExpungeEdenFileReq(eden_inode),
        metadata_utils.shard_from_inode(eden_inode)
    )
    print(resp)


def do_create_test_file(parent_inode: ResolvedInode, name: str,
    creation_ts: int) -> None:

    if creation_ts != 0:
        raise ValueError('creation_ts should be unspecified for create eden')

    if name == '':
        raise ValueError('Cannot use empty name')

    if parent_inode.inode_type != metadata_msgs.InodeType.DIRECTORY:
        raise Exception('Target not a directory')

    shard = metadata_utils.shard_from_inode(parent_inode.id)

    resp = send_request(
        metadata_msgs.CreateEdenFileReq(metadata_msgs.InodeType.FILE),
        shard
    )

    if not isinstance(resp, metadata_msgs.CreateEdenFileResp):
        raise RuntimeError(f'{resp}')

    new_inode_id = resp.inode
    cookie = resp.cookie

    def add_span(data: bytes, crc: bytes, byte_offset: int, mode: str) -> metadata_msgs.RespBodyTy:
        payload: Union[bytes, List[metadata_msgs.NewBlockInfo]]

        if mode == 'INLINE':
            storage_class = metadata_msgs.INLINE_STORAGE
            parity_mode = 0
            payload = data
        elif mode == 'ZERO_FILL':
            assert all(b == 0 for b in data)
            storage_class = metadata_msgs.ZERO_FILL_STORAGE
            parity_mode = 0
            payload = b''
        elif mode == 'MIRRORING':
            storage_class = 2 # arbitrary, but not inline or zero-fill
            parity_mode = metadata_utils.create_parity_mode(1, 2)
            payload = [metadata_msgs.NewBlockInfo(crc, len(data))] * 3
        else:
            raise Exception(f'unknown mode {mode}')

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

        return ret

    # writes block, returns proof
    def write_block(data: bytes, addr: Tuple[str, int], block_id: int,
        crc: bytes, certificate: bytes) -> bytes:
        header = struct.pack('<cQ4sI8s', b'w', block_id, crc, len(data),
            certificate)
        with socket.create_connection(addr) as conn:
            conn.send(header + data)
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
    for data, mode in [(b'hello,', 'INLINE'), (b' world', 'MIRRORING'), (b'\x00' * 10, 'ZERO_FILL')]:
        crc = crypto.crc32c(data)
        resp = add_span(data, crc, cur_offset, mode)
        if not isinstance(resp, metadata_msgs.AddEdenSpanResp):
            raise RuntimeError(f'{resp}')
        write_proofs = []
        if mode == 'MIRRORING':
            write_proofs = [
                write_block(data, (socket.inet_ntoa(binfo.ip), binfo.port),
                    binfo.block_id, crc, binfo.certificate)
                for binfo in resp.span
            ]
        else:
            assert mode in ['INLINE', 'ZERO_FILL']

        if write_proofs:
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
            parent_inode.id,
            name
        ),
        shard
    )
    print(resp)


def do_playground(parent_inode: ResolvedInode, name: str,
    creation_ts: int) -> None:
    source = (513, 16334980891291861289)
    sink = (769, 981523036800970143)
    target = 257
    resp = send_request(
        metadata_msgs.RepairSpansReq(
            source[0],
            source[1],
            sink[0],
            sink[1],
            target,
            0
        ),
        1
    )
    print(resp)


requests: Dict[str, Callable[[ResolvedInode, str, int], None]] = {
    'resolve': functools.partial(do_resolve, metadata_msgs.ResolveMode.ALIVE),
    'resolve_dead_le': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_LE),
    'resolve_dead_ge': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_GE),
    'mkdir': do_mkdir,
    'rmdir': do_rmdir,
    'stat': do_stat,
    'rm': do_rm,
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
    'expunge_span': do_expunge_span,
    'expunge_file': do_expunge_file,
    'create_test_file': do_create_test_file,
    'playground': do_playground,
}


def main() -> None:
    global VERBOSE
    parser = argparse.ArgumentParser(
        description='Runs a single metadata shard without raft')
    parser.add_argument('request', choices=requests)
    parser.add_argument('path')
    parser.add_argument('creation_ts', nargs='?', default=0, type=int)
    parser.add_argument('--verbose', action='store_true')
    config = parser.parse_args(sys.argv[1:])

    VERBOSE = config.verbose

    if not posixpath.isabs(config.path):
        raise ValueError('Only abs paths supported')
    parent_path = posixpath.dirname(config.path)
    basename = posixpath.basename(config.path)

    parent_inode = resolve_abs_path(parent_path, config.creation_ts)

    request_doer = requests[config.request]
    request_doer(parent_inode, basename, config.creation_ts)


if __name__ == '__main__':
    main()
