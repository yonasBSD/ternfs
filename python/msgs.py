# Automatically generated with go run bincodegen.
# Run `go generate ./...` from the go/ directory to regenerate it.

import enum
from dataclasses import dataclass
import bincode
from typing import ClassVar, List, Any, Union, Tuple, Type
from common import *

class ErrCode(enum.IntEnum):
    INTERNAL_ERROR = 10
    FATAL_ERROR = 11
    TIMEOUT = 12
    MALFORMED_REQUEST = 13
    MALFORMED_RESPONSE = 14
    NOT_AUTHORISED = 15
    UNRECOGNIZED_REQUEST = 16
    FILE_NOT_FOUND = 17
    DIRECTORY_NOT_FOUND = 18
    NAME_NOT_FOUND = 19
    EDGE_NOT_FOUND = 20
    EDGE_IS_LOCKED = 21
    TYPE_IS_DIRECTORY = 22
    TYPE_IS_NOT_DIRECTORY = 23
    BAD_COOKIE = 24
    INCONSISTENT_STORAGE_CLASS_PARITY = 25
    LAST_SPAN_STATE_NOT_CLEAN = 26
    COULD_NOT_PICK_BLOCK_SERVICES = 27
    BAD_SPAN_BODY = 28
    SPAN_NOT_FOUND = 29
    BLOCK_SERVICE_NOT_FOUND = 30
    CANNOT_CERTIFY_BLOCKLESS_SPAN = 31
    BAD_NUMBER_OF_BLOCKS_PROOFS = 32
    BAD_BLOCK_PROOF = 33
    CANNOT_OVERRIDE_NAME = 34
    NAME_IS_LOCKED = 35
    MTIME_IS_TOO_RECENT = 36
    MISMATCHING_TARGET = 37
    MISMATCHING_OWNER = 38
    MISMATCHING_CREATION_TIME = 39
    DIRECTORY_NOT_EMPTY = 40
    FILE_IS_TRANSIENT = 41
    OLD_DIRECTORY_NOT_FOUND = 42
    NEW_DIRECTORY_NOT_FOUND = 43
    LOOP_IN_DIRECTORY_RENAME = 44
    DIRECTORY_HAS_OWNER = 45
    FILE_IS_NOT_TRANSIENT = 46
    FILE_NOT_EMPTY = 47
    CANNOT_REMOVE_ROOT_DIRECTORY = 48
    FILE_EMPTY = 49
    CANNOT_REMOVE_DIRTY_SPAN = 50
    BAD_SHARD = 51
    BAD_NAME = 52
    MORE_RECENT_SNAPSHOT_EDGE = 53
    MORE_RECENT_CURRENT_EDGE = 54
    BAD_DIRECTORY_INFO = 55
    DEADLINE_NOT_PASSED = 56
    SAME_SOURCE_AND_DESTINATION = 57
    SAME_DIRECTORIES = 58
    SAME_SHARD = 59

class ShardMessageKind(enum.IntEnum):
    LOOKUP = 0x1
    STAT_FILE = 0x2
    STAT_TRANSIENT_FILE = 0xA
    STAT_DIRECTORY = 0x8
    READ_DIR = 0x3
    CONSTRUCT_FILE = 0x4
    ADD_SPAN_INITIATE = 0x5
    ADD_SPAN_CERTIFY = 0x6
    LINK_FILE = 0x7
    SOFT_UNLINK_FILE = 0xC
    FILE_SPANS = 0xD
    SAME_DIRECTORY_RENAME = 0xE
    SET_DIRECTORY_INFO = 0xF
    SNAPSHOT_LOOKUP = 0x9
    EXPIRE_TRANSIENT_FILE = 0xB
    VISIT_DIRECTORIES = 0x15
    VISIT_FILES = 0x20
    VISIT_TRANSIENT_FILES = 0x16
    FULL_READ_DIR = 0x21
    REMOVE_NON_OWNED_EDGE = 0x17
    SAME_SHARD_HARD_FILE_UNLINK = 0x18
    REMOVE_SPAN_INITIATE = 0x19
    REMOVE_SPAN_CERTIFY = 0x1A
    SWAP_BLOCKS = 0x22
    BLOCK_SERVICE_FILES = 0x23
    REMOVE_INODE = 0x24
    CREATE_DIRECTORY_INODE = 0x80
    SET_DIRECTORY_OWNER = 0x81
    REMOVE_DIRECTORY_OWNER = 0x89
    CREATE_LOCKED_CURRENT_EDGE = 0x82
    LOCK_CURRENT_EDGE = 0x83
    UNLOCK_CURRENT_EDGE = 0x84
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 0x86
    MAKE_FILE_TRANSIENT = 0x87

class CDCMessageKind(enum.IntEnum):
    MAKE_DIRECTORY = 0x1
    RENAME_FILE = 0x2
    SOFT_UNLINK_DIRECTORY = 0x3
    RENAME_DIRECTORY = 0x4
    HARD_UNLINK_DIRECTORY = 0x5
    CROSS_SHARD_HARD_UNLINK_FILE = 0x6

@dataclass
class TransientFile(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 # id + cookie + deadline_time
    id: int
    cookie: bytes
    deadline_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        bincode.pack_u64_into(self.deadline_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'TransientFile':
        id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        deadline_time = bincode.unpack_u64(u)
        return TransientFile(id, cookie, deadline_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # cookie
        _size += 8 # deadline_time
        return _size

@dataclass
class FetchedBlock(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 8 + 4 # block_service_ix + block_id + crc32
    block_service_ix: int
    block_id: int
    crc32: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.block_service_ix, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedBlock':
        block_service_ix = bincode.unpack_u8(u)
        block_id = bincode.unpack_u64(u)
        crc32 = bincode.unpack_fixed(u, 4)
        return FetchedBlock(block_service_ix, block_id, crc32)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # block_service_ix
        _size += 8 # block_id
        _size += 4 # crc32
        return _size

@dataclass
class CurrentEdge(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # target_id + name_hash + len(name) + creation_time
    target_id: int
    name_hash: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.name_hash, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CurrentEdge':
        target_id = bincode.unpack_u64(u)
        name_hash = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return CurrentEdge(target_id, name_hash, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # name_hash
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class Edge(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 8 + 8 + 1 + 8 # current + target_id + name_hash + len(name) + creation_time
    current: bool
    target_id: InodeIdWithExtra
    name_hash: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.current, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.name_hash, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'Edge':
        current = bool(bincode.unpack_u8(u))
        target_id = InodeIdWithExtra(bincode.unpack_u64(u))
        name_hash = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return Edge(current, target_id, name_hash, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # current
        _size += 8 # target_id
        _size += 8 # name_hash
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class FetchedSpan(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 1 + 4 + 1 + 2 # parity + storage_class + crc32 + len(body_bytes) + len(body_blocks)
    byte_offset: int
    parity: int
    storage_class: int
    crc32: bytes
    size: int
    block_size: int
    body_bytes: bytes
    body_blocks: List[FetchedBlock]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_v61_into(self.block_size, b)
        bincode.pack_bytes_into(self.body_bytes, b)
        bincode.pack_u16_into(len(self.body_blocks), b)
        for i in range(len(self.body_blocks)):
            self.body_blocks[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedSpan':
        byte_offset = bincode.unpack_v61(u)
        parity = bincode.unpack_u8(u)
        storage_class = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        block_size = bincode.unpack_v61(u)
        body_bytes = bincode.unpack_bytes(u)
        body_blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(body_blocks)):
            body_blocks[i] = FetchedBlock.unpack(u)
        return FetchedSpan(byte_offset, parity, storage_class, crc32, size, block_size, body_bytes, body_blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 1 # parity
        _size += 1 # storage_class
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
        _size += bincode.v61_packed_size(self.block_size) # block_size
        _size += 1 # len(body_bytes)
        _size += len(self.body_bytes) # body_bytes contents
        _size += 2 # len(body_blocks)
        for i in range(len(self.body_blocks)):
            _size += self.body_blocks[i].calc_packed_size() # body_blocks[i]
        return _size

@dataclass
class BlockInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 + 8 + 8 + 8 # block_service_ip + block_service_port + block_service_id + block_id + certificate
    block_service_ip: bytes
    block_service_port: int
    block_service_id: int
    block_id: int
    certificate: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.block_service_ip, 4, b)
        bincode.pack_u16_into(self.block_service_port, b)
        bincode.pack_u64_into(self.block_service_id, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.certificate, 8, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockInfo':
        block_service_ip = bincode.unpack_fixed(u, 4)
        block_service_port = bincode.unpack_u16(u)
        block_service_id = bincode.unpack_u64(u)
        block_id = bincode.unpack_u64(u)
        certificate = bincode.unpack_fixed(u, 8)
        return BlockInfo(block_service_ip, block_service_port, block_service_id, block_id, certificate)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # block_service_ip
        _size += 2 # block_service_port
        _size += 8 # block_service_id
        _size += 8 # block_id
        _size += 8 # certificate
        return _size

@dataclass
class NewBlockInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 # crc32
    crc32: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.crc32, 4, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'NewBlockInfo':
        crc32 = bincode.unpack_fixed(u, 4)
        return NewBlockInfo(crc32)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # crc32
        return _size

@dataclass
class BlockProof(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 # block_id + proof
    block_id: int
    proof: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.proof, 8, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockProof':
        block_id = bincode.unpack_u64(u)
        proof = bincode.unpack_fixed(u, 8)
        return BlockProof(block_id, proof)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # block_id
        _size += 8 # proof
        return _size

@dataclass
class SpanPolicy(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 1 # max_size + storage_class + parity
    max_size: int
    storage_class: int
    parity: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.max_size, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SpanPolicy':
        max_size = bincode.unpack_u64(u)
        storage_class = bincode.unpack_u8(u)
        parity = bincode.unpack_u8(u)
        return SpanPolicy(max_size, storage_class, parity)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # max_size
        _size += 1 # storage_class
        _size += 1 # parity
        return _size

@dataclass
class DirectoryInfoBody(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 8 + 2 + 2 # version + delete_after_time + delete_after_versions + len(span_policies)
    version: int
    delete_after_time: int
    delete_after_versions: int
    span_policies: List[SpanPolicy]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.version, b)
        bincode.pack_u64_into(self.delete_after_time, b)
        bincode.pack_u16_into(self.delete_after_versions, b)
        bincode.pack_u16_into(len(self.span_policies), b)
        for i in range(len(self.span_policies)):
            self.span_policies[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'DirectoryInfoBody':
        version = bincode.unpack_u8(u)
        delete_after_time = bincode.unpack_u64(u)
        delete_after_versions = bincode.unpack_u16(u)
        span_policies: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(span_policies)):
            span_policies[i] = SpanPolicy.unpack(u)
        return DirectoryInfoBody(version, delete_after_time, delete_after_versions, span_policies)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # version
        _size += 8 # delete_after_time
        _size += 2 # delete_after_versions
        _size += 2 # len(span_policies)
        for i in range(len(self.span_policies)):
            _size += self.span_policies[i].calc_packed_size() # span_policies[i]
        return _size

@dataclass
class SetDirectoryInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 1 # inherited + len(body)
    inherited: bool
    body: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.inherited, b)
        bincode.pack_bytes_into(self.body, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryInfo':
        inherited = bool(bincode.unpack_u8(u))
        body = bincode.unpack_bytes(u)
        return SetDirectoryInfo(inherited, body)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # inherited
        _size += 1 # len(body)
        _size += len(self.body) # body contents
        return _size

@dataclass
class BlockServiceBlacklist(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 + 8 # ip + port + id
    ip: bytes
    port: int
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockServiceBlacklist':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        id = bincode.unpack_u64(u)
        return BlockServiceBlacklist(ip, port, id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # ip
        _size += 2 # port
        _size += 8 # id
        return _size

@dataclass
class BlockService(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 + 8 + 1 # ip + port + id + flags
    ip: bytes
    port: int
    id: int
    flags: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u8_into(self.flags, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockService':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        id = bincode.unpack_u64(u)
        flags = bincode.unpack_u8(u)
        return BlockService(ip, port, id, flags)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # ip
        _size += 2 # port
        _size += 8 # id
        _size += 1 # flags
        return _size

@dataclass
class FullReadDirCursor(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 1 + 8 + 1 + 8 # current + start_hash + len(start_name) + start_time
    current: bool
    start_hash: int
    start_name: bytes
    start_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.current, b)
        bincode.pack_u64_into(self.start_hash, b)
        bincode.pack_bytes_into(self.start_name, b)
        bincode.pack_u64_into(self.start_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FullReadDirCursor':
        current = bool(bincode.unpack_u8(u))
        start_hash = bincode.unpack_u64(u)
        start_name = bincode.unpack_bytes(u)
        start_time = bincode.unpack_u64(u)
        return FullReadDirCursor(current, start_hash, start_name, start_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # current
        _size += 8 # start_hash
        _size += 1 # len(start_name)
        _size += len(self.start_name) # start_name contents
        _size += 8 # start_time
        return _size

@dataclass
class EntryNewBlockInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 4 # block_service_id + crc32
    block_service_id: int
    crc32: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.block_service_id, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'EntryNewBlockInfo':
        block_service_id = bincode.unpack_u64(u)
        crc32 = bincode.unpack_fixed(u, 4)
        return EntryNewBlockInfo(block_service_id, crc32)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # block_service_id
        _size += 4 # crc32
        return _size

@dataclass
class SnapshotLookupEdge(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 # target_id + creation_time
    target_id: InodeIdWithExtra
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SnapshotLookupEdge':
        target_id = InodeIdWithExtra(bincode.unpack_u64(u))
        creation_time = bincode.unpack_u64(u)
        return SnapshotLookupEdge(target_id, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # creation_time
        return _size

@dataclass
class BlockServiceInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 4 + 2 + 1 + 16 + 16 + 8 + 8 + 8 + 1 # id + ip + port + storage_class + failure_domain + secret_key + capacity_bytes + available_bytes + blocks + len(path)
    id: int
    ip: bytes
    port: int
    storage_class: int
    failure_domain: bytes
    secret_key: bytes
    capacity_bytes: int
    available_bytes: int
    blocks: int
    path: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.failure_domain, 16, b)
        bincode.pack_fixed_into(self.secret_key, 16, b)
        bincode.pack_u64_into(self.capacity_bytes, b)
        bincode.pack_u64_into(self.available_bytes, b)
        bincode.pack_u64_into(self.blocks, b)
        bincode.pack_bytes_into(self.path, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockServiceInfo':
        id = bincode.unpack_u64(u)
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        storage_class = bincode.unpack_u8(u)
        failure_domain = bincode.unpack_fixed(u, 16)
        secret_key = bincode.unpack_fixed(u, 16)
        capacity_bytes = bincode.unpack_u64(u)
        available_bytes = bincode.unpack_u64(u)
        blocks = bincode.unpack_u64(u)
        path = bincode.unpack_bytes(u)
        return BlockServiceInfo(id, ip, port, storage_class, failure_domain, secret_key, capacity_bytes, available_bytes, blocks, path)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 4 # ip
        _size += 2 # port
        _size += 1 # storage_class
        _size += 16 # failure_domain
        _size += 16 # secret_key
        _size += 8 # capacity_bytes
        _size += 8 # available_bytes
        _size += 8 # blocks
        _size += 1 # len(path)
        _size += len(self.path) # path contents
        return _size

@dataclass
class ShardInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 # ip + port
    ip: bytes
    port: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ShardInfo':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        return ShardInfo(ip, port)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # ip
        _size += 2 # port
        return _size

@dataclass
class LookupReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LOOKUP
    STATIC_SIZE: ClassVar[int] = 8 + 1 # dir_id + len(name)
    dir_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LookupReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return LookupReq(dir_id, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        return _size

@dataclass
class LookupResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LOOKUP
    STATIC_SIZE: ClassVar[int] = 8 + 8 # target_id + creation_time
    target_id: int
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LookupResp':
        target_id = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        return LookupResp(target_id, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # creation_time
        return _size

@dataclass
class StatFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_FILE
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatFileReq':
        id = bincode.unpack_u64(u)
        return StatFileReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class StatFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 # mtime + size
    mtime: int
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatFileResp':
        mtime = bincode.unpack_u64(u)
        size = bincode.unpack_u64(u)
        return StatFileResp(mtime, size)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # mtime
        _size += 8 # size
        return _size

@dataclass
class StatTransientFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_TRANSIENT_FILE
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatTransientFileReq':
        id = bincode.unpack_u64(u)
        return StatTransientFileReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class StatTransientFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_TRANSIENT_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # mtime + size + len(note)
    mtime: int
    size: int
    note: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size, b)
        bincode.pack_bytes_into(self.note, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatTransientFileResp':
        mtime = bincode.unpack_u64(u)
        size = bincode.unpack_u64(u)
        note = bincode.unpack_bytes(u)
        return StatTransientFileResp(mtime, size, note)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # mtime
        _size += 8 # size
        _size += 1 # len(note)
        _size += len(self.note) # note contents
        return _size

@dataclass
class StatDirectoryReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatDirectoryReq':
        id = bincode.unpack_u64(u)
        return StatDirectoryReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class StatDirectoryResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # mtime + owner + len(info)
    mtime: int
    owner: int
    info: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.owner, b)
        bincode.pack_bytes_into(self.info, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatDirectoryResp':
        mtime = bincode.unpack_u64(u)
        owner = bincode.unpack_u64(u)
        info = bincode.unpack_bytes(u)
        return StatDirectoryResp(mtime, owner, info)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # mtime
        _size += 8 # owner
        _size += 1 # len(info)
        _size += len(self.info) # info contents
        return _size

@dataclass
class ReadDirReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.READ_DIR
    STATIC_SIZE: ClassVar[int] = 8 + 8 # dir_id + start_hash
    dir_id: int
    start_hash: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.start_hash, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirReq':
        dir_id = bincode.unpack_u64(u)
        start_hash = bincode.unpack_u64(u)
        return ReadDirReq(dir_id, start_hash)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # start_hash
        return _size

@dataclass
class ReadDirResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.READ_DIR
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_hash + len(results)
    next_hash: int
    results: List[CurrentEdge]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_hash, b)
        bincode.pack_u16_into(len(self.results), b)
        for i in range(len(self.results)):
            self.results[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirResp':
        next_hash = bincode.unpack_u64(u)
        results: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(results)):
            results[i] = CurrentEdge.unpack(u)
        return ReadDirResp(next_hash, results)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # next_hash
        _size += 2 # len(results)
        for i in range(len(self.results)):
            _size += self.results[i].calc_packed_size() # results[i]
        return _size

@dataclass
class ConstructFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CONSTRUCT_FILE
    STATIC_SIZE: ClassVar[int] = 1 + 1 # type + len(note)
    type: InodeType
    note: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.type, b)
        bincode.pack_bytes_into(self.note, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ConstructFileReq':
        type = InodeType(bincode.unpack_u8(u))
        note = bincode.unpack_bytes(u)
        return ConstructFileReq(type, note)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # type
        _size += 1 # len(note)
        _size += len(self.note) # note contents
        return _size

@dataclass
class ConstructFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CONSTRUCT_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 # id + cookie
    id: int
    cookie: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ConstructFileResp':
        id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        return ConstructFileResp(id, cookie)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # cookie
        return _size

@dataclass
class AddSpanInitiateReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.ADD_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 2 + 1 + 4 + 1 + 2 # file_id + cookie + storage_class + len(blacklist) + parity + crc32 + len(body_bytes) + len(body_blocks)
    file_id: int
    cookie: bytes
    byte_offset: int
    storage_class: int
    blacklist: List[BlockServiceBlacklist]
    parity: int
    crc32: bytes
    size: int
    block_size: int
    body_bytes: bytes
    body_blocks: List[NewBlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u16_into(len(self.blacklist), b)
        for i in range(len(self.blacklist)):
            self.blacklist[i].pack_into(b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_v61_into(self.block_size, b)
        bincode.pack_bytes_into(self.body_bytes, b)
        bincode.pack_u16_into(len(self.body_blocks), b)
        for i in range(len(self.body_blocks)):
            self.body_blocks[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanInitiateReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        byte_offset = bincode.unpack_v61(u)
        storage_class = bincode.unpack_u8(u)
        blacklist: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(blacklist)):
            blacklist[i] = BlockServiceBlacklist.unpack(u)
        parity = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        block_size = bincode.unpack_v61(u)
        body_bytes = bincode.unpack_bytes(u)
        body_blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(body_blocks)):
            body_blocks[i] = NewBlockInfo.unpack(u)
        return AddSpanInitiateReq(file_id, cookie, byte_offset, storage_class, blacklist, parity, crc32, size, block_size, body_bytes, body_blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 1 # storage_class
        _size += 2 # len(blacklist)
        for i in range(len(self.blacklist)):
            _size += self.blacklist[i].calc_packed_size() # blacklist[i]
        _size += 1 # parity
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
        _size += bincode.v61_packed_size(self.block_size) # block_size
        _size += 1 # len(body_bytes)
        _size += len(self.body_bytes) # body_bytes contents
        _size += 2 # len(body_blocks)
        for i in range(len(self.body_blocks)):
            _size += self.body_blocks[i].calc_packed_size() # body_blocks[i]
        return _size

@dataclass
class AddSpanInitiateResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.ADD_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 2 # len(blocks)
    blocks: List[BlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u16_into(len(self.blocks), b)
        for i in range(len(self.blocks)):
            self.blocks[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanInitiateResp':
        blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(blocks)):
            blocks[i] = BlockInfo.unpack(u)
        return AddSpanInitiateResp(blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 2 # len(blocks)
        for i in range(len(self.blocks)):
            _size += self.blocks[i].calc_packed_size() # blocks[i]
        return _size

@dataclass
class AddSpanCertifyReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.ADD_SPAN_CERTIFY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 2 # file_id + cookie + len(proofs)
    file_id: int
    cookie: bytes
    byte_offset: int
    proofs: List[BlockProof]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for i in range(len(self.proofs)):
            self.proofs[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanCertifyReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        byte_offset = bincode.unpack_v61(u)
        proofs: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(proofs)):
            proofs[i] = BlockProof.unpack(u)
        return AddSpanCertifyReq(file_id, cookie, byte_offset, proofs)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 2 # len(proofs)
        for i in range(len(self.proofs)):
            _size += self.proofs[i].calc_packed_size() # proofs[i]
        return _size

@dataclass
class AddSpanCertifyResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.ADD_SPAN_CERTIFY
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanCertifyResp':
        return AddSpanCertifyResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class LinkFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LINK_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 + 1 # file_id + cookie + owner_id + len(name)
    file_id: int
    cookie: bytes
    owner_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        owner_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return LinkFileReq(file_id, cookie, owner_id, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        _size += 8 # owner_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        return _size

@dataclass
class LinkFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LINK_FILE
    STATIC_SIZE: ClassVar[int] = 8 # creation_time
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileResp':
        creation_time = bincode.unpack_u64(u)
        return LinkFileResp(creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # creation_time
        return _size

@dataclass
class SoftUnlinkFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SOFT_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # owner_id + file_id + len(name) + creation_time
    owner_id: int
    file_id: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkFileReq':
        owner_id = bincode.unpack_u64(u)
        file_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return SoftUnlinkFileReq(owner_id, file_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # file_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class SoftUnlinkFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SOFT_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkFileResp':
        return SoftUnlinkFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class FileSpansReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.FILE_SPANS
    STATIC_SIZE: ClassVar[int] = 8 # file_id
    file_id: int
    byte_offset: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_v61_into(self.byte_offset, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FileSpansReq':
        file_id = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        return FileSpansReq(file_id, byte_offset)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        return _size

@dataclass
class FileSpansResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.FILE_SPANS
    STATIC_SIZE: ClassVar[int] = 2 + 2 # len(block_services) + len(spans)
    next_offset: int
    block_services: List[BlockService]
    spans: List[FetchedSpan]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.next_offset, b)
        bincode.pack_u16_into(len(self.block_services), b)
        for i in range(len(self.block_services)):
            self.block_services[i].pack_into(b)
        bincode.pack_u16_into(len(self.spans), b)
        for i in range(len(self.spans)):
            self.spans[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FileSpansResp':
        next_offset = bincode.unpack_v61(u)
        block_services: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(block_services)):
            block_services[i] = BlockService.unpack(u)
        spans: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(spans)):
            spans[i] = FetchedSpan.unpack(u)
        return FileSpansResp(next_offset, block_services, spans)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += bincode.v61_packed_size(self.next_offset) # next_offset
        _size += 2 # len(block_services)
        for i in range(len(self.block_services)):
            _size += self.block_services[i].calc_packed_size() # block_services[i]
        _size += 2 # len(spans)
        for i in range(len(self.spans)):
            _size += self.spans[i].calc_packed_size() # spans[i]
        return _size

@dataclass
class SameDirectoryRenameReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_DIRECTORY_RENAME
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 + 1 # target_id + dir_id + len(old_name) + old_creation_time + len(new_name)
    target_id: int
    dir_id: int
    old_name: bytes
    old_creation_time: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.old_creation_time, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameReq':
        target_id = bincode.unpack_u64(u)
        dir_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        old_creation_time = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return SameDirectoryRenameReq(target_id, dir_id, old_name, old_creation_time, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # dir_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 8 # old_creation_time
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class SameDirectoryRenameResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_DIRECTORY_RENAME
    STATIC_SIZE: ClassVar[int] = 8 # new_creation_time
    new_creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.new_creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameResp':
        new_creation_time = bincode.unpack_u64(u)
        return SameDirectoryRenameResp(new_creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # new_creation_time
        return _size

@dataclass
class SetDirectoryInfoReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SET_DIRECTORY_INFO
    STATIC_SIZE: ClassVar[int] = 8 + SetDirectoryInfo.STATIC_SIZE # id + info
    id: int
    info: SetDirectoryInfo

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        self.info.pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryInfoReq':
        id = bincode.unpack_u64(u)
        info = SetDirectoryInfo.unpack(u)
        return SetDirectoryInfoReq(id, info)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += self.info.calc_packed_size() # info
        return _size

@dataclass
class SetDirectoryInfoResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SET_DIRECTORY_INFO
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryInfoResp':
        return SetDirectoryInfoResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class SnapshotLookupReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SNAPSHOT_LOOKUP
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 8 # dir_id + len(name) + start_from
    dir_id: int
    name: bytes
    start_from: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.start_from, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SnapshotLookupReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        start_from = bincode.unpack_u64(u)
        return SnapshotLookupReq(dir_id, name, start_from)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # start_from
        return _size

@dataclass
class SnapshotLookupResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SNAPSHOT_LOOKUP
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_time + len(edges)
    next_time: int
    edges: List[SnapshotLookupEdge]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_time, b)
        bincode.pack_u16_into(len(self.edges), b)
        for i in range(len(self.edges)):
            self.edges[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SnapshotLookupResp':
        next_time = bincode.unpack_u64(u)
        edges: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(edges)):
            edges[i] = SnapshotLookupEdge.unpack(u)
        return SnapshotLookupResp(next_time, edges)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # next_time
        _size += 2 # len(edges)
        for i in range(len(self.edges)):
            _size += self.edges[i].calc_packed_size() # edges[i]
        return _size

@dataclass
class ExpireTransientFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.EXPIRE_TRANSIENT_FILE
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpireTransientFileReq':
        id = bincode.unpack_u64(u)
        return ExpireTransientFileReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class ExpireTransientFileResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.EXPIRE_TRANSIENT_FILE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ExpireTransientFileResp':
        return ExpireTransientFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class VisitDirectoriesReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_DIRECTORIES
    STATIC_SIZE: ClassVar[int] = 8 # begin_id
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitDirectoriesReq':
        begin_id = bincode.unpack_u64(u)
        return VisitDirectoriesReq(begin_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # begin_id
        return _size

@dataclass
class VisitDirectoriesResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_DIRECTORIES
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_id + len(ids)
    next_id: int
    ids: List[int]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.ids), b)
        for i in range(len(self.ids)):
            bincode.pack_u64_into(self.ids[i], b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitDirectoriesResp':
        next_id = bincode.unpack_u64(u)
        ids: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(ids)):
            ids[i] = bincode.unpack_u64(u)
        return VisitDirectoriesResp(next_id, ids)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # next_id
        _size += 2 # len(ids)
        for i in range(len(self.ids)):
            _size += 8 # ids[i]
        return _size

@dataclass
class VisitFilesReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_FILES
    STATIC_SIZE: ClassVar[int] = 8 # begin_id
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitFilesReq':
        begin_id = bincode.unpack_u64(u)
        return VisitFilesReq(begin_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # begin_id
        return _size

@dataclass
class VisitFilesResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_FILES
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_id + len(ids)
    next_id: int
    ids: List[int]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.ids), b)
        for i in range(len(self.ids)):
            bincode.pack_u64_into(self.ids[i], b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitFilesResp':
        next_id = bincode.unpack_u64(u)
        ids: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(ids)):
            ids[i] = bincode.unpack_u64(u)
        return VisitFilesResp(next_id, ids)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # next_id
        _size += 2 # len(ids)
        for i in range(len(self.ids)):
            _size += 8 # ids[i]
        return _size

@dataclass
class VisitTransientFilesReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_TRANSIENT_FILES
    STATIC_SIZE: ClassVar[int] = 8 # begin_id
    begin_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.begin_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitTransientFilesReq':
        begin_id = bincode.unpack_u64(u)
        return VisitTransientFilesReq(begin_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # begin_id
        return _size

@dataclass
class VisitTransientFilesResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.VISIT_TRANSIENT_FILES
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_id + len(files)
    next_id: int
    files: List[TransientFile]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.next_id, b)
        bincode.pack_u16_into(len(self.files), b)
        for i in range(len(self.files)):
            self.files[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'VisitTransientFilesResp':
        next_id = bincode.unpack_u64(u)
        files: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(files)):
            files[i] = TransientFile.unpack(u)
        return VisitTransientFilesResp(next_id, files)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # next_id
        _size += 2 # len(files)
        for i in range(len(self.files)):
            _size += self.files[i].calc_packed_size() # files[i]
        return _size

@dataclass
class FullReadDirReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.FULL_READ_DIR
    STATIC_SIZE: ClassVar[int] = 8 + FullReadDirCursor.STATIC_SIZE # dir_id + cursor
    dir_id: int
    cursor: FullReadDirCursor

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        self.cursor.pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FullReadDirReq':
        dir_id = bincode.unpack_u64(u)
        cursor = FullReadDirCursor.unpack(u)
        return FullReadDirReq(dir_id, cursor)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += self.cursor.calc_packed_size() # cursor
        return _size

@dataclass
class FullReadDirResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.FULL_READ_DIR
    STATIC_SIZE: ClassVar[int] = FullReadDirCursor.STATIC_SIZE + 2 # next + len(results)
    next: FullReadDirCursor
    results: List[Edge]

    def pack_into(self, b: bytearray) -> None:
        self.next.pack_into(b)
        bincode.pack_u16_into(len(self.results), b)
        for i in range(len(self.results)):
            self.results[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FullReadDirResp':
        next = FullReadDirCursor.unpack(u)
        results: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(results)):
            results[i] = Edge.unpack(u)
        return FullReadDirResp(next, results)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += self.next.calc_packed_size() # next
        _size += 2 # len(results)
        for i in range(len(self.results)):
            _size += self.results[i].calc_packed_size() # results[i]
        return _size

@dataclass
class RemoveNonOwnedEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_NON_OWNED_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # dir_id + target_id + len(name) + creation_time
    dir_id: int
    target_id: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveNonOwnedEdgeReq':
        dir_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return RemoveNonOwnedEdgeReq(dir_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class RemoveNonOwnedEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_NON_OWNED_EDGE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveNonOwnedEdgeResp':
        return RemoveNonOwnedEdgeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class SameShardHardFileUnlinkReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_SHARD_HARD_FILE_UNLINK
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # owner_id + target_id + len(name) + creation_time
    owner_id: int
    target_id: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameShardHardFileUnlinkReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return SameShardHardFileUnlinkReq(owner_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class SameShardHardFileUnlinkResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_SHARD_HARD_FILE_UNLINK
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameShardHardFileUnlinkResp':
        return SameShardHardFileUnlinkResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class RemoveSpanInitiateReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 8 + 8 # file_id + cookie
    file_id: int
    cookie: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanInitiateReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        return RemoveSpanInitiateReq(file_id, cookie)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        return _size

@dataclass
class RemoveSpanInitiateResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 2 # len(blocks)
    byte_offset: int
    blocks: List[BlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.blocks), b)
        for i in range(len(self.blocks)):
            self.blocks[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanInitiateResp':
        byte_offset = bincode.unpack_v61(u)
        blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(blocks)):
            blocks[i] = BlockInfo.unpack(u)
        return RemoveSpanInitiateResp(byte_offset, blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 2 # len(blocks)
        for i in range(len(self.blocks)):
            _size += self.blocks[i].calc_packed_size() # blocks[i]
        return _size

@dataclass
class RemoveSpanCertifyReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_SPAN_CERTIFY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 2 # file_id + cookie + len(proofs)
    file_id: int
    cookie: bytes
    byte_offset: int
    proofs: List[BlockProof]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_fixed_into(self.cookie, 8, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for i in range(len(self.proofs)):
            self.proofs[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanCertifyReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_fixed(u, 8)
        byte_offset = bincode.unpack_v61(u)
        proofs: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(proofs)):
            proofs[i] = BlockProof.unpack(u)
        return RemoveSpanCertifyReq(file_id, cookie, byte_offset, proofs)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 2 # len(proofs)
        for i in range(len(self.proofs)):
            _size += self.proofs[i].calc_packed_size() # proofs[i]
        return _size

@dataclass
class RemoveSpanCertifyResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_SPAN_CERTIFY
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanCertifyResp':
        return RemoveSpanCertifyResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class SwapBlocksReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SWAP_BLOCKS
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 + 8 + 8 + 8 # file_id1 + byte_offset1 + block_id1 + file_id2 + byte_offset2 + block_id2
    file_id1: int
    byte_offset1: int
    block_id1: int
    file_id2: int
    byte_offset2: int
    block_id2: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id1, b)
        bincode.pack_u64_into(self.byte_offset1, b)
        bincode.pack_u64_into(self.block_id1, b)
        bincode.pack_u64_into(self.file_id2, b)
        bincode.pack_u64_into(self.byte_offset2, b)
        bincode.pack_u64_into(self.block_id2, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SwapBlocksReq':
        file_id1 = bincode.unpack_u64(u)
        byte_offset1 = bincode.unpack_u64(u)
        block_id1 = bincode.unpack_u64(u)
        file_id2 = bincode.unpack_u64(u)
        byte_offset2 = bincode.unpack_u64(u)
        block_id2 = bincode.unpack_u64(u)
        return SwapBlocksReq(file_id1, byte_offset1, block_id1, file_id2, byte_offset2, block_id2)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id1
        _size += 8 # byte_offset1
        _size += 8 # block_id1
        _size += 8 # file_id2
        _size += 8 # byte_offset2
        _size += 8 # block_id2
        return _size

@dataclass
class SwapBlocksResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SWAP_BLOCKS
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SwapBlocksResp':
        return SwapBlocksResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class BlockServiceFilesReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.BLOCK_SERVICE_FILES
    STATIC_SIZE: ClassVar[int] = 8 + 8 # block_service_id + start_from
    block_service_id: int
    start_from: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.block_service_id, b)
        bincode.pack_u64_into(self.start_from, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockServiceFilesReq':
        block_service_id = bincode.unpack_u64(u)
        start_from = bincode.unpack_u64(u)
        return BlockServiceFilesReq(block_service_id, start_from)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # block_service_id
        _size += 8 # start_from
        return _size

@dataclass
class BlockServiceFilesResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.BLOCK_SERVICE_FILES
    STATIC_SIZE: ClassVar[int] = 2 # len(file_ids)
    file_ids: List[int]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u16_into(len(self.file_ids), b)
        for i in range(len(self.file_ids)):
            bincode.pack_u64_into(self.file_ids[i], b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockServiceFilesResp':
        file_ids: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(file_ids)):
            file_ids[i] = bincode.unpack_u64(u)
        return BlockServiceFilesResp(file_ids)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 2 # len(file_ids)
        for i in range(len(self.file_ids)):
            _size += 8 # file_ids[i]
        return _size

@dataclass
class RemoveInodeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_INODE
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveInodeReq':
        id = bincode.unpack_u64(u)
        return RemoveInodeReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class RemoveInodeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_INODE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveInodeResp':
        return RemoveInodeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class CreateDirectoryInodeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_DIRECTORY_INODE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + SetDirectoryInfo.STATIC_SIZE # id + owner_id + info
    id: int
    owner_id: int
    info: SetDirectoryInfo

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.owner_id, b)
        self.info.pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryInodeReq':
        id = bincode.unpack_u64(u)
        owner_id = bincode.unpack_u64(u)
        info = SetDirectoryInfo.unpack(u)
        return CreateDirectoryInodeReq(id, owner_id, info)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # owner_id
        _size += self.info.calc_packed_size() # info
        return _size

@dataclass
class CreateDirectoryInodeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_DIRECTORY_INODE
    STATIC_SIZE: ClassVar[int] = 8 # mtime
    mtime: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryInodeResp':
        mtime = bincode.unpack_u64(u)
        return CreateDirectoryInodeResp(mtime)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # mtime
        return _size

@dataclass
class SetDirectoryOwnerReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SET_DIRECTORY_OWNER
    STATIC_SIZE: ClassVar[int] = 8 + 8 # dir_id + owner_id
    dir_id: int
    owner_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.owner_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryOwnerReq':
        dir_id = bincode.unpack_u64(u)
        owner_id = bincode.unpack_u64(u)
        return SetDirectoryOwnerReq(dir_id, owner_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # owner_id
        return _size

@dataclass
class SetDirectoryOwnerResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SET_DIRECTORY_OWNER
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SetDirectoryOwnerResp':
        return SetDirectoryOwnerResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class RemoveDirectoryOwnerReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_DIRECTORY_OWNER
    STATIC_SIZE: ClassVar[int] = 8 + 1 # dir_id + len(info)
    dir_id: int
    info: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.info, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveDirectoryOwnerReq':
        dir_id = bincode.unpack_u64(u)
        info = bincode.unpack_bytes(u)
        return RemoveDirectoryOwnerReq(dir_id, info)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(info)
        _size += len(self.info) # info contents
        return _size

@dataclass
class RemoveDirectoryOwnerResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_DIRECTORY_OWNER
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveDirectoryOwnerResp':
        return RemoveDirectoryOwnerResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class CreateLockedCurrentEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 8 # dir_id + len(name) + target_id
    dir_id: int
    name: bytes
    target_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        return CreateLockedCurrentEdgeReq(dir_id, name, target_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # target_id
        return _size

@dataclass
class CreateLockedCurrentEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 8 # creation_time
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeResp':
        creation_time = bincode.unpack_u64(u)
        return CreateLockedCurrentEdgeResp(creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # creation_time
        return _size

@dataclass
class LockCurrentEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LOCK_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 + 1 # dir_id + target_id + creation_time + len(name)
    dir_id: int
    target_id: int
    creation_time: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return LockCurrentEdgeReq(dir_id, target_id, creation_time, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # target_id
        _size += 8 # creation_time
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        return _size

@dataclass
class LockCurrentEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LOCK_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LockCurrentEdgeResp':
        return LockCurrentEdgeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class UnlockCurrentEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.UNLOCK_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 8 + 8 + 1 # dir_id + len(name) + creation_time + target_id + was_moved
    dir_id: int
    name: bytes
    creation_time: int
    target_id: int
    was_moved: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u8_into(self.was_moved, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        was_moved = bool(bincode.unpack_u8(u))
        return UnlockCurrentEdgeReq(dir_id, name, creation_time, target_id, was_moved)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        _size += 8 # target_id
        _size += 1 # was_moved
        return _size

@dataclass
class UnlockCurrentEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.UNLOCK_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlockCurrentEdgeResp':
        return UnlockCurrentEdgeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class RemoveOwnedSnapshotFileEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_OWNED_SNAPSHOT_FILE_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # owner_id + target_id + len(name) + creation_time
    owner_id: int
    target_id: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveOwnedSnapshotFileEdgeReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return RemoveOwnedSnapshotFileEdgeReq(owner_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class RemoveOwnedSnapshotFileEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_OWNED_SNAPSHOT_FILE_EDGE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveOwnedSnapshotFileEdgeResp':
        return RemoveOwnedSnapshotFileEdgeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class MakeFileTransientReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.MAKE_FILE_TRANSIENT
    STATIC_SIZE: ClassVar[int] = 8 + 1 # id + len(note)
    id: int
    note: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_bytes_into(self.note, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeFileTransientReq':
        id = bincode.unpack_u64(u)
        note = bincode.unpack_bytes(u)
        return MakeFileTransientReq(id, note)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 1 # len(note)
        _size += len(self.note) # note contents
        return _size

@dataclass
class MakeFileTransientResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.MAKE_FILE_TRANSIENT
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeFileTransientResp':
        return MakeFileTransientResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class MakeDirectoryReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.MAKE_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 1 + SetDirectoryInfo.STATIC_SIZE # owner_id + len(name) + info
    owner_id: int
    name: bytes
    info: SetDirectoryInfo

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)
        self.info.pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirectoryReq':
        owner_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        info = SetDirectoryInfo.unpack(u)
        return MakeDirectoryReq(owner_id, name, info)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += self.info.calc_packed_size() # info
        return _size

@dataclass
class MakeDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.MAKE_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 8 # id + creation_time
    id: int
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirectoryResp':
        id = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        return MakeDirectoryResp(id, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # creation_time
        return _size

@dataclass
class RenameFileReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 + 8 + 1 # target_id + old_owner_id + len(old_name) + old_creation_time + new_owner_id + len(new_name)
    target_id: int
    old_owner_id: int
    old_name: bytes
    old_creation_time: int
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.old_creation_time, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        old_creation_time = bincode.unpack_u64(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameFileReq(target_id, old_owner_id, old_name, old_creation_time, new_owner_id, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # old_owner_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 8 # old_creation_time
        _size += 8 # new_owner_id
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class RenameFileResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_FILE
    STATIC_SIZE: ClassVar[int] = 8 # creation_time
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileResp':
        creation_time = bincode.unpack_u64(u)
        return RenameFileResp(creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # creation_time
        return _size

@dataclass
class SoftUnlinkDirectoryReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.SOFT_UNLINK_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 + 1 # owner_id + target_id + creation_time + len(name)
    owner_id: int
    target_id: int
    creation_time: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkDirectoryReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return SoftUnlinkDirectoryReq(owner_id, target_id, creation_time, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 8 # creation_time
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        return _size

@dataclass
class SoftUnlinkDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.SOFT_UNLINK_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkDirectoryResp':
        return SoftUnlinkDirectoryResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class RenameDirectoryReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 + 8 + 1 # target_id + old_owner_id + len(old_name) + old_creation_time + new_owner_id + len(new_name)
    target_id: int
    old_owner_id: int
    old_name: bytes
    old_creation_time: int
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.old_creation_time, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        old_creation_time = bincode.unpack_u64(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameDirectoryReq(target_id, old_owner_id, old_name, old_creation_time, new_owner_id, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # old_owner_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 8 # old_creation_time
        _size += 8 # new_owner_id
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class RenameDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 # creation_time
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryResp':
        creation_time = bincode.unpack_u64(u)
        return RenameDirectoryResp(creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # creation_time
        return _size

@dataclass
class HardUnlinkDirectoryReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.HARD_UNLINK_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 # dir_id
    dir_id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'HardUnlinkDirectoryReq':
        dir_id = bincode.unpack_u64(u)
        return HardUnlinkDirectoryReq(dir_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        return _size

@dataclass
class HardUnlinkDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.HARD_UNLINK_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'HardUnlinkDirectoryResp':
        return HardUnlinkDirectoryResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class CrossShardHardUnlinkFileReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.CROSS_SHARD_HARD_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # owner_id + target_id + len(name) + creation_time
    owner_id: int
    target_id: int
    name: bytes
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossShardHardUnlinkFileReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return CrossShardHardUnlinkFileReq(owner_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class CrossShardHardUnlinkFileResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.CROSS_SHARD_HARD_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CrossShardHardUnlinkFileResp':
        return CrossShardHardUnlinkFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

ShardRequestBody = Union[LookupReq, StatFileReq, StatTransientFileReq, StatDirectoryReq, ReadDirReq, ConstructFileReq, AddSpanInitiateReq, AddSpanCertifyReq, LinkFileReq, SoftUnlinkFileReq, FileSpansReq, SameDirectoryRenameReq, SetDirectoryInfoReq, SnapshotLookupReq, ExpireTransientFileReq, VisitDirectoriesReq, VisitFilesReq, VisitTransientFilesReq, FullReadDirReq, RemoveNonOwnedEdgeReq, SameShardHardFileUnlinkReq, RemoveSpanInitiateReq, RemoveSpanCertifyReq, SwapBlocksReq, BlockServiceFilesReq, RemoveInodeReq, CreateDirectoryInodeReq, SetDirectoryOwnerReq, RemoveDirectoryOwnerReq, CreateLockedCurrentEdgeReq, LockCurrentEdgeReq, UnlockCurrentEdgeReq, RemoveOwnedSnapshotFileEdgeReq, MakeFileTransientReq]
ShardResponseBody = Union[LookupResp, StatFileResp, StatTransientFileResp, StatDirectoryResp, ReadDirResp, ConstructFileResp, AddSpanInitiateResp, AddSpanCertifyResp, LinkFileResp, SoftUnlinkFileResp, FileSpansResp, SameDirectoryRenameResp, SetDirectoryInfoResp, SnapshotLookupResp, ExpireTransientFileResp, VisitDirectoriesResp, VisitFilesResp, VisitTransientFilesResp, FullReadDirResp, RemoveNonOwnedEdgeResp, SameShardHardFileUnlinkResp, RemoveSpanInitiateResp, RemoveSpanCertifyResp, SwapBlocksResp, BlockServiceFilesResp, RemoveInodeResp, CreateDirectoryInodeResp, SetDirectoryOwnerResp, RemoveDirectoryOwnerResp, CreateLockedCurrentEdgeResp, LockCurrentEdgeResp, UnlockCurrentEdgeResp, RemoveOwnedSnapshotFileEdgeResp, MakeFileTransientResp]

SHARD_REQUESTS: Dict[ShardMessageKind, Tuple[Type[ShardRequestBody], Type[ShardResponseBody]]] = {
    ShardMessageKind.LOOKUP: (LookupReq, LookupResp),
    ShardMessageKind.STAT_FILE: (StatFileReq, StatFileResp),
    ShardMessageKind.STAT_TRANSIENT_FILE: (StatTransientFileReq, StatTransientFileResp),
    ShardMessageKind.STAT_DIRECTORY: (StatDirectoryReq, StatDirectoryResp),
    ShardMessageKind.READ_DIR: (ReadDirReq, ReadDirResp),
    ShardMessageKind.CONSTRUCT_FILE: (ConstructFileReq, ConstructFileResp),
    ShardMessageKind.ADD_SPAN_INITIATE: (AddSpanInitiateReq, AddSpanInitiateResp),
    ShardMessageKind.ADD_SPAN_CERTIFY: (AddSpanCertifyReq, AddSpanCertifyResp),
    ShardMessageKind.LINK_FILE: (LinkFileReq, LinkFileResp),
    ShardMessageKind.SOFT_UNLINK_FILE: (SoftUnlinkFileReq, SoftUnlinkFileResp),
    ShardMessageKind.FILE_SPANS: (FileSpansReq, FileSpansResp),
    ShardMessageKind.SAME_DIRECTORY_RENAME: (SameDirectoryRenameReq, SameDirectoryRenameResp),
    ShardMessageKind.SET_DIRECTORY_INFO: (SetDirectoryInfoReq, SetDirectoryInfoResp),
    ShardMessageKind.SNAPSHOT_LOOKUP: (SnapshotLookupReq, SnapshotLookupResp),
    ShardMessageKind.EXPIRE_TRANSIENT_FILE: (ExpireTransientFileReq, ExpireTransientFileResp),
    ShardMessageKind.VISIT_DIRECTORIES: (VisitDirectoriesReq, VisitDirectoriesResp),
    ShardMessageKind.VISIT_FILES: (VisitFilesReq, VisitFilesResp),
    ShardMessageKind.VISIT_TRANSIENT_FILES: (VisitTransientFilesReq, VisitTransientFilesResp),
    ShardMessageKind.FULL_READ_DIR: (FullReadDirReq, FullReadDirResp),
    ShardMessageKind.REMOVE_NON_OWNED_EDGE: (RemoveNonOwnedEdgeReq, RemoveNonOwnedEdgeResp),
    ShardMessageKind.SAME_SHARD_HARD_FILE_UNLINK: (SameShardHardFileUnlinkReq, SameShardHardFileUnlinkResp),
    ShardMessageKind.REMOVE_SPAN_INITIATE: (RemoveSpanInitiateReq, RemoveSpanInitiateResp),
    ShardMessageKind.REMOVE_SPAN_CERTIFY: (RemoveSpanCertifyReq, RemoveSpanCertifyResp),
    ShardMessageKind.SWAP_BLOCKS: (SwapBlocksReq, SwapBlocksResp),
    ShardMessageKind.BLOCK_SERVICE_FILES: (BlockServiceFilesReq, BlockServiceFilesResp),
    ShardMessageKind.REMOVE_INODE: (RemoveInodeReq, RemoveInodeResp),
    ShardMessageKind.CREATE_DIRECTORY_INODE: (CreateDirectoryInodeReq, CreateDirectoryInodeResp),
    ShardMessageKind.SET_DIRECTORY_OWNER: (SetDirectoryOwnerReq, SetDirectoryOwnerResp),
    ShardMessageKind.REMOVE_DIRECTORY_OWNER: (RemoveDirectoryOwnerReq, RemoveDirectoryOwnerResp),
    ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE: (CreateLockedCurrentEdgeReq, CreateLockedCurrentEdgeResp),
    ShardMessageKind.LOCK_CURRENT_EDGE: (LockCurrentEdgeReq, LockCurrentEdgeResp),
    ShardMessageKind.UNLOCK_CURRENT_EDGE: (UnlockCurrentEdgeReq, UnlockCurrentEdgeResp),
    ShardMessageKind.REMOVE_OWNED_SNAPSHOT_FILE_EDGE: (RemoveOwnedSnapshotFileEdgeReq, RemoveOwnedSnapshotFileEdgeResp),
    ShardMessageKind.MAKE_FILE_TRANSIENT: (MakeFileTransientReq, MakeFileTransientResp),
}

CDCRequestBody = Union[MakeDirectoryReq, RenameFileReq, SoftUnlinkDirectoryReq, RenameDirectoryReq, HardUnlinkDirectoryReq, CrossShardHardUnlinkFileReq]
CDCResponseBody = Union[MakeDirectoryResp, RenameFileResp, SoftUnlinkDirectoryResp, RenameDirectoryResp, HardUnlinkDirectoryResp, CrossShardHardUnlinkFileResp]

CDC_REQUESTS: Dict[CDCMessageKind, Tuple[Type[CDCRequestBody], Type[CDCResponseBody]]] = {
    CDCMessageKind.MAKE_DIRECTORY: (MakeDirectoryReq, MakeDirectoryResp),
    CDCMessageKind.RENAME_FILE: (RenameFileReq, RenameFileResp),
    CDCMessageKind.SOFT_UNLINK_DIRECTORY: (SoftUnlinkDirectoryReq, SoftUnlinkDirectoryResp),
    CDCMessageKind.RENAME_DIRECTORY: (RenameDirectoryReq, RenameDirectoryResp),
    CDCMessageKind.HARD_UNLINK_DIRECTORY: (HardUnlinkDirectoryReq, HardUnlinkDirectoryResp),
    CDCMessageKind.CROSS_SHARD_HARD_UNLINK_FILE: (CrossShardHardUnlinkFileReq, CrossShardHardUnlinkFileResp),
}

