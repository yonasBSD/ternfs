#!/usr/bin/env python3

import argparse
import collections
import dataclasses
from dataclasses import dataclass, field
import enum
import hashlib
import itertools
import math
import operator
import os
import random
import requests
import secrets
import struct
import socket
from sortedcontainers import SortedDict
import sys
import time
from typing import (Any, DefaultDict, Dict, Iterator, List, Mapping, NamedTuple,
    Optional, Sequence, Set, Tuple, Union)

import bincode
import cross_dir_key
import crypto
from metadata_msgs import *
import metadata_utils


PROTOCOL_VERSION = 0

# one hour in ns
DEADLINE_DURATION_NS = 60 * 60 * 1000 * 1000 * 1000

PURGE_TIME_LOWER_BOUND = DEADLINE_DURATION_NS

# 24 hours in ns
BLOCK_CREATION_DEADLINE = 24 * DEADLINE_DURATION_NS


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
    bserver_id: int
    block_id: int
    crc32: bytes
    size: int

    def calc_create_cert(self, key: crypto.ExpandedKey) -> bytes:
        b = bytearray(32)
        struct.pack_into('<cQ4sI', b, 0, b'w', self.block_id, self.crc32,
            self.size)
        return crypto.cbc_mac_impl(b, key)[:8]

    def calc_create_proof(self, key: crypto.ExpandedKey) -> bytes:
        b = bytearray(16)
        struct.pack_into('<cQ', b, 0, b'W', self.block_id)
        return crypto.cbc_mac_impl(b, key)[:8]

    def calc_delete_cert(self, key: crypto.ExpandedKey) -> bytes:
        b = bytearray(16)
        struct.pack_into('<cQ', b, 0, b'e', self.block_id)
        return crypto.cbc_mac_impl(b, key)[:8]

    def calc_delete_proof(self, key: crypto.ExpandedKey) -> bytes:
        b = bytearray(16)
        struct.pack_into('<cQ', b, 0, b'E', self.block_id)
        return crypto.cbc_mac_impl(b, key)[:8]


@dataclass
class Span:
    parity: int # (two nibbles, 8+3 is stored as (8,3))
    storage_class: int # opaque (except inline and zero-fill)
    crc32: bytes
    size: int
    payload: Union[bytes, List[Block]]

    # returns None if consistent
    # otherwise returns inconsistency reason
    def check_consistency(self) -> bool:
        # special case: inline
        if self.storage_class == INLINE_STORAGE:
            if self.parity != 0:
                return False
            return (self.parity == 0
                and isinstance(self.payload, bytes)
                and (self.size == len(self.payload))
            )

        # special case: zero fill
        if self.storage_class == ZERO_FILL_STORAGE:
            if self.parity != 0:
                return False
            return crypto.crc32c(b'\x00' * self.size) == self.crc32

        # for non-special cases, want a block list in payload
        if not isinstance(self.payload, list):
            return False

        # TODO: implement crc consistency check
        #
        # implementation strategy as per the doc:
        # For parity mode 1+M (mirroring), for any M, the server can validate
        # that the CRC of the span equals the CRC of every block.
        # For N+M in the general case, it can probably validate that the CRC of
        # the span equals the concatenation of the CRCs of the N, and that the
        # CRC of the 1st of the M equals the XOR of the CRCs of the N.
        # It cannot validate anything about the rest of the M though (scrubbing
        # can validate the rest, if it wants to, but it would be too expensive
        # for the metadata server to validate).

        # also check size consistency
        num_data_blocks = metadata_utils.num_data_blocks(self.parity)
        implied_size = sum(b.size for b in self.payload[:num_data_blocks])
        if self.size != implied_size:
            return False

        return True


@dataclass
class File:
    mtime: int
    size: int # redundant: must equal sum(spans.size)
    type: InodeType # must not be InodeType.DIRECTORY
    # offset -> span
    spans: 'SortedDict[int, Span]' = field(default_factory=SortedDict)


class SpanState(enum.IntEnum):
    CLEAN = 0
    DIRTY = 1
    CONDEMNED = 2


@dataclass
class EdenFile:
    f: File
    deadline_time: int
    last_span_state: SpanState


InodePayload = Union[Directory, File]


@dataclass
class StorageNode:
    write_weight: float # used to weight random variable for block creation
    private_key: bytes
    addr: Tuple[str, int]


class BlockServerInfo(NamedTuple):
    id: int
    ip: bytes
    port: int
    storage_class: int
    flags: BlockFlags
    write_weight: float
    secret_key: crypto.ExpandedKey

    @staticmethod
    def from_shuckle(json_obj: Dict[str, Any], shard: 'MetadataShard'
        ) -> 'BlockServerInfo':
        secret = bytes.fromhex(json_obj['secret_key'])
        id = shard.register_bserver(secret)
        flags = BlockFlags(0)
        if json_obj['is_stale']:
            flags |= BlockFlags.STALE

        write_weight = json_obj['write_weight']

        if json_obj['is_terminal']:
            shard.notify_terminal_bserver(id)
            flags |= BlockFlags.TERMINAL
        elif shard.is_bserver_terminal(id):
            flags |= BlockFlags.TERMINAL
            write_weight = 0.0

        return BlockServerInfo(
            id=id,
            ip=socket.inet_aton(json_obj['ip']),
            port=json_obj['port'],
            storage_class=json_obj['storage_class'],
            flags=flags,
            write_weight=write_weight,
            secret_key=crypto.aes_expand_key(secret),
        )


class WeightedBlockMeta(NamedTuple):
    cum_weights: Optional[List[float]]
    block_meta: List[BlockServerInfo]


def compute_weights(meta: List[BlockServerInfo]) -> WeightedBlockMeta:
    cum_weights = list(
        itertools.accumulate(b.write_weight for b in meta)
    )
    if any(b.write_weight < 0.0 for b in meta):
        raise ValueError('Negative weights not OK')
    if math.isclose(cum_weights[-1], 0.0):
        return WeightedBlockMeta(None, meta)
    return WeightedBlockMeta(cum_weights, meta)


class ShuckleData:
    # TODO: add support for storage classes
    def __init__(self, block_meta: List[BlockServerInfo]):
        key = operator.attrgetter('storage_class')
        block_meta.sort(key=key)

        self._storage_class_to_meta = {
            sc: compute_weights(list(g))
            for sc, g in itertools.groupby(block_meta, key)
        }

        self._block_index: Dict[int, Tuple[int, int]] = {}
        for sc, meta in self._storage_class_to_meta.items():
            self._block_index.update(
                (bserver.id, (sc, idx))
                for idx, bserver in enumerate(meta.block_meta)
            )

    def try_pick_blocks(self, storage_class: int, num: int
        ) -> Optional[List[BlockServerInfo]]:
        meta_for_sc = self._storage_class_to_meta.get(storage_class,
            WeightedBlockMeta(None, []))
        if meta_for_sc.cum_weights is None:
            return None
        return random.choices(meta_for_sc.block_meta,
            cum_weights=meta_for_sc.cum_weights, k=num)

    def lookup(self, dense_id: int) -> Optional[BlockServerInfo]:
        sc, idx = self._block_index.get(dense_id, (None, 0))
        if sc is None:
            return None
        return self._storage_class_to_meta[sc].block_meta[idx]


# returned in the order from shuckle
# (which doesn't appear to be specifically ordered currently)
def try_fetch_block_metadata(shard: 'MetadataShard') -> Optional[ShuckleData]:
    try:
        resp = requests.get('http://localhost:5000/show_me_what_you_got')
        resp.raise_for_status()
        block_meta = [
            BlockServerInfo.from_shuckle(datum, shard)
            for datum in resp.json()
        ]
        return ShuckleData(block_meta)
    except Exception as e:
        print('WARNING: failed to get shuckle data:', e)
        return None


def _try_link(parent: Directory, new_key: LivingKey, new_val: LivingValue,
    now: int, *, dry: bool = False) -> Optional[MetadataError]:
    # try inserting the new value
    # fails => consider overriding the existing thing
    #
    # N.B. if the link is already there, this will quietly succeed
    # (for idempotency reasons this is usually the desired behaviour)
    if dry:
        res = parent.living_items.get(new_key, new_val)
    else:
        res = parent.living_items.setdefault(new_key, new_val)
    if res.inode_id != new_val.inode_id:
        # not inserted, check if we can overwrite
        if res.type == InodeType.DIRECTORY:
            return MetadataError(
                MetadataErrorKind.ALREADY_EXISTS,
                'Cannot overwrite directory'
            )
        if not res.is_owning:
            return MetadataError(
                MetadataErrorKind.ALREADY_EXISTS,
                'Cannot overwrite non-owning file'
            )
        if not dry:
            # add old file into dead map
            dead_key = DeadKey(new_key.hash_name, new_key.name, now)
            parent.dead_items[dead_key] = DeadValue(
                res.inode_id,
                res.type,
                res.is_owning
            )
            # can reuse the old value object
            res.creation_time = new_val.creation_time
            res.inode_id = new_val.inode_id
            res.type = new_val.type
            res.is_owning = new_val.is_owning
    return None


class BlockIndexKey(NamedTuple):
    bserver_id: int
    block_id: int


class SpanId(NamedTuple):
    file_inode: int
    byte_offset: int


class MetadataShard:
    def __init__(self, shard: int):
        assert 0 <= shard <= 255
        self.shard_id = shard
        self.next_inode_id = shard | 0x100 # 00-FF is reserved
        self.last_block_id = shard | 0x100
        self.next_bserver_id = 0
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
                mtime=metadata_utils.now(),
                opaque=b'', #FIXME: what should opaque be for root?
            )
        self.bserver_secret_to_id: DefaultDict[bytes, int] = (
            collections.defaultdict(self._dispense_bserver_id)
        )
        self.terminal_bservers: Set[int] = set()
        self.reverse_block_index: SortedDict[BlockIndexKey, SpanId] = (
            SortedDict()
        )

    def register_bserver(self, secret: bytes) -> int:
        return self.bserver_secret_to_id[secret]

    def notify_terminal_bserver(self, bserver_id: int) -> None:
        self.terminal_bservers.add(bserver_id)

    def is_bserver_terminal(self, bserver_id: int) -> bool:
        return bserver_id in self.terminal_bservers

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
                lower_bound = DeadKey(last_key, last_name,
                    0xFFFF_FFFF_FFFF_FFFF)
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
            # FIXME: inefficient varint encoding
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
        now = metadata_utils.now()
        new_inode = self._dispense_inode()
        self.eden[new_inode] = EdenFile(
            File(now, 0, r.type),
            now + DEADLINE_DURATION_NS,
            SpanState.CLEAN
        )
        return CreateEdenFileResp(new_inode, self._calc_cookie(new_inode))

    def do_add_eden_span(self, r: AddEdenSpanReq, s: Optional[ShuckleData]
        ) -> RespBodyTy:

        now = metadata_utils.now()
        eden_file = self.eden.get(r.inode)
        if eden_file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such eden file {r.inode}'
            )
        if now > eden_file.deadline_time:
            return MetadataError(
                MetadataErrorKind.EDEN_DEADLINE,
                f'now={now} deadline_time={eden_file.deadline_time}'
            )
        if r.cookie != self._calc_cookie(r.inode):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Wrong cookie'
            )
        # "blockless requests" use no blocks
        # we support two kinds, INLINE and ZERO_FILL
        blockless_req = (r.parity_mode == 0)
        if (blockless_req
            != (r.storage_class in [INLINE_STORAGE, ZERO_FILL_STORAGE])):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Inconsistent storage class/parity mode'
            )
        if not blockless_req and s is None:
            return MetadataError(
                MetadataErrorKind.SHUCKLE_ERROR,
                'No shuckle data'
            )

        # left empty for a blockless request
        resp_payload: List[BlockInfo] = []

        # if r.offset is end of file, create and append new span
        # otherwise, resynthesis previous response from existing span
        if r.byte_offset == eden_file.f.size:
            if eden_file.last_span_state != SpanState.CLEAN:
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Last span in state {eden_file.last_span_state} not clean'
                )
            num_blocks = metadata_utils.total_blocks(r.parity_mode)

            if blockless_req:
                assert isinstance(r.payload, bytes)
                new_span = Span(
                    r.parity_mode,
                    r.storage_class,
                    r.crc32,
                    r.size,
                    r.payload
                )
            else:
                assert s is not None
                assert isinstance(r.payload, list)
                target_block_servers = s.try_pick_blocks(
                    r.storage_class, num_blocks)
                if target_block_servers is None:
                    return MetadataError(
                        MetadataErrorKind.SHUCKLE_ERROR,
                        'No block servers available'
                    )

                assert len(target_block_servers) == len(r.payload)
                new_blocks = []
                span_id = SpanId(r.inode, r.byte_offset)
                for binfo, bserver in zip(r.payload, target_block_servers):
                    block_id = self._dispense_block_id(now)
                    new_blocks.append(Block(
                        bserver.id,
                        block_id,
                        binfo.crc32,
                        binfo.size,
                    ))
                    index_key = BlockIndexKey(bserver.id, block_id)
                    self.reverse_block_index[index_key] = span_id
                new_span = Span(
                    r.parity_mode,
                    r.storage_class,
                    r.crc32,
                    r.size,
                    new_blocks
                )
                resp_payload = [
                    BlockInfo(
                        bserver.ip,
                        bserver.port,
                        binfo.block_id,
                        binfo.calc_create_cert(bserver.secret_key)
                    )
                    for binfo, bserver in zip(new_blocks, target_block_servers)
                ]

            if not new_span.check_consistency():
                return MetadataError(MetadataErrorKind.BAD_REQUEST,
                    'Span/Block data not consistent')
            eden_file.f.spans[eden_file.f.size] = new_span
            eden_file.f.size += new_span.size
            if new_span.parity:
                eden_file.last_span_state = SpanState.DIRTY
            eden_file.deadline_time = now + DEADLINE_DURATION_NS
        else:
            existing_span = eden_file.f.spans.get(r.byte_offset)
            if existing_span is None:
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Offset {r.byte_offset} not found in {r.inode}'
                )
            if (existing_span.parity != r.parity_mode
                or existing_span.storage_class != r.storage_class
                or existing_span.crc32 != r.crc32
                or existing_span.size != r.size):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    'Not consistent with existing span'
                )
            if not blockless_req:
                assert s is not None
                assert isinstance(existing_span.payload, list)
                for b in existing_span.payload:
                    maybe_bserver = s.lookup(b.bserver_id)
                    if maybe_bserver is None:
                        return MetadataError(
                            MetadataErrorKind.SHUCKLE_ERROR,
                            f'Previously allocated storage node not found'
                        )
                    resp_payload.append(
                        BlockInfo(
                            maybe_bserver.ip,
                            maybe_bserver.port,
                            b.block_id,
                            b.calc_create_cert(maybe_bserver.secret_key)
                        )
                    )

        return AddEdenSpanResp(resp_payload)

    def do_certify_eden_span(self, r: CertifyEdenSpanReq,
        s: Optional[ShuckleData]) -> RespBodyTy:

        now = metadata_utils.now()
        eden_file = self.eden.get(r.inode)
        if eden_file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such eden file {r.inode}'
            )
        if now > eden_file.deadline_time:
            return MetadataError(
                MetadataErrorKind.EDEN_DEADLINE,
                f'now={now} deadline_time={eden_file.deadline_time}'
            )
        if r.cookie != self._calc_cookie(r.inode):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Wrong cookie'
            )
        target_span = eden_file.f.spans.get(r.byte_offset)
        if target_span is None:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'Byte offset {r.byte_offset} not found in {r.inode}'
            )
        if not isinstance(target_span.payload, list):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Span is blockless, certification not required'
            )
        if len(target_span.payload) != len(r.proofs):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'Expected {len(target_span.payload)} proofs, got {len(r.proofs)}'
            )
        if s is None:
            return MetadataError(
                MetadataErrorKind.SHUCKLE_ERROR,
                'No shuckle data'
            )

        for block, proof in zip(target_span.payload, r.proofs):
            bserver = s.lookup(block.bserver_id)
            if bserver is None:
                return MetadataError(
                    MetadataErrorKind.SHUCKLE_ERROR,
                    f'Storage node for {block.block_id} not found'
                )
            if proof != block.calc_create_proof(bserver.secret_key):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Bad proof for block {block.block_id}'
                )

        # all checks passed, we can now do the thing
        eden_file.deadline_time = now + DEADLINE_DURATION_NS
        if ((eden_file.f.size == r.byte_offset + target_span.size)
            and (eden_file.last_span_state == SpanState.DIRTY)
            ):
            eden_file.last_span_state = SpanState.CLEAN
            eden_file.f.mtime = now

        return CertifyEdenSpanResp(r.inode, r.byte_offset)

    def do_link_eden_file(self, r: LinkEdenFileReq) -> RespBodyTy:
        now = metadata_utils.now()
        eden_file = self.eden.get(r.eden_inode)
        parent = self.inodes.get(r.parent_inode)
        if eden_file is None:
            return MetadataError(
                # for this request, only use NOT_FOUND for the eden file
                # gives a hint that a previous invocation may have succeeded
                MetadataErrorKind.NOT_FOUND,
                'Eden file not found'
            )
        assert r.eden_inode not in self.inodes
        if now > eden_file.deadline_time:
            return MetadataError(
                MetadataErrorKind.EDEN_DEADLINE,
                f'now={now} deadline_time={eden_file.deadline_time}'
            )
        if r.cookie != self._calc_cookie(r.eden_inode):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Wrong cookie'
            )
        if parent is None:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST, # not NOT_FOUND (see above)
                'Parent inode not found'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Parent inode is not a directory'
            )

        new_key = LivingKey(parent.hash_name(r.new_name), r.new_name)
        new_val = LivingValue(now, r.eden_inode, eden_file.f.type, True)
        error = _try_link(parent, new_key, new_val, now)
        if error is not None:
            return error

        # move file out of eden into this shard
        self.inodes[r.eden_inode] = eden_file.f
        del self.eden[r.eden_inode]

        return LinkEdenFileResp()

    def do_repair_spans(self, r: RepairSpansReq) -> RespBodyTy:
        if (r.source_inode == r.sink_inode
            or r.source_inode == r.target_inode
            or r.sink_inode == r.target_inode):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source, sink and target must be distinct files'
            )
        source = self.eden.get(r.source_inode)
        sink = self.eden.get(r.sink_inode)
        if not (source and sink):
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                "At least one inode doesn't exist"
            )
        eden_files = [('source', source), ('sink', sink)]
        inode_cookie_pairs = [
            (r.source_inode, r.source_cookie),
            (r.sink_inode, r.sink_cookie)
        ]
        begin_offset = r.byte_offset
        end_offset = r.byte_offset + source.f.size
        end_span_state = SpanState.CLEAN
        if r.target_cookie:
            eden_target = self.eden.get(r.target_inode)
            if eden_target is None:
                return MetadataError(
                    MetadataErrorKind.NOT_FOUND,
                    'Target inode not found in eden'
                )
            eden_files.append(('target', eden_target))
            inode_cookie_pairs.append((r.target_inode, r.target_cookie))
            target = eden_target.f
            # all spans in an eden file are CLEAN execpt the final span
            if end_offset == target.size:
                end_span_state = eden_target.last_span_state
        else:
            maybe_target = self.inodes.get(r.target_inode)
            if maybe_target is None:
                return MetadataError(
                    MetadataErrorKind.NOT_FOUND,
                    'Target inode not found'
                )
            if not isinstance(maybe_target, File):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    'Target not a file'
                )
            target = maybe_target
        if source.last_span_state != end_span_state:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source and target have inconsistent end span states'
                f' source={source.last_span_state};'
                f' target={end_span_state}'
            )
        if sink.last_span_state != SpanState.CLEAN:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Sink must have last span state == CLEAN'
            )
        for id, cookie in inode_cookie_pairs:
            if self._calc_cookie(id) != cookie:
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Wrong cookie for {id}'
                )
        if source.f.size == 0:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source is empty'
            )
        now = metadata_utils.now()
        for name, eden_file in eden_files:
            if now > eden_file.deadline_time:
                return MetadataError(
                    MetadataErrorKind.EDEN_DEADLINE,
                    f'{name} has expired deadline time'
                )
        for required_offset in [begin_offset, end_offset]:
            if not (required_offset == target.size
                or required_offset in target.spans):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Offset {required_offset} not in target'
                )

        def combine_crcs(spans: Sequence[Tuple[int, Span]]) -> bytes:
            assert spans
            crc = spans[0][1].crc32
            for _, span in spans[1:]:
                crc = crypto.crc32c_combine(crc, span.crc32, span.size)
            return crc

        outgoing_spans = [
            (k, target.spans[k])
            for k in target.spans.irange(begin_offset, end_offset)
        ]
        incoming_spans = list(source.f.spans.items())

        if combine_crcs(outgoing_spans) != combine_crcs(incoming_spans):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'CRC mismatch between source and target'
            )

        # copy outgoing values into sink
        new_sink_offsets = itertools.accumulate(
            s.size for _, s in outgoing_spans)
        sink.f.spans.update(
            (sink.f.size + o, s[1])
            for o, s in zip(new_sink_offsets, outgoing_spans)
        )
        sink.f.size += source.f.size

        # as far as I can tell, there's no efficient way to splice values
        # into a SortedDict
        for offset, _ in outgoing_spans:
            del target.spans[offset]
        new_target_offsets = itertools.accumulate(
            s.size for _, s in incoming_spans)
        target.spans.update(
            (r.byte_offset + o, s[1])
            for o, s in zip(new_target_offsets, incoming_spans)
        )

        source.f.spans.clear()
        source.f.size = 0
        source.last_span_state = SpanState.CLEAN

        # sink inheriets the end span state of the target
        sink.last_span_state = end_span_state

        # update deadline time for any touched eden files
        for _, eden_file in eden_files:
            eden_file.deadline_time = now + DEADLINE_DURATION_NS

        return RepairSpansResp()

    def do_expunge_eden_span(self, r: ExpungeEdenSpanReq,
        s: Optional[ShuckleData]) -> RespBodyTy:
        eden_file = self.eden.get(r.inode)
        now = metadata_utils.now()
        if eden_file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'No such inode in eden'
            )
        if now < eden_file.deadline_time:
            return MetadataError(
                MetadataErrorKind.EDEN_DEADLINE,
                f'now={now} deadline_time={eden_file.deadline_time}'
            )
        if not eden_file.f.spans:
            # the expunger is free to expunge the file now
            return ExpungeEdenSpanResp(r.inode, 0, [])
        last_offset, last_span = eden_file.f.spans.peekitem()
        block_infos: List[BlockInfo] = []
        if isinstance(last_span.payload, list):
            if s is None:
                return MetadataError(
                    MetadataErrorKind.SHUCKLE_ERROR,
                    'No shuckle data'
                )
            for block in last_span.payload:
                if self.is_bserver_terminal(block.bserver_id):
                    continue
                if now < block.block_id + BLOCK_CREATION_DEADLINE:
                    return MetadataError(
                        MetadataErrorKind.BAD_REQUEST,
                        f'Block {block.block_id} still inside creation window'
                    )
                bserver = s.lookup(block.bserver_id)
                if bserver is None:
                    return MetadataError(
                        MetadataErrorKind.SHUCKLE_ERROR,
                        f'Storage node for {block.block_id} not found'
                    )
                block_infos.append(BlockInfo(
                    bserver.ip,
                    bserver.port,
                    block.block_id,
                    block.calc_delete_cert(bserver.secret_key)
                ))
            if eden_file.last_span_state != SpanState.CONDEMNED:
                eden_file.last_span_state = SpanState.CONDEMNED
        else:
            # last span is blockless, delete immediately
            del eden_file.f.spans[last_offset]
            eden_file.f.size = last_offset
        return ExpungeEdenSpanResp(r.inode, last_offset, block_infos)

    def do_delete_file(self, r: DeleteFileReq) -> RespBodyTy:
        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Parent inode not found'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Parent inode is not a directory'
            )
        hashed_name = parent.hash_name(r.name)
        living_key = LivingKey(hashed_name, r.name)
        target = parent.living_items.get(LivingKey(hashed_name, r.name))
        if target is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                "Inode not found in parent's living items"
            )
        if target.type == InodeType.DIRECTORY:
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Target is a directory'
            )
        if not target.is_owning:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Target is non-owning'
            )
        parent.dead_items[DeadKey(hashed_name, r.name, target.creation_time)] = DeadValue(
            target.inode_id,
            target.type,
            target.is_owning
        )
        parent.dead_items[DeadKey(hashed_name, r.name, metadata_utils.now())] = None
        del parent.living_items[living_key]
        return DeleteFileResp(r.parent_inode, r.name)

    def do_certify_expunge(self, r: CertifyExpungeReq,
        s: Optional[ShuckleData]) -> RespBodyTy:
        eden_file = self.eden.get(r.inode)
        if eden_file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'No such inode in eden'
            )

        if not eden_file.f.spans:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST_IDEMPOTENT_HINT,
                'Eden file has no spans'
            )
        last_offset, last_span = eden_file.f.spans.peekitem()
        if last_offset != r.offset:
            kind = MetadataErrorKind.BAD_REQUEST
            if last_offset < r.offset:
                kind = MetadataErrorKind.BAD_REQUEST_IDEMPOTENT_HINT
            return MetadataError(
                kind,
                f'Offset {r.offset} is not last span offset {last_offset}'
            )

        if eden_file.last_span_state != SpanState.CONDEMNED:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Span not condemned'
            )
        if not isinstance(last_span.payload, list):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                ''
            )

        if s is None:
            return MetadataError(
                MetadataErrorKind.SHUCKLE_ERROR,
                'No shuckle data'
            )

        blocks_to_cert = {
            block.block_id: block
            for block in last_span.payload
        }

        for block_id, proof in r.proofs:
            block = blocks_to_cert.get(block_id)
            if block is None:
                continue
            bserver = s.lookup(block.bserver_id)
            if bserver is None:
                return MetadataError(
                    MetadataErrorKind.SHUCKLE_ERROR,
                    f'Storage node for {block_id} not found'
                )
            if proof != block.calc_delete_proof(bserver.secret_key):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Bad proof for block {block.block_id}'
                )
            del blocks_to_cert[block_id]

        if blocks_to_cert:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'Missing proofs for {len(blocks_to_cert)} blocks:'
                f' {", ".join(str(bid) for bid in blocks_to_cert)}'
            )

        # all checks passed, we can now do the thing
        for block in last_span.payload:
            del self.reverse_block_index[
                BlockIndexKey(block.bserver_id, block.block_id)
            ]
        del eden_file.f.spans[last_offset]
        eden_file.f.size = last_offset
        eden_file.last_span_state = SpanState.CLEAN
        return CertifyExpungeResp(r.inode, r.offset)

    def do_expunge_eden_file(self, r: ExpungeEdenFileReq) -> RespBodyTy:
        eden_file = self.eden.get(r.inode)
        now = metadata_utils.now()
        if eden_file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'No such file in eden'
            )
        if now < eden_file.deadline_time:
            return MetadataError(
                MetadataErrorKind.EDEN_DEADLINE,
                f'now={now} deadline_time={eden_file.deadline_time}'
            )
        if eden_file.f.spans:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Eden file not empty'
            )
        del self.eden[r.inode]
        return ExpungeEdenFileResp(r.inode)

    def do_fetch_spans(self, r: FetchSpansReq, s: Optional[ShuckleData]
        ) -> RespBodyTy:

        file = self.inodes.get(r.inode)
        if file is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'No such inode'
            )
        if not isinstance(file, File):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Not a file'
            )
        if s is None:
            return MetadataError(
                MetadataErrorKind.SHUCKLE_ERROR,
                'No shuckle data'
            )
        budget = metadata_utils.UDP_MTU
        budget -= MetadataResponse.SIZE
        budget -= FetchSpansResp.SIZE_UPPER_BOUND
        # start from first offset <= r.offset
        next_offset = 0
        fetched_spans = []
        begin_idx = max(file.spans.bisect_right(r.offset) - 1, 0)
        for next_offset in file.spans.islice(begin_idx):
            span = file.spans[next_offset]
            payload: Union[bytes, List[FetchedBlock]]
            if isinstance(span.payload, bytes):
                payload = span.payload
            else:
                payload = []
                # FIXME: needs to be changed when metadata server
                # can self-identify terminal bservers
                for block in span.payload:
                    bserver = s.lookup(block.bserver_id)
                    if bserver is None:
                        # don't fail just because we don't know where the
                        # bserver is, client may be able to continue using
                        # parity data
                        ip = b'\xff\xff\xff\xff'
                        port = 65535
                        flags = BlockFlags.STALE
                        if self.is_bserver_terminal(block.bserver_id):
                            flags |= BlockFlags.TERMINAL
                    else:
                        ip = bserver.ip
                        port = bserver.port
                        flags = bserver.flags
                    payload.append(FetchedBlock(
                        ip,
                        port,
                        block.block_id,
                        block.crc32,
                        block.size,
                        flags
                    ))
            fetched_span = FetchedSpan(
                next_offset,
                span.parity,
                span.storage_class,
                span.crc32,
                span.size,
                payload
            )
            size = fetched_span.calc_packed_size()
            if size > budget:
                break
            budget -= size
            fetched_spans.append(fetched_span)
        else:
            next_offset = 0
        return FetchSpansResp(next_offset, fetched_spans)

    def do_same_dir_rename(self, r: SameDirRenameReq) -> RespBodyTy:
        now = metadata_utils.now()
        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f'No such inode {r.parent_inode}'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                f'Inode {r.parent_inode} not a directory'
            )
        if r.old_name == r.new_name:
            # or maybe just return success?
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Cannot rename file to itself'
            )
        hash_old_name = parent.hash_name(r.old_name)
        old_living_key = LivingKey(hash_old_name, r.old_name)
        old_living_val = parent.living_items.get(
            LivingKey(hash_old_name, r.old_name))
        if old_living_val is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                f"{r.parent_inode} has no living child named '{r.old_name}'"
            )
        if not old_living_val.is_owning:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source inode is non-owning'
            )
        hash_new_name = parent.hash_name(r.new_name)
        new_living_key = LivingKey(hash_new_name, r.new_name)
        new_living_val = dataclasses.replace(old_living_val, creation_time=now)

        # attempt to link into the new location
        error = _try_link(parent, new_living_key, new_living_val, now)
        if error is not None:
            return error

        new_dead_key = DeadKey(hash_old_name, r.old_name,
            old_living_val.creation_time)
        new_dead_val = DeadValue(
            old_living_val.inode_id,
            old_living_val.type,
            False,
        )
        new_placeholder = DeadKey(hash_old_name, r.old_name, now)
        parent.dead_items.update([
            (new_dead_key, new_dead_val),
            (new_placeholder, None),
        ])
        del parent.living_items[old_living_key]
        return SameDirRenameResp()

    def do_purge_dirent(self, r: PurgeDirentReq) -> RespBodyTy:
        now = metadata_utils.now()
        if (now < r.creation_time + PURGE_TIME_LOWER_BOUND):
            return MetadataError(
                MetadataErrorKind.TOO_SOON,
                'Too early to purge'
            )
        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Could not find parent'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Parent not a directory'
            )
        key = DeadKey(parent.hash_name(r.name), r.name, r.creation_time)
        target = parent.dead_items.get(key)
        if target is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                "Target not found in parent's dead map"
            )
        if (self.shard_id != metadata_utils.shard_from_inode(target.inode_id)
            and target.is_owning):
            return MetadataError(
                MetadataErrorKind.NOT_AUTHORISED,
                'Target is an owning reference to a remote file'
            )
        if (target.is_owning):
            assert target.type != InodeType.DIRECTORY
            # the target is an owning link to a shard-local file
            # before purging the target, need to transfer the file to eden
            # and then eden cleanup will expunge it later
            file = self.inodes.pop(target.inode_id)
            assert isinstance(file, File)
            self.eden[target.inode_id] = EdenFile(
                f=file,
                deadline_time=now,
                last_span_state=SpanState.CLEAN,
            )
        # if adjacent element is None, it also needs to be removed
        idx = parent.dead_items.index(key)
        if idx + 1 < len(parent.dead_items):
            next_key, next_val = parent.dead_items.peekitem(idx + 1)
            if next_val is None:
                del parent.dead_items[next_key]
        del parent.dead_items[key]
        if (parent.parent_inode == metadata_utils.NULL_INODE
            and len(parent.dead_items) == 0):
            print(f'PURGING INODE {r.parent_inode}; reason: last item purged;')
            del self.inodes[r.parent_inode]
        return PurgeDirentResp(r.parent_inode, r.name, r.creation_time)

    def do_visit_inodes(self, r: VisitInodesReq) -> RespBodyTy:
        budget = metadata_utils.UDP_MTU - MetadataResponse.SIZE - VisitInodesResp.SIZE
        num_inodes_to_fetch = (budget // 8) + 1 # include continuation key
        inode_range = self.inodes.irange(r.begin_inode)
        inodes = list(itertools.islice(inode_range, num_inodes_to_fetch))
        continuation_key = 0xFFFF_FFFF_FFFF_FFFF
        if len(inodes) == num_inodes_to_fetch:
            continuation_key = inodes[-1]
            inodes = inodes[:-1]
        return VisitInodesResp(continuation_key, inodes)

    def do_visit_eden(self, r: VisitEdenReq) -> RespBodyTy:
        budget = metadata_utils.UDP_MTU - MetadataResponse.SIZE - VisitEdenResp.SIZE
        num_files_to_fetch = (budget // 16) + 1 # include continuation key
        begin_idx = self.eden.bisect_left(r.begin_inode)
        end_idx = begin_idx + num_files_to_fetch
        eden_vals = [
            EdenVal(inode, eden_file.deadline_time)
            for inode, eden_file in self.eden.items()[begin_idx:end_idx]
        ]
        continuation_key = 0xFFFF_FFFF_FFFF_FFFF
        if len(eden_vals) == num_files_to_fetch:
            continuation_key = eden_vals[-1].inode
            eden_vals = eden_vals[:-1]
        return VisitEdenResp(continuation_key, eden_vals)

    def do_reverse_block_query(self, r: ReverseBlockQueryReq) -> RespBodyTy:
        bserver = self.bserver_secret_to_id.get(r.bserver_secret)
        if bserver is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Could not locate given bserver'
            )
        budget = metadata_utils.UDP_MTU - MetadataResponse.SIZE - ReverseBlockQueryResp.SIZE
        begin = BlockIndexKey(bserver, r.from_block_id)
        end = BlockIndexKey(bserver + 1, 0)
        blocks = []
        for key in self.reverse_block_index.irange(begin, end, inclusive=(True, False)):
            span = self.reverse_block_index[key]
            continuation_key = key.block_id
            queried_block = QueriedBlock(
                key.block_id, span.file_inode, span.byte_offset
            )
            size = queried_block.calc_packed_size()
            if budget < size:
                break
            budget -= size
            blocks.append(queried_block)
        else:
            continuation_key = 0xFFFF_FFFF_FFFF_FFFF
        return ReverseBlockQueryResp(continuation_key, blocks)

    def do_repair_block(self, r: RepairBlockReq) -> RespBodyTy:
        if r.source_inode == r.target_inode:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source and target must be distinct'
            )
        source = self.eden.get(r.source_inode)
        if source is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Source inode not found in eden'
            )
        eden_files = [('source', source)]
        inode_cookie_pairs = [(r.source_inode, r.source_cookie)]
        target_span_state = SpanState.CLEAN
        if r.target_cookie:
            eden_target = self.eden.get(r.target_inode)
            if eden_target is None:
                return MetadataError(
                    MetadataErrorKind.NOT_FOUND,
                    'Target inode not found in eden'
                )
            eden_files.append(('target', eden_target))
            inode_cookie_pairs.append((r.target_inode, r.target_cookie))
            target = eden_target.f
            if target.spans.bisect_right(r.byte_offset) == len(target.spans):
                # client wants to exchange block in final span
                target_span_state = eden_target.last_span_state
        else:
            maybe_target = self.inodes.get(r.target_inode)
            if maybe_target is None:
                return MetadataError(
                    MetadataErrorKind.NOT_FOUND,
                    'Target inode not found'
                )
            if not isinstance(maybe_target, File):
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    'Target not a file'
                )
            target = maybe_target
        if source.last_span_state != target_span_state:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source and target have inconsistent end span states'
                f' source={source.last_span_state};'
                f' target={target_span_state}'
            )
        for id, cookie in inode_cookie_pairs:
            if self._calc_cookie(id) != cookie:
                return MetadataError(
                    MetadataErrorKind.BAD_REQUEST,
                    f'Wrong cookie for {id}'
                )
        if len(source.f.spans) != 1:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source has more than one span'
            )
        source_span = source.f.spans[0]
        if (not isinstance(source_span.payload, list)
            or len(source_span.payload) != 1):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Source must have only one block'
            )
        source_block = source_span.payload[0]
        now = metadata_utils.now()
        for name, eden_file in eden_files:
            if now > eden_file.deadline_time:
                return MetadataError(
                    MetadataErrorKind.EDEN_DEADLINE,
                    f'{name} has expired deadline time'
                )
        target_span = target.spans.get(r.byte_offset)
        if target_span is None:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Offset not found in target'
            )
        if not isinstance(target_span.payload, list):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Target span is blockless'
            )

        for idx, target_block in enumerate(target_span.payload):
            if target_block.block_id == r.faulty_block_id:
                break
        else:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Fault block not found in target'
            )

        if source_block.crc32 != target_block.crc32:
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'CRCs do not match'
            )

        # passed all our checks... we can do the swap
        source_span.payload[0] = target_block
        target_span.payload[idx] = source_block
        for _, eden_file in eden_files:
            eden_file.deadline_time = now + DEADLINE_DURATION_NS

        return RepairBlockResp()

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
        now = metadata_utils.now()
        if (self.last_created_inode == r.token_inode
            or self.last_created_inode == metadata_utils.NULL_INODE):
            # implies the previous created dir succeeded, so we need to create
            # a "true" new directory (can't just reuse the existing one)
            new_dir_inode = self._dispense_inode()
            self.inodes[new_dir_inode] = Directory(
                parent_inode=r.new_parent,
                mtime=now,
                opaque=r.opaque,
            )
            self.last_created_inode = new_dir_inode
        else:
            # implies the previous created dir failed and didn't get linked in
            # so we can reuse it
            recycled_dir = self.inodes[self.last_created_inode]
            assert isinstance(recycled_dir, Directory)
            recycled_dir.parent_inode = r.new_parent
            recycled_dir.mtime = now
            recycled_dir.opaque = r.opaque
            new_dir_inode = self.last_created_inode
        return CreateDirResp(
            inode=new_dir_inode,
            mtime=now,
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
        new_val = LivingValue(
            r.creation_ts,
            r.child_inode,
            r.child_type,
            False,
        )
        error = _try_link(parent, LivingKey(hashed_name, r.subname), new_val,
            metadata_utils.now(), dry=r.dry)
        if error is not None:
            return error
        return InjectDirentResp(r.dry)

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

        res.is_owning = False

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
        now = metadata_utils.now()
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
                    assert not target.is_owning
                    dead_value = DeadValue(target.inode_id, target.type,
                        target.is_owning)
                    parent.dead_items[dead_key] = dead_value
                    placeholder_key = DeadKey(hashname, r.subname, now)
                    parent.dead_items[placeholder_key] = None
                    del parent.living_items[living_key]
                else:
                    target.is_owning = True
        return ReleaseDirentResp(found)

    def do_purge_remote_owning_dirent(self, r: PurgeRemoteOwningDirentReq
        ) -> RespBodyTy:

        parent = self.inodes.get(r.parent_inode)
        if parent is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Could not find parent'
            )
        if not isinstance(parent, Directory):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Parent not a directory'
            )
        key = DeadKey(parent.hash_name(r.name), r.name, r.creation_time)
        target = parent.dead_items.get(key)
        if target is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                "Target not found in parent's dead map"
            )
        if (self.shard_id == metadata_utils.shard_from_inode(target.inode_id)
            or not target.is_owning):
            return MetadataError(
                MetadataErrorKind.BAD_REQUEST,
                'Target is not an owning reference to a remote file'
            )
        # if adjacent element is None, it also needs to be removed
        idx = parent.dead_items.index(key)
        if idx + 1 < len(parent.dead_items):
            next_key, next_val = parent.dead_items.peekitem(idx + 1)
            if next_val is None:
                del parent.dead_items[next_key]
        del parent.dead_items[key]
        if (parent.parent_inode == metadata_utils.NULL_INODE
            and len(parent.dead_items) == 0):
            print(f'PURGING INODE {r.parent_inode}; reason: last item purged;')
            del self.inodes[r.parent_inode]
        return PurgeRemoteOwningDirentResp(r.parent_inode, r.name,
            r.creation_time)

    def do_move_file_to_eden(self, r: MoveFileToEdenReq) -> RespBodyTy:
        target = self.inodes.get(r.inode)
        if target is None:
            return MetadataError(
                MetadataErrorKind.NOT_FOUND,
                'Target not found'
            )
        if not isinstance(target, File):
            return MetadataError(
                MetadataErrorKind.BAD_INODE_TYPE,
                'Target not a file'
            )
        self.eden[r.inode] = EdenFile(target, metadata_utils.now(),
            SpanState.CLEAN)
        del self.inodes[r.inode]
        return MoveFileToEdenResp(r.inode)

    def _dispense_inode(self) -> int:
        ret = self.next_inode_id
        self.next_inode_id += 0x100 # preserve LSB
        return ret

    def _dispense_block_id(self, now: int) -> int:
        # now is embedded into the id
        # (other than LSB which is shard)
        ret = max(self.last_block_id + 0x100, self.shard_id | (now & ~0xFF))
        self.last_block_id = ret
        return ret

    def _dispense_bserver_id(self) -> int:
        ret = self.next_bserver_id
        self.next_bserver_id += 1
        return ret

    def _calc_cookie(self, eden_inode: int) -> int:
        b = eden_inode.to_bytes(8, 'little') + (b'\x00' * 8)
        mac = crypto.cbc_mac_impl(b, self.secret_key)
        return int.from_bytes(mac[-8:], 'little')


SHUCKLE_POLL_PERIOD_SEC = 60.0


def run_forever(shard: MetadataShard, db_fn: str) -> None:
    port = metadata_utils.shard_to_port(shard.shard_id)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    shuckle_data = try_fetch_block_metadata(shard)
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
                elif isinstance(request.body, AddEdenSpanReq):
                    resp_body = shard.do_add_eden_span(request.body,
                        shuckle_data)
                    dirty = True
                elif isinstance(request.body, CertifyEdenSpanReq):
                    resp_body = shard.do_certify_eden_span(request.body,
                        shuckle_data)
                    dirty = True
                elif isinstance(request.body, LinkEdenFileReq):
                    resp_body = shard.do_link_eden_file(request.body)
                    dirty = True
                elif isinstance(request.body, RepairSpansReq):
                    resp_body = shard.do_repair_spans(request.body)
                    dirty = True
                elif isinstance(request.body, ExpungeEdenSpanReq):
                    resp_body = shard.do_expunge_eden_span(request.body,
                        shuckle_data)
                    dirty = True
                elif isinstance(request.body, DeleteFileReq):
                    resp_body = shard.do_delete_file(request.body)
                    dirty = True
                elif isinstance(request.body, CertifyExpungeReq):
                    resp_body = shard.do_certify_expunge(request.body,
                        shuckle_data)
                    dirty = True
                elif isinstance(request.body, ExpungeEdenFileReq):
                    resp_body = shard.do_expunge_eden_file(request.body)
                    dirty = True
                elif isinstance(request.body, FetchSpansReq):
                    resp_body = shard.do_fetch_spans(request.body, shuckle_data)
                elif isinstance(request.body, SameDirRenameReq):
                    resp_body = shard.do_same_dir_rename(request.body)
                elif isinstance(request.body, PurgeDirentReq):
                    resp_body = shard.do_purge_dirent(request.body)
                    dirty = True
                elif isinstance(request.body, VisitInodesReq):
                    resp_body = shard.do_visit_inodes(request.body)
                elif isinstance(request.body, VisitEdenReq):
                    resp_body = shard.do_visit_eden(request.body)
                elif isinstance(request.body, ReverseBlockQueryReq):
                    resp_body = shard.do_reverse_block_query(request.body)
                elif isinstance(request.body, RepairBlockReq):
                    resp_body = shard.do_repair_block(request.body)
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
                elif isinstance(request.body, PurgeRemoteOwningDirentReq):
                    resp_body = shard.do_purge_remote_owning_dirent(
                        request.body)
                    dirty = True
                elif isinstance(request.body, MoveFileToEdenReq):
                    resp_body = shard.do_move_file_to_eden(request.body)
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
            maybe_shuckle_data = try_fetch_block_metadata(shard)
            if maybe_shuckle_data is not None:
                shuckle_data = maybe_shuckle_data
                metadata_utils.persist(shard, db_fn)
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
