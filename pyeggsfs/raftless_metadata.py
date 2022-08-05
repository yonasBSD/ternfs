#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
import enum
import hashlib
from typing import Dict, Iterator, List, NamedTuple, Optional, Tuple, Union
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
    is_owning: bool # always false?


class HashMode(enum.IntEnum):
    TRUNC_MD5 = 1


@dataclass
class Directory:
    parent_inode: int # NULL_INODE => no parent
    mtime: int
    # used by clients when selecting storage classes, parity modes, etc
    # unclear what size this should be, for now it's variable-length
    opaque: bytes
    hash_mode: HashMode = HashMode.TRUNC_MD5
    living_items: 'SortedDict[LivingKey, LivingValue]' = field(default_factory=SortedDict)
    dead_items: 'SortedDict[DeadKey, Optional[DeadValue]]' = field(default_factory=SortedDict)

    def hash_name(self, n: str) -> int:
        # for production we should consider alternatives, e.g. murmur3 or xxhash
        if self.hash_mode == HashMode.TRUNC_MD5:
            h = hashlib.md5()
            h.update(n.encode())
            return int.from_bytes(h.digest()[:8], 'little')
        else:
            raise ValueError(f'Unsupported hash mode: {self.hash_mode}')


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
        hashed_name = parent.hash_name(r.subname)
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
            result_idx = parent.dead_items.bisect_right(search_key) - 1
            if result_idx < 0:
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

    def do_ls_dir(self, r: LsDirReq) -> RespBodyTy:
        dir = self.inodes.get(r.inode)
        if dir is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.inode}'
            )
        elif not isinstance(dir, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                f'{r.inode} not a directory',
            )

        def living_iter() -> Iterator[LsPayload]:
            assert isinstance(dir, Directory)
            lower_bound = LivingKey(r.continuation_key, '')
            key_range = dir.living_items.irange(lower_bound)
            for k in key_range:
                v = dir.living_items[k]
                yield LsPayload(v.inode_id, k.hash_name, k.name, v.type)

        def dead_iter() -> Iterator[LsPayload]:
            assert isinstance(dir, Directory)
            last_key = r.continuation_key
            as_of = r.as_of
            yield_newest_since = r.flags & LsFlags.NEWER_THAN_AS_OF
            keys_sorted_list = dir.dead_items.keys()
            while True:
                # two lookups required:
                #    1) determine the name of the next inode
                #    2) locate the required version based on as_of time
                lower_bound = DeadKey(last_key, '', 0)
                next_name_idx = dir.dead_items.bisect_left(lower_bound)
                if next_name_idx >= len(dir.dead_items):
                    # we have reached the end
                    return
                next_name_k = keys_sorted_list[next_name_idx]
                last_key = next_name_k.hash_name

                locator_key = DeadKey(next_name_k.hash_name, next_name_k.name,
                    as_of)
                if yield_newest_since:
                    # Note that left/right bisection only differ when we get
                    # an exact match on the as_of time. In this case we always
                    # want this exactly-matching result.
                    # Left gives us this result, whereas right skips it.
                    res_idx = dir.dead_items.bisect_left(locator_key)
                else:
                    # Bisect always returns the point where locator_key
                    # would be inserted - but if we want the element <= locator_key
                    # we need to decrement this.
                    # (The decrement is why we want bisect_right here.)
                    res_idx = dir.dead_items.bisect_right(locator_key) - 1

                # yeild a "nothing_result" if we we "overshot" or "undershot"
                # the name we wanted, or the value is a None placeholder
                nothing_result = LsPayload(
                    metadata_utils.NULL_INODE,
                    locator_key.hash_name,
                    locator_key.name,
                    InodeType.SYMLINK, # arbitrary
                )
                if res_idx < 0 or res_idx >= len(dir.dead_items):
                    yield nothing_result
                    continue
                res_k = keys_sorted_list[res_idx]
                if res_k.hash_name != locator_key.hash_name:
                    yield nothing_result
                    continue
                res_v = dir.dead_items[res_k]
                if res_v is None:
                    yield nothing_result
                    continue

                yield LsPayload(res_v.inode_id, res_k.hash_name, res_k.name,
                    res_v.type)

        i = dead_iter() if r.flags & LsFlags.USE_DEAD_MAP else living_iter()
        results = []
        budget = metadata_utils.UDP_MTU - MetadataResponse.SIZE - LsDirResp.SIZE
        for result in i:
            continuation_key = result.hash_of_name
            cost = result.calc_packed_size()
            if cost > budget:
                break
            budget -= cost
            results.append(result)
        else:
            continuation_key = 0xFFFF_FFFF_FFFF_FFFF

        return LsDirResp(
            continuation_key,
            results,
        )

    def do_set_parent(self, r: SetParentReq) -> RespBodyTy:
        affected_inode = self.inodes.get(r.inode)
        if affected_inode is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.inode}',
            )
        if not isinstance(affected_inode, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                f'{r.inode} not a directory',
            )
        # can only set parent to null if inode has no living children
        if r.new_parent == metadata_utils.NULL_INODE:
            if len(affected_inode.living_items) != 0:
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'{r.inode} is not empty',
                )
            elif len(affected_inode.dead_items) == 0:
                # null parent + empty living + empty dead => free the directory
                print(f'PURGING INODE {r.inode}; reason: parent set to null;')
                del self.inodes[r.inode]
                return SetParentResp()
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
        hashed_name = parent.hash_name(r.subname)
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

    def do_acquire_dirent(self, r: AcquireDirentReq) -> RespBodyTy:
        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.parent_inode}'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                f'Inode {r.parent_inode} is not a directory'
            )

        hashed_name = parent.hash_name(r.subname)
        res = parent.living_items.get(LivingKey(hashed_name, r.subname))
        if res is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'Inode {r.parent_inode} has no living element {r.subname}'
            )
        if not (r.acceptable_types & res.type):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                f'Target inode {res.inode_id} as wrong type {res.type}'
            )

        return AcquireDirentResp(
            res.inode_id,
            res.type
        )

    def do_release_dirent(self, r: ReleaseDirentReq) -> RespBodyTy:
        # this operation cannot fail, even if the target doesn't exist
        # this is for idempotency
        # To understand why: consider that a previous invocation succeeded;
        # and then after that, the target was removed and cleaned up by
        # a 3rd party.
        parent = self.inodes.get(r.parent_inode)
        found = False
        now = int(time.time())
        if isinstance(parent, Directory): # implictly checks for None
            found = True
            hashname = parent.hash_name(r.subname)
            living_key = LivingKey(hashname, r.subname)
            target = parent.living_items.get(living_key)
            if target is not None:
                if r.kill:
                    dead_key = DeadKey(hashname, r.subname,
                        target.creation_time)
                    # TODO: should we check that target.is_owning is False?
                    # target.is_owning == True would imply some kind of logic
                    # error, though it's not clear how to handle this error
                    # given the release operation cannot fail
                    # (maybe it should emit a warning and then do nothing?)
                    dead_value = DeadValue(target.inode_id, target.type,
                        target.is_owning)
                    parent.dead_items[dead_key] = dead_value
                    placeholder_key = DeadKey(hashname, r.subname, now)
                    parent.dead_items[placeholder_key] = None
                    del parent.living_items[living_key]
                else:
                    target.is_owning = True
        return ReleaseDirentResp(found)


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
            elif isinstance(request.body, LsDirReq):
                resp_body = shard.do_ls_dir(request.body)
            elif isinstance(request.body, SetParentReq):
                resp_body = shard.do_set_parent(request.body)
                dirty = True
            elif isinstance(request.body, CreateDirReq):
                resp_body = shard.do_create_unlinked_dir(request.body)
                dirty = True
            elif isinstance(request.body, InjectDirentReq):
                resp_body = shard.do_inject_dirent(request.body)
                dirty = True
            elif isinstance(request.body, AcquireDirentReq):
                resp_body = shard.do_acquire_dirent(request.body)
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
        import pprint
        pprint.pprint(shard_object.inodes)
    else:
        print('Creating fresh db')
        shard_object = MetadataShard(config.shard)

    run_forever(shard_object, db_fn)


if __name__ == '__main__':
    main()
