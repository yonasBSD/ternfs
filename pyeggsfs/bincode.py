#!/usr/bin/env python3

import abc
import struct
from typing import Type, TypeVar


V61_MAX = 2**61 - 1


# v61 is an unsigned type with range [0, 2**61)
# encoded using between 1 and 8 bytes
# 3 bits of the first byte give the length bytes of the int, so that it
# is more efficient to encode smaller numbers

def v61_packed_size(x: int) -> int:
    # in C++ this can be implemented efficiently using _BitScanReverse64
    if x < 2**5:
        return 1
    elif x < 2**13:
        return 2
    elif x < 2**21:
        return 3
    elif x < 2**29:
        return 4
    elif x < 2**37:
        return 5
    elif x < 2**45:
        return 6
    elif x < 2**53:
        return 7
    elif x < 2**61:
        return 8
    raise ValueError(f'{x} too large for v61')


def bytes_packed_size(x: bytes) -> int:
    return 1 + len(x)


def pack_bytes_into(x: bytes, b: bytearray) -> None:
    pack_u8_into(len(x), b)
    b.extend(x)


def pack_fixed_into(x: bytes, size: int, b: bytearray) -> None:
    if (len(x) != size):
        raise Exception(f'Bad size bytes: {x!r} vs {size}')
    b.extend(x)


def pack_u8_into(x: int, b: bytearray) -> None:
    b.append(x)


def pack_u16_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<H', x))


def pack_u32_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<I', x))


def pack_u64_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<Q', x))


def pack_v61_into(x: int, b: bytearray) -> None:
    num_bytes = v61_packed_size(x)
    with_length = (x << 3) | num_bytes
    packed = struct.pack('<Q', with_length)
    b.extend(packed[:num_bytes])


class UnpackWrapper:
    def __init__(self, data: bytes):
        self.data = data
        self.idx = 0

    def read(self) -> bytes:
        return self.data[self.idx:]

    def advance(self, i: int) -> None:
        if self.idx + i > len(self.data):
            raise ValueError('Attempt to advance past end')
        self.idx += i

    def bytes_remaining(self) -> int:
        return len(self.data) - self.idx


def unpack_u8(u: UnpackWrapper) -> int:
    ret = u.read()[0]
    u.advance(1)
    return ret


def unpack_u16(u: UnpackWrapper) -> int:
    ret: int = struct.unpack('<H', u.read()[:2])[0]
    u.advance(2)
    return ret


def unpack_u32(u: UnpackWrapper) -> int:
    ret: int = struct.unpack('<I', u.read()[:4])[0]
    u.advance(4)
    return ret


def unpack_u64(u: UnpackWrapper) -> int:
    ret: int = struct.unpack('<Q', u.read()[:8])[0]
    u.advance(8)
    return ret


def unpack_v61(u: UnpackWrapper) -> int:
    num_bytes = u.read()[0] & 0b111
    zpadded_val = bytearray(8)
    zpadded_val[:num_bytes] = u.read()[:num_bytes]
    u.advance(num_bytes)
    raw_unpacked: int = struct.unpack('<Q', zpadded_val)[0]
    return raw_unpacked >> 3


def unpack_bytes(u: UnpackWrapper) -> bytes:
    size = unpack_u8(u)
    return unpack_fixed(u, size)


def unpack_fixed(u: UnpackWrapper, size: int) -> bytes:
    ret = u.read()[:size]
    u.advance(size)
    return ret


T = TypeVar('T', bound='Packable')


class Packable(abc.ABC):
    @abc.abstractmethod
    def pack_into(self, b: bytearray) -> None:
        pass

    @classmethod
    @abc.abstractmethod
    def unpack(cls: Type[T], u: UnpackWrapper) -> T:
        pass


def pack(x: Packable) -> bytes:
    ret = bytearray()
    x.pack_into(ret)
    return bytes(ret)


def unpack(cls: Type[T], b: bytes) -> T:
    u = UnpackWrapper(b)
    ret = cls.unpack(u)
    if u.idx != len(b):
        raise ValueError('Some bytes not unpacked')
    return ret


if __name__ == '__main__':
    import doctest
    doctest.testmod()
