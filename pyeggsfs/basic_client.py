#!/usr/bin/env python3

import argparse
import posixpath
import socket
import sys
import time
from typing import Callable, Dict, Optional

import bincode
import metadata_msgs
from metadata_msgs import ResolvedInode
import metadata_utils


LOCAL_HOST = '127.0.0.1'
PROTOCOL_VER = 0


def resolve(parent_id: int, subname: str, ts: int) -> Optional[ResolvedInode]:
    request_id = int(time.time())
    shard = metadata_utils.shard_from_inode(parent_id)
    port = metadata_utils.shard_to_port(shard)
    target = (LOCAL_HOST, port)
    request = metadata_msgs.MetadataRequest(
        ver=PROTOCOL_VER,
        request_id=request_id,
        body=metadata_msgs.ResolveReq(
            parent_id=parent_id,
            subname=subname,
        ),
    )
    packed_req = bincode.pack(request)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', 0))
    sock.sendto(packed_req, target)
    sock.settimeout(2.0)
    packed_resp = sock.recv(metadata_utils.UDP_MTU)
    response = metadata_msgs.MetadataResponse.unpack(
        bincode.UnpackWrapper(packed_resp))
    if request_id != response.request_id:
        raise ValueError('Bad request_id from server:'
            f' expected {request_id} got {response.request_id}')
    if not isinstance(response.body, metadata_msgs.ResolveResp):
        raise ValueError(f'Bad response from server:'
            f' expected ResolveResp got {response.body}')
    return response.body.f


def resolve_abs_path(path: str, ts: int) -> ResolvedInode:

    bits = path.split('/')
    if bits[0] != "":
        raise ValueError('Path must start with /')

    # start at root
    cur_inode = ResolvedInode(
        id=metadata_utils.ROOT_INODE_NUMBER,
        inode_type=metadata_msgs.InodeType.DIRECTORY,
    )

    if path == '/':
        return cur_inode

    for i, bit in enumerate(bits[1:]):
        if cur_inode.inode_type != metadata_msgs.InodeType.DIRECTORY:
            filename = '/'.join(bits[:i+1])
            raise ValueError(f"'{filename}' is not a directory")

        next_inode = resolve(cur_inode.id, bit, ts)

        if next_inode is None:
            subdirname = '/'.join(bits[:i+2])
            raise ValueError(f'No such file or directory {subdirname}')

        cur_inode = next_inode

    return cur_inode


def do_resolve(parent_inode: ResolvedInode, basename: str, creation_ts: int
    ) -> None:

    inode: Optional[ResolvedInode]
    if basename == '':
        inode = parent_inode
    else:
        inode = resolve(parent_inode.id, basename, creation_ts)
    print(inode)


def do_mkdir(parent_inode: ResolvedInode, name: str, creation_ts: int) -> None:

    raise NotImplementedError()


requests: Dict[str, Callable[[ResolvedInode, str, int], None]] = {
    'resolve': do_resolve,
    'mkdir': do_mkdir,
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
