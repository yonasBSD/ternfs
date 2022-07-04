#!/usr/bin/env python3

import argparse
import enum
from typing import Dict, List, NamedTuple, Optional, Tuple, Union
from sortedcontainers import SortedDict
import os
import pickle
import socket
import sys

import metadata_msgs


ROOT_INODE_NUMBER = 0
PROTOCOL_VERSION = 0


def shard_from_inode(inode_number: int) -> int:
    return inode_number & 0xFF


class InodeType(enum.IntEnum):
    DIRECTORY = 0
    FILE = 1
    SYMLINK = 2


class LivingKey(NamedTuple):
    hash_name: int
    name: str


class LivingValue:
    creation_time: int
    inode_id: int
    type: InodeType
    is_owning: bool


class DeadKey(NamedTuple):
    hash_name: int
    name: str
    creation_time: int


class DeadValue:
    inode_id: int
    type: InodeType
    is_owning: bool


class Directory:
    parent_inode_id: Optional[int]
    mtime: int
    living_items: 'SortedDict[LivingKey, LivingValue]'
    dead_items: 'SortedDict[DeadKey, Optional[DeadValue]]'
    purge_policy_id: int
    # <opaque_data>
    storage_class_policy: int
    parity_mode_policy: int
    preferred_block_size: int
    # </opaque_data>


class Block:
    storage_id: int
    block_id: int
    crc32: bytes
    size: int
    block_idx: int


class Span:
    parity: int # (two nibbles, 8+3 is stored as (8,3))
    storage_class: int # opaque (except for 0 => inline)
    crc32: bytes
    size: int
    payload: Union[bytes, List[Block]] # storage_class == 0 means bytes


class File:
    mtime: int
    is_eden: bool
    cookie: int # or maybe str?
    last_span_is_dirty: bool
    type: InodeType # must not be InodeType.DIRECTORY
    spans: List[Span]


class StorageNode:
    write_weight: float # used to weight random variable for block creation
    private_key: bytes
    addr: Tuple[str, int]


class MetadataShard:
    def __init__(self, shard: int):
        assert 0 <= shard <= 255
        self.shard_id = shard
        self.next_inode_id = shard | 0x100 # 00-FF is reserved
        self.next_block_id = shard | 0x100
        self.directories: Dict[int, Directory] = {}
        self.files: Dict[int, File] = {}
        self.storage_nodes: Dict[int, StorageNode] = {}

    def resolve(self, r: metadata_msgs.ResolveReq) -> Optional[metadata_msgs.ResolvedInode]:
        parent = self.directories.get(r.parent_id)
        if parent is None:
            return None
        hashed_name = hash(r.subname)
        res = parent.living_items.get(LivingKey(hashed_name, r.subname))
        if res is None:
            #TODO: maybe check dead items? (or not since interface will change)
            return None
        return metadata_msgs.ResolvedInode(
            id=res.inode_id,
            creation_time=res.creation_time,
            deletion_time=0,
            is_file=(res.type != InodeType.DIRECTORY),
        )

def run_forever(shard: MetadataShard) -> None:
    port = shard.shard_id + 22272
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    while True:
        data, addr = sock.recvfrom(8192)
        request = metadata_msgs.unpack_request(data)
        if request.ver != PROTOCOL_VERSION:
            print('Ignoring request, unsupported ver:', request.ver,
                file=sys.stderr)
            continue
        if isinstance(request.body, metadata_msgs.ResolveReq):
            result = shard.resolve(request.body)
            resp_body = metadata_msgs.ResolveResp(result)
        else:
            print('Ignoring request, unrecognised body:', request.body,
                file=sys.stderr)
            continue
        resp = metadata_msgs.MetadataResponse(
            request_id=request.request_id,
            body=resp_body
        )
        print(request, resp, '', sep='\n')
        packed = metadata_msgs.pack_response(resp)
        sock.sendto(packed, addr)


def main() -> None:
    parser = argparse.ArgumentParser(
        description='Runs a single metadata shard without raft')
    parser.add_argument('shard', help='Between 0 and 255', type=int)
    parser.add_argument('db_path', help='Location to create or load db')
    config = parser.parse_args(sys.argv[1:])

    assert 0 <= config.shard <= 255, f'{config.shard} is out of range'

    os.makedirs(config.db_path, exist_ok=True)

    db_fn = os.path.join(config.db_path, f'shard_{config.shard}.pickle')

    shard_object: MetadataShard

    if os.path.exists(db_fn):
        print(f'Loading from {db_fn}')
        with open(db_fn, 'rb') as f:
            shard_object = pickle.load(f)
    else:
        print('Creating fresh db')
        shard_object = MetadataShard(config.shard)

    try:
        run_forever(shard_object)
    finally:
        print(f'Dumping to {db_fn}')
        with open(db_fn, 'wb') as f:
            pickle.dump(shard_object, f)


if __name__ == '__main__':
    main()
