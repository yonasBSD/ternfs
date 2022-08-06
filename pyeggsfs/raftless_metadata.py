#!/usr/bin/env python3

import argparse
from dataclasses import dataclass, field
import enum
import hashlib
import itertools
import math
import os
import random
import requests
import secrets
import socket
from sortedcontainers import SortedDict
import sys
import time
from typing import (Any, Dict, Iterator, List, NamedTuple, Optional, Sequence,
    Tuple, Union)

import bincode
import cross_dir_key
import crypto
from metadata_msgs import *
import metadata_utils


PROTOCOL_VERSION = 0

DEADLINE_DURATION_SEC = 60 * 60 * 24


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
    storage_id: int # IP and port?
    block_id: int
    crc32: int
    size: int
    block_idx: int


@dataclass
class Span:
    parity: int # (two nibbles, 8+3 is stored as (8,3))
    storage_class: int # opaque (except for 0 => inline)
    crc32: int
    size: int
    payload: Union[bytes, List[Block]] # storage_class == 0 means bytes


@dataclass
class File:
    mtime: int
    size: int # redundant: must equal sum(spans.size)
    type: InodeType # must not be InodeType.DIRECTORY
    spans: List[Span] = field(default_factory=list) # TODO: wants to be SortedDict? (keyed on offset)


class SpanState(enum.IntEnum):
    CLEAN = 0
    DIRTY = 1
    CONDEMNED = 2


@dataclass
class EdenFile:
    mtime: int
    type: InodeType
    deadline_time: float
    last_span_state: SpanState
    spans: List[Span] = field(default_factory=list) # TODO: wants to be SortedDict? (keyed on offset)


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
        self.eden: SortedDict[int, EdenFile] = SortedDict()
        self.last_created_inode = metadata_utils.NULL_INODE
        token = secrets.token_bytes(16)
        self.secret_key = crypto.aes_expand_key(token)
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
            assert r.mode in (ResolveMode.DEAD_LE, ResolveMode.DEAD_GE)
            search_key = DeadKey(hashed_name, r.subname, r.creation_ts)

            # need to probe around the result if we land on a None placeholder
            # invariant: never have two Nones beloning to the same name next to
            # each other, meaning we only need to probe at most one element to
            # the left or right (direction depends on search mode)
            if r.mode == ResolveMode.DEAD_LE:
                result_idx = parent.dead_items.bisect_right(search_key) - 1
                idxes_to_check = [result_idx, result_idx - 1]
            else:
                result_idx = parent.dead_items.bisect_left(search_key)
                idxes_to_check = [result_idx, result_idx + 1]
            for idx in idxes_to_check:
                if idx < 0 or idx >= len(parent.dead_items):
                    return None
                result_key = parent.dead_items.keys()[idx]
                if result_key.name != r.subname:
                    return None
                result_value = parent.dead_items[result_key]
                if result_value is not None:
                    # found a good result
                    break
            else:
                return None

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
            last_name = ''
            as_of = r.as_of
            yield_newest_since = r.flags & LsFlags.NEWER_THAN_AS_OF
            keys_sorted_list = dir.dead_items.keys()
            while True:
                # two lookups required:
                #    1) determine the name of the next inode
                #    2) locate the required version based on as_of time
                lower_bound = DeadKey(last_key, last_name, 0xFFFF_FFFF_FFFF_FFFF)
                next_name_idx = dir.dead_items.bisect_left(lower_bound)
                if next_name_idx >= len(dir.dead_items):
                    # we have reached the end
                    return
                next_name_k = keys_sorted_list[next_name_idx]
                last_key = next_name_k.hash_name
                last_name = next_name_k.name

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
        include_nothing_results = r.flags & LsFlags.INCLUDE_NOTHING_ENTRIES
        results = []
        budget = metadata_utils.UDP_MTU - MetadataResponse.SIZE - LsDirResp.SIZE
        for result in i:
            continuation_key = result.hash_of_name
            cost = result.calc_packed_size()
            if cost > budget:
                break
            budget -= cost
            if (result.inode != metadata_utils.NULL_INODE
                or include_nothing_results):
                results.append(result)
        else:
            continuation_key = 0xFFFF_FFFF_FFFF_FFFF

        return LsDirResp(
            continuation_key,
            results,
        )

    def do_create_eden_file(self, r: CreateEdenFileReq) -> RespBodyTy:
        if r.type == InodeType.DIRECTORY:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Cannot create an eden directory'
            )
        now = int(time.time())
        new_inode = self._dispense_inode()
        self.eden[new_inode] = EdenFile(
            now,
            r.type,
            now + DEADLINE_DURATION_SEC,
            SpanState.CLEAN
        )
        return CreateEdenFileResp(new_inode, self._calc_cookie(new_inode))

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
            new_dir_inode = self._dispense_inode()
            self.inodes[new_dir_inode] = Directory(
                parent_inode=r.new_parent,
                mtime=mtime,
                opaque=r.opaque,
            )
            self.last_created_inode = new_dir_inode
        else:
            # implies the previous created dir failed and didn't get linked in
            # so we can reuse it
            recycled_dir = self.inodes[self.last_created_inode]
            assert isinstance(recycled_dir, Directory)
            recycled_dir.parent_inode = r.new_parent
            recycled_dir.mtime = mtime
            recycled_dir.opaque = r.opaque
            new_dir_inode = self.last_created_inode
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

    def _dispense_inode(self) -> int:
        ret = self.next_inode_id
        self.next_inode_id += 0x100 # preserve LSB
        return ret

    def _calc_cookie(self, eden_inode: int) -> int:
        b = eden_inode.to_bytes(8, 'little') + (b'\x00' * 8)
        mac = crypto.cbc_mac_impl(b, self.secret_key)
        return int.from_bytes(mac[-8:], 'little')


class BlockServerInfo(NamedTuple):
    ip: str
    port: int
    is_terminal: bool
    is_stale: bool
    write_weight: float
    secret_key: bytes

    @staticmethod
    def from_shuckle(json_obj: Dict[str, Any]) -> 'BlockServerInfo':
        return BlockServerInfo(
            ip=json_obj['ip'],
            port=json_obj['port'],
            is_terminal=json_obj['is_terminal'],
            is_stale=json_obj['is_stale'],
            write_weight=json_obj['write_weight'],
            secret_key=bytes.fromhex(json_obj['secret_key']),
        )


# returned in the order from shuckle
# (which doesn't appear to be specifically ordered currently)
def try_fetch_block_metadata() -> Optional[List[BlockServerInfo]]:
    try:
        resp = requests.get('http://localhost:5000/show_me_what_you_got')
        resp.raise_for_status()
        return [BlockServerInfo.from_shuckle(datum) for datum in resp.json()]
    except requests.RequestException as e:
        print('WARNING: failed to get shuckle data:', e)
        return None


class BlockPicker:
    def __init__(self, block_meta: Sequence[BlockServerInfo]):
        if not block_meta:
            raise ValueError('No blocks to pick')
        if any(b.write_weight < 0.0 for b in block_meta):
            raise ValueError('Negative weights not OK')

        self.block_meta = block_meta
        self.cum_weights = list(
            itertools.accumulate(b.write_weight for b in block_meta)
        )
        if math.isclose(self.cum_weights[-1], 0.0):
            raise ValueError('Need some nonzero weights')

    def pick_block(self) -> BlockServerInfo:
        return self.pick_blocks(1)[0]

    def pick_blocks(self, num: int) -> List[BlockServerInfo]:
        return random.choices(self.block_meta, cum_weights=self.cum_weights,
            k=num)


SHUCKLE_POLL_PERIOD_SEC = 60.0


def run_forever(shard: MetadataShard, db_fn: str) -> None:
    port = metadata_utils.shard_to_port(shard.shard_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    shuckle_data = try_fetch_block_metadata() or []
    last_shuckle_update = time.time()
    while True:
        authorised = False
        try:
            next_timeout = max(0.0,
                (last_shuckle_update + SHUCKLE_POLL_PERIOD_SEC) - time.time())
            sock.settimeout(next_timeout)
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
                elif isinstance(request.body, CreateEdenFileReq):
                    resp_body = shard.do_create_eden_file(request.body)
                    dirty = True
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
        except socket.timeout:
            pass

        if time.time() > last_shuckle_update + SHUCKLE_POLL_PERIOD_SEC:
            maybe_shuckle_data = try_fetch_block_metadata()
            if maybe_shuckle_data is None:
                print('WARNING: could not connect to shuckle')
            else:
                shuckle_data = maybe_shuckle_data
            last_shuckle_update = time.time()



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
        pprint.pprint(shard_object.eden)
    else:
        print('Creating fresh db')
        shard_object = MetadataShard(config.shard)

    run_forever(shard_object, db_fn)


if __name__ == '__main__':
    main()
