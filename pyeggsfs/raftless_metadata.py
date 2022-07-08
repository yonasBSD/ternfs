#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
import enum
from typing import Dict, List, NamedTuple, Optional, Tuple, Union
from sortedcontainers import SortedDict
import os
import pickle
import socket
import sys
import time

import bincode
import metadata_msgs
from metadata_msgs import InodeType, MetadataRequest, MetadataResponse
import metadata_utils


PROTOCOL_VERSION = 0


class LivingKey(NamedTuple):
    hash_name: int
    name: str


@dataclass
class LivingValue:
    creation_time: int
    inode_id: int
    type: InodeType


class DeadKey(NamedTuple):
    hash_name: int
    name: str
    creation_time: int


@dataclass
class DeadValue:
    inode_id: int
    type: InodeType
    is_owning: bool


@dataclass
class Directory:
    parent_inode_id: int # NULL_INODE => no parent
    mtime: int
    # used by clients when selecting storage classes, parity modes, etc
    # unclear what size this should be, for now it's variable-length
    opaque: bytes
    living_items: 'SortedDict[LivingKey, LivingValue]' = field(default_factory=SortedDict)
    dead_items: 'SortedDict[DeadKey, Optional[DeadValue]]' = field(default_factory=SortedDict)


@dataclass
class Block:
    storage_id: int
    block_id: int
    crc32: bytes
    size: int
    block_idx: int


@dataclass
class Span:
    parity: int # (two nibbles, 8+3 is stored as (8,3))
    storage_class: int # opaque (except for 0 => inline)
    crc32: bytes
    size: int
    payload: Union[bytes, List[Block]] # storage_class == 0 means bytes


@dataclass
class File:
    mtime: int
    size: int # redundant: must equal sum(spans.size)
    is_eden: bool
    last_span_is_dirty: bool
    type: InodeType # must not be InodeType.DIRECTORY
    spans: List[Span] = field(default_factory=list)


InodePayload = Union[Directory, File]


@dataclass
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
        self.inodes: SortedDict[int, InodePayload] = SortedDict()
        self.storage_nodes: SortedDict[int, StorageNode] = SortedDict()
        # if we're the shard that contains root, create it now
        if shard == metadata_utils.shard_from_inode(metadata_utils.ROOT_INODE):
            self.inodes[metadata_utils.ROOT_INODE] = Directory(
                parent_inode_id=metadata_utils.ROOT_INODE,
                mtime=int(time.time()),
                opaque=b'', #FIXME: what should opaque be for root?
            )

    def resolve(self, r: metadata_msgs.ResolveReq
        ) -> Optional[metadata_msgs.ResolvedInode]:
        parent = self.inodes.get(r.parent_id)
        if parent is None or not isinstance(parent, Directory):
            return None
        hashed_name = metadata_utils.string_hash(r.subname)
        if r.mode == metadata_msgs.ResolveMode.ALIVE:
            res = parent.living_items.get(LivingKey(hashed_name, r.subname))
            if res is None:
                return None
            else:
                return metadata_msgs.ResolvedInode(
                    id=res.inode_id,
                    inode_type=res.type,
                    creation_ts=res.creation_time,
                    is_owning=True,
                )
        else:
            assert r.mode == metadata_msgs.ResolveMode.DEAD_LE
            search_ts = r.creation_ts or 2**64 - 1
            search_key = DeadKey(hashed_name, r.subname, search_ts)
            result_idx = parent.dead_items.bisect_left(search_key)
            if result_idx >= len(parent.dead_items):
                return None
            result_key = parent.dead_items.keys()[result_idx]
            if result_key.name != r.subname:
                return None
            result_value = parent.dead_items[result_key]
            if result_value is None:
                # in the snapshot history, the inode is dead at creation_ts
                # we want the most recently deleted one, i.e. one before
                # this should exist (invariant of the data structure)
                # TODO: - is this invariant real?
                #       - will the None entry definitely be removed?
                assert result_idx != 0
                result_key = parent.dead_items.keys()[result_idx - 1]
                assert(result_key.name == r.subname)
                result_value = parent.dead_items[result_key]
            assert result_value is not None
            return metadata_msgs.ResolvedInode(
                id=result_value.inode_id,
                inode_type=result_value.type,
                creation_ts=result_key.creation_time,
                is_owning=result_value.is_owning,
            )

    def do_stat(self, r: metadata_msgs.StatReq
        ) -> metadata_msgs.RespBodyTy:
        res = self.inodes.get(r.inode_id)
        if res is None:
            return metadata_msgs.MetadataError(
                metadata_msgs.MetadataErrorKind.NOT_FOUND,
                f'No entry for {r.inode_id}',
            )
        payload: metadata_msgs.StatPayloadTy
        if isinstance(res, Directory):
            inode_type = metadata_msgs.InodeType.DIRECTORY
            payload = metadata_msgs.StatDirPayload(
                res.mtime, res.parent_inode_id, res.opaque,
            )
        else:
            inode_type = res.type
            payload = metadata_msgs.StatFilePayload(
                res.mtime, res.size,
            )
        return metadata_msgs.StatResp(inode_type, payload)


def run_forever(shard: MetadataShard) -> None:
    port = metadata_utils.shard_to_port(shard.shard_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    while True:
        data, addr = sock.recvfrom(metadata_utils.UDP_MTU)
        request = bincode.unpack(MetadataRequest, data)
        if request.ver != PROTOCOL_VERSION:
            print('Ignoring request, unsupported ver:', request.ver,
                file=sys.stderr)
            continue
        resp_body: metadata_msgs.RespBodyTy
        if isinstance(request.body, metadata_msgs.ResolveReq):
            result = shard.resolve(request.body)
            resp_body = metadata_msgs.ResolveResp(result)
        elif isinstance(request.body, metadata_msgs.StatReq):
            resp_body = shard.do_stat(request.body)
        else:
            print('Ignoring request, unrecognised body:', request.body,
                file=sys.stderr)
            continue
        resp = MetadataResponse(
            request_id=request.request_id,
            body=resp_body
        )
        print(request, resp, '', sep='\n')
        packed = bincode.pack(resp)
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
