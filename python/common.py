import enum
import time
from typing import Any, Optional, ClassVar, Dict, NewType
import sqlite3
from dataclasses import dataclass
from datetime import datetime
import errno
import stat

import bincode

# DON'T assume jumbo frames are enabled
# (should be an optimisation rather than correctness requirement)
UDP_MTU = 1472

# 64 bit inode id:
# ETTIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIISSSSSSSS
#
# Shard is first so that we can keep a counter with current directory
# inode on the CDC and just bump it to get the next one. This is
# the same reason the other bits are in the end.
#
# E = u1 extra bit
# T = u2  inode type
# I = u53 local inode id
# S = u8  shard id
#
# We sometimes use the extra bit to signify ownership or
# lockedness in the API.
#
# Note that this schema is also utilized in the SQLite checks. So if
# you change them, you also have to change those.

# Reserved is 0 so that the NULL_INODE can be zero without conflicts.
class InodeType(enum.IntEnum):
    RESERVED = 0
    DIRECTORY = 1
    FILE = 2
    SYMLINK = 3

def inode_id_type(inode: int) -> InodeType:
    return InodeType((inode >> 61) & 0x03)

def inode_id_shard(inode: int) -> int:
    return inode & 0xFF

# Extremely easy to confuse these two, so let's newtype
InodeIdWithExtra = NewType('InodeIdWithExtra', int)

def inode_id_set_extra(inode: int, extra: bool) -> InodeIdWithExtra:
    if extra:
        return InodeIdWithExtra(inode | (1 << 63))
    else:
        return InodeIdWithExtra(inode & ~(1 << 63))

def inode_id_extra(inode_with_extra: InodeIdWithExtra) -> bool:
    return bool(inode_with_extra >> 63)

def inode_id_strip_extra(inode_with_extra: InodeIdWithExtra) -> int:
    return inode_id_set_extra(inode_with_extra, False)

def make_inode_id(type: InodeType, shard: int, id: int) -> int:
    assert shard >= 0 and shard < 256
    assert id >= 0 and id < (1<<53)-1
    return (type << 61) | (id << 8) | shard

NULL_INODE_ID = 0 # used to indicate "no inode id" in various circumstances (dir owner, removal snapshot edges)
ROOT_DIR_INODE_ID = make_inode_id(type=InodeType.DIRECTORY, shard=0, id=0)

def shard_to_port(shard: int) -> int:
    return 22272 + shard

CDC_PORT = 36137

def num_data_blocks(parity_mode: int) -> int:
    return parity_mode & 0x0F

def num_parity_blocks(parity_mode: int) -> int:
    return parity_mode >> 4

def num_total_blocks(parity_mode: int) -> int:
    return num_data_blocks(parity_mode) + num_parity_blocks(parity_mode)

def create_parity_mode(data_blocks: int, parity_blocks: int) -> int:
    assert data_blocks >= 0 and data_blocks < 16
    assert parity_blocks >= 0 and data_blocks < 16
    return data_blocks | (parity_blocks << 4)

# EGGS EPOCH is 2020-01-01
EGGS_EPOCH = 1_577_836_800_000_000_000

def eggs_time() -> int:
    return time.time_ns() - EGGS_EPOCH

def eggs_time_str(ns: int) -> str:
    dt = datetime.fromtimestamp((EGGS_EPOCH + ns) // 1000000000)
    s = dt.strftime('%Y-%m-%d %H:%M:%S')
    s += '.' + str(int(ns%1000000000)).zfill(9)
    return s

def sql_insert_args(table: str, **kwargs):
    return (
        f'insert into {table} ({", ".join(kwargs.keys())}) values ({", ".join(map(lambda k: ":"+k, kwargs.keys()))})',
        kwargs
    )
def sql_insert(cur: sqlite3.Cursor, table: str, **kwargs):
    return cur.execute(*sql_insert_args(table, **kwargs))

