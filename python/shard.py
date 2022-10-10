#!/usr/bin/env python3
import argparse
from ast import BitAnd
from asyncio import wait_for
from enum import IntEnum
from inspect import BoundArguments
from multiprocessing.connection import wait
import sqlite3
import sys
import os
import tempfile
from typing import Any, TypedDict, cast
from dataclasses import replace
import hashlib
import secrets
import math
import struct
import random
import socket
import traceback
import json
import unittest
import logging
import requests
from multiprocessing import Process

import crypto
from common import *
from shard_msgs import *
import cdc_key

# TODO add an execute_many that does everything in a single transaction, for test speed (e.g. the one that creates a ton of files is slow)
# TODO go through all repeats=1 and make sure they make sense
# TODO consider revising edge operations to always take the modification time, for safety (much like they also now take the inode)

def crc32_to_int(b: bytes) -> int:
    return struct.unpack('<I', b)[0]
def crc32_from_int(i: int) -> bytes:
    return struct.pack('<I', i)

# one hour in ns
DEADLINE_DURATION = 60 * 60 * 1000 * 1000 * 1000

class SpanState(enum.IntEnum):
    CLEAN = 0
    DIRTY = 1
    CONDEMNED = 2

def init_db(shard: int, db: sqlite3.Connection):
    # TODO go through indices/queries and make sure that the indices we have make sense, referring to the queries that need them
    assert 0 <= shard < 256
    cur = db.cursor()
    cur.execute('pragma foreign_keys = ON')
    cur.execute('pragma journal_mode = WAL')
    # For some reason you can't splice parameters in check constraints
    cur.execute(f'''
        create table if not exists shard (
            shard integer primary key not null check (shard = {shard}),
            secret_key blob not null,
            next_file_id integer not null,
            next_symlink_id integer not null,
            next_block_id integer not null
        )
    ''')
    cur.execute(
        f'insert or ignore into shard values (:shard, :secret_key, :shard, :shard, :shard)',
        {'shard': shard, 'secret_key': secrets.token_bytes(16)}
    )
    cur.execute('''
        create table if not exists block_servers (
            id integer primary key autoincrement,
            secret_key blob not null unique,
            ip str not null,
            port integer not null,
            storage_class integer not null,
            terminal bool not null,
            stale bool not null,
            write_weight real not null,
            -- This is whether the block server was present in the last shuckle
            -- update. We keep all the others to have stable ids.
            active bool not null
        )
    ''')
    cur.execute('create unique index if not exists block_servers_by_addr on block_servers (ip, port)')
    cur.execute('create index if not exists block_servers_by_storage_class on block_servers (storage_class)')
    # TODO is it worth including creation time for files/dir objects themselves?
    # I think it _would_ be useful for forensics/debugging
    cur.execute(f'''
        create table if not exists files (
            transient bool not null,
            id integer primary key not null check (id >= 0 and id != {NULL_INODE_ID}),
            size integer not null,
            mtime integer not null,
            -- these are present only if the file is transient, which we check below
            deadline integer,
            last_span_state integer,
            check ((id & 0xFF) = {shard}),
            check (((id >> 61) & 0x03) in ({InodeType.FILE}, {InodeType.SYMLINK})),
            check (not transient or (deadline is not null and last_span_state is not null)),
            check (transient or (deadline is null and last_span_state is null))
        )
    ''')
    cur.execute('create index if not exists files_by_transient on files (transient, id)')
    cur.execute(f'''
        create table if not exists spans (
            file_id integer not null,
            byte_offset integer not null,
            state integer not null,
            storage_class integer not null,
            parity integer not null,
            crc32 integer not null,
            size integer not null,
            -- * ZERO_FILL_STORAGE: empty blob
            -- * INLINE_STORAGE: blob with length < 256
            -- * All the others: json list of Blocks
            body any not null,
            primary key (file_id, byte_offset),
            foreign key (file_id) references files (id),
            check (state in ({SpanState.CLEAN}, {SpanState.DIRTY}, {SpanState.CONDEMNED})),
            check (storage_class not in ({INLINE_STORAGE}, {ZERO_FILL_STORAGE}) or parity = 0)
        )
    ''')
    cur.execute(f'''
        create table if not exists directories (
            id integer primary key not null check (id >= 0 and id != {NULL_INODE_ID}),
            owner_id integer not null,
            mtime integer not null,
            hash_mode integer not null,
            opaque blob not null,
            check ((id & 0xFF) = {shard}),
            check (((id >> 61) & 0x03) = {InodeType.DIRECTORY}),
            check (owner_id = {NULL_INODE_ID} or ((owner_id >> 61) & 0x03) = {InodeType.DIRECTORY})
        )
    ''')
    if shard == inode_id_shard(ROOT_DIR_INODE_ID):
        create_directory_inode(
            cur, CreateDirectoryINodeReq(id=ROOT_DIR_INODE_ID, owner_id=NULL_INODE_ID, opaque=b'')
        )
    cur.execute(f'''
        create table if not exists edges (
            dir_id integer not null,
            name_hash integer not null,
            name blob not null,
            creation_time integer not null,
            current boolean not null,
            -- We use the special NULL_INODE_ID to indicate removal edges.
            target_id integer not null check (target_id >= 0),
            -- We could just use the same boolean, but using different one for clarity
            -- and to increase the likelihood that the operations manipulating them
            -- set and remove them correctly.
            owned boolean,
            locked boolean,
            primary key (dir_id, name_hash, name, creation_time),
            foreign key (dir_id) references directories (id),
            check (target_id != {NULL_INODE_ID} or not current),
            check (not current or (owned is null and locked is not null)),
            check (current or (locked is null and owned is not null))
        )
    ''')
    cur.execute('create index if not exists edges_name_time on edges (dir_id, name, creation_time)')
    cur.execute('create index if not exists edges_name_current on edges (dir_id, name, current)')
    # the "reverse block index"
    cur.execute(f'''
        create table if not exists block_to_span (
            block_server_id integer not null,
            block_id integer not null,
            file_id integer not null,
            byte_offset integer not null,
            primary key (block_server_id, block_id),
            -- the defer makes writing some code a bit simpler
            foreign key (file_id, byte_offset) references spans (file_id, byte_offset) deferrable initially deferred
        )
    ''')
    db.commit()

class Block(TypedDict):
    block_server_id: int
    block_id: int
    crc32: int
    size: int

def block_add_cert(block: Block, key: crypto.ExpandedKey) -> bytes:
    assert isinstance(key, list) and len(key) == 44
    b = bytearray(32)
    struct.pack_into('<cQ4sI', b, 0, b'w', block['block_id'], crc32_from_int(block['crc32']), block['size'])
    return crypto.cbc_mac_impl(b, key)[:8]

def block_add_proof(block: Union[Block, BlockInfo], key: crypto.ExpandedKey) -> bytes:
    assert isinstance(key, list) and len(key) == 44
    b = bytearray(16)
    block_id = block.block_id if isinstance(block, BlockInfo) else block['block_id']
    struct.pack_into('<cQ', b, 0, b'W', block_id)
    return crypto.cbc_mac_impl(b, key)[:8]

def block_delete_cert(block: Block, key: crypto.ExpandedKey) -> bytes:
    assert isinstance(key, list) and len(key) == 44
    b = bytearray(16)
    struct.pack_into('<cQ', b, 0, b'e', block['block_id'])
    return crypto.cbc_mac_impl(b, key)[:8]

def block_delete_prof(block: Block, key: crypto.ExpandedKey) -> bytes:
    assert isinstance(key, list) and len(key) == 44
    b = bytearray(16)
    struct.pack_into('<cQ', b, 0, b'E', block['block_id'])
    return crypto.cbc_mac_impl(b, key)[:8]

class Span(TypedDict):
    parity: int
    storage_class: int
    crc32: int
    body: Union[bytes, List[Block]]
    size: int

def check_span_body(span: Span) -> bool:
    crc32 = crc32_from_int(span['crc32'])

    # The two special cases
    if span['storage_class'] == ZERO_FILL_STORAGE:
        if span['body'] != b'' or span['parity'] != 0:
            return False
        if crypto.crc32c(b'\0' * span['size']) != crc32 :
            return False
        return True
    if span['storage_class'] == INLINE_STORAGE:
        if not (span['parity'] == 0 and isinstance(span['body'], bytes) and span['size'] == len(span['body'])):
            return False
        if crypto.crc32c(span['body']) != crc32:
            return False
        return True

    if not isinstance(span['body'], list):
        return False
    
    # check size consistency
    data_blocks = num_data_blocks(span['parity'])
    implied_size = sum(b['size'] for b in span['body'][:data_blocks])
    if span['size'] != implied_size:
        return False
    
    # consistency check for mirroring: they must be all the same
    if data_blocks == 1:
        for block in span['body']:
            if block['crc32'] != span['crc32']:
                return False
    # consistency check for general case
    else:
        # For parity mode 1+M (mirroring), for any M, the server can validate
        # that the CRC of the span equals the CRC of every block.
        # For N+M in the general case, it can probably validate that the CRC of
        # the span equals the concatenation of the CRCs of the N, and that the
        # CRC of the 1st of the M equals the XOR of the CRCs of the N.
        # It cannot validate anything about the rest of the M though (scrubbing
        # can validate the rest, if it wants to, but it would be too expensive
        # for the shard to validate).
        blocks_crc32 = b'\0\0\0\0'
        for block in span['body'][:data_blocks]:
            blocks_crc32 = crypto.crc32c_combine(blocks_crc32, crc32_from_int(block['crc32']), block['size'])
        if crc32 != blocks_crc32:
            return False
        # TODO check parity block CRC32

    return True

def pick_block_servers(cur: sqlite3.Cursor, storage_class: int, num: int) -> Optional[List[Dict[str, Any]]]:
    servers = cur.execute(
        '''
            select id, ip, port, secret_key, sum(write_weight) over (rows unbounded preceding) as cum_weight from block_servers
                where storage_class = :storage_class and active and not terminal and not stale
        ''',
        {'storage_class': storage_class}
    ).fetchall()
    if not servers:
        return None
    # can't use random.choices in prod
    # needs to select bservers on different "failure domains"
    return random.choices(servers, cum_weights=[s['cum_weight'] for s in servers], k=num)

def lookup_block_server(cur: sqlite3.Cursor, id: int) -> Optional[Dict[str, Any]]:
    return cur.execute('select ip, port, terminal, stale, secret_key from block_servers where id = :id', {'id': id}).fetchone()

class ShardInfo:
    def __init__(self, cur: sqlite3.Cursor):
        row = cur.execute('select shard, secret_key, next_file_id, next_symlink_id, next_block_id from shard').fetchone()
        self._shard = row['shard']
        self._raw_secret_key = row['secret_key']
        self._secret_key = crypto.aes_expand_key(self._raw_secret_key)
        self._next_file_id = row['next_file_id']
        self._next_symlink_id = row['next_symlink_id']
        self._next_block_id = row['next_block_id']
    
    @property
    def shard(self) -> int:
        return self._shard
    @property
    def secret_key(self) -> crypto.ExpandedKey:
        return self._secret_key
    
    def next_inode_id(self, cur: sqlite3.Cursor, type: InodeType):
        if type == InodeType.FILE:
            id = self._next_file_id
            self._next_file_id += 0x100
            self._update(cur)
            return make_inode_id(type, self.shard, id)
        elif type == InodeType.SYMLINK:
            id = self._next_symlink_id
            self._next_symlink_id += 0x100
            self._update(cur)
            return make_inode_id(type, self.shard, id)
        else:
            assert False, f'Bad inode type {type}'
    
    def next_block_id(self, cur: sqlite3.Cursor, now: int):
        # now is embedded into the id, other than LSB which is shard
        ret = max(self._next_block_id + 0x100, self._shard | (now & ~0xFF))
        self._next_block_id = ret
        self._update(cur)
        return ret
    
    def _update(self, cur: sqlite3.Cursor):
        cur.execute(
            'update shard set next_file_id = :next_file_id, next_symlink_id = :next_symlink_id, next_block_id = :next_block_id',
            {'next_file_id': self._next_file_id, 'next_symlink_id': self._next_symlink_id, 'next_block_id': self._next_block_id}
        )

CHECK_CONSTRAINTS = True

class _EnableConstraints:
    def __enter__(self):
        global CHECK_CONSTRAINTS
        self.before = CHECK_CONSTRAINTS
        CHECK_CONSTRAINTS = True
    
    def __exit__(self, exc_type, exc_value, traceback):
        global CHECK_CONSTRAINTS
        CHECK_CONSTRAINTS = self.before
EnableConstraints = _EnableConstraints()

class _DisableLogging:
    def __enter__(self):
        self.before = logging.root.level
        logging.root.setLevel(logging.CRITICAL)
    
    def __exit__(self, exc_type, exc_value, traceback):
        logging.root.setLevel(self.before)
DisableLogging = _DisableLogging()

def check_edge_constraints(cur: sqlite3.Cursor, dir_id: int) -> bool:
    if not CHECK_CONSTRAINTS: return True
    # No multiple current edges with same name
    num_edges_per_name = cur.execute(
        'select name, count(*) as outgoing from edges where dir_id = :dir_id and current group by name having outgoing > 1', 
        {'dir_id': dir_id}
    ).fetchone()
    if num_edges_per_name is not None:
        logging.error(f'Name {num_edges_per_name["name"]} has {num_edges_per_name["outgoing"]} edges out of dir {dir_id}')
        return False
    # No snapshot edge more recent than current edge
    more_recent_snapshot = cur.execute(
        '''
            select e1.name from edges as e1, edges as e2
                where
                    e1.dir_id = :dir_id and e2.dir_id = :dir_id and e1.name = e2.name and   -- edges match
                    not e1.current and e2.current and e1.creation_time >= e2.creation_time  -- non-current edge is more recent
        ''',
        {'dir_id': dir_id}
    ).fetchone()
    if more_recent_snapshot is not None:
        logging.error(f'Name {more_recent_snapshot["name"]} in dir {dir_id} has a snapshot edge more recent than the current edge')
        return False
    # no file we point to is transient
    edges_to_transient = cur.execute(
        'select name, target_id from edges, files where dir_id = :dir_id and edges.target_id = files.id and files.transient',
        {'dir_id': dir_id}
    ).fetchone()
    if edges_to_transient is not None:
        logging.error(f'Name {edges_to_transient["name"]} in dir {dir_id} points to transient file {edges_to_transient["target_id"]}')
        return False
    return True

def check_spans_constraints(cur: sqlite3.Cursor, file_id: int) -> bool:
    if not CHECK_CONSTRAINTS: return True
    # there's clean spans, then at most one dirty/condemned span
    # there's a suffix of dirty/condemned spans, and afterwards, clean ones.
    # TODO this could be done more efficiently with smarter queries
    entries = cur.execute('select * from spans where file_id = :file_id order by byte_offset asc', {'file_id': file_id})
    last_span_state = SpanState.CLEAN
    for entry in entries:
        if last_span_state != SpanState.CLEAN:
            return False
        last_span_state = entry['state']
    file = cur.execute('select * from files where id = :file_id', {'file_id': file_id}).fetchone()
    file_last_span_state = file['last_span_state'] if file['transient'] else SpanState.CLEAN
    if file_last_span_state != last_span_state:
        return False    
    if last_span_state != SpanState.CLEAN:
        if not file['transient']:
            return False
    return True

# not read only
def create_directory_inode(cur: sqlite3.Cursor, req: CreateDirectoryINodeReq) -> Union[EggsError, CreateDirectoryINodeResp]:
    if inode_id_type(req.id) != InodeType.DIRECTORY:
        return EggsError(ErrCode.TYPE_IS_NOT_DIRECTORY)
    now = eggs_time()
    existing = cur.execute('select * from directories where id = ?', (req.id,)).fetchone()
    if existing is None:
        sql_insert(
            cur, 'directories',
            id=req.id, owner_id=req.owner_id, mtime=now, hash_mode=HashMode.TRUNC_MD5, opaque=req.opaque,
        )
    else:
        # The assumption here is that only the CDC creates directories, and it doles out
        # inode ids per transaction, so that you'll never get competing creates here, but
        # we still check that the parent makes sense.
        if req.owner_id != existing['owner_id']:
            return EggsError(ErrCode.MISMATCHING_OWNER)
        cur.execute(
            'update directories set mtime = :mtime, opaque = :opaque where id = :id',
            {'mtime': now, 'opaque': req.opaque, 'id': req.id}
        )
    return CreateDirectoryINodeResp(mtime=now)

# not read only
def set_directory_owner(cur: sqlite3.Cursor, req: SetDirectoryOwnerReq) -> Union[EggsError, SetDirectoryOwnerResp]:
    if req.owner_id == NULL_INODE_ID:
        # if we have any outgoing current edges, we can't disown this
        num_current_edges = cur.execute(
            'select count(*) as c from edges where dir_id = :dir_id and current', {'dir_id': req.dir_id}
        ).fetchone()['c']
        if num_current_edges > 0:
            return EggsError(ErrCode.DIRECTORY_NOT_EMPTY)
    # do the update
    cur.execute(
        'update directories set owner_id = :owner_id where id = :dir_id',
        {'owner_id': req.owner_id, 'dir_id': req.dir_id}
    )
    if cur.rowcount == 0:
        return EggsError(ErrCode.DIRECTORY_NOT_FOUND)
    return SetDirectoryOwnerResp()

def valid_name(b: bytes) -> bool:
    if b in (b'.', b'..'):
        return False
    if 0 in b or ord('/') in b:
        return False
    return True

class HashMode(IntEnum):
    TRUNC_MD5 = 1
def hash_name(hash_mode: HashMode, n: bytes) -> int:
    # for production we should consider alternatives, e.g. murmur3 or xxhash
    if hash_mode == HashMode.TRUNC_MD5:
        h = hashlib.md5()
        h.update(n)
        # Here we lose one bit of precision for simplicity because SQLite does not support
        # 64-bit unsigned
        return int.from_bytes(h.digest()[:8], 'little') & 0x7FFFFFFFFFFFFFFF
    else:
        raise ValueError(f'Unsupported hash mode: {hash_mode}')

# We cannot expose an API which allows us to create non-locked current edges, see comment for
# CreateLockedCurrentEdgeReq.
def create_current_edge_internal(
    cur: sqlite3.Cursor,
    req: CreateLockedCurrentEdgeReq,
    locked: bool,
) -> Union[EggsError, CreateLockedCurrentEdgeResp]:
    assert valid_name(req.name)
    dir = get_directory(cur, req.dir_id, ErrCode.DIRECTORY_NOT_FOUND)
    if isinstance(dir, EggsError):
        return dir
    name_hash = hash_name(dir['hash_mode'], req.name)
    # This fetches
    existing_current_edges = cur.execute(
        # This fetches the current edge (which we know to be the most recent one), and
        # at most another edge with creation time geq than what we want to create.
        '''
            select * from edges
                where dir_id = :dir_id and name = :name and (creation_time >= :creation_time or current)
                order by creation_time desc
                limit 2
        ''',
        {'dir_id': req.dir_id, 'name': req.name, 'creation_time': req.creation_time}
    ).fetchall()
    # This means some non-current edge is newer. We can't let it happen.
    if any([not e['current'] for e in existing_current_edges]):
        return EggsError(ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS)
    assert len(existing_current_edges) < 2
    existing_current_edge = existing_current_edges[0] if existing_current_edges else None
    def create():
        sql_insert(
            cur, 'edges',
            dir_id=req.dir_id, name_hash=name_hash, name=req.name, target_id=req.target_id, creation_time=req.creation_time, current=True, locked=locked
        )
    # We're the first one here, we can definitely create the edge
    if existing_current_edge is None:
        create()
    # We have an existing locked edge, we need to make sure that it's the one we expect for idempotency
    elif existing_current_edge['locked']:
        if not locked or ((req.target_id, req.creation_time) != (existing_current_edge['target_id'], existing_current_edge['creation_time'])):
            return EggsError(ErrCode.NAME_IS_LOCKED)
    # We're kicking out a non-locked current edge. The only circumstance where we allow
    # this automatically is if a file is overriding another file, which is also how it
    # works in linux/poxis (see `man 2 rename`).
    else:
        if inode_id_type(req.target_id) == InodeType.DIRECTORY or inode_id_type(existing_current_edge['target_id']) == InodeType.DIRECTORY:
            return EggsError(ErrCode.CANNOT_OVERRIDE_NAME)
        cur.execute(
            'update edges set current = FALSE, locked = NULL, owned = FALSE where dir_id = :dir_id and creation_time = :creation_time',
            {k: existing_current_edge[k] for k in ('dir_id', 'creation_time')},
        )
        create()
    return CreateLockedCurrentEdgeResp()

# not read only
def create_locked_current_edge(cur: sqlite3.Cursor, req: CreateLockedCurrentEdgeReq) -> Union[EggsError, CreateLockedCurrentEdgeResp]:
    res = create_current_edge_internal(cur, req, locked=True)
    assert check_edge_constraints(cur, req.dir_id)
    return res

# read only
def stat(cur: sqlite3.Cursor, req: StatReq) -> Union[EggsError, StatResp]:
    if inode_id_type(req.id) == InodeType.DIRECTORY:
        dir = get_directory(cur, req.id, ErrCode.DIRECTORY_NOT_FOUND)
        if isinstance(dir, EggsError):
            return dir
        return StatResp(mtime=dir['mtime'], size_or_owner=dir['owner_id'], opaque=dir['opaque'])
    else:
        file = cur.execute('select * from files where id = :id', {'id': req.id}).fetchone()
        if file is None:
            return EggsError(ErrCode.FILE_NOT_FOUND)
        return StatResp(mtime=file['mtime'], size_or_owner=file['size'], opaque=b'')


# read only
def read_dir(cur: sqlite3.Cursor, req: ReadDirReq) -> Union[EggsError, ReadDirResp]:
    dir = get_directory(cur, req.dir_id, ErrCode.DIRECTORY_NOT_FOUND)
    if isinstance(dir, EggsError):
        return dir
    entries = cur.execute(
        # The window selects the current row, and the previous row chronologically, excluding
        # everything after as_of. We want the rows that are first, and that have non-null target
        # id.
        #
        # TODO this will always consider at least one edge per name (current or snapshot), which
        # is not good if we create a million file and then remove them. We should
        # * Just use the `current` column in the common case
        # * Add a (dir_id, name_ash, creation_time) type index to make the other case faster too
        '''
            with results as (
                select row_number() over this_and_successor as row, target_id, name_hash, name
                    from edges
                    where dir_id = :dir_id and name_hash >= :next_hash and creation_time <= :as_of
                    window this_and_successor as (partition by name_hash, name order by creation_time desc range between 1 preceding and current row)
            ) select target_id, name_hash, name from results where row = 1 and target_id != :null;
        ''',
        {
            'dir_id': req.dir_id,
            'next_hash': req.start_hash,
            # usual sqlite problem that it cannot work with 64bit unsigned int
            'as_of': req.as_of_time & 0x7FFFFFFFFFFFFFFF,
            'null': NULL_INODE_ID,
        }
    )
    payloads: List[ReadDirPayload] = []
    curr_size = ShardResponse.SIZE + ReadDirResp.SIZE
    next_hash = 0
    # Note that this is assuming that we can always clear one next hash in one UDP_MTU
    # to make progress.
    for entry in entries:
        payload = ReadDirPayload(
            target_id=entry['target_id'],
            name_hash=entry['name_hash'],
            name=entry['name'],
        )
        if curr_size + payload.calc_packed_size() > UDP_MTU:
            next_hash = entry['name_hash']
            # Do not straddle with same hash -- we don't want repeated entries
            while payloads[-1].name_hash == next_hash:
                payloads.pop()
            break
        payloads.append(payload)
        curr_size += payload.calc_packed_size()
    return ReadDirResp(
        next_hash=next_hash,
        results=payloads,
    )

def calc_cookie(secret_key: crypto.ExpandedKey, inode: int):
    b = inode.to_bytes(8, 'little') + (b'\x00' * 8)
    mac = crypto.cbc_mac_impl(b, secret_key)
    return int.from_bytes(mac[-8:], 'little')

# not read only
def construct_file(cur: sqlite3.Cursor, req: ConstructFileReq) -> Union[EggsError, ConstructFileResp]:
    shard_info = ShardInfo(cur)
    id = shard_info.next_inode_id(cur, req.type)
    now = eggs_time()
    sql_insert(
        cur, 'files',
        id=id,
        size=0,
        mtime=now,
        transient=True,
        deadline=now+DEADLINE_DURATION,
        last_span_state=SpanState.CLEAN,
    )
    return ConstructFileResp(
        id=id,
        cookie=calc_cookie(shard_info.secret_key, id)
    )

# read only
def visit_transient_files(cur: sqlite3.Cursor, req: VisitTransientFilesReq) -> VisitTransientFilesResp:
    num_entries = (UDP_MTU-ShardResponse.SIZE-VisitTransientFilesResp.SIZE)//TransientFile.SIZE
    entries = cur.execute(
        'select * from files where transient and id >= :begin_id order by id asc limit :max_entries',
        {'begin_id': req.begin_id, 'max_entries': num_entries+1}
    ).fetchall()
    return VisitTransientFilesResp(
        next_id=0 if len(entries) < num_entries else entries[-1]['id'],
        files=[TransientFile(entry['id'], entry['deadline']) for entry in entries[:num_entries]]
    )

def get_transient_file(cur: sqlite3.Cursor, shard_info: ShardInfo, *, file_id: int, cookie: int, now: int):
    file = cur.execute('select * from files where id = :file_id and transient and deadline > :now', {'file_id': file_id, 'now': now}).fetchone()
    if file is None:
        return EggsError(ErrCode.FILE_NOT_FOUND)
    if cookie != calc_cookie(shard_info.secret_key, file['id']):
        return EggsError(ErrCode.BAD_COOKIE)
    return file

def add_span_initiate_inner(cur: sqlite3.Cursor, req: AddSpanInitiateReq) -> Union[EggsError, AddSpanInitiateResp]:
    now = eggs_time()
    shard_info = ShardInfo(cur)
    file = get_transient_file(cur, shard_info, file_id=req.file_id, cookie=req.cookie, now=now)
    if isinstance(file, EggsError):
        return file
    blockless = req.storage_class in (ZERO_FILL_STORAGE, INLINE_STORAGE)
    if req.parity == 0 and not blockless:
        return EggsError(ErrCode.INCONSISTENT_STORAGE_CLASS_PARITY)
    resp_blocks: List[BlockInfo] = []
    span_crc32 = crc32_to_int(req.crc32)
    # special case: adding a zero-sized span does nothing
    if req.size == 0:
        if req.storage_class != ZERO_FILL_STORAGE:
            return EggsError(ErrCode.BAD_SPAN_BODY)
    # req.byte_offset is at the end of file: we actually need to create a span
    elif req.byte_offset == file['size']:
        if file['last_span_state'] != SpanState.CLEAN:
            return EggsError(ErrCode.LAST_SPAN_STATE_NOT_CLEAN)
        span: Dict[str, Any] = {
            'parity': req.parity,
            'storage_class': req.storage_class,
            'crc32': span_crc32,
            'size': req.size,
        }
        if blockless:
            # nothing to do, just store the body, which won't be anything in zero-fill case
            assert isinstance(req.body, bytes)
            span['body'] = req.body
            span['state'] = SpanState.CLEAN
        else:
            # we need to actually store the span
            block_servers = pick_block_servers(cur, req.storage_class, num_total_blocks(req.parity))
            if block_servers is None:
                return EggsError(ErrCode.COULD_NOT_PICK_BLOCK_SERVERS)
            assert len(block_servers) == len(req.body)
            span['state'] = SpanState.DIRTY
            span['body'] = [] # the blocks
            for block_info, block_server in zip(cast(List[NewBlockInfo], req.body), block_servers):
                block_id = shard_info.next_block_id(cur, now)
                block: Block = {
                    'block_server_id': block_server['id'],
                    'block_id': block_id,
                    'crc32': crc32_to_int(block_info.crc32),
                    'size': block_info.size,
                }
                span['body'].append(block)
                sql_insert(
                    cur, 'block_to_span',
                    block_server_id=block['block_server_id'],
                    block_id=block_id,
                    file_id=req.file_id,
                    byte_offset=req.byte_offset,
                )
                resp_blocks.append(BlockInfo(
                    ip=socket.inet_aton(block_server['ip']),
                    port=block_server['port'],
                    block_id=block_id,
                    certificate=block_add_cert(block, crypto.aes_expand_key(block_server['secret_key'])),
                ))
        if not check_span_body(cast(Span, span)):
            return EggsError(ErrCode.BAD_SPAN_BODY)
        if isinstance(span['body'], list):
            span['body'] = json.dumps(span['body'])
        # add span
        sql_insert(cur, 'spans', file_id=req.file_id, byte_offset=req.byte_offset, **span)
        # increase file size
        cur.execute(
            'update files set size = :size, last_span_state = :span_state where id = :file_id',
            {'size': file['size']+req.size, 'file_id': req.file_id, 'span_state': span['state']}
        )
    # otherwise, we fetch the existing span
    else:
        existing_span = cur.execute(
            'select * from spans where file_id = :file_id and byte_offset = :byte_offset and storage_class = :storage_class and crc32 = :crc32 and size = :size and parity = :parity',
            {
                'file_id': req.file_id,
                'byte_offset': req.byte_offset,
                'storage_class': req.storage_class,
                'crc32': span_crc32,
                'size': req.size,
                'parity': req.parity,
            }
        ).fetchone()
        if existing_span is None:
            return EggsError(ErrCode.SPAN_NOT_FOUND)
        if not blockless:
            body = json.loads(existing_span['body'])
            assert isinstance(body, list)
            for block in body:
                bserver = lookup_block_server(cur, block['block_server_id'])
                if bserver is None:
                    return EggsError(ErrCode.BLOCK_SERVER_NOT_FOUND)
                resp_blocks.append(BlockInfo(
                    ip=socket.inet_aton(bserver['ip']),
                    port=bserver['port'],
                    block_id=block['block_id'],
                    certificate=block_add_cert(block, crypto.aes_expand_key(bserver['secret_key'])),
                ))
    # advance deadline
    cur.execute(
        'update files set deadline = :deadline where id = :file_id',
        {'deadline': now+DEADLINE_DURATION, 'file_id': req.file_id}
    )
    return AddSpanInitiateResp(resp_blocks)

def add_span_initiate(cur: sqlite3.Cursor, req: AddSpanInitiateReq) -> Union[EggsError, AddSpanInitiateResp]:
    res = add_span_initiate_inner(cur, req)
    assert check_spans_constraints(cur, req.file_id)
    return res

def add_span_certify_inner(cur: sqlite3.Cursor, req: AddSpanCertifyReq) -> Union[EggsError, AddSpanCertifyResp]:
    shard_info = ShardInfo(cur)
    now = eggs_time()
    file = get_transient_file(cur, shard_info, file_id=req.file_id, cookie=req.cookie, now=now)
    if isinstance(file, EggsError):
        return file
    # we do _not_ need to check if the last span state is dirty, instead we just check the
    # proofs and update blindly for idempotency.
    span = cur.execute(
        'select * from spans where file_id = :file_id and byte_offset = :byte_offset',
        {'file_id': req.file_id, 'byte_offset': req.byte_offset}
    ).fetchone()
    if span is None:
        return EggsError(ErrCode.SPAN_NOT_FOUND)
    if isinstance(span['body'], bytes):
        return EggsError(ErrCode.CANNOT_CERTIFY_BLOCKLESS_SPAN)
    span_body = cast(List[Block], json.loads(span['body']))
    if len(span_body) != len(req.proofs):
        return EggsError(ErrCode.BAD_NUMBER_OF_BLOCKS_PROOFS)
    for block, proof in zip(span_body, req.proofs):
        bserver = lookup_block_server(cur, block['block_server_id'])
        if bserver is None:
            return EggsError(ErrCode.BLOCK_SERVER_NOT_FOUND)
        if proof != block_add_proof(block, crypto.aes_expand_key(bserver['secret_key'])):
            return EggsError(ErrCode.BAD_BLOCK_PROOF)
    # update everything in an idempotent fashion
    cur.execute(
        'update spans set state = :state_after where file_id = :file_id and byte_offset = :byte_offset',
        {'file_id': req.file_id, 'byte_offset': req.byte_offset, 'state_after': SpanState.CLEAN}
    )
    cur.execute(
        'update files set last_span_state = :state_after, mtime = :now where id = :file_id and size = :expected_size',
        {'file_id': req.file_id, 'expected_size': req.byte_offset + span['size'], 'state_after': SpanState.CLEAN, 'now': now}
    )
    return AddSpanCertifyResp()

def add_span_certify(cur: sqlite3.Cursor, req: AddSpanCertifyReq) -> Union[EggsError, AddSpanCertifyResp]:
    res = add_span_certify_inner(cur, req)
    assert check_spans_constraints(cur, req.file_id)
    return res

# not read only
def link_file_inner(cur: sqlite3.Cursor, req: LinkFileReq) -> Union[LinkFileResp, EggsError]:
    now = eggs_time()
    shard_info = ShardInfo(cur)
    # First check if the file has been linked already
    not_transient = cur.execute('select not transient as not_transient from files where id = :file_id', {'file_id': req.file_id}).fetchone()
    if not_transient is not None and not_transient['not_transient']:
        # We assume that the call is idempotent without checking further, given
        # that transient file requests are cookie'd anyway.
        return LinkFileResp()
    file = get_transient_file(cur, shard_info, file_id=req.file_id, cookie=req.cookie, now=now)
    if isinstance(file, EggsError):
        return file
    if file['last_span_state'] != SpanState.CLEAN:
        return EggsError(ErrCode.LAST_SPAN_STATE_NOT_CLEAN)
    res = create_current_edge_internal(
        cur,
        locked=False,
        req=CreateLockedCurrentEdgeReq(
            dir_id=req.owner_id,
            name=req.name,
            target_id=req.file_id,
            creation_time=now,
        ),
    )
    cur.execute(
        'update files set transient = false, mtime = :now, deadline = null, last_span_state = null where id = :file_id',
        {'now': now, 'file_id': req.file_id}
    )
    if isinstance(res, EggsError):
        return res
    return LinkFileResp()

def link_file(cur: sqlite3.Cursor, req: LinkFileReq) -> Union[LinkFileResp, EggsError]:
    res = link_file_inner(cur, req)
    assert check_spans_constraints(cur, req.file_id)
    assert check_edge_constraints(cur, req.owner_id)
    return res

# read only
def file_spans(cur: sqlite3.Cursor, req: FileSpansReq) -> Union[FileSpansResp, EggsError]:
    file = cur.execute(
        'select id, transient from files where id = :file_id',
        {'file_id': req.file_id}
    ).fetchone()
    if file is None:
        return EggsError(ErrCode.FILE_NOT_FOUND)
    if file['transient']:
        return EggsError(ErrCode.FILE_IS_TRANSIENT)
    spans = cur.execute(
        'select * from spans where file_id = :file_id and byte_offset >= :byte_offset',
        {'file_id': req.file_id, 'byte_offset': req.byte_offset}
    ).fetchall() # eager fetch because we interact with the db later to lookup the blocks
    budget = UDP_MTU - ShardResponse.SIZE - FileSpansResp.SIZE_UPPER_BOUND
    resp_spans: List[FetchedSpan] = []
    next_offset = 0
    for span in spans:
        resp_body: Union[bytes, List[FetchedBlock]]
        if span['storage_class'] in (ZERO_FILL_STORAGE, INLINE_STORAGE):
            assert isinstance(span['body'], bytes)
            resp_body = span['body']
        else:
            assert isinstance(span['body'], str)
            blocks = cast(List[Block], json.loads(span['body']))
            resp_body = []
            for block in blocks:
                bserver = lookup_block_server(cur, block['block_server_id'])
                if bserver is None:
                    # don't fail just because we don't know where the bserver is, client may be able to continue using
                    # parity data
                    ip = b'\xff\xff\xff\xff'
                    port = 65535
                    flags = BlockFlags.STALE
                else:
                    ip = socket.inet_aton(bserver['ip'])
                    port = bserver['port']
                    flags = (bserver['terminal'] & BlockFlags.TERMINAL) | (bserver['stale'] & BlockFlags.STALE)
                resp_body.append(FetchedBlock(
                    ip=ip, port=port, block_id=block['block_id'], crc32=crc32_from_int(block['crc32']), size=block['size'], flags=flags
                ))
        resp_span = FetchedSpan(
            byte_offset=span['byte_offset'],
            parity=span['parity'],
            storage_class=span['storage_class'],
            crc32=crc32_from_int(span['crc32']),
            size=span['size'],
            body=resp_body,
        )
        size = resp_span.calc_packed_size()
        if size > budget:
            next_offset = resp_span.byte_offset
            break
        budget -= size
        resp_spans.append(resp_span)
    return FileSpansResp(next_offset, resp_spans)

# TODO check if we ever have anything but DIRECTORY_INODE_NOT_FOUND
def get_directory(cur: sqlite3.Cursor, id: int, not_found: ErrCode) -> Union[EggsError, Dict[str, Any]]:
    dir = cur.execute('select * from directories where id = :dir_id', {'dir_id': id}).fetchone()
    if dir is None:
        return EggsError(not_found)
    return dir

def get_current_edge(cur, *, dir_id: int, name: bytes, target_id: Union[int, None]) -> Union[Dict[str, Any], EggsError]:
    current_edge = cur.execute(
        'select * from edges where dir_id = :dir_id and current and name = :name',
        {'dir_id': dir_id, 'name': name, 'target_id': target_id}
    ).fetchone()
    if current_edge is None:
        if cur.execute('select id from directories where id = ?', (dir_id,)).fetchone():
            return EggsError(ErrCode.NAME_NOT_FOUND)
        else:
            return EggsError(ErrCode.DIRECTORY_NOT_FOUND)
    if target_id is not None and current_edge['target_id'] != target_id:
        return EggsError(ErrCode.MISMATCHING_TARGET)
    return current_edge

# not exposed to the api
def soft_unlink_current_edge(cur, *, dir_id: int, now: int, name: bytes, target_id: int) -> Union[EggsError, None]:
    dir = get_directory(cur, dir_id, ErrCode.DIRECTORY_NOT_FOUND)
    if isinstance(dir, EggsError):
        return dir
    current_edge = get_current_edge(cur, dir_id=dir_id, name=name, target_id=target_id)
    if isinstance(current_edge, EggsError):
        return current_edge
    if current_edge['locked']:
        return EggsError(ErrCode.NAME_IS_LOCKED)
    # first turn the current into not current, and also create the nothing edge to signify deletion
    cur.execute(
        '''
            update edges set current = FALSE, owned = TRUE, locked = NULL
                where dir_id = :dir_id and name = :name and creation_time = :time
        ''',
        {'dir_id': dir_id, 'name': name, 'time': current_edge['creation_time']}
    )
    sql_insert(
        cur, 'edges',
        dir_id=dir_id,
        name_hash=current_edge['name_hash'],
        name=name,
        creation_time=now,
        current=False,
        target_id=NULL_INODE_ID,
        owned=True,
    )
    return None

# not read only
def same_directory_rename_inner(cur: sqlite3.Cursor, req: SameDirectoryRenameReq) -> Union[EggsError, SameDirectoryRenameResp]:
    now = eggs_time()
    # First, remove the old edge
    res1 = soft_unlink_current_edge(cur, dir_id=req.dir_id, now=now, name=req.old_name, target_id=req.target_id)
    if isinstance(res1, EggsError):
        return res1
    # Now we create the new one
    res2 = create_current_edge_internal(cur, locked=False, req=CreateLockedCurrentEdgeReq(
        dir_id=req.dir_id, name=req.new_name, target_id=req.target_id, creation_time=now,
    ))
    if isinstance(res2, EggsError):
        return res2
    return SameDirectoryRenameResp()
def same_directory_rename(cur: sqlite3.Cursor, req: SameDirectoryRenameReq) -> Union[EggsError, SameDirectoryRenameResp]:
    res = same_directory_rename_inner(cur, req)
    assert check_edge_constraints(cur, req.dir_id)
    return res

# not read only
def soft_unlink_file_inner(cur: sqlite3.Cursor, req: SoftUnlinkFileReq) -> Union[EggsError, SoftUnlinkFileResp]:
    now = eggs_time()
    # We cannot rename directories within the shard -- the CDC is needed
    if inode_id_type(req.file_id) == InodeType.DIRECTORY:
        return EggsError(ErrCode.TYPE_IS_DIRECTORY)
    res = soft_unlink_current_edge(cur, dir_id=req.owner_id, now=now, name=req.name, target_id=req.file_id)
    if isinstance(res, EggsError):
        return res
    return SoftUnlinkFileResp()
def soft_unlink_file(cur: sqlite3.Cursor, req: SoftUnlinkFileReq) -> Union[EggsError, SoftUnlinkFileResp]:
    res = soft_unlink_file_inner(cur, req)
    assert check_edge_constraints(cur, req.owner_id)
    return res

# read only
def visit_inodes(cur: sqlite3.Cursor, req: VisitInodesReq) -> Union[EggsError, VisitInodesResp]:
    budget = UDP_MTU - ShardResponse.SIZE - VisitInodesResp.SIZE
    assert InodeType.DIRECTORY < InodeType.FILE < InodeType.SYMLINK
    begin_id = req.begin_id
    def q(table: str) -> List[int]:
        return list(map(
            lambda e: e['id'],
            cur.execute(
                f'select id from {table} where id >= :id order by id asc limit :num_inodes',
                {'id': begin_id, 'num_inodes': remaining_ids}
            ).fetchall()
        ))
    expected_ids = (budget//8) + 1 # include next inode
    remaining_ids = expected_ids
    ids = q('directories')
    remaining_ids -= len(ids)
    ids.extend(q('files'))
    next_id = 0
    if len(ids) == expected_ids:
        next_id = ids[-1]
        ids = ids[:-1]
    return VisitInodesResp(next_id=next_id, ids=ids)

# not read only
def lock_current_edge_inner(cur: sqlite3.Cursor, req: LockCurrentEdgeReq) -> Union[EggsError, LockCurrentEdgeResp]:
    current_edge = get_current_edge(cur, dir_id=req.dir_id, name=req.name, target_id=req.target_id)
    if isinstance(current_edge, EggsError):
        return current_edge
    if not current_edge['locked']:
        cur.execute(
            '''
                update edges set locked = TRUE
                    where dir_id = :dir_id and name = :name and target_id = :target_id and current
            ''',
            {'dir_id': req.dir_id, 'name': req.name, 'target_id': req.target_id}
        )
    return LockCurrentEdgeResp()
def lock_current_edge(cur: sqlite3.Cursor, req: LockCurrentEdgeReq) -> Union[EggsError, LockCurrentEdgeResp]:
    res = lock_current_edge_inner(cur, req)
    assert check_edge_constraints(cur, req.dir_id)
    return res

def unlock_current_edge_inner(cur: sqlite3.Cursor, req: UnlockCurrentEdgeReq) -> Union[EggsError, UnlockCurrentEdgeResp]:
    now = eggs_time()
    current_edge = get_current_edge(cur, dir_id=req.dir_id, name=req.name, target_id=req.target_id)
    if isinstance(current_edge, EggsError):
        return current_edge
    if current_edge['locked']:
        cur.execute(
            '''
                update edges set locked = FALSE
                    where dir_id = :dir_id and name = :name and target_id = :target_id and current
            ''',
            {'dir_id': req.dir_id, 'name': req.name, 'target_id': req.target_id}
        )
    if req.was_moved:
        # We need to move the current edge to snapshot, and create a new snapshot
        # edge with the deletion. 
        cur.execute(
            '''
                update edges set current = FALSE, locked = NULL, owned = FALSE
                    where dir_id = :dir_id and name = :name and creation_time = :creation_time
            ''',
            {'dir_id': req.dir_id, 'name': req.name, 'creation_time': current_edge['creation_time']}
        )
        sql_insert(
            cur, 'edges',
            dir_id=req.dir_id,
            name_hash=current_edge['name_hash'],
            name=req.name,
            creation_time=now,
            current=False,
            owned=False,
            target_id=NULL_INODE_ID
        )        
    return UnlockCurrentEdgeResp()
def unlock_current_edge(cur: sqlite3.Cursor, req: UnlockCurrentEdgeReq) -> Union[EggsError, UnlockCurrentEdgeResp]:
    res = unlock_current_edge_inner(cur, req)
    assert check_edge_constraints(cur, req.dir_id)
    return res

def lookup(cur: sqlite3.Cursor, req: LookupReq) -> Union[EggsError, LookupResp]:
    current_edge = get_current_edge(cur, dir_id=req.dir_id, name=req.name, target_id=None)
    if isinstance(current_edge, EggsError):
        return current_edge
    return LookupResp(target_id=current_edge['target_id'], creation_time=current_edge['creation_time'])

class BlockServerInfo(TypedDict):
    ip: str
    port: int
    storage_class: int
    terminal: bool
    stale: bool
    write_weight: float
    secret_key: bytes

@dataclass
class UpdateBlockServersInfoReq:
    block_servers: List[BlockServerInfo]

@dataclass
class UpdateBlockServersInfoResp:
    pass

def update_block_servers_info(cur: sqlite3.Cursor, req: UpdateBlockServersInfoReq) -> UpdateBlockServersInfoResp:
    # TODO test, I think this works but I need to test this better
    cur.execute('update block_servers set active = FALSE')
    cur.executemany(
        '''
            insert into block_servers
                (active, ip, port, secret_key, storage_class, write_weight, terminal, stale) values (TRUE, :ip, :port, :secret_key, :storage_class, :write_weight, :terminal, :stale)
                on conflict (ip, port)
                    do update set
                        active = TRUE, secret_key = excluded.secret_key, storage_class = excluded.storage_class,
                        write_weight = :write_weight, terminal = :terminal, stale = :stale
        ''',
        req.block_servers,
    )
    return UpdateBlockServersInfoResp()

# This includes requests that we only issue internally, but essentially this is
# LogEntry
ShadRequestBodyInternal = Union[ShardRequestBody, UpdateBlockServersInfoReq]
ShardResponseBodyInternal = Union[ShardResponseBody, UpdateBlockServersInfoResp]

def execute_internal(db: sqlite3.Connection, req_body: ShadRequestBodyInternal) -> ShardResponseBody:
    logging.debug(f'Starting to execute {type(req_body)}')
    cur = db.cursor()    
    resp_body: Union[EggsError, ShardResponseBodyInternal]
    try:
        if isinstance(req_body, CreateDirectoryINodeReq):
            resp_body = create_directory_inode(cur, req_body)
        elif isinstance(req_body, StatReq):
            resp_body = stat(cur, req_body)
        elif isinstance(req_body, CreateLockedCurrentEdgeReq):
            resp_body = create_locked_current_edge(cur, req_body)
        elif isinstance(req_body, ReadDirReq):
            resp_body = read_dir(cur, req_body)
        elif isinstance(req_body, ConstructFileReq):
            resp_body = construct_file(cur, req_body)
        elif isinstance(req_body, VisitTransientFilesReq):
            resp_body = visit_transient_files(cur, req_body)
        elif isinstance(req_body, UpdateBlockServersInfoReq):
            resp_body = update_block_servers_info(cur, req_body)
        elif isinstance(req_body, AddSpanInitiateReq):
            resp_body = add_span_initiate(cur, req_body)
        elif isinstance(req_body, AddSpanCertifyReq):
            resp_body = add_span_certify(cur, req_body)
        elif isinstance(req_body, LinkFileReq):
            resp_body = link_file(cur, req_body)
        elif isinstance(req_body, FileSpansReq):
            resp_body = file_spans(cur, req_body)
        elif isinstance(req_body, SameDirectoryRenameReq):
            resp_body = same_directory_rename(cur, req_body)
        elif isinstance(req_body, SoftUnlinkFileReq):
            resp_body = soft_unlink_file(cur, req_body)
        elif isinstance(req_body, SetDirectoryOwnerReq):
            resp_body = set_directory_owner(cur, req_body)
        elif isinstance(req_body, VisitInodesReq):
            resp_body = visit_inodes(cur, req_body)
        elif isinstance(req_body, LockCurrentEdgeReq):
            resp_body = lock_current_edge(cur, req_body)
        elif isinstance(req_body, UnlockCurrentEdgeReq):
            resp_body = unlock_current_edge(cur, req_body)
        elif isinstance(req_body, LookupReq):
            resp_body = lookup(cur, req_body)
        else:
            resp_body = EggsError(ErrCode.UNRECOGNIZED_REQUEST)
    except Exception:
        logging.error(f'Got exception while processing shard request of type {type(req_body)}')
        traceback.print_exc()
        resp_body = EggsError(ErrCode.INTERNAL_ERROR)
    # We _never_ commit when there's an error. This is possible due to the "stateless"
    # nature of the shard (request->response), it would _not_ be the case with the CDC.
    if isinstance(resp_body, EggsError):
        db.rollback()
    else:
        db.commit()
    logging.debug(f'Finished executing {type(req_body)}')
    return cast(ShardResponseBody, resp_body)

def execute(db: sqlite3.Connection, req: ShardRequest) -> ShardResponse:
    return ShardResponse(request_id=req.request_id, body=execute_internal(db, req.body))

def open_db(db_path: str, shard: int) -> sqlite3.Connection:
    assert 0 <= shard <= 255, f'{shard} is out of range'
    os.makedirs(db_path, exist_ok=True)
    db_fn = os.path.join(db_path, f'shard_{shard}.sqlite')

    db = sqlite3.connect(db_fn)
    db.row_factory = sqlite3.Row
    init_db(shard, db)

    return db

class TestDriver:
    def __init__(self, db_dir: str, shard: int):
        self.shard = shard
        self.db = open_db(db_dir, shard)
        # self.db.set_trace_callback(print)
        self.inode_id_counter = 0

    def fresh_inode_id(self, type: InodeType, shard: int):
        self.inode_id_counter += 1
        return make_inode_id(type, shard, self.inode_id_counter)
    
    def sql_dry(self, sql: str, parameters: Any):
        self.db.execute(sql, parameters)
        self.db.rollback()

    def sql(self, sql: str, parameters: Any):
        self.db.execute(sql, parameters)
        self.db.commit()

    def sql_check_constraint_fail(self, sql: str, parameters: Any):
        try:
            self.db.execute(sql, parameters)
            self.db.commit()
        except sqlite3.IntegrityError:
            return
        assert False, 'Did not get IntegrityError'

    def execute(self, full_req: ShardRequest):
        # test encoding roundtrip
        full_req_bytes = full_req.pack(cdc_key.CDC_KEY)
        assert len(full_req_bytes) < UDP_MTU
        unpacked_req = UnpackedShardRequest.unpack(full_req_bytes, cdc_key.CDC_KEY)
        assert full_req == unpacked_req.request
        full_resp = execute(self.db, full_req)
        full_resp_bytes = bincode.pack(full_resp)
        assert len(full_resp_bytes) < UDP_MTU
        assert full_resp == bincode.unpack(ShardResponse, full_resp_bytes)
        return full_resp

    # We repeat by default as a very rough test for idempotency, but with a
    # blacklist for things we know will fail.
    def execute_ok(self, req: ShardRequestBody, repeats: int = 2) -> Any:
        if req.kind in (ShardRequestKind.SAME_DIRECTORY_RENAME, ShardRequestKind.SOFT_UNLINK_FILE):
            repeats = 1
        resp: Any = None
        for i in range(repeats):
            resp = self.execute(ShardRequest(request_id=eggs_time(), body=req))
            assert not isinstance(resp.body, EggsError), f'Got error at round {i}: {resp}'
        return resp.body

    def execute_err(self, req: ShardRequestBody, kind: Union[ErrCode, None] = None):
        resp = self.execute(ShardRequest(request_id=eggs_time(), body=req))
        assert isinstance(resp.body, EggsError), f'Expected error, got response {resp.body}'
        acceptable_errors = SHARD_ERRORS[req.kind]
        assert resp.body.error_code in acceptable_errors, f'Expected error to be one of {acceptable_errors}, but got {resp.body.error_code}'
        if kind is not None:
            assert resp.body.error_code == kind, f'Expected error {repr(kind)}, got {repr(resp.body.error_code)}'
        return resp.body

    def execute_internal_ok(self, req: ShadRequestBodyInternal) -> Any:
        # We do this twice as a very rough test for idempotency.
        resp = None
        for _ in range(2):
            resp = execute_internal(self.db, req)
            assert not isinstance(resp, EggsError), resp 
        return resp

    def execute_internal_err(self, req: ShadRequestBodyInternal):
        resp = execute_internal(self.db, req)
        assert isinstance(resp, EggsError), resp 
        return resp
    
class ShardTests(unittest.TestCase):
    def setUp(self):
        db_dir = tempfile.mkdtemp(prefix='eggs_')
        logging.debug(f'Running shard test with db dir {db_dir}')
        self.shard = TestDriver(db_dir, 0)

        self.test_block_servers: List[BlockServerInfo] = []
        for i in range(50):
            self.test_block_servers.append(BlockServerInfo(
                ip=f'192.168.0.{i}',
                port=0,
                storage_class=2,
                write_weight=random.uniform(0.0, 1.0),
                secret_key=secrets.token_bytes(16),
                terminal=False,
                stale=False,
            ))
        # we use the IP to uniquely recognize these later on
        self.test_block_servers_by_ip = {
            socket.inet_aton(block_server['ip']): block_server for block_server in self.test_block_servers
        }

    def test_sql_constraints(self):
        # shard
        self.shard.sql_check_constraint_fail('insert into shard values (:bad_shard, 0, 0, 0, 0)', {'bad_shard': (self.shard.shard+1)%256})
        # files
        self.shard.sql_check_constraint_fail('insert into files (id, size) values (:id, 0)', {'id': self.shard.fresh_inode_id(InodeType.FILE, (self.shard.shard+1)%256)})
        self.shard.sql_check_constraint_fail('insert into files (id, size) values (:id, 0)', {'id': self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard)})
        self.shard.sql_dry('insert into files (id, size, transient, mtime) values (:id, 0, FALSE, 0)', {'id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)})
        self.shard.sql_check_constraint_fail('insert into files (id, size, transient, mtime) values (:id, 0, TRUE, 0)', {'id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)})
        self.shard.sql_check_constraint_fail('insert into files (id, size, transient, mtime, last_span_state) values (:id, 0, FALSE, 0, 0)', {'id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)})
        # directories
        self.shard.sql_check_constraint_fail(
            "insert into directories (id, owner_id, mtime, hash_mode, opaque) values (:id, :owner, 0, 0, x'')",
            {'id': self.shard.fresh_inode_id(InodeType.DIRECTORY, (self.shard.shard+1)%256), 'owner': NULL_INODE_ID}
        )
        self.shard.sql_check_constraint_fail(
            "insert into directories (id, owner_id, mtime, hash_mode, opaque) values (:id, :owner, 0, 0, x'')",
            {'id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard), 'owner': NULL_INODE_ID}
        )
        self.shard.sql_check_constraint_fail(
            "insert into directories (id, owner_id, mtime, hash_mode, opaque) values (:id, :owner, 0, 0, x'')",
            {'id': self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard), 'owner': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)}
        )
        self.shard.sql_dry(
            "insert into directories (id, owner_id, mtime, hash_mode, opaque) values (:id, :owner, 0, 0, x'')",
            {'id': self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard), 'owner': NULL_INODE_ID}
        )
        self.shard.sql_dry(
            "insert into directories (id, owner_id, mtime, hash_mode, opaque) values (:id, :owner, 0, 0, x'')",
            {'id': self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard), 'owner': self.shard.fresh_inode_id(InodeType.DIRECTORY, (self.shard.shard+1)%255)}
        )
        # edges
        test_dir = self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard)
        self.shard.execute_ok(CreateDirectoryINodeReq(test_dir, ROOT_DIR_INODE_ID, b''))
        test_name = {
            'name': b'test',
            'name_hash': hash_name(HashMode.TRUNC_MD5, b'test'),
        }
        # No locked
        self.shard.sql_check_constraint_fail(
            "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
            (test_name | {
                'dir_id': test_dir,
                'creation_time': eggs_time(),
                'current': True,
                'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                'owned': None,
                'locked': None,
            })
        )
        # NULL target for current
        self.shard.sql_check_constraint_fail(
            "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
            (test_name | {
                'dir_id': test_dir,
                'creation_time': eggs_time(),
                'current': True,
                'target_id': NULL_INODE_ID,
                'owned': None,
                'locked': False,
            })
        )
        # Owned
        self.shard.sql_check_constraint_fail(
            "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
            (test_name | {
                'dir_id': test_dir,
                'creation_time': eggs_time(),
                'current': True,
                'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                'owned': False,
                'locked': False,
            })
        )
        # Same for non-current
        self.shard.sql_check_constraint_fail(
            "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
            (test_name | {
                'dir_id': test_dir,
                'creation_time': eggs_time(),
                'current': False,
                'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                'owned': None,
                'locked': None,
            })
        )
        self.shard.sql_check_constraint_fail(
            "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
            (test_name | {
                'dir_id': test_dir,
                'creation_time': eggs_time(),
                'current': False,
                'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                'owned': False,
                'locked': False,
            })
        )
        # two currents
        cur = self.shard.db.cursor()
        for _ in range(2):
            cur.execute(
                "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
                (test_name | {
                    'dir_id': test_dir,
                    'creation_time': eggs_time(),
                    'current': True,
                    'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                    'owned': None,
                    'locked': False,
                })   
            )
        def fail_check_edge_constraints():
            with EnableConstraints, DisableLogging:
                assert not check_edge_constraints(cur, test_dir)
        fail_check_edge_constraints()
        # current older than non-current
        cur = self.shard.db.cursor()
        for current in range(2):
            current = bool(current)
            cur.execute(
                "insert into edges values (:dir_id, :name_hash, :name, :creation_time, :current, :target_id, :owned, :locked)",
                (test_name | {
                    'dir_id': test_dir,
                    'creation_time': eggs_time(),
                    'current': current,
                    'target_id': self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard),
                    'owned': None if current else False,
                    'locked': False if current else None,
                })   
            )
        fail_check_edge_constraints()

    def test_root_dir(self):
        self.shard.execute_ok(StatReq(ROOT_DIR_INODE_ID))

    def test_create_dir(self):
        dir_id = self.shard.fresh_inode_id(InodeType.DIRECTORY, self.shard.shard)
        create_dir_req = CreateDirectoryINodeReq(id=dir_id, owner_id=ROOT_DIR_INODE_ID, opaque=b'first')
        create_dir_1 = self.shard.execute_ok(create_dir_req)
        stat_1 = self.shard.execute_ok(StatReq(dir_id))
        assert stat_1.mtime == create_dir_1.mtime
        assert stat_1.size_or_owner == ROOT_DIR_INODE_ID
        assert stat_1.opaque == b'first'
        # can't change owner
        self.shard.execute_err(replace(create_dir_req, owner_id=NULL_INODE_ID), kind=ErrCode.MISMATCHING_OWNER)
        # we can change the opaque stuff though TODO is this correct behavior? probably doesn't matter
        self.shard.execute_ok(replace(create_dir_req, opaque=b'second'))
        stat_2 = self.shard.execute_ok(StatReq(dir_id))
        assert stat_2.opaque == b'second'
    
    def test_create_current_edge(self):
        dummy_target_1 = self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)
        dummy_target_2 = self.shard.fresh_inode_id(InodeType.FILE, self.shard.shard)
        t_create = eggs_time()
        create_locked_edge_req = CreateLockedCurrentEdgeReq(dir_id=ROOT_DIR_INODE_ID, name=b'test', target_id=dummy_target_2, creation_time=t_create)
        self.shard.execute_ok(create_locked_edge_req)
        # check that the edge is there
        read_dir = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(dir_id=ROOT_DIR_INODE_ID, start_hash=0)))
        assert len(read_dir.results) == 1
        assert read_dir.results[0].name == b'test' and read_dir.results[0].target_id == dummy_target_2
        assert not self.shard.execute_ok(ReadDirReq(dir_id=ROOT_DIR_INODE_ID, start_hash=0, as_of_time=t_create-1)).results 
        # idempotence
        self.shard.execute_ok(create_locked_edge_req)
        # switching the id or the time should fail
        self.shard.execute_err(replace(create_locked_edge_req, target_id=dummy_target_1))
        self.shard.execute_err(replace(create_locked_edge_req, creation_time=eggs_time()))
        # we can unlock
        self.shard.execute_ok(UnlockCurrentEdgeReq(dir_id=ROOT_DIR_INODE_ID, name=b'test', target_id=dummy_target_2, was_moved=False))
        # and lock again
        self.shard.execute_ok(LockCurrentEdgeReq(dir_id=ROOT_DIR_INODE_ID, name=b'test', target_id=dummy_target_2))
        # now we unlock and move
        t_before_move = eggs_time()
        self.shard.execute_ok(UnlockCurrentEdgeReq(dir_id=ROOT_DIR_INODE_ID, name=b'test', target_id=dummy_target_2, was_moved=True), repeats=1)
        read_dir = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(dir_id=ROOT_DIR_INODE_ID, start_hash=0)))
        assert len(read_dir.results) == 0
        read_dir = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(dir_id=ROOT_DIR_INODE_ID, start_hash=0, as_of_time=t_before_move)))
        assert len(read_dir.results) == 1
        assert read_dir.results[0].name == b'test' and read_dir.results[0].target_id == dummy_target_2
        assert not self.shard.execute_ok(ReadDirReq(dir_id=ROOT_DIR_INODE_ID, start_hash=0, as_of_time=t_create-1)).results
    
    def test_construct_file_1(self):
        # we don't check for idempotency because later we inspect those files
        self.shard.execute_ok(ConstructFileReq(InodeType.FILE), repeats=1)
        self.shard.execute_ok(ConstructFileReq(InodeType.SYMLINK), repeats=1)
        transient_files = cast(VisitTransientFilesResp, self.shard.execute_ok(VisitTransientFilesReq(0)))
        assert len(transient_files.files) == 2
        assert inode_id_type(transient_files.files[0].id) == InodeType.FILE
        assert inode_id_type(transient_files.files[1].id) == InodeType.SYMLINK
    
    def test_construct_file_2(self):
        num_files = 3*(UDP_MTU//TransientFile.SIZE)
        for _ in range(num_files):
            self.shard.execute_ok(ConstructFileReq(InodeType.FILE), repeats=1)
        read_files = 0
        current_inode = 0
        while True:
            transient_files = cast(VisitTransientFilesResp, self.shard.execute_ok(VisitTransientFilesReq(current_inode)))
            read_files += len(transient_files.files)
            assert read_files <= num_files
            assert (read_files == num_files) == (transient_files.next_id == 0)
            if read_files == num_files:
                break
            current_inode = transient_files.next_id

    def test_spans(self):
        # TODO add check that the span file state is clean after adding INLINE/ZERO spans, dirty afterwards
        self.shard.execute_internal_ok(UpdateBlockServersInfoReq(self.test_block_servers))
        transient_file = cast(ConstructFileResp, self.shard.execute_ok(ConstructFileReq(InodeType.FILE), repeats=1))
        add_span_req = AddSpanInitiateReq(file_id=transient_file.id, cookie=transient_file.cookie, byte_offset=0, storage_class=0, parity=0, crc32=b'0000', size=0, body=b'')
        byte_offset = 0
        # Can't add a block ahead
        inline_span_req = replace(
            add_span_req,
            byte_offset=byte_offset,
            storage_class=INLINE_STORAGE,
            parity=0,
            size=len(b'test-inline-block'),
            body=b'test-inline-block',
            crc32=crypto.crc32c(b'test-inline-block'),
        )
        # can't add a block ahead
        self.shard.execute_err(replace(inline_span_req, byte_offset=10), ErrCode.SPAN_NOT_FOUND)
        # can't have non-zero parity mode
        self.shard.execute_err(replace(inline_span_req, parity=create_parity_mode(4,2)))
        # can't have zero-sized inline span
        self.shard.execute_err(replace(inline_span_req, size=0, body=b'', crc32=(b'\0'*4)), ErrCode.BAD_SPAN_BODY)
        # can't have bad crc
        self.shard.execute_err(replace(inline_span_req, crc32=b'1234'), ErrCode.BAD_SPAN_BODY)
        self.shard.execute_err(replace(inline_span_req, size=100, storage_class=ZERO_FILL_STORAGE, body=b'', crc32=b'1234'), ErrCode.BAD_SPAN_BODY)
        inline_span = self.shard.execute_ok(inline_span_req)
        byte_offset += inline_span_req.size
        transient_file_1 = cast(VisitTransientFilesResp, self.shard.execute_ok(VisitTransientFilesReq(0))).files[0]
        # empty list when adding inline block
        assert not inline_span.blocks
        # idempotent, but bumps the deadline
        self.shard.execute_ok(inline_span_req)
        transient_file_2 = cast(VisitTransientFilesResp, self.shard.execute_ok(VisitTransientFilesReq(0))).files[0]
        assert transient_file_2.deadline_time > transient_file_1.deadline_time
        # empty, only bumps the deadline
        self.shard.execute_ok(replace(inline_span_req, size=0, byte_offset=inline_span_req.size, storage_class=ZERO_FILL_STORAGE, body=b'', crc32=(b'\0\0\0\0')))
        # non-empty zero span
        self.shard.execute_ok(replace(inline_span_req, size=100, byte_offset=inline_span_req.size, storage_class=ZERO_FILL_STORAGE, body=b'', crc32=crypto.crc32c(b'\0'*100)))
        byte_offset += 100
        # onto proper blocks
        data = random.randbytes(1000)
        data_crc32 = crypto.crc32c(data)
        proper_span_req = replace(
            add_span_req,
            byte_offset=byte_offset,
            storage_class=2,
            parity=create_parity_mode(1, 0),
            crc32=data_crc32,
            size=len(data),
            body=[NewBlockInfo(data_crc32, len(data))],
        )
        # bad size/crc
        self.shard.execute_err(replace(proper_span_req, body=[NewBlockInfo(data_crc32, len(data)-1)]))
        data_1 = data[:100]
        data_1_crc32 = crypto.crc32c(data_1)
        data_2 = data[100:200]
        data_2_crc32 = crypto.crc32c(data_2)
        data_3 = data[200:]
        data_3_crc32 = crypto.crc32c(data_3)
        assert crypto.crc32c_combine(crypto.crc32c_combine(data_1_crc32, data_2_crc32, len(data_2)), data_3_crc32, len(data_3)) == data_crc32
        self.shard.execute_err(replace(
            proper_span_req,
            parity=create_parity_mode(3,1),
            body=[NewBlockInfo(data_1_crc32, len(data_1)), NewBlockInfo(data_2_crc32, len(data_2)), NewBlockInfo(data_3_crc32, len(data_3)-1), NewBlockInfo(b'1234', 100)])
        )
        self.shard.execute_err(replace(proper_span_req, body=[NewBlockInfo(b'4321', 10000)]))
        self.shard.execute_err(replace(proper_span_req, parity=create_parity_mode(1,1), body=[NewBlockInfo(b'1234', 10000), NewBlockInfo(b'4321', 10000)]))
        # no mirroring
        proper_span_resp = self.shard.execute_ok(proper_span_req)
        byte_offset += proper_span_req.size
        # RAID1
        mirrored_span_req = replace(proper_span_req, parity=create_parity_mode(1,1), body=[NewBlockInfo(data_crc32, len(data)), NewBlockInfo(data_crc32, len(data))])
        # can't insert at the same position with different parity
        self.shard.execute_err(mirrored_span_req, ErrCode.SPAN_NOT_FOUND)
        mirrored_span_req = replace(mirrored_span_req, byte_offset=byte_offset)
        # this fails, because we haven't certified the previous block
        self.shard.execute_err(mirrored_span_req, ErrCode.LAST_SPAN_STATE_NOT_CLEAN)
        # certifying with bad proof
        certify_req = AddSpanCertifyReq(
            file_id=transient_file.id,
            cookie=transient_file.cookie,
            byte_offset=proper_span_req.byte_offset,
            proofs=[b'12345678'],
        )
        self.shard.execute_err(
            replace(certify_req, byte_offset=proper_span_req.byte_offset, proofs=[b'12345678']),
            ErrCode.BAD_BLOCK_PROOF
        )
        # certifying for real
        self.shard.execute_ok(replace(
            certify_req, byte_offset=proper_span_req.byte_offset,
            proofs=[block_add_proof(block, crypto.aes_expand_key(self.test_block_servers_by_ip[block.ip]['secret_key'])) for block in proper_span_resp.blocks]
        ))
        mirrored_span_resp = self.shard.execute_ok(mirrored_span_req)
        byte_offset += mirrored_span_req.size
        # certify this other one...
        self.shard.execute_ok(replace(
            certify_req, byte_offset=mirrored_span_req.byte_offset,
            proofs=[block_add_proof(block, crypto.aes_expand_key(self.test_block_servers_by_ip[block.ip]['secret_key'])) for block in mirrored_span_resp.blocks],
        ))
        # RAID5
        raid5_span_req = replace(
            proper_span_req,
            byte_offset=byte_offset,
            parity=create_parity_mode(3,1),
            body=[NewBlockInfo(data_1_crc32, len(data_1)), NewBlockInfo(data_2_crc32, len(data_2)), NewBlockInfo(data_3_crc32, len(data_3)), NewBlockInfo(b'1234', 100)]
        )
        raid5_span_resp = cast(AddSpanInitiateResp, self.shard.execute_ok(raid5_span_req))
        assert len(set([b.block_id for b in raid5_span_resp.blocks])) == len(raid5_span_resp.blocks)
        # if we try to link now, we shouldn't be able to (file is dirty)
        link_req = LinkFileReq(
            file_id=transient_file.id,
            cookie=transient_file.cookie,
            owner_id=ROOT_DIR_INODE_ID, # this works only because we're on shard 0
            name=b'test-file'
        )
        self.shard.execute_err(link_req, ErrCode.LAST_SPAN_STATE_NOT_CLEAN)
        # certify, again
        self.shard.execute_ok(replace(
            certify_req, byte_offset=raid5_span_req.byte_offset,
            proofs=[block_add_proof(block, crypto.aes_expand_key(self.test_block_servers_by_ip[block.ip]['secret_key'])) for block in raid5_span_resp.blocks],
        ))
        # and now linking should go through
        self.shard.execute_ok(link_req)
        # check that we get it in the directory
        root_dir_files = self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, start_hash=0))
        assert root_dir_files.results[0].name == b'test-file'
        # check that the file is as big as we expect
        file_stat = cast(StatResp, self.shard.execute_ok(StatReq(root_dir_files.results[0].target_id)))
        assert file_stat.size_or_owner == (raid5_span_req.byte_offset + raid5_span_req.size)
        # check that the blocks are what we expect
        spans = cast(FileSpansResp, self.shard.execute_ok(FileSpansReq(root_dir_files.results[0].target_id, 0)))
        assert spans.next_offset == 0
        assert len(spans.spans) == 5 # inline, zero, no mirroring/raid, simple mirroring, raid5
        assert file_stat.size_or_owner == (spans.spans[-1].byte_offset + spans.spans[-1].size)
        for i, span in enumerate(spans.spans[:-1]):
            assert span.byte_offset + span.size == spans.spans[i+1].byte_offset
        assert spans.spans[0].body == b'test-inline-block'
        assert spans.spans[1].body == b''
        assert len(spans.spans[2].body) == 1
        assert len(spans.spans[3].body) == 2
        assert len(spans.spans[4].body) == 4

    def test_same_dir_renames(self):
        # overwrite a file, and store another to keep alongside
        transient_file_old = cast(ConstructFileResp, self.shard.execute_ok(ConstructFileReq(InodeType.FILE)))
        self.shard.execute_ok(LinkFileReq(file_id=transient_file_old.id, cookie=transient_file_old.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'1'))
        assert self.shard.execute_ok(LookupReq(dir_id=ROOT_DIR_INODE_ID, name=b'1')).target_id == transient_file_old.id
        self.shard.execute_err(LookupReq(dir_id=ROOT_DIR_INODE_ID, name=b'2'), ErrCode.NAME_NOT_FOUND)
        transient_file_other = cast(ConstructFileResp, self.shard.execute_ok(ConstructFileReq(InodeType.FILE)))
        self.shard.execute_ok(LinkFileReq(file_id=transient_file_other.id, cookie=transient_file_other.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'other'))
        t_before_creation = eggs_time()
        transient_file = cast(ConstructFileResp, self.shard.execute_ok(ConstructFileReq(InodeType.FILE)))
        self.shard.execute_ok(LinkFileReq(file_id=transient_file.id, cookie=transient_file.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'1'))
        t_before_first_move = eggs_time()
        file_id = transient_file.id
        # Getting the name or the inode wrong won't work
        self.shard.execute_err(SameDirectoryRenameReq(dir_id=ROOT_DIR_INODE_ID, target_id=1234, old_name=b'1', new_name=b'2'), ErrCode.MISMATCHING_TARGET)
        self.shard.execute_err(SameDirectoryRenameReq(dir_id=ROOT_DIR_INODE_ID, target_id=file_id, old_name=b'2', new_name=b'3'), ErrCode.NAME_NOT_FOUND)
        # now let's do it right
        # can't check idempotency for directory renames -- the client is supposed to resolve these
        self.shard.execute_ok(SameDirectoryRenameReq(dir_id=ROOT_DIR_INODE_ID, target_id=file_id, old_name=b'1', new_name=b'2'))
        self.shard.execute_err(SameDirectoryRenameReq(dir_id=ROOT_DIR_INODE_ID, target_id=file_id, old_name=b'1', new_name=b'2'), ErrCode.NAME_NOT_FOUND)
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, start_hash=0)))
        def read_dir_names(files):
            return set([r.name for r in files.results])
        def read_dir_targets(files):
            return set([r.target_id for r in files.results])
        assert read_dir_targets(files) == {transient_file_other.id, transient_file.id} and read_dir_names(files) == {b'2', b'other'}
        # move again
        t_before_second_move = eggs_time()
        self.shard.execute_ok(SameDirectoryRenameReq(dir_id=ROOT_DIR_INODE_ID, target_id=file_id, old_name=b'2', new_name=b'3'))
        # verify that we get the right result with the as of time
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, as_of_time=t_before_creation, start_hash=0)))
        assert read_dir_targets(files) == {transient_file_old.id, transient_file_other.id} and read_dir_names(files) == {b'1', b'other'}
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, as_of_time=t_before_first_move, start_hash=0)))
        assert read_dir_targets(files) == {transient_file_other.id, transient_file.id} and read_dir_names(files) == {b'1', b'other'}
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, as_of_time=t_before_second_move, start_hash=0)))
        assert read_dir_targets(files) == {transient_file_other.id, transient_file.id} and read_dir_names(files) == {b'2', b'other'}
        # finally, the current one
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, start_hash=0)))
        assert read_dir_targets(files) == {transient_file_other.id, transient_file.id} and read_dir_names(files) == {b'3', b'other'}
        # now delete the files
        t_before_first_deletion = eggs_time()
        self.shard.execute_err(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, file_id=transient_file_other.id, name=b'wrong-other'), ErrCode.NAME_NOT_FOUND)
        self.shard.execute_err(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, file_id=transient_file.id, name=b'other'), ErrCode.MISMATCHING_TARGET)
        self.shard.execute_ok(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, file_id=transient_file_other.id, name=b'other'))
        t_before_second_deletion = eggs_time()
        self.shard.execute_err(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, file_id=transient_file.id, name=b'1'), ErrCode.NAME_NOT_FOUND)
        self.shard.execute_ok(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, file_id=transient_file.id, name=b'3'))
        # now get the files, again in order
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, as_of_time=t_before_first_deletion, start_hash=0)))
        assert read_dir_targets(files) == {transient_file_other.id, transient_file.id} and read_dir_names(files) == {b'3', b'other'}
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, as_of_time=t_before_second_deletion, start_hash=0)))
        assert read_dir_targets(files) == {transient_file.id} and read_dir_names(files) == {b'3'}
        files = cast(ReadDirResp, self.shard.execute_ok(ReadDirReq(ROOT_DIR_INODE_ID, start_hash=0)))
        assert not read_dir_targets(files)
        # as a bonus, try and fail to create a current edge older than the dead edge
        self.shard.execute_err(
            CreateLockedCurrentEdgeReq(dir_id=ROOT_DIR_INODE_ID, name=b'1', target_id=transient_file.id, creation_time=0),
            ErrCode.MORE_RECENT_SNAPSHOT_ALREADY_EXISTS,
        )        


SHUCKLE_POLL_PERIOD_SEC = 60

def shuckle_json_to_block_server(json_obj: Dict[str, Any]) -> BlockServerInfo:
    return BlockServerInfo(
        ip=json_obj['ip'],
        port=json_obj['port'],
        storage_class=json_obj['storage_class'],
        write_weight=json_obj['write_weight'],
        secret_key=bytes.fromhex(json_obj['secret_key']),
        terminal=json_obj['is_terminal'],
        stale=json_obj['is_stale'],
    )


def fetch_block_servers_from_shuckle() -> List[BlockServerInfo]:
    resp = requests.get('http://localhost:5000/show_me_what_you_got')
    resp.raise_for_status()
    return [
        shuckle_json_to_block_server(datum)
        for datum in resp.json()
    ]

def update_block_servers(db: sqlite3.Connection, *, swallow_errors: bool):
    def log_err(err):
        logging.error(f'Could not update block servers: {err}')
    try:
        block_servers_update = execute_internal(db, UpdateBlockServersInfoReq(fetch_block_servers_from_shuckle()))
        if isinstance(block_servers_update, EggsError):
            log_err(block_servers_update)
    except Exception as err:
        if swallow_errors:
            log_err(err)
        else:
            raise err

def run_forever(db: sqlite3.Connection, *, wait_for_shuckle: bool) -> None:
    shard_info = ShardInfo(db.cursor())
    port = shard_to_port(shard_info.shard)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.bind(('', port))
    logging.info(f'Running on port {port}')
    # Block for 5 secs until shuckle comes up, hopefully
    started_checking_at = time.time()
    while True:
        try:
            update_block_servers(db, swallow_errors=False)
            break
        except Exception as err:
            if not wait_for_shuckle or time.time() - started_checking_at > 5:
                raise err
            else:
                time.sleep(0.1)
    last_shuckle_update = time.time()
    while True:
        try:
            try:
                next_timeout = max(0.0, (last_shuckle_update + SHUCKLE_POLL_PERIOD_SEC) - time.time())
                sock.settimeout(next_timeout)
                data, addr = sock.recvfrom(UDP_MTU)
                resp_body: Optional[ShardResponseBody] = None
                unpacked_req = UnpackedShardRequest.unpack(data, cdc_key.CDC_KEY)
                if isinstance(unpacked_req.request, EggsError):
                    resp_body = unpacked_req.request
                else:
                    resp_body = execute(db, unpacked_req.request).body
                resp = ShardResponse(request_id=unpacked_req.request_id, body=resp_body)
                packed = bincode.pack(resp)
                if len(packed) > UDP_MTU:
                    logging.error(f'Packed size for response {type(resp.body)} exceeds {UDP_MTU}: {len(packed)}')
                    packed = bincode.pack(replace(resp, body=EggsError(ErrCode.INTERNAL_ERROR)))
                sock.sendto(packed, addr)
            except socket.timeout:
                pass
            # get shuckle update if we should
            if time.time() > last_shuckle_update + SHUCKLE_POLL_PERIOD_SEC:
                update_block_servers(db, swallow_errors=False)
                last_shuckle_update = time.time()
        # catch all exceptions in main loop -- i.e., never die apart from when
        # asked to
        except Exception as err:
            traceback.print_exc()
            logging.error(f'Got exception {err} in main loop!')

def main(*, db_dir: str, shard: int, wait_for_shuckle: bool) -> None:
    global CHECK_CONSTRAINTS
    CHECK_CONSTRAINTS = False # much faster this way
    db = open_db(db_dir, shard)
    run_forever(db, wait_for_shuckle=wait_for_shuckle)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Runs a single metadata shard without raft')
    parser.add_argument('db_dir', help='Directory in which to create or load db')
    parser.add_argument('shard', help='Which shard number to run', type=int)
    config = parser.parse_args(sys.argv[1:])

    main(db_dir=config.db_dir, shard=config.shard, wait_for_shuckle=False)
