#!/usr/bin/env python3

import argparse
import functools
import posixpath
import pprint
import socket
import sys
import time
from typing import Callable, Dict, List, Optional

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
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
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
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
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


requests: Dict[str, Callable[[ResolvedInode, str, int], None]] = {
    'resolve': functools.partial(do_resolve, metadata_msgs.ResolveMode.ALIVE),
    'resolve_dead_le': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_LE),
    'resolve_dead_ge': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_GE),
    'mkdir': do_mkdir,
    'rmdir': do_rmdir,
    'stat': do_stat,
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
