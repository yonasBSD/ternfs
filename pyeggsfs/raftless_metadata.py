#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
import enum
from typing import Dict, List, NamedTuple, Optional, Tuple, Union
from sortedcontainers import SortedDict
import os
import socket
import sys
import time

import bincode
import cross_dir_key
import crypto
from metadata_msgs import *
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
    is_owning: bool


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
    parent_inode: int # NULL_INODE => no parent
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
        self.last_created_inode = metadata_utils.NULL_INODE
        # if we're the shard that contains root, create it now
        if shard == metadata_utils.shard_from_inode(metadata_utils.ROOT_INODE):
            self.inodes[metadata_utils.ROOT_INODE] = Directory(
                parent_inode=metadata_utils.ROOT_INODE,
                mtime=int(time.time()),
                opaque=b'', #FIXME: what should opaque be for root?
            )

    def resolve(self, r: ResolveReq) -> Optional[ResolvedInode]:
        parent = self.inodes.get(r.parent_id)
        if parent is None or not isinstance(parent, Directory):
            return None
        hashed_name = metadata_utils.string_hash(r.subname)
        if r.mode == ResolveMode.ALIVE:
            res = parent.living_items.get(LivingKey(hashed_name, r.subname))
            if res is None:
                return None
            else:
                return ResolvedInode(
                    id=res.inode_id,
                    inode_type=res.type,
                    creation_ts=res.creation_time,
                    is_owning=True,
                )
        else:
            assert r.mode == ResolveMode.DEAD_LE
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
            return ResolvedInode(
                id=result_value.inode_id,
                inode_type=result_value.type,
                creation_ts=result_key.creation_time,
                is_owning=result_value.is_owning,
            )

    def do_stat(self, r: StatReq) -> RespBodyTy:
        res = self.inodes.get(r.inode_id)
        if res is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No entry for {r.inode_id}',
            )
        payload: StatPayloadTy
        if isinstance(res, Directory):
            inode_type = InodeType.DIRECTORY
            payload = StatDirPayload(
                res.mtime, res.parent_inode, res.opaque,
            )
        else:
            inode_type = res.type
            payload = StatFilePayload(
                res.mtime, res.size,
            )
        return StatResp(inode_type, payload)

    def do_set_parent(self, r: SetParentReq) -> RespBodyTy:
        affected_inode = self.inodes.get(r.inode)
        if affected_inode is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.inode}',
            )
        if not isinstance(affected_inode, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'{r.inode} not a directory',
            )
        # can only set parent to null if inode has no living children
        if (r.new_parent == metadata_utils.NULL_INODE
            and len(affected_inode.living_items) != 0):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'{r.inode} is not empty',
            )

        affected_inode.parent_inode = r.new_parent
        return SetParentResp()

    def do_create_unlinked_dir(self, r: CreateDirReq) -> RespBodyTy:
        # int(time.time()) might not be the best way to do this
        # but it's the python prototype so ¯\_(-_-))_/¯
        #
        # although a real problem I thought of here is idempotency... does the
        # mtime need to be identical on each re-request?
        # for that reason it might be better if mtime is a parameter to
        # CreateDirReq
        mtime = int(time.time())
        if (self.last_created_inode == r.token_inode
            or self.last_created_inode == metadata_utils.NULL_INODE):
            # implies the previous created dir succeeded, so we need to create
            # a "true" new directory (can't just reuse the existing one)
            self.inodes[self.next_inode_id] = Directory(
                parent_inode=r.new_parent,
                mtime=mtime,
                opaque=r.opaque,
            )
            new_dir_inode = self.next_inode_id
            self.next_inode_id += 0x100
            self.last_created_inode = new_dir_inode
        else:
            # implies the previous created dir failed and didn't get linked in
            # so we can reuse it
            recycled_dir = self.inodes[self.last_created_inode]
            assert isinstance(recycled_dir, Directory)
            recycled_dir.parent_inode = r.new_parent
            recycled_dir.mtime = mtime
            recycled_dir.opaque = r.opaque
            new_dir_inode = self.next_inode_id
        return CreateDirResp(
            inode=new_dir_inode,
            mtime=mtime,
        )

    def do_inject_dirent(self, r: InjectDirentReq) -> RespBodyTy:
        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.parent_inode}',
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                f'{r.parent_inode} is not a directory'
            )
        if parent.parent_inode == metadata_utils.NULL_INODE:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'{r.parent_inode} has no parent'
            )
        hashed_name = metadata_utils.string_hash(r.subname)
        expected_val = LivingValue(
            r.creation_ts,
            r.child_inode,
            InodeType.DIRECTORY,
            False,
        )
        actual_val = parent.living_items.setdefault(
            LivingKey(hashed_name, r.subname), expected_val)
        if actual_val != expected_val:
            # if the expect val happened to be in there, should return success
            # for idempotency; if there was any deviation, fail
            return MetadataError(
                MetadataErrorKind.ALREADY_EXISTS,
                f'{actual_val}',
            )
        return InjectDirentResp()

    def do_release_dirent(self, r: ReleaseDirentReq) -> RespBodyTy:
        # this operation cannot fail, even if the target doesn't exist
        # this is for idempotency
        # To understand why: consider that a previous invocation succeeded;
        # and then after that, the target was removed and cleaned up by
        # a 3rd party.
        parent = self.inodes.get(r.parent_inode)
        if isinstance(parent, Directory): # implictly checks for None
            hashname = metadata_utils.string_hash(r.subname)
            target = parent.living_items.get(LivingKey(hashname, r.subname))
            if target is not None:
                target.is_owning = True
        return ReleaseDirentResp()


def run_forever(shard: MetadataShard, db_fn: str) -> None:
    port = metadata_utils.shard_to_port(shard.shard_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    while True:
        authorised = False
        data, addr = sock.recvfrom(metadata_utils.UDP_MTU)
        if data[0] & 0x80:
            # this is a privilaged command
            maybe_data = crypto.remove_mac(data, cross_dir_key.CROSS_DIR_KEY)
            if maybe_data is not None:
                authorised = True
                data = maybe_data
        else:
            authorised = True
        resp_body: RespBodyTy
        dirty = False
        if not authorised:
            resp_body = MetadataError(
                MetadataErrorKind.NOT_AUTHORISED,
                '')
        else:
            request = bincode.unpack(MetadataRequest, data)
            if request.ver != PROTOCOL_VERSION:
                print('Ignoring request, unsupported ver:', request.ver,
                    file=sys.stderr)
                continue
            if isinstance(request.body, ResolveReq):
                result = shard.resolve(request.body)
                resp_body = ResolveResp(result)
            elif isinstance(request.body, StatReq):
                resp_body = shard.do_stat(request.body)
            elif isinstance(request.body, SetParentReq):
                resp_body = shard.do_set_parent(request.body)
                dirty = True
            elif isinstance(request.body, CreateDirReq):
                resp_body = shard.do_create_unlinked_dir(request.body)
                dirty = True
            elif isinstance(request.body, InjectDirentReq):
                resp_body = shard.do_inject_dirent(request.body)
                dirty = True
            elif isinstance(request.body, ReleaseDirentReq):
                resp_body = shard.do_release_dirent(request.body)
                # TODO: maybe dirty should be returned by do_blah
                # sometimes do_release_dirent has no effect
                dirty = True
            else:
                resp_body = MetadataError(
                    MetadataErrorKind.LOGIC_ERROR,
                    'unrecognised message kind')
        resp = MetadataResponse(
            request_id=request.request_id,
            body=resp_body,
        )
        if dirty:
            metadata_utils.persist(shard, db_fn)
        packed = bincode.pack(resp)
        sock.sendto(packed, addr)
        print(request, resp, '', sep='\n')


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

    maybe_shard_object = metadata_utils.restore(db_fn)

    if maybe_shard_object is not None:
        print(f'Loading from {db_fn}')
        shard_object = maybe_shard_object
    else:
        print('Creating fresh db')
        shard_object = MetadataShard(config.shard)

    run_forever(shard_object, db_fn)


if __name__ == '__main__':
    main()
