#!/usr/bin/env python3

import argparse
import functools
import posixpath
import socket
import sys
import time
from typing import Callable, Dict, Optional

import bincode
import metadata_msgs
from metadata_msgs import ResolvedInode, ResolveMode
import metadata_utils


LOCAL_HOST = '127.0.0.1'
PROTOCOL_VER = 0


# "key_inode" means the inode you use to determine the shard
def send_request(req_body: metadata_msgs.ReqBodyTy, key_inode: int
    ) -> metadata_msgs.RespBodyTy:
    shard = metadata_utils.shard_from_inode(key_inode)
    port = metadata_utils.shard_to_port(shard)
    ver = PROTOCOL_VER
    request_id = int(time.time())
    target = target = (LOCAL_HOST, port)
    req = metadata_msgs.MetadataRequest(ver, request_id, req_body)
    packed_req = bincode.pack(req)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', 0))
    sock.sendto(packed_req, target)
    sock.settimeout(2.0)
    packed_resp = sock.recv(metadata_utils.UDP_MTU)
    response = metadata_msgs.MetadataResponse.unpack(
        bincode.UnpackWrapper(packed_resp))
    if request_id != response.request_id:
        raise Exception('Bad request_id from server:'
            f' expected {request_id} got {response.request_id}')
    return response.body


def resolve(parent_id: int, subname: str, creation_ts: int = 0,
    mode: ResolveMode = ResolveMode.ALIVE) -> Optional[ResolvedInode]:

    req_body = metadata_msgs.ResolveReq(
        mode=mode,
        parent_id=parent_id,
        creation_ts=creation_ts,
        subname=subname,
    )
    resp_body = send_request(req_body, parent_id)
    if not isinstance(resp_body, metadata_msgs.ResolveResp):
        raise Exception(f'Bad response from server:'
            f' expected ResolveResp got {resp_body}')
    return resp_body.f


def stat(inode: int) -> Optional[metadata_msgs.StatResp]:
    req_body = metadata_msgs.StatReq(
        inode,
    )
    resp_body = send_request(req_body, inode)
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

    raise NotImplementedError()


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
        raise Exception(f'resolve ({parent_inode.id}, {name}) returned None')

    resp = stat(inode.id)
    if resp is not None and resp.inode_type != inode.inode_type:
        raise Exception('Bad inode type:'
            f' expected {inode.inode_type} got {resp.inode_type}')

    print(resp)



requests: Dict[str, Callable[[ResolvedInode, str, int], None]] = {
    'resolve': functools.partial(do_resolve, metadata_msgs.ResolveMode.ALIVE),
    'resolve_dead_le': functools.partial(do_resolve, metadata_msgs.ResolveMode.DEAD_LE),
    'mkdir': do_mkdir,
    'stat': do_stat,
}


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Runs a single metadata shard without raft')
    parser.add_argument('request', choices=requests)
    parser.add_argument('path')
    parser.add_argument('creation_ts', nargs='?', default=0, type=int)
    config = parser.parse_args(sys.argv[1:])

    if not posixpath.isabs(config.path):
        raise ValueError('Only abs paths supported')
    parent_path = posixpath.dirname(config.path)
    basename = posixpath.basename(config.path)

    parent_inode = resolve_abs_path(parent_path, config.creation_ts)

    request_doer = requests[config.request]
    request_doer(parent_inode, basename, config.creation_ts)


if __name__ == '__main__':
    main()
