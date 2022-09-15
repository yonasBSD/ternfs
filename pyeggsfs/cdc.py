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
import crypto

from cdc_msgs import *
from cdc_key import *
from common import *
import shard_msgs as s
import shard
from shard import sql_insert, DisableLogging
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
        {'shard': shard, 'next_dir': ROOT_DIR_INODE_ID+1}
    )
    # What transactions we're currently or want to execute
    cur.execute('''
        create table if not exists transactions (
            -- The ix specifies in which order we run the transactions
            ix integer primary key autoincrement,
            id integer not null, -- what was in the request
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
            shard_request_body blob,
            check (not done or (response_kind is not null and response_body is not null and state is null and next_step is null and shard_request_id is null and shard_request_shard is null and shard_request_kind is null and shard_request_body is null)),
            check (not started or (response_kind is null and response_body is null and state is not null and next_step is not null))
            -- Unless we've just begun the request, we have a shard request in flight to proceed
            check (not (started and next_step != 'begin') or (shard_request_id is not null and shard_request_shard is not null and shard_request_kind is not null and shard_request_body is not null))
        )
    ''')
    cur.execute('create unique index if not exists transactions_by_id on transactions (id)')
    # We very often look for the first non-done request (that's the one we're doing)
    cur.execute('create index if not exists transactions_by_completion on transactions (done, started, ix)')
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
    request: s.ShardRequestBody

# When we make progress in the CDC, we either finish a transaction,
# getting a response, or request data from a shard to make progress
# in said transaction.
CDCStepInternal = Union[CDCResponseBody, CDCNeedsShard]

@dataclass
class CDCShardRequest:
    shard: int
    request: s.ShardRequest
CDCStep = Union[CDCResponse, CDCShardRequest]

# This is just a type to type methods better in transactions
T = TypeVar('T')
ExpectedShardResp = Union[EggsError, T]

class Transaction:
    state: Any

    def __init__(self, state: Optional[Any] = None):
        pass

# Steps:
#
# 1. Allocate inode id here in the CDC
# 2. Create directory in shard we get from the inode
# 3. Create locked edge from owner to newly created directory
# 4. Unlock the edge created in 3
#
# If 3 fails, 2 must be rolled back. 4 does not fail.
class MakeDir(Transaction):
    class MakeDirState(TypedDict):
        dir_id: int
        # What error to return after we've finished rolling back
        exit_error: Optional[ErrCode]
    state: MakeDirState

    def __init__(self, state: Optional[MakeDirState] = None):
        self.state = state or {
            'dir_id': NULL_INODE_ID,
            'exit_error': None,
        }

    def begin(self, cur: sqlite3.Cursor, req: MakeDirReq) -> CDCStepInternal:
        cdc_info = CDCInfo(cur)
        self.state['dir_id'] = cdc_info.next_directory_id(cur)
        return CDCNeedsShard(
            next_step='create_dir_resp',
            shard=inode_id_shard(self.state['dir_id']),
            request=s.CreateDirectoryINodeReq(self.state['dir_id'], owner_id=req.owner_id, opaque=b''),
        )

    def create_dir_resp(self, cur: sqlite3.Cursor, req: MakeDirReq, resp: ExpectedShardResp[s.CreateDirectoryINodeResp]) -> CDCStepInternal:
        # If we errored, let's error as well
        if isinstance(resp, EggsError):
            return resp
        # We've succeeded, let's proceed: we need to link the directory
        now = eggs_time()
        return CDCNeedsShard(
            next_step='create_locked_edge_resp',
            shard=inode_id_shard(req.owner_id),
            request=s.CreateLockedCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=self.state['dir_id'], creation_time=now)
        )
    
    def create_locked_edge_resp(self, cur: sqlite3.Cursor, req: MakeDirReq, resp: ExpectedShardResp[s.CreateLockedCurrentEdgeResp]) -> CDCStepInternal:
        # We've failed to link: we need to rollback the directory creation
        if isinstance(resp, EggsError):
            return self._init_rollback(resp.error_code)
        # Now, unlock the edge
        return CDCNeedsShard(
            next_step='unlock_edge_resp',
            shard=inode_id_shard(req.owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=self.state['dir_id'], was_moved=False),
        )
    
    def unlock_edge_resp(self, cur: sqlite3.Cursor, req: MakeDirReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]):
        if isinstance(resp, EggsError):
            logging.error("We couldn't unlock the newly created locked edge when making a directory! This is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        # We're done!
        return MakeDirResp(self.state['dir_id'])

    def _init_rollback(self, err: ErrCode):
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(self.state['dir_id']),
            request=s.SetDirectoryOwnerReq(self.state['dir_id'], owner_id=NULL_INODE_ID)
        )

    def rollback_resp(self, cur: sqlite3.Cursor, req: MakeDirReq, resp: ExpectedShardResp[s.SetDirectoryOwnerResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f"We couldn't roll back MakeDir {self.state['dir_id']} (error {resp.error_code}), this is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
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
            return EggsError(ErrCode.UNEXPECTED_DIRECTORY_IN_MOVE_FILE)
        return CDCNeedsShard(
            next_step='lock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.LockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id),
        )
    
    def lock_old_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.LockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            # We couldn't acquire the lock at the source -- we can terminate immediately, there's nothing to roll back
            return resp
        # onwards and upwards
        return CDCNeedsShard(
            next_step='create_new_locked_edge_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=s.CreateLockedCurrentEdgeReq(
                dir_id=req.new_owner_id,
                name=req.new_name,
                target_id=req.target_id,
                creation_time=self.state['new_edge_creation_time'],
            )
        )
    
    def create_new_locked_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.CreateLockedCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            # We couldn't create the new edge, we need to unlock the old edge.
            return self._init_rollback(req, resp.error_code)
        # Now, unlock the edge
        return CDCNeedsShard(
            next_step='unlock_new_edge_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.new_owner_id, name=req.new_name, target_id=req.target_id, was_moved=False),
        )
    
    def unlock_new_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error("We couldn't unlock the newly created locked edge when making a directory! This is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        # We're done creating the destination edge, now unlock the source, marking it as moved
        return CDCNeedsShard(
            next_step='unlock_old_edge_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.UnlockCurrentEdgeReq(
                dir_id=req.old_owner_id,
                name=req.old_name,
                target_id=req.target_id,
                was_moved=True,
            )
        )
    
    def unlock_old_edge_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeReq]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f"We couldn't unlock old edge in {req} (error {resp.error_code}), this is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        return RenameFileResp()

    def _init_rollback(self, req: RenameFileReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=False),
        )
    
    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f"We couldn't roll back {req} (error {resp.error_code}), this is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        assert self.state['exit_error'] is not None
        return EggsError(self.state['exit_error'])

# Steps:
#
# 1. Lock edge going into the directory to remove. This prevents things making
#     making it snapshot or similar in the meantime.
# 2. Remove directory owner from directory that we want to remove. This will fail if there still
#     are current edges there.
# 3. Unlock edge going into the directory, making it snapshot.
#
# If 2 fails, we need to roll back the locking, without making the edge snapshot.
class UnlinkDirectory(Transaction):
    class UnlinkDirectoryState(TypedDict):
        exit_error: Optional[ErrCode] # after we roll back, we return this error to the user
    state: UnlinkDirectoryState

    def __init__(self, state: Optional[UnlinkDirectoryState] = None):
        self.state = state or {
            'exit_error': None,
        }

    def begin(self, cur: sqlite3.Cursor, req: UnlinkDirectoryReq) -> CDCStepInternal:
        return CDCNeedsShard(
            next_step='lock_resp',
            shard=inode_id_shard(req.owner_id),
            request=s.LockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id),
        )

    def lock_resp(self, cur: sqlite3.Cursor, req: UnlinkDirectoryReq, resp: ExpectedShardResp[s.LockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return resp # Nothing to rollback
        return CDCNeedsShard(
            next_step='remove_owner_resp',
            shard=inode_id_shard(req.target_id),
            request=s.SetDirectoryOwnerReq(dir_id=req.target_id, owner_id=NULL_INODE_ID),
        )
    
    def remove_owner_resp(self, cur: sqlite3.Cursor, req: UnlinkDirectoryReq, resp: ExpectedShardResp[s.SetDirectoryOwnerResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return self._init_rollback(req, resp.error_code) # we need to unlock what we locked
        return CDCNeedsShard(
            next_step='unlock_resp',
            shard=inode_id_shard(req.owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id, was_moved=True),
        )
    
    def unlock_resp(self, cur: sqlite3.Cursor, req: s.UnlockCurrentEdgeResp, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):        
            logging.error(f"We couldn't unlock old edge in {req} (error {resp.error_code}), this is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        return UnlinkDirectoryResp()
    
    def _init_rollback(self, req: UnlinkDirectoryReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.owner_id, name=req.name, target_id=req.target_id, was_moved=False),
        )

    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameFileReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f"We couldn't roll back {req} (error {resp.error_code}), this is bad!")
            return EggsError(ErrCode.FATAL_ERROR)
        assert self.state['exit_error'] is not None
        return EggsError(self.state['exit_error'])

# Steps:
#
# 1. Lock old edge
# 2. Make sure that there's no directory loop by traversing the parents
# 3. Create and lock the new edge
# 4. Unlock the new edge
# 5. Unlock and unlink the old edge
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
        if req.target_id == req.new_owner_id:
            return EggsError(ErrCode.LOOP_IN_DIRECTORY_RENAME) # important -- we don't catch this later
        return CDCNeedsShard(
            next_step='lock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.LockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id),
        )
    
    def lock_old_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.LockCurrentEdgeResp]) -> CDCStepInternal:
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
            request=s.StatReq(self.state['current_directory']),
        )
    
    def stat_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.StatResp]) -> CDCStepInternal:
        assert self.state['current_directory'] is not None
        if isinstance(resp, EggsError):
            # Internal error because this should never happen, unless we haven't found the destination
            # dir, which might be.
            err: ErrCode
            if self.state['current_directory'] == req.new_owner_id:
                err = resp.error_code
            else:
                logging.error(f'Unexpected error for stat of directory {self.state["current_directory"]} which should exist: {resp}')
                err = ErrCode.INTERNAL_ERROR
            return self._init_rollback(req, err)
        owner = resp.size_or_owner
        if owner in self.state['parent_cache']:
            return self._init_rollback(req, ErrCode.LOOP_IN_DIRECTORY_RENAME)
        if owner == NULL_INODE_ID:
            # We're done traversing
            return CDCNeedsShard(
                next_step='create_new_resp',
                shard=inode_id_shard(req.new_owner_id),
                request=s.CreateLockedCurrentEdgeReq(req.new_owner_id, req.new_name, req.target_id, eggs_time()),
            )
        self.state['parent_cache'][self.state['current_directory']] = owner
        self.state['current_directory'] = owner
        return self._init_parent_lookup()
    
    def create_new_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.CreateLockedCurrentEdgeReq]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            return self._init_rollback(req, resp.error_code)
        return CDCNeedsShard(
            next_step='unlock_new_resp',
            shard=inode_id_shard(req.new_owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.new_owner_id, name=req.new_name, target_id=req.target_id, was_moved=False),
        )
    
    def unlock_new_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeReq]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f'Unexpected error in edge unlocking operation! {resp}')
            return EggsError(ErrCode.FATAL_ERROR)
        return CDCNeedsShard(
            next_step='unlock_old_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=True),
        )
    
    def unlock_old_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeReq]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f'Unexpected error in edge unlocking operation! {resp}')
            return EggsError(ErrCode.FATAL_ERROR)
        # We're finally done
        return RenameDirectoryResp()
   
    def _init_rollback(self, req: RenameDirectoryReq, err: ErrCode) -> CDCStepInternal:
        self.state['exit_error'] = err
        return CDCNeedsShard(
            next_step='rollback_resp',
            shard=inode_id_shard(req.old_owner_id),
            request=s.UnlockCurrentEdgeReq(dir_id=req.old_owner_id, name=req.old_name, target_id=req.target_id, was_moved=False),
        )
    
    def rollback_resp(self, cur: sqlite3.Cursor, req: RenameDirectoryReq, resp: ExpectedShardResp[s.UnlockCurrentEdgeResp]) -> CDCStepInternal:
        if isinstance(resp, EggsError):
            logging.error(f'Unexpected error in edge unlocking operation! {resp}')
            return EggsError(ErrCode.FATAL_ERROR)
        assert self.state['exit_error']
        return EggsError(self.state['exit_error'])
        

TRANSACTIONS: Dict[CDCRequestKind, Type[Transaction]] = {
    CDCRequestKind.MAKE_DIR: MakeDir,
    CDCRequestKind.RENAME_FILE: RenameFile,
    CDCRequestKind.UNLINK_DIRECTORY: UnlinkDirectory,
    CDCRequestKind.RENAME_DIRECTORY: RenameDirectory,
}

# Returns whether a new transaction was started
def begin_next_transaction(cur: sqlite3.Cursor) -> bool:
    # check if the first non-done transaction is not started
    candidate = cur.execute(
        '''
            select * from transactions
                where not done
                order by ix asc, started desc -- started first, since true = 1
        '''
    ).fetchone()
    if candidate is None or candidate['started']:
        return False # nothing to start, we're already running something
    cur.execute(
        "update transactions set started = TRUE, next_step = 'begin', state = :state, last_update_time = :now where ix = :ix",
        {'ix': candidate['ix'], 'state': json.dumps(TRANSACTIONS[candidate['kind']]().state), 'now': eggs_time()}
    )
    return True

# A new CDC request has arrived.
def enqueue_transaction(cur: sqlite3.Cursor, req: CDCRequest) -> Optional[CDCStep]:
    sql_insert(
        cur, 'transactions',
        id=req.request_id,
        kind=req.body.kind,
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
def process_shard_response(cur: sqlite3.Cursor, resp: s.ShardResponse) -> Optional[CDCStep]:
    # First, we must check that the request is in flight -- otherwise we just drop it
    current_tx = get_current_transaction(cur)
    if (
        current_tx is None or
        current_tx['shard_request_id'] is None or
        current_tx['shard_request_id'] != resp.request_id
    ):
        logging.debug(f'Dropping unexpected response to request {resp.request_id}')
        return None
    assert resp.body.kind in (s.ShardRequestKind.ERROR, current_tx['shard_request_kind'])
    # Now we get the started transaction and advance it
    return advance_current_transaction(cur, resp.body)

def advance(db: sqlite3.Connection, msg: Union[CDCRequest, s.ShardResponse]) -> Optional[CDCStep]:
    cur = db.cursor()
    result: Optional[CDCStep] = None
    try:
        if isinstance(msg, s.ShardResponse):
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
def finish(db: sqlite3.Connection, msg: Union[CDCRequest, s.ShardResponse]) -> Tuple[List[CDCResponse], Optional[CDCShardRequest]]:
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
            responses.append(resp)
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

def mark_transaction_as_done(cur, *, ix: int, now: int, resp: CDCResponseBody):
    cur.execute(
        '''
            update transactions
                set
                    started = FALSE, done = TRUE, response_kind = :kind, response_body = :body,
                    next_step = NULL, state = NULL, shard_request_id = NULL, shard_request_shard = NULL, shard_request_kind = NULL, shard_request_body = NULL
                where ix = :ix
        ''',
        {
            'ix': ix,
            'now': now,
            'kind': resp.kind,
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
        print('No transaction was running -- nothing was aborted', file=sys.stderr)
        return None
    mark_transaction_as_done(
        cur, ix=current_tx['ix'], now=eggs_time(),
        resp=EggsError(ErrCode.INTERNAL_ERROR),
    )
    print(f'Current transaction of type {current_tx["kind"]} was aborted')
    return CDCResponse(current_tx['id'], EggsError(ErrCode.INTERNAL_ERROR))

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
                    set last_update_time = :now, next_step = :next_step, state = :state, shard_request_id = :req_id, shard_request_shard = :shard, shard_request_kind = :kind, shard_request_body = :body
                    where ix = :ix
            ''',
            {
                'ix': current_tx['ix'],
                'next_step': resp_internal.next_step,
                'state': json.dumps(transaction.state),
                'now': now,
                'req_id': req_id,
                'shard': resp_internal.shard,
                'kind': resp_internal.request.kind,
                'body': bincode.pack(resp_internal.request),
            }
        )
        resp = CDCShardRequest(shard=resp_internal.shard, request=s.ShardRequest(version=s.PROTOCOL_VERSION, request_id=req_id, body=resp_internal.request))
    else:
        mark_transaction_as_done(cur, ix=current_tx['ix'], now=now, resp=resp_internal)
        resp = CDCResponse(current_tx['id'], resp_internal)
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
    s.ShardRequestKind,
    Dict[int, s.ShardResponseBody]
]

def execute(
    execute_shard_request: Callable[[CDCShardRequest], s.ShardResponse],
    db: sqlite3.Connection,
    req: CDCRequest,
    # Useful for testing
    replacements: ShardRequestsReplacements = {},
) -> CDCResponse:
    replacements = collections.defaultdict(dict, replacements) # we'll destruct this
    msg: Union[CDCRequest, s.ShardResponse] = req
    response: Optional[CDCResponse] = None
    seen_kinds: collections.defaultdict[s.ShardRequestKind, int] = collections.defaultdict(lambda: 0)
    while response is None:
        step = advance(db, msg)
        assert step is not None
        if isinstance(step, CDCResponse):
            response = step
        else:
            shard_request = step
            shard_req_kind = shard_request.request.body.kind
            # If required, inject a response, avoiding calling
            # the shard at all.
            if seen_kinds[shard_req_kind] in replacements[shard_req_kind]:
                msg = s.ShardResponse(shard_request.request.request_id, replacements[shard_req_kind][seen_kinds[shard_req_kind]])
                del replacements[shard_req_kind][seen_kinds[shard_req_kind]]
            else:
                shard_resp = execute_shard_request(shard_request)
                msg = s.ShardResponse(shard_resp.request_id, shard_resp.body)
            seen_kinds[shard_req_kind] += 1
    # Check that all replacements have fired
    for m in replacements.values():
        assert not m
    return response


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
        assert len(full_req_bytes) < s.UDP_MTU
        assert full_req == bincode.unpack(CDCRequest, full_req_bytes)
        full_resp = self._execute(full_req, replacements)
        full_resp_bytes = bincode.pack(full_resp)
        assert len(full_resp_bytes) < s.UDP_MTU
        assert full_resp == bincode.unpack(CDCResponse, full_resp_bytes)
        return full_resp

    def execute_ok(self, req: CDCRequestBody, replacements: ShardRequestsReplacements = {}):
        resp = self.execute(CDCRequest(version=s.PROTOCOL_VERSION, request_id=eggs_time(), body=req), replacements)
        assert not isinstance(resp.body, EggsError), f'Got error for req {req}: {resp}'
        return resp.body

    def execute_err(self, req: CDCRequestBody, kind: Optional[ErrCode] = None, replacements: ShardRequestsReplacements = {}):
        resp = self.execute(CDCRequest(version=s.PROTOCOL_VERSION, request_id=eggs_time(), body=req), replacements)
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
        for shard_id in range(256):
            current_id = 0
            while True:
                results = cast(s.VisitInodesResp, self.cdc.shards[shard_id].execute_ok(s.VisitInodesReq(begin_id=current_id)))
                for id in results.ids:
                    yield id
                current_id = results.next_id
                if current_id == 0:
                    break
    
    def test_visit_inodes(self):
        entries_per_packet = s.UDP_MTU//8
        num_files = entries_per_packet + entries_per_packet//2
        num_dirs = num_files # these will be evenly spread, so it's actually not so great as a test, but it's pretty slow otherwise
        for i in range(num_files):
            f = cast(s.ConstructFileResp, self.cdc.shards[0].execute_ok(s.ConstructFileReq(InodeType.FILE), repeats=1))
            self.cdc.shards[0].execute_ok(s.LinkFileReq(file_id=f.id, cookie=f.cookie, owner_id=ROOT_DIR_INODE_ID, name=f'file-{i}'.encode()))
        for i in range(num_dirs):
            self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, f'dir-{i}'.encode()))
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
        dir_1 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test-1'))).id
        dir_2 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test-2'))).id
        def read_dir_names(files):
            return set([r.name for r in files.results])
        def read_dir_targets(files):
            return set([r.target_id for r in files.results])
        dirs = self.cdc.shards[0].execute_ok(s.ReadDirReq(dir_id=ROOT_DIR_INODE_ID, continuation_key=0))
        assert read_dir_names(dirs) == {b'test-1', b'test-2'} and read_dir_targets(dirs) == {dir_1, dir_2}
    
    def _only_root_dir_and_orphans(self, ids: Generator[int, None, None]):
        for id in ids:
            assert inode_id_type(id) == InodeType.DIRECTORY
            if id == ROOT_DIR_INODE_ID: continue
            stat = cast(s.StatResp, self.cdc.shards[inode_id_shard(id)].execute_ok(s.StatReq(id)))
            if stat.size_or_owner != NULL_INODE_ID: return False
        return True

    def test_make_dir_error_1(self):
        self.cdc.execute_err(
            MakeDirReq(ROOT_DIR_INODE_ID, b'test'),
            replacements={s.ShardRequestKind.CREATE_DIRECTORY_INODE: {0: EggsError(ErrCode.INTERNAL_ERROR)}},
            kind=ErrCode.INTERNAL_ERROR,
        )
        assert self._only_root_dir_and_orphans(self._collect_all_inodes())
    
    def test_make_dir_error_2(self):
        self.cdc.execute_err(
            MakeDirReq(ROOT_DIR_INODE_ID, b'test'),
            replacements={s.ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE: {0: EggsError(ErrCode.INTERNAL_ERROR)}},
            kind=ErrCode.INTERNAL_ERROR,
        )
        assert self._only_root_dir_and_orphans(self._collect_all_inodes())

    def test_make_dir_error_3(self):
        # In this case, when rolling back fails, we do expect an
        # orphan dir.
        with DisableLogging:
            self.cdc.execute_err(
                MakeDirReq(ROOT_DIR_INODE_ID, b'test'),
                replacements={
                    s.ShardRequestKind.CREATE_LOCKED_CURRENT_EDGE: {0: EggsError(ErrCode.INTERNAL_ERROR)},
                    s.ShardRequestKind.SET_DIRECTORY_OWNER: {0: EggsError(s.ErrCode.INTERNAL_ERROR)},
                },
                kind=ErrCode.FATAL_ERROR,
            )
        assert not self._only_root_dir_and_orphans(self._collect_all_inodes())
        
    def test_make_dir_no_override(self):
        self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test'))
        self.cdc.execute_err(MakeDirReq(ROOT_DIR_INODE_ID, b'test'), kind=ErrCode.CANNOT_OVERRIDE_CURRENT_EDGE)

    def test_move_file(self):
        file = cast(s.ConstructFileResp, self.cdc.shards[0].execute_ok(s.ConstructFileReq(InodeType.FILE)))
        self.cdc.shards[0].execute_ok(s.LinkFileReq(file_id=file.id, cookie=file.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'test-file'))
        dir = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test-dir'))).id
        assert inode_id_shard(dir) != 0
        cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(dir, b'test-dir-inner'))).id
        self.cdc.execute_err(RenameFileReq( # don't override dirs
            target_id=file.id,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-file',
            new_owner_id=dir,
            new_name=b'test-dir-inner',
        ), kind=ErrCode.CANNOT_OVERRIDE_CURRENT_EDGE)
        self.cdc.execute_ok(RenameFileReq(
            target_id=file.id,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-file',
            new_owner_id=dir,
            new_name=b'test-file',
        ))
        dir_2 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test-dir-2'))).id
        self.cdc.execute_err(RenameFileReq( # refuse to move directories
            target_id=dir_2,
            old_owner_id=ROOT_DIR_INODE_ID,
            old_name=b'test-dir-2',
            new_owner_id=dir,
            new_name=b'test-dir-2',
        ), kind=ErrCode.UNEXPECTED_DIRECTORY_IN_MOVE_FILE)
        # These are some unrecoverable errors
        file = cast(s.ConstructFileResp, self.cdc.shards[0].execute_ok(s.ConstructFileReq(InodeType.FILE)))
        self.cdc.shards[0].execute_ok(s.LinkFileReq(file_id=file.id, cookie=file.cookie, owner_id=ROOT_DIR_INODE_ID, name=b'test-file-3'))
        with DisableLogging:
            self.cdc.execute_err(
                RenameFileReq(
                    target_id=file.id,
                    old_owner_id=ROOT_DIR_INODE_ID,
                    old_name=b'test-file-3',
                    new_owner_id=dir,
                    new_name=b'test-file-3',
                ),
                kind=ErrCode.FATAL_ERROR,
                replacements={s.ShardRequestKind.UNLOCK_CURRENT_EDGE: {0: EggsError(ErrCode.INTERNAL_ERROR)}},
            )
    
    def test_unlink_dir(self):
        dir = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'test-dir'))).id
        # We can unlink an empty dir
        self.cdc.execute_ok(UnlinkDirectoryReq(owner_id=ROOT_DIR_INODE_ID, target_id=dir, name=b'test-dir'))
    
    def test_move_dir(self):
        dir_1 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'1'))).id
        dir_2 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'2'))).id
        dir_1_3 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(dir_1, b'3'))).id
        dir_2_4 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(dir_2, b'4'))).id
        file_2_foo = cast(s.ConstructFileResp, self.cdc.shards[inode_id_shard(dir_2)].execute_ok(s.ConstructFileReq(InodeType.FILE)))
        self.cdc.shards[inode_id_shard(dir_2)].execute_ok(s.LinkFileReq(file_2_foo.id, file_2_foo.cookie, dir_2, b'foo'))
        dir_1_3_5 = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(dir_1, b'5'))).id
        # not found failures
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'2', dir_2, b'bad'), ErrCode.NOT_FOUND)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', assemble_inode_id(InodeType.DIRECTORY, 4, 123), b'blah'), ErrCode.NOT_FOUND)
        self.cdc.execute_err(RenameDirectoryReq(assemble_inode_id(InodeType.DIRECTORY, 4, 123), ROOT_DIR_INODE_ID, b'1', dir_2, b'blah'), ErrCode.NOT_FOUND)
        # loop failures
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1, b'11'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1_3, b'1'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_1_3_5, b'1'), ErrCode.LOOP_IN_DIRECTORY_RENAME)
        # no directory override
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'4'), ErrCode.CANNOT_OVERRIDE_CURRENT_EDGE)
        # no file override
        self.cdc.execute_err(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'foo'), ErrCode.CANNOT_OVERRIDE_CURRENT_EDGE)
        # /1 -> /2/1
        self.cdc.execute_ok(RenameDirectoryReq(dir_1, ROOT_DIR_INODE_ID, b'1', dir_2, b'1'))
        # /2/1 -> /1
        self.cdc.execute_ok(RenameDirectoryReq(dir_1, dir_2, b'1', ROOT_DIR_INODE_ID, b'1'))

    def test_bad_soft_unlink_dir(self):
        dir = cast(MakeDirResp, self.cdc.execute_ok(MakeDirReq(ROOT_DIR_INODE_ID, b'dir'))).id
        self.cdc.shards[0].execute_err(s.SoftUnlinkFileReq(ROOT_DIR_INODE_ID, dir, b'dir'), ErrCode.CANNOT_SOFT_UNLINK_DIRECTORY)



def run_forever(db: sqlite3.Connection):
    api_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    api_sock.bind(('', CDC_PORT))
    shard_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    shard_sock.bind(('', 0))
    shard_sock.settimeout(2.0)
    def execute_shard_req(req: CDCShardRequest) -> s.ShardResponse:
        packed_req = bincode.pack(req.request)
        if req.request.body.kind.is_privileged():
            packed_req = crypto.add_mac(packed_req, CDC_KEY)
        shard_sock.sendto(packed_req, ('127.0.0.1', shard_to_port(req.shard)))
        resp: s.ShardResponse
        try:
            packed_resp = shard_sock.recv(UDP_MTU)
            resp = bincode.unpack(s.ShardResponse, packed_resp)
        except socket.timeout:
            resp = s.ShardResponse(req.request.request_id, EggsError(ErrCode.TIMED_OUT))
        assert req.request.request_id == resp.request_id
        return resp
    while True:
        data, addr = api_sock.recvfrom(UDP_MTU)
        req = bincode.unpack(CDCRequest, data)
        resp = execute(execute_shard_req, db, req)
        api_sock.sendto(bincode.pack(resp), addr)


def main(db_dir: str):
    db = open_db(db_dir)
    run_forever(db)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Runs the cross-directory coordinator')
    parser.add_argument('db_dir', help='Location to create or load db')
    config = parser.parse_args(sys.argv[1:])

    main(config.db_dir)
