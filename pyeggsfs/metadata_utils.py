
NULL_INODE = 0 # used for parent_inode to indicate no parent
ROOT_INODE = 1


# assume jumbo frames are enabled
UDP_MTU = 8192


def shard_from_inode(inode_number: int) -> int:
    return inode_number & 0xFF


def shard_to_port(shard: int) -> int:
    return shard + 22272


def string_hash(s: str) -> int:
    # the built-in hash uses SIP... except for small (<= 7) strings
    # for production we should consider alternatives, e.g. murmur3 or xxhash
    return hash(s)
