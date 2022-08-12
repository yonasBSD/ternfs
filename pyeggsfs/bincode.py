#!/usr/bin/env python3

import abc
import struct
from typing import Any, Tuple, Type, TypeVar


U16_MAX = 2**16 - 1
U32_MAX = 2**32 - 1
U64_MAX = 2**64 - 1
U128_MAX = 2**128 - 1


def varint_packed_size(x: int) -> int:
    if x < 251:
        return 1
    elif x < 2**16:
        return 3
    elif x < 2**32:
        return 5
    elif x < 2**64:
        return 9
    else:
        return 17


def varbytes_packed_size(x: bytes) -> int:
    return varint_packed_size(len(x)) + len(x)


# support for varint encoding
# see: https://github.com/bincode-org/bincode/blob/v2.0.0-rc.1/docs/spec.md
def pack_unsigned_into(x: int, b: bytearray) -> None:
    if x > U128_MAX:
        raise ValueError('Larger than u128 unsupported')

    if x < 251:
        b.append(x)
    elif x < 2**16:
        b.append(251)
        b.extend(struct.pack('<H', x))
    elif x < 2**32:
        b.append(252)
        b.extend(struct.pack('<I', x))
    elif x < 2**64:
        b.append(253)
        b.extend(struct.pack('<Q', x))
    else:
        b.append(254)
        b.extend(struct.pack('<QQ', x & 0xFFFF_FFFF_FFFF_FFFF, x >> 64))


def pack_bytes_into(x: bytes, b: bytearray) -> None:
    pack_unsigned_into(len(x), b)
    b.extend(x)


def pack_fixed_into(x: bytes, b: bytearray) -> None:
    b.extend(x)


def pack_u8_into(x: int, b: bytearray) -> None:
    b.append(x)


def pack_u16_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<H', x))


def pack_u32_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<I', x))


def pack_u64_into(x: int, b: bytearray) -> None:
    b.extend(struct.pack('<Q', x))


# converts signed values to unsigned using a "zigzag"
# this means signed values close to zero are smaller in varint representation
def zigzag(x: int) -> int:
    '''Converts signed values to unsigned using "zigzag" algorithm.
    This means signed values close to zero are smaller in varint representation.
    >>> zigzag(0)
    0
    >>> zigzag(-1)
    1
    >>> zigzag(1)
    2
    >>> zigzag(-2)
    3
    >>> zigzag(2)
    4
    >>> zigzag(-2**63)
    18446744073709551615
    '''
    return (~x << 1) + 1 if x < 0 else x << 1


def pack_signed_into(x: int, b: bytearray) -> None:
    pack_unsigned_into(zigzag(x), b)


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


def unpack_unsigned(u: UnpackWrapper) -> int:
    b = u.read()
    bytes_read = 0
    try:
        if b[0] < 251:
            bytes_read, value = 1, b[0]
        elif b[0] == 251:
            bytes_read, value = 3, struct.unpack('<H', b[1:3])[0]
        elif b[0] == 252:
            bytes_read, value = 5, struct.unpack('<I', b[1:5])[0]
        elif b[0] == 253:
            bytes_read, value = 9, struct.unpack('<Q', b[1:9])[0]
        elif b[0] == 254:
            lower, upper = struct.unpack('<QQ', b[1:17])
            bytes_read, value = 17, lower + (upper << 64)
        else:
            raise ValueError('Invalid first byte: 255')
    except (IndexError, struct.error):
        raise ValueError('Runt')
    u.advance(bytes_read)
    return value


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


def unzigzag(x: int) -> int:
    '''
    >>> unzigzag(0)
    0
    >>> unzigzag(1)
    -1
    >>> unzigzag(2)
    1
    >>> unzigzag(3)
    -2
    >>> unzigzag(4)
    2
    >>> unzigzag(2**64 - 1)
    -9223372036854775808
    '''
    return ~(x >> 1) if x & 1 else x >> 1


def unpack_signed(u: UnpackWrapper) -> int:
    '''Returns: (value, bytes_consumed)'''
    return unzigzag(unpack_unsigned(u))


def unpack_bytes(u: UnpackWrapper) -> bytes:
    size = unpack_unsigned(u)
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
