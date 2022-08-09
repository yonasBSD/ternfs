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
import struct
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

# one hour in ns
DEADLINE_DURATION_NS = 1000 * 1000 * 1000 * 60 * 60


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
    ip: bytes
    port: int
    block_id: int
    crc32: int
    size: int
    block_idx: int

    def create_cert(self, key: crypto.ExpandedKey) -> bytes:
        b = bytearray(32)
        struct.pack_into('<cQII', b, 0, b'w', self.block_id, self.crc32,
            self.size)
        return crypto.cbc_mac_impl(b, key)[:8]


@dataclass
class Span:
    parity: int # (two nibbles, 8+3 is stored as (8,3))
    storage_class: int # opaque (except inline and zero-fill)
    crc32: int
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
            return crypto.crc32c(b'\x00' * self.size) == self.crc32.to_bytes(
                4, 'little')

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
    ip: bytes
    port: int
    is_terminal: bool
    is_stale: bool
    write_weight: float
    secret_key: crypto.ExpandedKey

    @staticmethod
    def from_shuckle(json_obj: Dict[str, Any]) -> 'BlockServerInfo':
        return BlockServerInfo(
            ip=socket.inet_aton(json_obj['ip']),
            port=json_obj['port'],
            is_terminal=json_obj['is_terminal'],
            is_stale=json_obj['is_stale'],
            write_weight=json_obj['write_weight'],
            secret_key=crypto.aes_expand_key(
                bytes.fromhex(json_obj['secret_key'])),
        )


class ShuckleData:
    def __init__(self, block_meta: Sequence[BlockServerInfo]):
        self.block_meta = block_meta

        if not block_meta:
            self._cum_weights = None
        else:
            if any(b.write_weight < 0.0 for b in block_meta):
                raise ValueError('Negative weights not OK')
            cum_weights = list(
                itertools.accumulate(b.write_weight for b in block_meta)
            )
            if math.isclose(cum_weights[-1], 0.0):
                self._cum_weights = None
            else:
                self._cum_weights = cum_weights

        self._block_index = {
            (bserver.ip, bserver.port): idx
            for idx, bserver in enumerate(block_meta)
        }

    def try_pick_blocks(self, num: int) -> Optional[List[BlockServerInfo]]:
        if self._cum_weights is None:
            return None
        return random.choices(self.block_meta, cum_weights=self._cum_weights,
            k=num)

    def lookup(self, ip: bytes, port: int) -> Optional[BlockServerInfo]:
        maybe_idx = self._block_index.get((ip, port))
        if maybe_idx is None:
            return None
        return self.block_meta[maybe_idx]


# returned in the order from shuckle
# (which doesn't appear to be specifically ordered currently)
def try_fetch_block_metadata() -> Optional[ShuckleData]:
    try:
        resp = requests.get('http://localhost:5000/show_me_what_you_got')
        resp.raise_for_status()
        block_meta = [
            BlockServerInfo.from_shuckle(datum) for datum in resp.json()
        ]
        return ShuckleData(block_meta)
    except Exception as e:
        print('WARNING: failed to get shuckle data:', e)
        return None


class MetadataShard:
    def __init__(self, shard: int):
        assert 0 <= shard <= 255
        self.shard_id = shard
        self.next_inode_id = shard | 0x100 # 00-FF is reserved
        self.last_block_id = shard | 0x100
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
                MetadataErrorKind.EDEN_TIME_EXPIRED,
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
                target_block_servers = s.try_pick_blocks(num_blocks)
                if target_block_servers is None:
                    return MetadataError(
                        MetadataErrorKind.SHUCKLE_ERROR,
                        'No block servers available'
                    )

                assert len(target_block_servers) == len(r.payload)
                new_blocks = [
                    Block(
                        bserver.ip,
                        bserver.port,
                        self._dispense_block_id(now),
                        binfo.crc32,
                        binfo.size,
                        idx
                    )
                    for idx, (binfo, bserver)
                    in enumerate(zip(r.payload, target_block_servers))
                ]
                new_span = Span(
                    r.parity_mode,
                    r.storage_class,
                    r.crc32,
                    r.size,
                    new_blocks
                )
                resp_payload = [
                    BlockInfo(
                        binfo.ip,
                        binfo.port,
                        binfo.block_id,
                        binfo.create_cert(bserver.secret_key)
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
                    bserver = s.lookup(b.ip, b.port)
                    if bserver is None:
                        return MetadataError(
                            MetadataErrorKind.SHUCKLE_ERROR,
                            f'Storage node ({socket.inet_ntoa(b.ip)}, {b.port})'
                            ' not found'
                        )
                    resp_payload.append(
                        BlockInfo(
                            b.ip,
                            b.port,
                            b.block_id,
                            b.create_cert(bserver.secret_key)
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
                MetadataErrorKind.EDEN_TIME_EXPIRED,
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
            bserver = s.lookup(block.ip, block.port)
            if bserver is None:
                return MetadataError(
                    MetadataErrorKind.SHUCKLE_ERROR,
                    f'Storage node ({socket.inet_ntoa(block.ip)}, {block.port})'
                    ' not found'
                )
            b = bytearray(16)
            struct.pack_into('<cQ', b, 0, b'W', block.block_id)
            mac = crypto.cbc_mac_impl(b, bserver.secret_key)
            if mac[:8] != proof:
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

    def _dispense_block_id(self, now: int) -> int:
        # now is embedded into the id
        # (other than LSB which is shard)
        ret = max(self.last_block_id + 0x100, self.shard_id | (now & ~0xFF))
        self.last_block_id = ret
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
    shuckle_data = try_fetch_block_metadata()
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
            if maybe_shuckle_data is not None:
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
