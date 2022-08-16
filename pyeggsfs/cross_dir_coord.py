#!/usr/bin/env python3

import abc
import os
import random
import socket
import sys
import time
from typing import (Any, Callable, ClassVar, Dict, NamedTuple, NewType,
    Optional, Set, Tuple, Type, Union)

import basic_client
import bincode
import cross_dir_key
from cross_dir_msgs import *
import metadata_msgs
from metadata_msgs import InodeType, MetadataErrorKind
import metadata_utils
from metadata_utils import NULL_INODE


PROTOCOL_VERSION = 0


# <boilerplate>

# this code is real ugly, and I'm not clever enough to staticlly type it correctly
# but it works
#
# it allows us to register methods into a dict of state machine nodes
# each node is a function, which returns the next state when run

class StateMachineMeta(abc.ABCMeta):
    def __new__(cls: Type[type], name: str, bases: Tuple[Any, ...], classdict: Dict[Any, Any]) -> Any:
        x = super().__new__(cls, name, bases, classdict) # type: ignore[misc]
        x.state_machine_nodes = {}
        for name, method in x.__dict__.items():
            if hasattr(method, "__state_machine_func"):
                x.state_machine_nodes[name] = method
        if 'initial' not in x.state_machine_nodes:
            raise ValueError('state_machine needs initial func')
        return x


StateFuncTy = Callable[['StateMachineBase', 'CrossDirCoordinator'], Optional[str]]


def state_func(f: Callable[..., Any]) -> Callable[..., Any]:
    f.__dict__['__state_machine_func'] = True
    return f


class StateMachineBase(metaclass=StateMachineMeta):
    state_machine_nodes: ClassVar[Dict[str, StateFuncTy]]

    @abc.abstractmethod
    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        pass

# </boilerplate>


class MkDir(StateMachineBase):
    def __init__(self, req: MkDirReq) -> None:
        self.parent_id = req.parent_id
        self.subname = req.subname
        self.new_inode: Optional[int] = None
        self.new_creation_ts: Optional[int] = None

    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        # Create a new directory object in a randomly selected shard
        shard = 0#random.randint(0, 255)
        token_inode = c.persist_state.token_inodes[shard]
        # TODO: define opaque somehow (from client?)
        body = metadata_msgs.CreateDirReq(
            token_inode, self.parent_id, b''
        )
        # TODO: need retry logic on timeout
        resp = basic_client.send_request(
            body, shard, cross_dir_key.CROSS_DIR_KEY)
        if not isinstance(resp, metadata_msgs.CreateDirResp):
            # no need to retry, it's find to "leak" these proto-directories
            c.send_reply(ResponseStatus.GENERAL_ERROR, None, str(resp))
            return None
        self.new_inode = resp.inode
        self.new_creation_ts = resp.mtime
        return 'acquire_dirent'

    @state_func
    def acquire_dirent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        # Perform “Inject directory entry” to attempt to link the directory
        # into the tree at the intended destination.
        # No rollback required on fail.
        assert self.new_inode is not None
        assert self.new_creation_ts is not None
        body = metadata_msgs.InjectDirentReq(
            self.parent_id,
            self.subname,
            self.new_inode,
            InodeType.DIRECTORY,
            self.new_creation_ts,
            dry=False
        )
        resp = basic_client.send_request(
            body,
            metadata_utils.shard_from_inode(self.parent_id),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.InjectDirentResp):
            assert isinstance(resp, metadata_msgs.MetadataError)
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent failed, retrying', resp)
                return 'acquire_dirent'
            c.send_reply(ResponseStatus.GENERAL_ERROR, None, str(resp))
            return None
        return 'release_dirent'

    @state_func
    def release_dirent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        # Perform “Release directory entry reference” to clean up the live
        # non-owning reference from step 2 (turning it into live owning).
        # Releases always succeed.
        #
        # On succeess, ensure the relevent token_inode is set.
        assert self.new_inode is not None
        body = metadata_msgs.ReleaseDirentReq(
            self.parent_id,
            self.subname,
            False
        )
        resp: object = None
        try:
            resp = basic_client.send_request(
                body,
                metadata_utils.shard_from_inode(self.parent_id),
                cross_dir_key.CROSS_DIR_KEY
            )
        except Exception as e:
            resp = e
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed', resp, file=sys.stderr)
            # must keep retrying until we get a success
            return 'release_dirent'
        new_dir_shard = metadata_utils.shard_from_inode(self.new_inode)
        c.persist_state.token_inodes[new_dir_shard] = self.new_inode
        c.parent_inode_cache[self.new_inode] = self.parent_id
        c.send_reply(ResponseStatus.OK, self.new_inode, '')
        return None


class MvFile(StateMachineBase):
    # 1)  Acquire Source Dirent - success => goto 2;       fail => retry;
    # 2)  Inject  Target Dirent - success => goto 3a;      fail => goto 3b;
    # 3a) Release Target Dirent - success => goto 4;       fail => retry;
    # 3b) Release Source Dirent - success => return error; fail => retry;
    # 4)  Kill Source Dirent    - success => return ok;    fail => retry;

    def __init__(self, req: MvFileReq) -> None:
        self.source_parent_inode = req.source_parent_inode
        self.target_parent_inode = req.target_parent_inode
        self.source_name = req.source_name
        self.target_name = req.target_name
        self.target_child_inode: Optional[int] = None
        self.target_type: Optional[InodeType] = None
        self.new_creation_ts = metadata_utils.now()
        self.fail_reason = ResponseStatus.GENERAL_ERROR

    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        if self.source_parent_inode == self.target_parent_inode:
            c.send_reply(ResponseStatus.BAD_REQUEST, None,
                "Don't use cross-dir coordinator for same dir renames")
            return None
        resp = basic_client.send_request(
            metadata_msgs.AcquireDirentReq(
                self.source_parent_inode,
                self.source_name,
                InodeType.FILE | InodeType.SYMLINK
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )

        if isinstance(resp, metadata_msgs.MetadataError):
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent failed, retrying', resp)
                return 'initial'
            elif resp.error_kind in (
                MetadataErrorKind.NOT_FOUND, MetadataErrorKind.BAD_INODE_TYPE):
                # either client gave us a bad request, or the inode disappeared
                # underneath us (perhaps we lost a race with a SameDirRename)
                status = (ResponseStatus.NOT_FOUND
                    if resp.error_kind == MetadataErrorKind.NOT_FOUND
                    else ResponseStatus.BAD_INODE_TYPE)
                c.send_reply(status, None, 'Failed to acquire dirent')
                return None
            else:
                # other error kinds are (currently) impossible
                assert False, f'Unexpected error {resp}'

        assert isinstance(resp, metadata_msgs.AcquireDirentResp)
        self.target_child_inode = resp.inode
        self.target_type = resp.inode_type
        return 'inject_target'

    @state_func
    def inject_target(self, c: 'CrossDirCoordinator') -> Optional[str]:
        assert self.target_child_inode is not None
        assert self.target_type is not None
        resp = basic_client.send_request(
            metadata_msgs.InjectDirentReq(
                self.target_parent_inode,
                self.target_name,
                self.target_child_inode,
                self.target_type,
                self.new_creation_ts,
                dry=False,
            ),
            metadata_utils.shard_from_inode(self.target_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )

        if isinstance(resp, metadata_msgs.MetadataError):
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent failed, retrying', resp)
                return 'inject_target'
            else:
                # failed to create the link, time to rollback
                if resp.error_kind == MetadataErrorKind.NOT_FOUND:
                    self.fail_reason = ResponseStatus.NOT_FOUND
                elif resp.error_kind == MetadataErrorKind.BAD_INODE_TYPE:
                    self.fail_reason = ResponseStatus.BAD_INODE_TYPE
                elif resp.error_kind == MetadataErrorKind.ALREADY_EXISTS:
                    self.fail_reason = ResponseStatus.CANNOT_OVERRIDE_TARGET
                return 'release_source'

        assert isinstance(resp, metadata_msgs.InjectDirentResp)
        return 'release_target'

    @state_func
    def release_target(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.target_parent_inode,
                self.target_name,
                kill=False
            ),
            metadata_utils.shard_from_inode(self.target_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'release_target'
        return 'kill_source'

    @state_func
    def kill_source(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.source_parent_inode,
                self.source_name,
                kill=True
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'kill_source'
        c.send_reply(ResponseStatus.OK, self.target_child_inode, '')
        return None

    @state_func
    def release_source(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.source_parent_inode,
                self.source_name,
                kill=False
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'release_source'
        c.send_reply(self.fail_reason, None, 'Failed to inject target')
        return None


# This is a more basic version of MvDir which doesn't support parallel renames
# the "proper version" requires a list of in-flight renames and potentially
# needs to walk down multiple branches of the tree when doing renaming checking
class MvDir(StateMachineBase):
    # 1)  Inject Target (Dry)  - success => goto 2;       fail => return error;
    # 2)  Aquire Source        - success => goto 3;       fail => return error;
    # 3)  Loop Check           - success => goto 4;       fail => goto 6b;
    # 4)  Inject Target        - success => goto 5;       fail => goto 6b;
    # 5)  Release Target       - success => goto 6a;      fail => goto 6b;
    # 6a) Kill Source          - success => goto 7;       fail => retry;
    # 6b) Release Source       - success => return error; fail => retry;
    # 7)  Set Child Parent     - success => return ok;    fail => retry;

    def __init__(self, req: MvDirReq) -> None:
        self.source_parent_inode = req.source_parent_inode
        self.target_parent_inode = req.target_parent_inode
        self.source_name = req.source_name
        self.target_name = req.target_name
        self.child_inode: Optional[int] = None
        self.new_creation_ts = metadata_utils.now()
        self.fail_reason = ResponseStatus.GENERAL_ERROR

    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        if self.source_parent_inode == self.target_parent_inode:
            c.send_reply(ResponseStatus.BAD_REQUEST, None,
                "Don't use cross-dir coordinator for same dir renames")
            return None

        # do a dry-run of Inject Target
        # this also verifies that target is indeed a directory
        resp = basic_client.send_request(
            metadata_msgs.InjectDirentReq(
                self.target_parent_inode,
                self.target_name,
                metadata_utils.NULL_INODE, # dummy value
                InodeType.DIRECTORY,
                self.new_creation_ts,
                dry=True,
            ),
            metadata_utils.shard_from_inode(self.target_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if isinstance(resp, metadata_msgs.MetadataError):
            status = ResponseStatus.GENERAL_ERROR
            if resp.error_kind == MetadataErrorKind.NOT_FOUND:
                status = ResponseStatus.NOT_FOUND
            elif resp.error_kind == MetadataErrorKind.BAD_INODE_TYPE:
                status = ResponseStatus.BAD_INODE_TYPE
            elif resp.error_kind == MetadataErrorKind.ALREADY_EXISTS:
                status = ResponseStatus.CANNOT_OVERRIDE_TARGET
            c.send_reply(status, None, f'Failed to acquire target: {resp.text}')
            return None
        assert isinstance(resp, metadata_msgs.InjectDirentResp)
        assert resp.dry
        return 'acquire_source'

    @state_func
    def acquire_source(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.AcquireDirentReq(
                self.source_parent_inode,
                self.source_name,
                InodeType.DIRECTORY,
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY,
        )

        if isinstance(resp, metadata_msgs.MetadataError):
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent timed out, retrying')
                return 'acquire_source'
            elif resp.error_kind in (
                MetadataErrorKind.NOT_FOUND, MetadataErrorKind.BAD_INODE_TYPE):
                # either client gave us a bad request, or the inode disappeared
                # underneath us (perhaps we lost a race with a SameDirRename)
                status = (ResponseStatus.NOT_FOUND
                    if resp.error_kind == MetadataErrorKind.NOT_FOUND
                    else ResponseStatus.BAD_INODE_TYPE)
                c.send_reply(status, None, 'Failed to acquire dirent')
                return None
            else:
                # other error kinds are (currently) impossible
                assert False, f'Unexpected error {resp}'

        assert isinstance(resp, metadata_msgs.AcquireDirentResp)
        self.child_inode = resp.inode
        assert resp.inode_type == InodeType.DIRECTORY
        return 'loop_check'

    @state_func
    def loop_check(self, c: 'CrossDirCoordinator') -> Optional[str]:
        assert self.child_inode is not None

        visted = {self.child_inode}
        cur = self.target_parent_inode
        while cur != metadata_utils.ROOT_INODE:
            if cur in visted:
                self.fail_reason = ResponseStatus.LOOP_DETECTED
                return 'release_source'
            maybe_parent = c.parent_inode_cache.get(cur)
            if maybe_parent is not None:
                visted.add(cur)
                cur = maybe_parent
                continue
            resp = basic_client.send_request(
                metadata_msgs.StatReq(cur),
                metadata_utils.shard_from_inode(cur)
            )
            if isinstance(resp, metadata_msgs.MetadataError):
                if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                    print(f'[WARNING] stat {cur} timed out, retrying')
                    continue
                # getting here means something has gone seriously wrong
                assert False, f'{resp}'
            assert isinstance(resp, metadata_msgs.StatResp)
            assert isinstance(resp.payload, metadata_msgs.StatDirPayload)
            c.parent_inode_cache[cur] = resp.payload.parent_inode
            visted.add(cur)
            # here is where we would need ot adjust for concurrent version
            # need to add current parent to a stack along with possible
            # future parent (if there is in-flight renames)
            cur = resp.payload.parent_inode
        # no loops detected, we can proceed
        return 'inject_target'

    @state_func
    def inject_target(self, c: 'CrossDirCoordinator') -> Optional[str]:
        assert self.child_inode is not None
        resp = basic_client.send_request(
            metadata_msgs.InjectDirentReq(
                self.target_parent_inode,
                self.target_name,
                self.child_inode,
                InodeType.DIRECTORY,
                self.new_creation_ts,
                dry=False,
            ),
            metadata_utils.shard_from_inode(self.target_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )

        if isinstance(resp, metadata_msgs.MetadataError):
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent failed, retrying', resp)
                return 'inject_target'
            else:
                # failed to create the link, time to rollback
                if resp.error_kind == MetadataErrorKind.NOT_FOUND:
                    self.fail_reason = ResponseStatus.NOT_FOUND
                elif resp.error_kind == MetadataErrorKind.BAD_INODE_TYPE:
                    self.fail_reason = ResponseStatus.BAD_INODE_TYPE
                elif resp.error_kind == MetadataErrorKind.ALREADY_EXISTS:
                    self.fail_reason = ResponseStatus.CANNOT_OVERRIDE_TARGET
                return 'release_source'

        assert isinstance(resp, metadata_msgs.InjectDirentResp)
        return 'release_target'

    @state_func
    def release_target(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.target_parent_inode,
                self.target_name,
                kill=False,
            ),
            metadata_utils.shard_from_inode(self.target_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'release_target'
        return 'kill_source'

    @state_func
    def kill_source(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.source_parent_inode,
                self.source_name,
                kill=True,
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'kill_source'
        return 'update_child_parent'

    @state_func
    def release_source(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.source_parent_inode,
                self.source_name,
                kill=False,
            ),
            metadata_utils.shard_from_inode(self.source_parent_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'release_source'
        c.send_reply(self.fail_reason, self.child_inode, 'Operation rolledback')
        return None

    @state_func
    def update_child_parent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        assert self.child_inode is not None
        resp = basic_client.send_request(
            metadata_msgs.SetParentReq(
                self.child_inode,
                self.target_parent_inode,
            ),
            metadata_utils.shard_from_inode(self.child_inode),
            cross_dir_key.CROSS_DIR_KEY
        )
        if isinstance(resp, metadata_msgs.SetParentResp):
            # success
            c.parent_inode_cache[self.child_inode] = self.target_parent_inode
            c.send_reply(ResponseStatus.OK, self.child_inode, '')
            return None
        else:
            assert isinstance(resp, metadata_msgs.MetadataError)
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise could leak the directory
                return 'update_child_parent'
            # getting here should be impossible
            assert False, f'{resp}'


class RmDir(StateMachineBase):
    # 1)  Acquire Dirent - success => goto 2;       fail => return error;
    # 2)  Unset Parent   - success => goto 3a;      fail => goto 3b;
    # 3a) Kill Dirent    - success => return ok;    fail => retry;
    # 3b) Release Dirent - success => return error; fail => retry;

    def __init__(self, req: RmDirReq) -> None:
        self.parent_id = req.parent_id
        self.subname = req.subname
        self.child_id: Optional[int] = None

    @state_func
    def initial(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ResolveReq(
                metadata_msgs.ResolveMode.ALIVE,
                self.parent_id,
                0,
                self.subname
            ),
            metadata_utils.shard_from_inode(self.parent_id)
        )
        if not isinstance(resp, metadata_msgs.ResolveResp):
            c.send_reply(ResponseStatus.GENERAL_ERROR, None, str(resp))
            return None
        if resp.f is None:
            c.send_reply(ResponseStatus.NOT_FOUND, None,
                f'No result for resolve({self.parent_id}, {self.subname})')
            return None
        if resp.f.inode_type != InodeType.DIRECTORY:
            c.send_reply(ResponseStatus.BAD_INODE_TYPE, None,
                f'{self.parent_id} has type {resp.f.inode_type}')
            return None
        if not resp.f.is_owning:
            # theoretically impossible?
            c.send_reply(ResponseStatus.PARENT_INVALID, None,
                f'{self.parent_id} is non-owning')
            return None
        self.child_id = resp.f.id
        return 'acquire_dirent'

    @state_func
    def acquire_dirent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.AcquireDirentReq(
                self.parent_id,
                self.subname,
                InodeType.DIRECTORY
            ),
            metadata_utils.shard_from_inode(self.parent_id),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.AcquireDirentResp):
            assert isinstance(resp, metadata_msgs.MetadataError), f'{resp}'
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise we may leak the dirent
                print('[WARNING] AcquireDirent failed, retrying', resp)
                return 'acquire_dirent'
            elif resp.error_kind in (
                MetadataErrorKind.NOT_FOUND, MetadataErrorKind.BAD_INODE_TYPE):
                # implies the dirent changed before we were able to acquire
                # it, perhaps we lost a race with a SameDirRename
                status = (ResponseStatus.NOT_FOUND
                    if resp.error_kind == MetadataErrorKind.NOT_FOUND
                    else ResponseStatus.BAD_INODE_TYPE)
                c.send_reply(status, None, 'Failed to acquire dirent')
                return None
            else:
                # other error kinds are (currently) impossible
                assert False, f'Unexpected error {resp}'
        return 'unset_parent'

    @state_func
    def unset_parent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        assert self.child_id is not None
        resp = basic_client.send_request(
            metadata_msgs.SetParentReq(
                self.child_id,
                metadata_utils.NULL_INODE
            ),
            metadata_utils.shard_from_inode(self.child_id),
            cross_dir_key.CROSS_DIR_KEY
        )
        if isinstance(resp, metadata_msgs.SetParentResp):
            # success
            c.parent_inode_cache.pop(self.child_id, None)
            return 'kill_dirent'
        else:
            assert isinstance(resp, metadata_msgs.MetadataError)
            if resp.error_kind == MetadataErrorKind.TIMED_OUT:
                # must retry, otherwise could leak the directory
                return 'unset_parent'
            # failure, need to rollback
            return 'release_dirent'

    @state_func
    def kill_dirent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.parent_id,
                self.subname,
                True
            ),
            metadata_utils.shard_from_inode(self.parent_id),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] KillDirent failed, retrying', resp)
            return 'kill_dirent'
        c.send_reply(ResponseStatus.OK, None, '')
        return None

    @state_func
    def release_dirent(self, c: 'CrossDirCoordinator') -> Optional[str]:
        resp = basic_client.send_request(
            metadata_msgs.ReleaseDirentReq(
                self.parent_id,
                self.subname,
                False
            ),
            metadata_utils.shard_from_inode(self.parent_id),
            cross_dir_key.CROSS_DIR_KEY
        )
        if not isinstance(resp, metadata_msgs.ReleaseDirentResp):
            print('[WARNING] ReleaseDirent failed, retrying', resp)
            return 'release_dirent'
        c.send_reply(ResponseStatus.GENERAL_ERROR, None,
            'Failed to unset parent')
        return None


class Transaction:
    def __init__(self, state_machine: StateMachineBase, request_id: int,
        return_addr: Tuple[str, int]) -> None:

        self.state_machine = state_machine
        self.next_state = ''
        self.request_id = request_id
        self.return_addr = return_addr


class PersistState:
    def __init__(self) -> None:
        self.transaction: Optional[Transaction] = None
        self.token_inodes = [metadata_utils.NULL_INODE] * 256


class CrossDirCoordinator:
    persist_state: PersistState
    def __init__(self, persist_fn: str) -> None:
        self._persist_fn = persist_fn
        maybe_persisted_state = metadata_utils.restore(self._persist_fn)
        if maybe_persisted_state is not None:
            print('Loading from persisted state')
            self.persist_state = maybe_persisted_state
        else:
            print('No persisted state found, creating fresh state')
            self.persist_state = PersistState()
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM,
            socket.IPPROTO_UDP)
        self.sock.bind(('', metadata_utils.CROSS_DIR_PORT))
        self.parent_inode_cache: Dict[int, int] = {}

    def _persist(self) -> None:
        metadata_utils.persist(self.persist_state, self._persist_fn)

    def _run_transaction(self) -> None:
        t = self.persist_state.transaction
        assert t is not None
        state_funcs = t.state_machine.state_machine_nodes
        next_state: Optional[str] = t.next_state or 'initial'

        while next_state is not None:
            # states can transition to themselves (i.e. a retry)
            # no need to persist these retry transitions
            if t.next_state != next_state:
                t.next_state = next_state
                self._persist()
            next_func = state_funcs[next_state]
            next_state = next_func(t.state_machine, self)

        self.persist_state.transaction = None
        self._persist()

    def send_reply(self, status_code: ResponseStatus, new_inode: Optional[int],
        text: str) -> None:
        t = self.persist_state.transaction
        assert t is not None
        reply = CrossDirResponse(
            t.request_id, status_code, new_inode, text)
        packed = bincode.pack(reply)
        self.sock.sendto(packed, t.return_addr)

    def run_forever(self) -> None:
        # before falling into our main loop, need to handle the last persisted
        # request (if there is one)
        if self.persist_state.transaction is not None:
            self._run_transaction()

        while True:
            data, addr = self.sock.recvfrom(metadata_utils.UDP_MTU)
            try:
                request = bincode.unpack(CrossDirRequest, data)
            except Exception as e:
                print("Couldn't unpack request:", data, e, file=sys.stderr)
                continue
            if request.ver != PROTOCOL_VERSION:
                print('Ignoring request, unsupported ver:', request.ver,
                    file=sys.stderr)
                continue
            state_machine: StateMachineBase
            if isinstance(request.body, MkDirReq):
                state_machine = MkDir(request.body)
            elif isinstance(request.body, MvFileReq):
                state_machine = MvFile(request.body)
            elif isinstance(request.body, MvDirReq):
                state_machine = MvDir(request.body)
            elif isinstance(request.body, RmDirReq):
                state_machine = RmDir(request.body)
            else:
                print('Ignoring request, unsupported msg:', request.body,
                    file=sys.stderr)
                continue
            t = Transaction(
                state_machine=state_machine,
                request_id=request.request_id,
                return_addr=addr,
            )
            self.persist_state.transaction = t
            print('Received request', request)
            self._run_transaction()


def main() -> None:
    persist_fn = os.path.expanduser('~/playground/pyfs/cross_dir.pickle')
    c = CrossDirCoordinator(persist_fn)
    c.run_forever()


if __name__ == '__main__':
    main()
