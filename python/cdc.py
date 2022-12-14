#!/usr/bin/env python3
import collections
import sqlite3
import os
import tempfile
from typing import Any, Literal, TypeVar, TypedDict, cast, List, Generator, Callable
import json
import sys
import traceback
import unittest
import logging
import argparse
import socket
import logging
from pathlib import Path
import log_format

from cdc_msgs import *
from cdc_key import *
from common import *
from msgs import *
from shard_msgs import *
from error import *
# import shard
import bincode

# TODO test automatic cleanup of timed out requests
# TODO test multiple requests at once (queued)

def init_db(db: sqlite3.Connection):
    cur = db.cursor()
    cur.execute('pragma foreign_keys = ON')
    cur.execute('pragma journal_mode = WAL')
    cur.execute('''
        create table if not exists cdc (
            -- only ever one row
            id integer primary key not null check (id = 0),
            next_directory_id integer not null
        )
    ''')
    cur.execute(
        f'insert or ignore into cdc values (0, :next_dir)',
        {'next_dir': ROOT_DIR_INODE_ID+1}
    )
    # What transactions we're currently or want to execute
    cur.execute('''
        create table if not exists transactions (
            -- The id also specifies in which order we run the transactions
            id integer primary key autoincrement,
            request_id integer not null, -- what was in the request
            kind integer not null,
            body blob not null,
            done bool not null,
            last_update_time integer not null,
            started bool not null,
            -- If we're done, we store what we replied.
            response_kind integer,
            response_body blob,
            -- As we're executing we store enough state to continue the current transaction.
            -- TODO would be good to have the entire history of these, in a separate table.
            -- However, doing it with a single row makes it easier to check constraints for now.
            -- So let's keep it like this until we have a lot of tests.
            next_step text,
            state text,
            shard_request_id integer,
            shard_request_shard integer,
            shard_request_kind integer,
            shard_request_critical bool,
            shard_request_body blob,
            check (not done or (response_kind is not null and response_body is not null and state is null and next_step is null and shard_request_id is null and shard_request_shard is null and shard_request_kind is null and shard_request_critical is null and shard_request_body is null)),
            check (not started or (response_kind is null and response_body is null and state is not null and next_step is not null))
            -- Unless we've just begun the request, we have a shard request in flight to proceed
            check (not (started and next_step != 'begin') or (shard_request_id is not null and shard_request_shard is not null and shard_request_kind is not null and shard_request_critical is not null and shard_request_body is not null))
        )
    ''')
    # We very often look for the first non-done request (that's the one we're doing)
    cur.execute('create index if not exists transactions_by_completion on transactions (done, started, id)')
    db.commit()
    pass

class CDCInfo:
    def __init__(self, cur: sqlite3.Cursor):
        row = cur.execute('select * from cdc').fetchone()
        self._next_directory_id = row['next_directory_id']

    def next_directory_id(self, cur: sqlite3.Cursor) -> int:
        id = self._next_directory_id
        self._next_directory_id += 1
        cur.execute('update cdc set next_directory_id = :id', {'id': self._next_directory_id})
        return id

@dataclass
class CDCNeedsShard:
    next_step: str
    shard: int
    request: ShardRequestBody
    # if this is set, we need this request to complete to preserve
    # the invariants of the system.
    critical: bool

# When we make progress in the CDC, we either finish a transaction,
# getting a response, or request data from a shard to make progress
# in said transaction.
CDCStepInternal = Union[CDCResponseBody, EggsError, CDCNeedsShard]

@dataclass
class CDCShardRequest:
    shard: int
    request: ShardRequest
    critical: bool
CDCStep = Union[CDCResponse, CDCShardRequest]

# This is just a type to type methods better in transactions
T = TypeVar('T')
ExpectedShardResp = Union[EggsError, T]

class Transaction:
    state: Any

    def __init__(self, state: Optional[Any] = None):
        pass

def map_err_code(map: Dict[ErrCode, ErrCode], resp: Union[EggsError, Any]):
    if isinstance(resp, EggsError):
        resp.error_code = map.get(resp.error_code, resp.error_code)

# Steps:
#
# 1. Allocate inode id here in the CDC
# 2. Create directory in shard we get from the inode
# 3. Create locked edge from owner to newly created directory
# 4. Unlock the edge created in 3
#
# If 3 fails, 2 must be rolled back. 4 does not fail.
class MakeDirectory(Transaction):
    class MakeDirectoryState(TypedDict):
        dir_id: int
        # What error to return after we've finished rolling back
        exit_error: Optional[ErrCode]
    state: MakeDirectoryState

    def __init__(self, state: Optional[MakeDirectoryState] = None):
        self.state = state or {
            'dir_id': NULL_INODE_ID,
            'exit_error': None,
        }

    def begin(self, cur: sqlite3.Cursor, req: MakeDirectoryReq) -> CDCStepInternal:
        cdc_info = CDCInfo(cur)
        self.state['dir_id'] = cdc_info.next_directory_id(cur)
        return CDCNeedsShard(
            next_step='create_dir_resp',
            shard=inode_id_shard(self.state['dir_id']),
            request=CreateDirectoryInodeReq(self.state['dir_id'], owner_id=req.owner_id, info=req.info),
            critical=False,
        )

    def create_dir_resp(self, cur: sqlite3.Cursor, req: MakeDirectoryReq, resp: ExpectedShardResp[CreateDirectoryInodeResp]) -> CDCStepInternal:
        # We never expect to fail here. That said, the rollback now is safe.
        if isinstance(resp, EggsError):
            logging.warning(f'Unexpected error {resp} when creating directory inode with id {self.state["dir_id"]}.')
            return EggsError(ErrCode.INTERNAL_ERROR)
        # We've succeeded, let's proceed: we need to link the directory
        now = eggs_time()
        return CDCNeedsShard(
            next_step='create_locked_edge_resp',
            shard=inode_id_shard(req.owner_id),
            request=CreateLockedCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=self.state['dir_id'], creation_time=now),
            critical=False,
        )
    
    def create_locked_edge_resp(self, cur: sqlite3.Cursor, req: MakeDirectoryReq, resp: ExpectedShardResp[CreateLockedCurrentEdgeResp]) -> CDCStepInternal:
        # We've failed to link: we need to rollback the directory creation
        if isinstance(resp, EggsError):
            return self._init_rollback(resp.error_code)
        # Now, unlock the edge
        return CDCNeedsShard(
            next_step='unlock_edge_resp',
            shard=inode_id_shard(req.owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=self.state['dir_id'], was_moved=False),
            critical=True,
        )
    
    def unlock_edge_resp(self, cur: sqlite3.Cursor, req: MakeDirectoryReq, resp: ExpectedShardResp[UnlockCurrentEdgeResp]):
        assert not isinstance(resp, EggsError) # critical
        # We're done!
        return MakeDirectoryResp(self.state['dir_id'])

    def _init_rollback(self, err: ErrCode):
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(self.state['dir_id']),
            # we've just created this directory, it is empty, therefore the policy
            # is irrelevant.
            request=RemoveDirectoryOwnerReq(self.state['dir_id'], DEFAULT_DIRECTORY_INFO.body[0]),
            critical=True,
        )

    def rollback_resp(self, cur: sqlite3.Cursor, req: MakeDirectoryReq, resp: ExpectedShardResp[RemoveDirectoryOwnerResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        assert self.state['exit_error'] is not None
        return EggsError(self.state['exit_error'])
    
# Steps:
#
# 1. lock source current edge
# 2. create destination locked current target edge
# 3. unlock edge in step 2
# 4. unlock source target current edge, and soft unlink it
#
# If we fail at step 2, we need to roll back step 1. Steps 3 and 4 should never fail.
class RenameFile(Transaction):
    class RenameFileState(TypedDict):
        new_edge_creation_time: int
        exit_error: Optional[ErrCode] # after we roll back, we return this error to the user
    state: RenameFileState

    def __init__(self, state: Optional[RenameFileState] = None):
        self.state = state or {
            'new_edge_creation_time': eggs_time(),
            'exit_error': None,
        }
    
    def begin(self, cur: sqlite3.Cursor, req: RenameFileReq) -> CDCStepInternal:
        # We need this explicit check here because moving directories is more complicated,
        # and therefore we do it in another transaction type entirely.
        if inode_id_type(req.target_id) == InodeType.DIRECTORY:
            return EggsError(ErrCode.TYPE_IS_DIRECTORY)
        return CDCNeedsShard(
            next_step='lock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=LockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id),
            critical=False,
        )
    
    def lock_old_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[LockCurrentEdgeResp]) -> CDCStepInternal:
        map_err_code(
            {
                ErrCode.DIRECTORY_NOT_FOUND: ErrCode.OLD_DIRECTORY_NOT_FOUND,
                ErrCode.NAME_IS_LOCKED: ErrCode.OLD_NAME_IS_LOCKED,
            },
            resp,
        )
        if isinstance(resp, EggsError):
            # We couldn't acquire the lock at the source -- we can terminate immediately, there's nothing to roll back
            return resp
        # onwards and upwards
        return CDCNeedsShard(
            next_step='create_new_locked_edge_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=CreateLockedCurrentEdgeReq(
                dir_id=req.new_owner_id,
                name=req.new_name,
                target_id=req.target_id,
                creation_time=self.state['new_edge_creation_time'],
            ),
            critical=False,
        )
    
    def create_new_locked_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[CreateLockedCurrentEdgeResp]) -> CDCStepInternal:
        map_err_code(
            {
                ErrCode.DIRECTORY_NOT_FOUND: ErrCode.NEW_DIRECTORY_NOT_FOUND,
                ErrCode.NAME_IS_LOCKED: ErrCode.NEW_NAME_IS_LOCKED,
            },
            resp,
        )
        if isinstance(resp, EggsError):
            # We couldn't create the new edge, we need to unlock the old edge.
            return self._init_rollback(req, resp.error_code)
        # Now, unlock the edge
        return CDCNeedsShard(
            next_step='unlock_new_edge_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.new_owner_id, name=req.new_name, target_id=req.target_id, was_moved=False),
            critical=True,
        )
    
    def unlock_new_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[UnlockCurrentEdgeResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        # We're done creating the destination edge, now unlock the source, marking it as moved
        return CDCNeedsShard(
            next_step='unlock_old_edge_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=UnlockCurrentEdgeReq(
                dir_id=req.old_owner_id,
                name=req.old_name,
                target_id=req.target_id,
                was_moved=True,
            ),
            critical=True,
        )
    
    def unlock_old_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[UnlockCurrentEdgeReq]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        return RenameFileResp()

    def _init_rollback(self, req: RenameFileReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=False),
            critical=True,
        )
    
    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[UnlockCurrentEdgeResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        assert self.state['exit_error'] is not None
        return EggsError(self.state['exit_error'])

# Steps:
#
# 1. Lock edge going into the directory to remove. This prevents things making
#     making it snapshot or similar in the meantime.
# 2. Resolve the directory info, since we'll need to store it when we remove the directory owner.
# 3. Remove directory owner from directory that we want to remove. This will fail if there still
#     are current edges there.
# 4. Unlock edge going into the directory, making it snapshot.
#
# If 2 or 3 fail, we need to roll back the locking, without making the edge snapshot.
class SoftUnlinkDirectory(Transaction):
    class RemoveDirectoryState(TypedDict):
        exit_error: Optional[ErrCode] # after we roll back, we return this error to the user
    state: RemoveDirectoryState

    def __init__(self, state: Optional[RemoveDirectoryState] = None):
        self.state = state or {
            'exit_error': None,
        }

    def begin(self, cur: sqlite3.Cursor, req: SoftUnlinkDirectoryReq) -> CDCStepInternal:
        if inode_id_type(req.target_id) != InodeType.DIRECTORY:
            return EggsError(ErrCode.TYPE_IS_NOT_DIRECTORY)
        return CDCNeedsShard(
            next_step='lock_resp',
            shard=inode_id_shard(req.owner_id),
            request=LockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id),
            critical=False,
        )

    def lock_resp(self, cur: sqlite3.Cursor, req: SoftUnlinkDirectoryReq, resp: ExpectedShardResp[LockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return resp # Nothing to rollback
        return CDCNeedsShard(
            next_step='stat_resp',
            shard=inode_id_shard(req.target_id),
            request=StatDirectoryReq(req.target_id),
            critical=False,
        )
    
    def stat_resp(self, cur: sqlite3.Cursor, req: SoftUnlinkDirectoryReq, resp: ExpectedShardResp[StatDirectoryResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return self._init_rollback(req, resp.error_code) # we need to unlock what we locked
        if len(resp.info) > 0:
            # if we get a directory info body, we're done finding the directory info
            return CDCNeedsShard(
                next_step='remove_owner_resp',
                shard=inode_id_shard(req.target_id),
                request=RemoveDirectoryOwnerReq(dir_id=req.target_id, info=resp.info),
                critical=False,
            )
        else:
            # otherwise, we keep going upwards
            return CDCNeedsShard(
                next_step='stat_resp',
                shard=inode_id_shard(resp.owner),
                request=StatDirectoryReq(resp.owner),
                critical=False,
            )
    
    def remove_owner_resp(self, cur: sqlite3.Cursor, req: SoftUnlinkDirectoryReq, resp: ExpectedShardResp[RemoveDirectoryOwnerResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return self._init_rollback(req, resp.error_code) # we need to unlock what we locked
        return CDCNeedsShard(
            next_step='unlock_resp',
            shard=inode_id_shard(req.owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id, was_moved=True),
            critical=True,
        )
    
    def unlock_resp(self, cur: sqlite3.Cursor, req: UnlockCurrentEdgeResp, resp: ExpectedShardResp[UnlockCurrentEdgeResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        return SoftUnlinkDirectoryResp()
    
    def _init_rollback(self, req: SoftUnlinkDirectoryReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id, was_moved=False),
            critical=True,
        )

    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[UnlockCurrentEdgeResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        assert self.state['exit_error'] is not None
        return EggsError(self.state['exit_error'])

# Steps:
#
# 1. Lock old edge
# 2. Make sure that there's no directory loop by traversing the parents
# 3. Create and lock the new edge
# 4. Unlock the new edge
# 5. Unlock and unlink the old edge
# 6. Update the owner of the moved directory to the new directory
#
# If we wail at step 2 or 3, we need to unlock the edge we locked at step 1. Step 4 and 5
# should never fail.
class RenameDirectory(Transaction):
    class RenameDirectoryState(TypedDict):
        exit_error: Optional[ErrCode]
        parent_cache: Dict[int, int]
        current_directory: Optional[int]
    state: RenameDirectoryState

    def __init__(self, state: Optional[RenameDirectoryState] = None):
        self.state = state or {
            'exit_error': None,
            'parent_cache': {},
            'current_directory': None,
        }
        if state is not None:
            # Sadly JSON deserialization forces the keys to be strings, so
            # we need to convert them here
            parent_cache = state['parent_cache']
            self.state['parent_cache'] = {}
            for k, v in parent_cache.items():
                self.state['parent_cache'][int(k)] = v

    def begin(self, cur: sqlite3.Cursor, req: RenameDirectoryReq) -> CDCStepInternal:
        if inode_id_type(req.target_id) != InodeType.DIRECTORY:
            return EggsError(ErrCode.TYPE_IS_NOT_DIRECTORY)
        if req.target_id == req.new_owner_id:
            return EggsError(ErrCode.LOOP_IN_DIRECTORY_RENAME) # important -- we don't catch this later
        return CDCNeedsShard(
            next_step='lock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=LockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id),
            critical=False,
        )
    
    def lock_old_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[LockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            # This is OK, we immediately failed to lock, no need to rollback
            return resp
        # Start traversing upwards until we reach the root or a loop
        self.state['parent_cache'][req.target_id] = req.new_owner_id
        self.state['current_directory'] = req.new_owner_id
        return self._init_parent_lookup()
    
    def _init_parent_lookup(self) -> CDCStepInternal:
        assert self.state['current_directory'] is not None
        return CDCNeedsShard(
            next_step='stat_resp',
            shard=inode_id_shard(self.state['current_directory']),
            request=StatDirectoryReq(self.state['current_directory']),
            critical=False,
        )
    
    def stat_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[StatDirectoryResp]) -> CDCStepInternal:
        assert self.state['current_directory'] is not None
        if isinstance(resp, EggsError):
            # Internal error because this should never happen, unless we haven't found the destination
            # dir, which might be.
            err: ErrCode
            if self.state['current_directory'] == req.new_owner_id:
                map_err_code({ErrCode.DIRECTORY_NOT_FOUND: ErrCode.NEW_DIRECTORY_NOT_FOUND}, resp)
                err = resp.error_code
            else:
                logging.error(f'Unexpected error for stat of directory {self.state["current_directory"]} which should exist: {resp}')
                err = ErrCode.INTERNAL_ERROR
            return self._init_rollback(req, err)
        if resp.owner in self.state['parent_cache']:
            return self._init_rollback(req, ErrCode.LOOP_IN_DIRECTORY_RENAME)
        if resp.owner == NULL_INODE_ID:
            # We're done traversing
            return CDCNeedsShard(
                next_step='create_new_resp',
                shard=inode_id_shard(req.new_owner_id),
                request=CreateLockedCurrentEdgeReq(req.new_owner_id, req.new_name, req.target_id, eggs_time()),
                critical=False,
            )
        self.state['parent_cache'][self.state['current_directory']] = resp.owner
        self.state['current_directory'] = resp.owner
        return self._init_parent_lookup()
    
    def create_new_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[CreateLockedCurrentEdgeReq]) -> CDCStepInternal:
        map_err_code(
            {
                ErrCode.DIRECTORY_NOT_FOUND: ErrCode.NEW_DIRECTORY_NOT_FOUND,
                ErrCode.NAME_IS_LOCKED: ErrCode.NEW_NAME_IS_LOCKED,
            },
            resp,
        )
        if isinstance(resp, EggsError):
            return self._init_rollback(req, resp.error_code)
        return CDCNeedsShard(
            next_step='unlock_new_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.new_owner_id, name=req.new_name, target_id=req.target_id, was_moved=False),
            critical=True,
        )
    
    def unlock_new_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[UnlockCurrentEdgeReq]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        return CDCNeedsShard(
            next_step='unlock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=True),
            critical=True,
        )
    
    def unlock_old_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[UnlockCurrentEdgeReq]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        return CDCNeedsShard(
            next_step='set_owner_resp',
            shard=inode_id_shard(req.target_id),
            request=SetDirectoryOwnerReq(dir_id=req.target_id, owner_id=req.new_owner_id),
            critical=True,
        )
    
    def set_owner_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[SetDirectoryOwnerResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        # We're finally done
        return RenameDirectoryResp()
   
    def _init_rollback(self, req: RenameDirectoryReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=False),
            critical=True,
        )
    
    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[UnlockCurrentEdgeResp]) -> CDCStepInternal:
        assert not isinstance(resp, EggsError) # critical
        assert self.state['exit_error']
        return EggsError(self.state['exit_error'])

# The only reason we have this here is for possible conflicts with RemoveDirectoryOwner,
# which might temporarily set the owner of a directory to NULL. Since in the current
# implementation we only ever have one transaction in flight in the CDC, we can just
# execute this.
class HardUnlinkDirectory(Transaction):
    HardUnlinkDirectoryState = Tuple[()]
    state: HardUnlinkDirectoryState

    def __init__(self, state: Optional[HardUnlinkDirectoryState] = None):
        self.state = state or ()

    def begin(self, cur: sqlite3.Cursor, req: HardUnlinkDirectoryReq) -> CDCStepInternal:
        if inode_id_type(req.dir_id) != InodeType.DIRECTORY:
            return EggsError(ErrCode.TYPE_IS_NOT_DIRECTORY)
        return CDCNeedsShard(
            next_step='remove_inode_resp',
            shard=inode_id_shard(req.dir_id),
            request=RemoveInodeReq(req.dir_id),
            critical=False,
        )
    
    def remove_inode_resp(self, cur: sqlite3.Cursor, req: RemoveInodeReq, resp: ExpectedShardResp[RemoveInodeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return resp
        return HardUnlinkDirectoryResp()

# 1. remove owning edge
# 2. make file transient
class HardUnlinkFile(Transaction):
    class HardUnlinkFileState(TypedDict):
        exit_error: Optional[ErrCode]
    state: HardUnlinkFileState

    def __init__(self, state: Optional[HardUnlinkFileState] = None):
        self.state = state or {
            'exit_error': None,
        }

    def begin(self, cur: sqlite3.Cursor, req: HardUnlinkFileReq) -> CDCStepInternal:
        return CDCNeedsShard(
            next_step='remove_edge_resp',
            shard=inode_id_shard(req.owner_id),
            request=RemoveOwnedSnapshotFileEdgeReq(req.owner_id, req.target_id, req.name, req.creation_time),
            critical=False,
        )
    
    def remove_edge_resp(self, cur: sqlite3.Cursor, req: HardUnlinkFileReq, resp: ExpectedShardResp[HardUnlinkFileResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return resp # nothing to roll back
        return CDCNeedsShard(
            next_step='make_transient_resp',
            shard=inode_id_shard(req.target_id),
            request=MakeFileTransientReq(req.target_id, req.name),
            critical=False,
        )
    
    def make_transient_resp(self, cur: sqlite3.Cursor, req: HardUnlinkDirectoryReq, resp: ExpectedShardResp[MakeFileTransientReq]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            if resp.error_code == ErrCode.FILE_NOT_FOUND:
                # this is a bit suspicious, but should be fine
                return HardUnlinkFileResp()
            raise RuntimeError('TODO implement hard unlink file rollback')
        return HardUnlinkFileResp()

TRANSACTIONS: Dict[CDCMessageKind, Type[Transaction]] = {
    CDCMessageKind.MAKE_DIRECTORY: MakeDirectory,
    CDCMessageKind.RENAME_FILE: RenameFile,
    CDCMessageKind.SOFT_UNLINK_DIRECTORY: SoftUnlinkDirectory,
    CDCMessageKind.RENAME_DIRECTORY: RenameDirectory,
    CDCMessageKind.HARD_UNLINK_DIRECTORY: HardUnlinkDirectory,
    CDCMessageKind.HARD_UNLINK_FILE: HardUnlinkFile,
}

# Returns whether a new transaction was started
def begin_next_transaction(cur: sqlite3.Cursor) -> bool:
    # check if the first non-done transaction is not started
    candidate = cur.execute(
        '''
            select * from transactions
                where not done
                order by id asc, started desc -- started first, since true = 1
        '''
    ).fetchone()
    if candidate is None or candidate['started']:
        return False # nothing to start, we're already running something
    cur.execute(
        "update transactions set started = TRUE, next_step = 'begin', state = :state, last_update_time = :now where id = :id",
        {'id': candidate['id'], 'state': json.dumps(TRANSACTIONS[candidate['kind']]().state), 'now': eggs_time()}
    )
    return True

# A new CDC request has arrived.
def enqueue_transaction(cur: sqlite3.Cursor, req: CDCRequest) -> Optional[CDCStep]:
    sql_insert(
        cur, 'transactions',
        request_id=req.request_id,
        kind=req.body.KIND,
        body=bincode.pack(req.body),
        done=False,
        started=False,
        last_update_time=eggs_time(),
    )
    if begin_next_transaction(cur):
        return advance_current_transaction(cur)
    else:
        return None

# A response from the shard has arrived.
def process_shard_response(cur: sqlite3.Cursor, resp: ShardResponse) -> Optional[CDCStep]:
    # First, we must check that the request is in flight -- otherwise we just drop it
    current_tx = get_current_transaction(cur)
    if (
        current_tx is None or
        current_tx['shard_request_id'] is None or
        current_tx['shard_request_id'] != resp.request_id
    ):
        logging.debug(f'Dropping unexpected response to request {resp.request_id}')
        return None
    assert resp.body.KIND in (EggsError.KIND, current_tx['shard_request_kind'])
    # Now we get the started transaction and advance it
    return advance_current_transaction(cur, resp.body)

def advance(db: sqlite3.Connection, msg: Union[CDCRequest, ShardResponse]) -> Optional[CDCStep]:
    cur = db.cursor()
    result: Optional[CDCStep] = None
    try:
        if isinstance(msg, ShardResponse):
            logging.debug(f'Processing shard response {type(msg.body)}')
            result = process_shard_response(cur, msg)
        else:
            logging.debug(f'Enqueueing transaction {msg}')
            result = enqueue_transaction(cur, msg)
    except Exception:
        # Rollback possible partial changes in the case of unexpected error
        db.rollback()
        # Then abort current transaction
        result = abort_current_transaction(db.cursor())
        db.commit()
        logging.error(f'Got exception while processing CDC request of type {type(msg)}:')
        traceback.print_exc()
    logging.debug(f'Got result {result}')
    db.commit()
    return result

'''
def finish(db: sqlite3.Connection, msg: Union[CDCRequest, ShardResponse]) -> Tuple[List[CDCResponse], Optional[CDCShardRequest]]:
    responses: List[CDCResponse] = []
    while True:
        resp = advance(db, msg)
        # Nothing to do (and nothing was done)
        if resp is None:
            return (responses, None)
        # We need help from the shard, we can stop here
        elif isinstance(resp, CDCShardRequest):
            return (responses, resp)
        # We got a response
        else:
            responseappend(resp)
'''

# If it did abort something, return what
def get_current_transaction(cur: sqlite3.Cursor) -> Optional[Dict[str, Any]]:
    started_transactions = cur.execute(
        'select * from transactions where done = FALSE and started = TRUE'
    ).fetchall()
    assert len(started_transactions) < 2
    if not started_transactions:
        return None
    else:
        return started_transactions[0]

def mark_transaction_as_done(cur, *, id: int, now: int, resp: Union[EggsError, CDCResponseBody]):
    cur.execute(
        '''
            update transactions
                set
                    started = FALSE, done = TRUE, response_kind = :kind, response_body = :body,
                    next_step = NULL, state = NULL, shard_request_id = NULL, shard_request_shard = NULL, shard_request_kind = NULL, shard_request_critical = NULL, shard_request_body = NULL
                where id = :id
        ''',
        {
            'id': id,
            'now': now,
            'kind': resp.KIND,
            'body': bincode.pack(resp),
        }
    )
    # Possibly begin next transaction -- but don't start it! We don't
    # want crashes in one tx to cause rollbacks in the other.
    begin_next_transaction(cur)

# Return what it aborted
def abort_current_transaction(cur: sqlite3.Cursor) -> Optional[CDCResponse]:
    current_tx = get_current_transaction(cur)
    if current_tx is None:
        logging.info('No transaction was running -- nothing was aborted')
        return None
    mark_transaction_as_done(
        cur, id=current_tx['id'], now=eggs_time(),
        resp=EggsError(ErrCode.INTERNAL_ERROR),
    )
    print(f'Current transaction of type {current_tx["kind"]} was aborted')
    return CDCResponse(request_id=current_tx['request_id'], body=EggsError(ErrCode.INTERNAL_ERROR))

def advance_current_transaction(cur: sqlite3.Cursor, *args) -> Optional[CDCStep]:
    current_tx = get_current_transaction(cur)
    assert current_tx is not None
    # Fetch state and execute next step
    transaction = TRANSACTIONS[current_tx['kind']](json.loads(current_tx['state']))
    req = bincode.unpack(CDC_REQUESTS[current_tx['kind']][0], current_tx['body'])
    resp_internal = cast(CDCStepInternal, getattr(transaction, current_tx['next_step'])(cur, req, *args))
    resp: CDCStep
    # Important to bump last_update_time -- important to reliably detect timeouts since we're not sending the request here.
    # Things might crash between this update and even sending the request.
    now = eggs_time()
    if isinstance(resp_internal, CDCNeedsShard):
        # Update the shard_request_* columns with the new needed shard request, and the
        # state
        req_id = now
        cur.execute(
            '''
                update transactions
                    set last_update_time = :now, next_step = :next_step, state = :state, shard_request_id = :req_id, shard_request_shard = :shard, shard_request_kind = :kind, shard_request_critical = :critical, shard_request_body = :body
                    where id = :id
            ''',
            {
                'id': current_tx['id'],
                'next_step': resp_internal.next_step,
                'state': json.dumps(transaction.state),
                'now': now,
                'req_id': req_id,
                'shard': resp_internal.shard,
                'kind': resp_internal.request.KIND,
                'critical': resp_internal.critical,
                'body': bincode.pack(resp_internal.request),
            }
        )
        resp = CDCShardRequest(
            shard=resp_internal.shard,
            request=ShardRequest(request_id=req_id, body=resp_internal.request),
            critical=resp_internal.critical,
        )
    else:
        mark_transaction_as_done(cur, id=current_tx['id'], now=now, resp=resp_internal)
        resp = CDCResponse(request_id=current_tx['request_id'], body=resp_internal)
    return resp

def open_db(db_dir: str) -> sqlite3.Connection:
    os.makedirs(db_dir, exist_ok=True)
    db_fn = os.path.join(db_dir, f'cdc.sqlite')

    db = sqlite3.connect(db_fn)
    db.row_factory = sqlite3.Row
    init_db(db)

    return db

# Replaces requests of one kind with something else.
# The list maps from the nth time we encountered that kind
# to the replacement
ShardRequestsReplacements = Dict[
    ShardMessageKind,
    Dict[int, Union[EggsError, ShardResponseBody]]
]

def execute(
    execute_shard_request: Callable[[CDCShardRequest], ShardResponse],
    db: sqlite3.Connection,
    req: CDCRequest,
    # Useful for testing
    replacements: ShardRequestsReplacements = {},
) -> CDCResponse:
    replacements = collections.defaultdict(dict, replacements) # we'll destruct this
    msg: Union[CDCRequest, ShardResponse] = req
    response: Optional[CDCResponse] = None
    seen_kinds: collections.defaultdict[ShardMessageKind, int] = collections.defaultdict(lambda: 0)
    while response is None:
        step = advance(db, msg)
        assert step is not None
        if isinstance(step, CDCResponse):
            response = step
        else:
            shard_request = step
            shard_req_kind = shard_request.request.body.KIND
            # If required, inject a response, avoiding calling
            # the shard at all.
            if seen_kinds[shard_req_kind] in replacements[shard_req_kind]:
                msg = ShardResponse(request_id=shard_request.request.request_id, body=replacements[shard_req_kind][seen_kinds[shard_req_kind]])
                del replacements[shard_req_kind][seen_kinds[shard_req_kind]]
            else:
                shard_resp = execute_shard_request(shard_request)
                if isinstance(shard_resp, EggsError) and shard_request.critical:
                    raise RuntimeError(f'Got error {shard_resp} for critical shard request {shard_request}! Cannot proceed safely.')
                msg = ShardResponse(request_id=shard_resp.request_id, body=shard_resp.body)
            seen_kinds[shard_req_kind] += 1
    # Check that all replacements have fired
    for m in replacements.values():
        assert not m
    return response

'''
# Useful for testing, does everything locally
class TestDriver:
    def __init__(self, db_dir: str):
        self.shards: List[shard.TestDriver] = []
        for shard_id in range(256):
            self.shards.append(shard.TestDriver(db_dir, shard_id))
        self.cdc = open_db(db_dir)
    
    def _execute(self, req: CDCRequest, replacements: ShardRequestsReplacements) -> CDCResponse:
        return execute(
            lambda shard_req: self.shards[shard_req.shard].execute(shard_req.request),
            self.cdc, req, replacements,
        )

    def execute(self, full_req: CDCRequest, replacements: ShardRequestsReplacements = {}):
        # test encoding roundtrip
        full_req_bytes = bincode.pack(full_req)
        assert len(full_req_bytes) < UDP_MTU
        assert full_req == bincode.unpack(CDCRequest, full_req_bytes)
        full_resp = self._execute(full_req, replacements)
        full_resp_bytes = bincode.pack(full_resp)
        assert len(full_resp_bytes) < UDP_MTU
        assert full_resp == bincode.unpack(CDCResponse, full_resp_bytes)
        return full_resp

    def execute_ok(self, req: CDCRequestBody, replacements: ShardRequestsReplacements = {}):
        resp = self.execute(CDCRequest(request_id=eggs_time(), body=req), replacements)
        assert not isinstance(resp.body, EggsError), f'Got error for req {req}: {resp}'
        return resp.body

    def execute_err(self, req: CDCRequestBody, kind: Optional[ErrCode] = None, replacements: ShardRequestsReplacements = {}):
        resp = self.execute(CDCRequest(request_id=eggs_time(), body=req), replacements)
        assert isinstance(resp.body, EggsError), f'Expected error, got response {resp.body}'
        if kind is not None:
            assert resp.body.error_code == kind, f'Expected error {repr(kind)}, got {repr(ErrCode(resp.body.error_code))}'
        return resp.body

class CDCTests(unittest.TestCase):
    def setUp(self):
        db_dir = tempfile.mkdtemp(prefix='eggs_')
        logging.debug(f'Running CDC test with db_dir {db_dir}')
        self.cdc = TestDriver(db_dir)

    def _collect_all_inodes(self) -> Generator[int, None, None]:
        for req in (VisitDirectoriesReq, VisitFilesReq):
            for shard_id in range(256):
                current_id = 0
                while True:
                    results = self.cdc.shards[shard_id].execute_ok(req(begin_id=current_id)) # type: ignore
                    for id in results.ids:
                        yield id
                    current_id = results.next_id
                    if current_id == 0:
                        break
    
    def test_visit_inodes(self):
        entries_per_packet = UDP_MTU//8
        num_files = entries_per_packet + entries_per_packet//2
        num_dirs = num_files # these will be evenly spread, so it's actually not so great as a test, but it's pretty slow otherwise
        for i in range(num_files):
            f = cast(ConstructFileResp, self.cdc.shards[0].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
            self.cdc.shards[0].execute_ok(LinkFileReq(file_id=f.id, cookie=f.cookie, owner_id=ROOT_DIR_INODE_ID, name=f'file-{i}'.encode()))
        for i in range(num_dirs):
            self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, f'dir-{i}'.encode(), INHERIT_DIRECTORY_INFO))
        collected_files = 0
        collected_dirs = -1 # there's the root dir
        for id in self._collect_all_inodes():
            if inode_id_type(id) == InodeType.DIRECTORY:
                collected_dirs += 1
            else:
                collected_files += 1
        assert collected_files == num_files, f'Expected {num_files}, got {collected_files}'
        assert collected_dirs == num_dirs

    def test_make_dir(self):
        dir_1 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test-1', INHERIT_DIRECTORY_INFO))).id
        dir_2 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test-2', INHERIT_DIRECTORY_INFO))).id
        def read_dir_names(files):
            return set([r.name for r in files.results])
        def read_dir_targets(files):
            return set([r.target_id for r in files.results])
        dirs = self.cdc.shards[0].execute_ok(ReadDirReqCurrent(dir_id=ROOT_DIR_INODE_ID, start_hash=0))
        assert read_dir_names(dirs) == {b'test-1', b'test-2'} and read_dir_targets(dirs) == {dir_1, dir_2}
    
    def _only_root_dir_and_orphans(self, ids: Generator[int, None, None]):
        for id in ids:
            assert inode_id_type(id) == InodeType.DIRECTORY
            if id == ROOT_DIR_INODE_ID: continue
            stat = cast(StatDirectoryResp, self.cdc.shards[inode_id_shard(id)].execute_ok(StatDirectoryReq(id)))
            if stat.owner != NULL_INODE_ID: return False
        return True

    def test_make_dir_error_1(self):
        # We get rightful warnings instead
        with DisableLogging:
            self.cdc.execute_err(
                MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test', INHERIT_DIRECTORY_INFO),
                replacements={ShardMessageKind.CREATE_DIRECTORY_INODE: {0: EggsError(ErrCode.INTERNAL_ERROR)}},
                kind=ErrCode.INTERNAL_ERROR,
            )
        assert self._only_root_dir_and_orphans(self._collect_all_inodes())
    
    def test_make_dir_error_2(self):
        self.cdc.execute_err(
            MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test', INHERIT_DIRECTORY_INFO),
            replacements={ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE: {0: EggsError(ErrCode.INTERNAL_ERROR)}},
            kind=ErrCode.INTERNAL_ERROR,
        )
        assert self._only_root_dir_and_orphans(self._collect_all_inodes())

    def test_make_dir_no_override(self):
        self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test', INHERIT_DIRECTORY_INFO))
        self.cdc.execute_err(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test', INHERIT_DIRECTORY_INFO), kind=ErrCode.CANNOT_OVERRIDE_NAME)

    def test_move_file(self):
        file = cast(ConstructFileResp, self.cdc.shards[0].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
        self.cdc.shards[0].execute_ok(LinkFileReq(file_id=file.id, cookie=file.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'test-file'))
        dir = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test-dir', INHERIT_DIRECTORY_INFO))).id
        assert inode_id_shard(dir) != 0
        cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(dir, b'test-dir-inner', INHERIT_DIRECTORY_INFO))).id
        self.cdc.execute_err(RenameFileReq( # don't override dirs
            target_id=file.id,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-file',
            new_owner_id=dir,
            new_name=b'test-dir-inner',
        ), kind=ErrCode.CANNOT_OVERRIDE_NAME)
        self.cdc.execute_ok(RenameFileReq(
            target_id=file.id,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-file',
            new_owner_id=dir,
            new_name=b'test-file',
        ))
        dir_2 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test-dir-2', INHERIT_DIRECTORY_INFO))).id
        self.cdc.execute_err(RenameFileReq( # refuse to move directories
            target_id=dir_2,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-dir-2',
            new_owner_id=dir,
            new_name=b'test-dir-2',
        ), kind=ErrCode.TYPE_IS_DIRECTORY)
    
    def test_unlink_dir(self):
        dir = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test-dir', INHERIT_DIRECTORY_INFO))).id
        # We can unlink an empty dir
        self.cdc.execute_ok(SoftUnlinkDirectoryReq(owner_id=ROOT_DIR_INODE_ID, target_id=dir, name=b'test-dir'))
    
    def test_move_dir(self):
        dir_1 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'1', INHERIT_DIRECTORY_INFO))).id
        dir_2 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'2', INHERIT_DIRECTORY_INFO))).id
        dir_1_3 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(dir_1, b'3', INHERIT_DIRECTORY_INFO))).id
        dir_2_4 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(dir_2, b'4', INHERIT_DIRECTORY_INFO))).id
        file_2_foo = cast(ConstructFileResp, self.cdc.shards[inode_id_shard(dir_2)].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
        self.cdc.shards[inode_id_shard(dir_2)].execute_ok(LinkFileReq(file_2_foo.id, file_2_foo.cookie, dir_2, b'foo'))
        dir_1_3_5 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(dir_1, b'5', INHERIT_DIRECTORY_INFO))).id
        # not found failures
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'2', dir_2, b'bad'), ErrCode.MISMATCHING_TARGET)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', make_inode_id(InodeType.DIRECTORY, 4, 123), b'blah'), ErrCode.NEW_DIRECTORY_NOT_FOUND)
        self.cdc.execute_err(RenameDirectoryReq(make_inode_id(InodeType.DIRECTORY, 4, 123), ROOT_DIR_INODE_ID, b'1', dir_2, b'blah'), ErrCode.MISMATCHING_TARGET)
        # loop failures
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1, b'11'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1_3, b'1'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1_3_5, b'1'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        # no directory override
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'4'), ErrCode.CANNOT_OVERRIDE_NAME)
        # no file override
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'foo'), ErrCode.CANNOT_OVERRIDE_NAME)
        # /1 -> /2/1
        self.cdc.execute_ok(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'1'))
        # /2/1 -> /1
        self.cdc.execute_ok(RenameDirectoryReq(dir_1, dir_2, b'1', ROOT_DIR_INODE_ID, b'1'))

    def test_bad_soft_unlink_dir(self):
        dir = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'dir', INHERIT_DIRECTORY_INFO))).id
        self.cdc.shards[0].execute_err(SoftUnlinkFileReq(ROOT_DIR_INODE_ID, dir, b'dir'), ErrCode.TYPE_IS_DIRECTORY)

    def test_file_cycle(self):
        transient_file_1 = cast(ConstructFileResp, self.cdc.shards[0].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
        self.cdc.shards[0].execute_ok(LinkFileReq(transient_file_1.id, transient_file_1.cookie, ROOT_DIR_INODE_ID, b'file'))
        file_1 = cast(LookupResp, self.cdc.shards[0].execute_ok(LookupReq(ROOT_DIR_INODE_ID, b'file')))
        self.cdc.shards[0].execute_err(RemoveNonOwnedEdgeReq(ROOT_DIR_INODE_ID, file_1.target_id, b'file', file_1.creation_time), ErrCode.EDGE_NOT_FOUND)
        transient_file_2 = cast(ConstructFileResp, self.cdc.shards[0].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
        self.cdc.shards[0].execute_ok(LinkFileReq(transient_file_2.id, transient_file_2.cookie, ROOT_DIR_INODE_ID, b'file'))
        file_2 = cast(LookupResp, self.cdc.shards[0].execute_ok(LookupReq(ROOT_DIR_INODE_ID, b'file')))
        # we still cannot remove the old edge because it is owned
        self.cdc.shards[0].execute_err(RemoveNonOwnedEdgeReq(ROOT_DIR_INODE_ID, file_1.target_id, b'file', file_1.creation_time), ErrCode.EDGE_NOT_FOUND)
        # however we can remove it if we mean it
        self.cdc.shards[0].execute_ok(IntraShardHardFileUnlinkReq(ROOT_DIR_INODE_ID, file_1.target_id, b'file', file_1.creation_time))
        # We have a transient file now, since we removed an owned snapshot.
        transients = cast(VisitTransientFilesResp, self.cdc.shards[0].execute_ok(VisitTransientFilesReq(0, False)))
        assert len(transients.files) == 1
        # if we move the second file, we can remove it using RemoveNonOwnedEdgeReq
        self.cdc.shards[0].execute_ok(SameDirectoryRenameReq(transient_file_2.id, ROOT_DIR_INODE_ID, b'file', b'newfile'))
        snapshot_edges = cast(SnapshotReadDirResp, self.cdc.shards[0].execute_ok(SnapshotReadDirReq(ROOT_DIR_INODE_ID, 0, b'', 0)))
        current_edges = cast(ReadDirResp, self.cdc.shards[0].execute_ok(ReadDirReqCurrent(ROOT_DIR_INODE_ID, 0)))
        assert snapshot_edges.finished and current_edges.next_hash == 0
        assert (len(snapshot_edges.results) + len(current_edges.results)) == 3
        file_create, file_delete, newfile_create = tuple(snapshot_edges.results + current_edges.results)
        assert file_create.target_id == file_2.target_id
        assert file_delete.target_id == NULL_INODE_ID
        self.cdc.shards[0].execute_ok(RemoveNonOwnedEdgeReq(ROOT_DIR_INODE_ID, transient_file_2.id,  b'file', file_create.creation_time))
        self.cdc.shards[0].execute_ok(RemoveNonOwnedEdgeReq(ROOT_DIR_INODE_ID, NULL_INODE_ID,  b'file', file_delete.creation_time))
        # TODO finish adding/removing blocks etc.
    
    def test_snapshot_directory(self):
        dir_1 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'1', INHERIT_DIRECTORY_INFO))).id
        self.cdc.execute_ok(SoftUnlinkDirectoryReq(ROOT_DIR_INODE_ID, dir_1, b'1'))
        # can't create files or directories in the deleted directory
        self.cdc.execute_err(MakeDirectoryReq(dir_1, b'2', INHERIT_DIRECTORY_INFO), ErrCode.DIRECTORY_NOT_FOUND)
        transient_file_1 = cast(ConstructFileResp, self.cdc.shards[inode_id_shard(dir_1)].execute_ok(ConstructFileReq(InodeType.FILE, b'')))
        self.cdc.shards[inode_id_shard(dir_1)].execute_err(LinkFileReq(transient_file_1.id, transient_file_1.cookie, dir_1, b'2'), ErrCode.DIRECTORY_NOT_FOUND)
    
    def test_remove_inode(self):
        dir_1 = cast(MakeDirectoryResp, self.cdc.execute_ok(MakeDirectoryReq(ROOT_DIR_INODE_ID, b'test', INHERIT_DIRECTORY_INFO))).id
        self.cdc.shards[inode_id_shard(dir_1)].execute_err(RemoveInodeReq(dir_1), ErrCode.DIRECTORY_HAS_OWNER)
'''

def run_forever(db: sqlite3.Connection):
    logging.info(f'Running on port {CDC_PORT}')
    api_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    api_sock.bind(('', CDC_PORT))
    shard_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    shard_sock.bind(('', 0))
    shard_sock.settimeout(2.0)
    def execute_shard_req(req: CDCShardRequest) -> ShardResponse:
        packed_req = req.request.pack(CDC_KEY)
        shard_sock.sendto(packed_req, ('127.0.0.1', shard_to_port(req.shard)))
        resp: ShardResponse
        try:
            packed_resp = shard_sock.recv(UDP_MTU)
            resp = bincode.unpack(ShardResponse, packed_resp)
        except socket.timeout:
            resp = ShardResponse(request_id=req.request.request_id, body=EggsError(ErrCode.TIMEOUT))
        assert req.request.request_id == resp.request_id
        return resp
    while True:
        try:
            data, addr = api_sock.recvfrom(UDP_MTU)
            req = bincode.unpack(CDCRequest, data)
            resp = execute(execute_shard_req, db, req)
            api_sock.sendto(bincode.pack(resp), addr)
        except Exception:
            logging.error(f'Got exception while processing CDC request')
            traceback.print_exc()

def main(db_dir: str):
    db = open_db(db_dir)
    run_forever(db)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Runs the cross-directory coordinator')
    parser.add_argument('db_dir', help='Location to create or load db')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--log_file', type=Path)
    parser.add_argument('--port', type=int, default=5000)
    config = parser.parse_args(sys.argv[1:])

    log_level = logging.DEBUG if config.verbose else logging.WARNING
    if config.log_file:
        logging.basicConfig(filename=config.log_file, encoding='utf-8', level=log_level, format=log_format.FORMAT)
    else:
        logging.basicConfig(level=log_level, format=log_format.FORMAT)

    main(config.db_dir)