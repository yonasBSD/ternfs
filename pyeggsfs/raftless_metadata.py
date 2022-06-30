#!/usr/bin/env python3

import argparse
import enum
from typing import Dict, List, NamedTuple, Optional, Union
from sortedcontainers import SortedDict
import os
import pickle
import sys


ROOT_INODE_NUMBER = 0


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
    inode_id: int
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
    inode_id: int
    mtime: int
    is_eden: bool
    cookie: int # or maybe str?
    last_span_is_dirty: bool
    type: InodeType # must not be InodeType.DIRECTORY
    spans: List[Span]


class MetadataShard:
    def __init__(self, shard: int):
        assert 0 <= shard <= 255
        self.shard = shard
        self.next_inode_id = shard if shard != 0 else 0x100 # never root
        self.next_block_id = shard
        self.directories: Dict[int, Directory] = {}
        self.files: Dict[int, File] = {}

    def run_forever(self) -> None:
        pass


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
        shard_object.run_forever()
    finally:
        print(f'Dumping to {db_fn}')
        with open(db_fn, 'wb') as f:
            pickle.dump(shard_object, f)


if __name__ == '__main__':
    main()
