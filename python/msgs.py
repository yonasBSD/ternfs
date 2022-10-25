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
    TYPE_IS_DIRECTORY = 20
    TYPE_IS_NOT_DIRECTORY = 21
    BAD_COOKIE = 22
    INCONSISTENT_STORAGE_CLASS_PARITY = 23
    LAST_SPAN_STATE_NOT_CLEAN = 24
    COULD_NOT_PICK_BLOCK_SERVERS = 25
    BAD_SPAN_BODY = 26
    SPAN_NOT_FOUND = 27
    BLOCK_SERVER_NOT_FOUND = 28
    CANNOT_CERTIFY_BLOCKLESS_SPAN = 29
    BAD_NUMBER_OF_BLOCKS_PROOFS = 30
    BAD_BLOCK_PROOF = 31
    CANNOT_OVERRIDE_NAME = 32
    NAME_IS_LOCKED = 33
    OLD_NAME_IS_LOCKED = 34
    NEW_NAME_IS_LOCKED = 35
    MORE_RECENT_SNAPSHOT_ALREADY_EXISTS = 36
    MISMATCHING_TARGET = 37
    MISMATCHING_OWNER = 38
    DIRECTORY_NOT_EMPTY = 39
    FILE_IS_TRANSIENT = 40
    OLD_DIRECTORY_NOT_FOUND = 41
    NEW_DIRECTORY_NOT_FOUND = 42
    LOOP_IN_DIRECTORY_RENAME = 43
    EDGE_NOT_FOUND = 44
    DIRECTORY_HAS_OWNER = 45
    FILE_IS_NOT_TRANSIENT = 46
    FILE_NOT_EMPTY = 47
    CANNOT_REMOVE_ROOT_DIRECTORY = 48
    FILE_EMPTY = 49
    CANNOT_REMOVE_DIRTY_SPAN = 50
    TARGET_NOT_IN_SAME_SHARD = 51

class ShardMessageKind(enum.IntEnum):
    LOOKUP = 0x1
    STAT = 0x2
    READ_DIR = 0x3
    CONSTRUCT_FILE = 0x4
    ADD_SPAN_INITIATE = 0x5
    ADD_SPAN_CERTIFY = 0x6
    LINK_FILE = 0x7
    SOFT_UNLINK_FILE = 0xC
    FILE_SPANS = 0xD
    SAME_DIRECTORY_RENAME = 0xE
    VISIT_DIRECTORIES = 0x15
    VISIT_FILES = 0x20
    VISIT_TRANSIENT_FILES = 0x16
    FULL_READ_DIR = 0x21
    REMOVE_NON_OWNED_EDGE = 0x17
    INTRA_SHARD_HARD_FILE_UNLINK = 0x18
    REMOVE_SPAN_INITIATE = 0x19
    REMOVE_SPAN_CERTIFY = 0x1A
    CREATE_DIRECTORY_INODE = 0x80
    SET_DIRECTORY_OWNER = 0x81
    CREATE_LOCKED_CURRENT_EDGE = 0x82
    LOCK_CURRENT_EDGE = 0x83
    UNLOCK_CURRENT_EDGE = 0x84
    REMOVE_INODE = 0x85
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 0x86
    MAKE_FILE_TRANSIENT = 0x87

class CDCMessageKind(enum.IntEnum):
    MAKE_DIRECTORY = 0x1
    RENAME_FILE = 0x2
    SOFT_UNLINK_DIRECTORY = 0x3
    RENAME_DIRECTORY = 0x4
    HARD_UNLINK_DIRECTORY = 0x5
    HARD_UNLINK_FILE = 0x6

@dataclass
class TransientFile(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 + 1 # id + cookie + deadline_time + len(note)
    id: int
    cookie: int
    deadline_time: int
    note: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_u64_into(self.deadline_time, b)
        bincode.pack_bytes_into(self.note, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'TransientFile':
        id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        deadline_time = bincode.unpack_u64(u)
        note = bincode.unpack_bytes(u)
        return TransientFile(id, cookie, deadline_time, note)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # cookie
        _size += 8 # deadline_time
        _size += 1 # len(note)
        _size += len(self.note) # note contents
        return _size

@dataclass
class FetchedBlock(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 + 8 + 4 + 1 # ip + port + block_id + crc32 + flags
    ip: bytes
    port: int
    block_id: int
    crc32: bytes
    size: int
    flags: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_u8_into(self.flags, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FetchedBlock':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_u64(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        flags = bincode.unpack_u8(u)
        return FetchedBlock(ip, port, block_id, crc32, size, flags)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # ip
        _size += 2 # port
        _size += 8 # block_id
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
        _size += 1 # flags
        return _size

@dataclass
class Edge(bincode.Packable):
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
    def unpack(u: bincode.UnpackWrapper) -> 'Edge':
        target_id = bincode.unpack_u64(u)
        name_hash = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return Edge(target_id, name_hash, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # name_hash
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class EdgeWithOwnership(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # target_id + name_hash + len(name) + creation_time
    target_id: InodeIdWithExtra
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
    def unpack(u: bincode.UnpackWrapper) -> 'EdgeWithOwnership':
        target_id = InodeIdWithExtra(bincode.unpack_u64(u))
        name_hash = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return EdgeWithOwnership(target_id, name_hash, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
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
    body_bytes: bytes
    body_blocks: List[FetchedBlock]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
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
        body_bytes = bincode.unpack_bytes(u)
        body_blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(body_blocks)):
            body_blocks[i] = FetchedBlock.unpack(u)
        return FetchedSpan(byte_offset, parity, storage_class, crc32, size, body_bytes, body_blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 1 # parity
        _size += 1 # storage_class
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
        _size += 1 # len(body_bytes)
        _size += len(self.body_bytes) # body_bytes contents
        _size += 2 # len(body_blocks)
        for i in range(len(self.body_blocks)):
            _size += self.body_blocks[i].calc_packed_size() # body_blocks[i]
        return _size

@dataclass
class BlockInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 + 2 + 8 + 8 # ip + port + block_id + certificate
    ip: bytes
    port: int
    block_id: int
    certificate: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.ip, 4, b)
        bincode.pack_u16_into(self.port, b)
        bincode.pack_u64_into(self.block_id, b)
        bincode.pack_fixed_into(self.certificate, 8, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'BlockInfo':
        ip = bincode.unpack_fixed(u, 4)
        port = bincode.unpack_u16(u)
        block_id = bincode.unpack_u64(u)
        certificate = bincode.unpack_fixed(u, 8)
        return BlockInfo(ip, port, block_id, certificate)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # ip
        _size += 2 # port
        _size += 8 # block_id
        _size += 8 # certificate
        return _size

@dataclass
class NewBlockInfo(bincode.Packable):
    STATIC_SIZE: ClassVar[int] = 4 # crc32
    crc32: bytes
    size: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'NewBlockInfo':
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        return NewBlockInfo(crc32, size)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
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
class StatReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatReq':
        id = bincode.unpack_u64(u)
        return StatReq(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class StatResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.STAT
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # mtime + size_or_owner + len(opaque)
    mtime: int
    size_or_owner: int
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        bincode.pack_u64_into(self.size_or_owner, b)
        bincode.pack_bytes_into(self.opaque, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'StatResp':
        mtime = bincode.unpack_u64(u)
        size_or_owner = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return StatResp(mtime, size_or_owner, opaque)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # mtime
        _size += 8 # size_or_owner
        _size += 1 # len(opaque)
        _size += len(self.opaque) # opaque contents
        return _size

@dataclass
class ReadDirReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.READ_DIR
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 8 # dir_id + start_hash + as_of_time
    dir_id: int
    start_hash: int
    as_of_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.start_hash, b)
        bincode.pack_u64_into(self.as_of_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ReadDirReq':
        dir_id = bincode.unpack_u64(u)
        start_hash = bincode.unpack_u64(u)
        as_of_time = bincode.unpack_u64(u)
        return ReadDirReq(dir_id, start_hash, as_of_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # start_hash
        _size += 8 # as_of_time
        return _size

@dataclass
class ReadDirResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.READ_DIR
    STATIC_SIZE: ClassVar[int] = 8 + 2 # next_hash + len(results)
    next_hash: int
    results: List[Edge]

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
            results[i] = Edge.unpack(u)
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
    cookie: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.cookie, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'ConstructFileResp':
        id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        return ConstructFileResp(id, cookie)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # cookie
        return _size

@dataclass
class AddSpanInitiateReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.ADD_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 1 + 4 + 1 + 2 # file_id + cookie + storage_class + parity + crc32 + len(body_bytes) + len(body_blocks)
    file_id: int
    cookie: int
    byte_offset: int
    storage_class: int
    parity: int
    crc32: bytes
    size: int
    body_bytes: bytes
    body_blocks: List[NewBlockInfo]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u8_into(self.storage_class, b)
        bincode.pack_u8_into(self.parity, b)
        bincode.pack_fixed_into(self.crc32, 4, b)
        bincode.pack_v61_into(self.size, b)
        bincode.pack_bytes_into(self.body_bytes, b)
        bincode.pack_u16_into(len(self.body_blocks), b)
        for i in range(len(self.body_blocks)):
            self.body_blocks[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanInitiateReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
        byte_offset = bincode.unpack_v61(u)
        storage_class = bincode.unpack_u8(u)
        parity = bincode.unpack_u8(u)
        crc32 = bincode.unpack_fixed(u, 4)
        size = bincode.unpack_v61(u)
        body_bytes = bincode.unpack_bytes(u)
        body_blocks: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(body_blocks)):
            body_blocks[i] = NewBlockInfo.unpack(u)
        return AddSpanInitiateReq(file_id, cookie, byte_offset, storage_class, parity, crc32, size, body_bytes, body_blocks)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # file_id
        _size += 8 # cookie
        _size += bincode.v61_packed_size(self.byte_offset) # byte_offset
        _size += 1 # storage_class
        _size += 1 # parity
        _size += 4 # crc32
        _size += bincode.v61_packed_size(self.size) # size
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
    cookie: int
    byte_offset: int
    proofs: List[BlockProof]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for i in range(len(self.proofs)):
            self.proofs[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'AddSpanCertifyReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
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
    cookie: int
    owner_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
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
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'LinkFileResp':
        return LinkFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class SoftUnlinkFileReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SOFT_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # owner_id + file_id + len(name)
    owner_id: int
    file_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkFileReq':
        owner_id = bincode.unpack_u64(u)
        file_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return SoftUnlinkFileReq(owner_id, file_id, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # file_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
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
    STATIC_SIZE: ClassVar[int] = 2 # len(spans)
    next_offset: int
    spans: List[FetchedSpan]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_v61_into(self.next_offset, b)
        bincode.pack_u16_into(len(self.spans), b)
        for i in range(len(self.spans)):
            self.spans[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FileSpansResp':
        next_offset = bincode.unpack_v61(u)
        spans: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(spans)):
            spans[i] = FetchedSpan.unpack(u)
        return FileSpansResp(next_offset, spans)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += bincode.v61_packed_size(self.next_offset) # next_offset
        _size += 2 # len(spans)
        for i in range(len(self.spans)):
            _size += self.spans[i].calc_packed_size() # spans[i]
        return _size

@dataclass
class SameDirectoryRenameReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_DIRECTORY_RENAME
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 1 # target_id + dir_id + len(old_name) + len(new_name)
    target_id: int
    dir_id: int
    old_name: bytes
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameReq':
        target_id = bincode.unpack_u64(u)
        dir_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_name = bincode.unpack_bytes(u)
        return SameDirectoryRenameReq(target_id, dir_id, old_name, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # dir_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class SameDirectoryRenameResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.SAME_DIRECTORY_RENAME
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SameDirectoryRenameResp':
        return SameDirectoryRenameResp()

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
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 # dir_id + start_hash + len(start_name) + start_time
    dir_id: int
    start_hash: int
    start_name: bytes
    start_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_u64_into(self.start_hash, b)
        bincode.pack_bytes_into(self.start_name, b)
        bincode.pack_u64_into(self.start_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FullReadDirReq':
        dir_id = bincode.unpack_u64(u)
        start_hash = bincode.unpack_u64(u)
        start_name = bincode.unpack_bytes(u)
        start_time = bincode.unpack_u64(u)
        return FullReadDirReq(dir_id, start_hash, start_name, start_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 8 # start_hash
        _size += 1 # len(start_name)
        _size += len(self.start_name) # start_name contents
        _size += 8 # start_time
        return _size

@dataclass
class FullReadDirResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.FULL_READ_DIR
    STATIC_SIZE: ClassVar[int] = 1 + 2 # finished + len(results)
    finished: bool
    results: List[EdgeWithOwnership]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u8_into(self.finished, b)
        bincode.pack_u16_into(len(self.results), b)
        for i in range(len(self.results)):
            self.results[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'FullReadDirResp':
        finished = bool(bincode.unpack_u8(u))
        results: List[Any] = [None]*bincode.unpack_u16(u)
        for i in range(len(results)):
            results[i] = EdgeWithOwnership.unpack(u)
        return FullReadDirResp(finished, results)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 1 # finished
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
class IntraShardHardFileUnlinkReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.INTRA_SHARD_HARD_FILE_UNLINK
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
    def unpack(u: bincode.UnpackWrapper) -> 'IntraShardHardFileUnlinkReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return IntraShardHardFileUnlinkReq(owner_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class IntraShardHardFileUnlinkResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.INTRA_SHARD_HARD_FILE_UNLINK
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'IntraShardHardFileUnlinkResp':
        return IntraShardHardFileUnlinkResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class RemoveSpanInitiateReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.REMOVE_SPAN_INITIATE
    STATIC_SIZE: ClassVar[int] = 8 + 8 # file_id + cookie
    file_id: int
    cookie: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanInitiateReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
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
    cookie: int
    byte_offset: int
    proofs: List[BlockProof]

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.file_id, b)
        bincode.pack_u64_into(self.cookie, b)
        bincode.pack_v61_into(self.byte_offset, b)
        bincode.pack_u16_into(len(self.proofs), b)
        for i in range(len(self.proofs)):
            self.proofs[i].pack_into(b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RemoveSpanCertifyReq':
        file_id = bincode.unpack_u64(u)
        cookie = bincode.unpack_u64(u)
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
class CreateDirectoryINodeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_DIRECTORY_INODE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # id + owner_id + len(opaque)
    id: int
    owner_id: int
    opaque: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.opaque, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryINodeReq':
        id = bincode.unpack_u64(u)
        owner_id = bincode.unpack_u64(u)
        opaque = bincode.unpack_bytes(u)
        return CreateDirectoryINodeReq(id, owner_id, opaque)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        _size += 8 # owner_id
        _size += 1 # len(opaque)
        _size += len(self.opaque) # opaque contents
        return _size

@dataclass
class CreateDirectoryINodeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_DIRECTORY_INODE
    STATIC_SIZE: ClassVar[int] = 8 # mtime
    mtime: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.mtime, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateDirectoryINodeResp':
        mtime = bincode.unpack_u64(u)
        return CreateDirectoryINodeResp(mtime)

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
class CreateLockedCurrentEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 8 + 8 # dir_id + len(name) + target_id + creation_time
    dir_id: int
    name: bytes
    target_id: int
    creation_time: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.creation_time, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        creation_time = bincode.unpack_u64(u)
        return CreateLockedCurrentEdgeReq(dir_id, name, target_id, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # target_id
        _size += 8 # creation_time
        return _size

@dataclass
class CreateLockedCurrentEdgeResp(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'CreateLockedCurrentEdgeResp':
        return CreateLockedCurrentEdgeResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class LockCurrentEdgeReq(bincode.Packable):
    KIND: ClassVar[ShardMessageKind] = ShardMessageKind.LOCK_CURRENT_EDGE
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
    def unpack(u: bincode.UnpackWrapper) -> 'LockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        return LockCurrentEdgeReq(dir_id, name, target_id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # target_id
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
    STATIC_SIZE: ClassVar[int] = 8 + 1 + 8 + 1 # dir_id + len(name) + target_id + was_moved
    dir_id: int
    name: bytes
    target_id: int
    was_moved: bool

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.dir_id, b)
        bincode.pack_bytes_into(self.name, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u8_into(self.was_moved, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'UnlockCurrentEdgeReq':
        dir_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        target_id = bincode.unpack_u64(u)
        was_moved = bool(bincode.unpack_u8(u))
        return UnlockCurrentEdgeReq(dir_id, name, target_id, was_moved)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # dir_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
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
    STATIC_SIZE: ClassVar[int] = 8 + 1 # owner_id + len(name)
    owner_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirectoryReq':
        owner_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return MakeDirectoryReq(owner_id, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        return _size

@dataclass
class MakeDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.MAKE_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 # id
    id: int

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.id, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'MakeDirectoryResp':
        id = bincode.unpack_u64(u)
        return MakeDirectoryResp(id)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # id
        return _size

@dataclass
class RenameFileReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_FILE
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 + 1 # target_id + old_owner_id + len(old_name) + new_owner_id + len(new_name)
    target_id: int
    old_owner_id: int
    old_name: bytes
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameFileReq(target_id, old_owner_id, old_name, new_owner_id, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # old_owner_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 8 # new_owner_id
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class RenameFileResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_FILE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameFileResp':
        return RenameFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

@dataclass
class SoftUnlinkDirectoryReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.SOFT_UNLINK_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 # owner_id + target_id + len(name)
    owner_id: int
    target_id: int
    name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.owner_id, b)
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_bytes_into(self.name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'SoftUnlinkDirectoryReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        return SoftUnlinkDirectoryReq(owner_id, target_id, name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
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
    STATIC_SIZE: ClassVar[int] = 8 + 8 + 1 + 8 + 1 # target_id + old_owner_id + len(old_name) + new_owner_id + len(new_name)
    target_id: int
    old_owner_id: int
    old_name: bytes
    new_owner_id: int
    new_name: bytes

    def pack_into(self, b: bytearray) -> None:
        bincode.pack_u64_into(self.target_id, b)
        bincode.pack_u64_into(self.old_owner_id, b)
        bincode.pack_bytes_into(self.old_name, b)
        bincode.pack_u64_into(self.new_owner_id, b)
        bincode.pack_bytes_into(self.new_name, b)
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryReq':
        target_id = bincode.unpack_u64(u)
        old_owner_id = bincode.unpack_u64(u)
        old_name = bincode.unpack_bytes(u)
        new_owner_id = bincode.unpack_u64(u)
        new_name = bincode.unpack_bytes(u)
        return RenameDirectoryReq(target_id, old_owner_id, old_name, new_owner_id, new_name)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # target_id
        _size += 8 # old_owner_id
        _size += 1 # len(old_name)
        _size += len(self.old_name) # old_name contents
        _size += 8 # new_owner_id
        _size += 1 # len(new_name)
        _size += len(self.new_name) # new_name contents
        return _size

@dataclass
class RenameDirectoryResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.RENAME_DIRECTORY
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'RenameDirectoryResp':
        return RenameDirectoryResp()

    def calc_packed_size(self) -> int:
        _size = 0
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
class HardUnlinkFileReq(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.HARD_UNLINK_FILE
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
    def unpack(u: bincode.UnpackWrapper) -> 'HardUnlinkFileReq':
        owner_id = bincode.unpack_u64(u)
        target_id = bincode.unpack_u64(u)
        name = bincode.unpack_bytes(u)
        creation_time = bincode.unpack_u64(u)
        return HardUnlinkFileReq(owner_id, target_id, name, creation_time)

    def calc_packed_size(self) -> int:
        _size = 0
        _size += 8 # owner_id
        _size += 8 # target_id
        _size += 1 # len(name)
        _size += len(self.name) # name contents
        _size += 8 # creation_time
        return _size

@dataclass
class HardUnlinkFileResp(bincode.Packable):
    KIND: ClassVar[CDCMessageKind] = CDCMessageKind.HARD_UNLINK_FILE
    STATIC_SIZE: ClassVar[int] = 0 # 

    def pack_into(self, b: bytearray) -> None:
        return None

    @staticmethod
    def unpack(u: bincode.UnpackWrapper) -> 'HardUnlinkFileResp':
        return HardUnlinkFileResp()

    def calc_packed_size(self) -> int:
        _size = 0
        return _size

ShardRequestBody = Union[LookupReq, StatReq, ReadDirReq, ConstructFileReq, AddSpanInitiateReq, AddSpanCertifyReq, LinkFileReq, SoftUnlinkFileReq, FileSpansReq, SameDirectoryRenameReq, VisitDirectoriesReq, VisitFilesReq, VisitTransientFilesReq, FullReadDirReq, RemoveNonOwnedEdgeReq, IntraShardHardFileUnlinkReq, RemoveSpanInitiateReq, RemoveSpanCertifyReq, CreateDirectoryINodeReq, SetDirectoryOwnerReq, CreateLockedCurrentEdgeReq, LockCurrentEdgeReq, UnlockCurrentEdgeReq, RemoveInodeReq, RemoveOwnedSnapshotFileEdgeReq, MakeFileTransientReq]
ShardResponseBody = Union[LookupResp, StatResp, ReadDirResp, ConstructFileResp, AddSpanInitiateResp, AddSpanCertifyResp, LinkFileResp, SoftUnlinkFileResp, FileSpansResp, SameDirectoryRenameResp, VisitDirectoriesResp, VisitFilesResp, VisitTransientFilesResp, FullReadDirResp, RemoveNonOwnedEdgeResp, IntraShardHardFileUnlinkResp, RemoveSpanInitiateResp, RemoveSpanCertifyResp, CreateDirectoryINodeResp, SetDirectoryOwnerResp, CreateLockedCurrentEdgeResp, LockCurrentEdgeResp, UnlockCurrentEdgeResp, RemoveInodeResp, RemoveOwnedSnapshotFileEdgeResp, MakeFileTransientResp]

SHARD_REQUESTS: Dict[ShardMessageKind, Tuple[Type[ShardRequestBody], Type[ShardResponseBody]]] = {
    ShardMessageKind.LOOKUP: (LookupReq, LookupResp),
    ShardMessageKind.STAT: (StatReq, StatResp),
    ShardMessageKind.READ_DIR: (ReadDirReq, ReadDirResp),
    ShardMessageKind.CONSTRUCT_FILE: (ConstructFileReq, ConstructFileResp),
    ShardMessageKind.ADD_SPAN_INITIATE: (AddSpanInitiateReq, AddSpanInitiateResp),
    ShardMessageKind.ADD_SPAN_CERTIFY: (AddSpanCertifyReq, AddSpanCertifyResp),
    ShardMessageKind.LINK_FILE: (LinkFileReq, LinkFileResp),
    ShardMessageKind.SOFT_UNLINK_FILE: (SoftUnlinkFileReq, SoftUnlinkFileResp),
    ShardMessageKind.FILE_SPANS: (FileSpansReq, FileSpansResp),
    ShardMessageKind.SAME_DIRECTORY_RENAME: (SameDirectoryRenameReq, SameDirectoryRenameResp),
    ShardMessageKind.VISIT_DIRECTORIES: (VisitDirectoriesReq, VisitDirectoriesResp),
    ShardMessageKind.VISIT_FILES: (VisitFilesReq, VisitFilesResp),
    ShardMessageKind.VISIT_TRANSIENT_FILES: (VisitTransientFilesReq, VisitTransientFilesResp),
    ShardMessageKind.FULL_READ_DIR: (FullReadDirReq, FullReadDirResp),
    ShardMessageKind.REMOVE_NON_OWNED_EDGE: (RemoveNonOwnedEdgeReq, RemoveNonOwnedEdgeResp),
    ShardMessageKind.INTRA_SHARD_HARD_FILE_UNLINK: (IntraShardHardFileUnlinkReq, IntraShardHardFileUnlinkResp),
    ShardMessageKind.REMOVE_SPAN_INITIATE: (RemoveSpanInitiateReq, RemoveSpanInitiateResp),
    ShardMessageKind.REMOVE_SPAN_CERTIFY: (RemoveSpanCertifyReq, RemoveSpanCertifyResp),
    ShardMessageKind.CREATE_DIRECTORY_INODE: (CreateDirectoryINodeReq, CreateDirectoryINodeResp),
    ShardMessageKind.SET_DIRECTORY_OWNER: (SetDirectoryOwnerReq, SetDirectoryOwnerResp),
    ShardMessageKind.CREATE_LOCKED_CURRENT_EDGE: (CreateLockedCurrentEdgeReq, CreateLockedCurrentEdgeResp),
    ShardMessageKind.LOCK_CURRENT_EDGE: (LockCurrentEdgeReq, LockCurrentEdgeResp),
    ShardMessageKind.UNLOCK_CURRENT_EDGE: (UnlockCurrentEdgeReq, UnlockCurrentEdgeResp),
    ShardMessageKind.REMOVE_INODE: (RemoveInodeReq, RemoveInodeResp),
    ShardMessageKind.REMOVE_OWNED_SNAPSHOT_FILE_EDGE: (RemoveOwnedSnapshotFileEdgeReq, RemoveOwnedSnapshotFileEdgeResp),
    ShardMessageKind.MAKE_FILE_TRANSIENT: (MakeFileTransientReq, MakeFileTransientResp),
}

CDCRequestBody = Union[MakeDirectoryReq, RenameFileReq, SoftUnlinkDirectoryReq, RenameDirectoryReq, HardUnlinkDirectoryReq, HardUnlinkFileReq]
CDCResponseBody = Union[MakeDirectoryResp, RenameFileResp, SoftUnlinkDirectoryResp, RenameDirectoryResp, HardUnlinkDirectoryResp, HardUnlinkFileResp]

CDC_REQUESTS: Dict[CDCMessageKind, Tuple[Type[CDCRequestBody], Type[CDCResponseBody]]] = {
    CDCMessageKind.MAKE_DIRECTORY: (MakeDirectoryReq, MakeDirectoryResp),
    CDCMessageKind.RENAME_FILE: (RenameFileReq, RenameFileResp),
    CDCMessageKind.SOFT_UNLINK_DIRECTORY: (SoftUnlinkDirectoryReq, SoftUnlinkDirectoryResp),
    CDCMessageKind.RENAME_DIRECTORY: (RenameDirectoryReq, RenameDirectoryResp),
    CDCMessageKind.HARD_UNLINK_DIRECTORY: (HardUnlinkDirectoryReq, HardUnlinkDirectoryResp),
    CDCMessageKind.HARD_UNLINK_FILE: (HardUnlinkFileReq, HardUnlinkFileResp),
}

