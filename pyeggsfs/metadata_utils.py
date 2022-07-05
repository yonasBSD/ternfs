
ROOT_INODE_NUMBER = 0


# assume jumbo frames are enabled
UDP_MTU = 8192


def shard_from_inode(inode_number: int) -> int:
    return inode_number & 0xFF


def shard_to_port(shard: int) -> int:
    return shard + 22272
