
import os
import pickle
import sys
import time
from typing import Any, NewType, Optional

NULL_INODE = 0 # used for parent_inode to indicate no parent
ROOT_INODE = 1


# DON'T assume jumbo frames are enabled
# (should be an optimisation rather than correctness requirement)
UDP_MTU = 1472


def shard_from_inode(inode_number: int) -> int:
    return inode_number & 0xFF


def shard_to_port(shard: int) -> int:
    return shard + 22272

CROSS_DIR_PORT = 36137


# persist/restore use a double buffered system
# when persisting, select the oldest of the two buffers and write there
# when restoring, try both buffers, newest first


def persist(o: object, fn: str) -> None:
    possible_fns = [fn + '.a', fn + '.b']
    def mtime_or_zero(fn: str) -> float:
        return 0.0 if not os.path.exists(fn) else os.path.getmtime(fn)
    oldest_fn = min(possible_fns, key=mtime_or_zero)
    os.makedirs(os.path.dirname(oldest_fn), exist_ok=True)
    with open(oldest_fn, 'wb') as f:
        pickle.dump(o, f)
        f.flush()
        os.fsync(f.fileno())


def restore(fn: str) -> Optional[Any]:
    possible_fns = [f for f in [fn + '.a', fn + '.b'] if os.path.exists(f)]
    if len(possible_fns) == 0:
        return None
    possible_fns.sort(key=os.path.getmtime, reverse=True)
    for candidate_fn in possible_fns:
        try:
            with open(candidate_fn, 'rb') as f:
                return pickle.load(f)
        except Exception as e:
            print(f"Failed to unpickle {candidate_fn}, reason {e}",
                file=sys.stderr)
    raise Exception('Restore failed')


def num_data_blocks(parity_mode: int) -> int:
    return parity_mode & 0x0F


def num_parity_blocks(parity_mode: int) -> int:
    return parity_mode >> 4


def total_blocks(parity_mode: int) -> int:
    return num_data_blocks(parity_mode) + num_parity_blocks(parity_mode)


def create_parity_mode(data_blocks: int, parity_blocks: int) -> int:
    if not all(0 <= x <= 0xF for x in [data_blocks, parity_blocks]):
        raise ValueError('More blocks than we support')
    return data_blocks | (parity_blocks << 4)


# EGGS EPOCH is 2020-01-01
UNIX_EGGS_OFFSET = 1_577_836_800_000_000_000

def now() -> int:
    return time.time_ns() - UNIX_EGGS_OFFSET
