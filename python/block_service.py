#!/usr/bin/env python

from pathlib import Path
from typing import Tuple, cast
import argparse
import asyncio
import crypto
import fcntl
import os
import requests
import secrets
import socket
import struct
import sys
import logging

import common

def block_id_to_path(base_path: Path, block_id: int) -> Path:
    # 256 subdirectories
    hex_block_id = block_id.to_bytes(8, 'little').hex()
    subdir = hex_block_id[:2]
    return base_path / subdir / hex_block_id

def fetch_block(base_path: Path, block_id: int) -> bytes:
    # if it doesn't exist the connection gets closed
    path = block_id_to_path(base_path, block_id)
    return path.read_bytes()

def erase_block(base_path: Path, block_id: int) -> None:
    # this cannot fail
    # (need to fsync the directory for prod version)
    path = block_id_to_path(base_path, block_id)
    try:
        path.unlink()
    except FileNotFoundError:
        pass

def write_block(base_path: Path, block_id: int, data: bytes) -> None:
    # (need to fsync the directory for prod version)
    path = block_id_to_path(base_path, block_id)
    path.parent.mkdir(exist_ok=True)
    with path.open('xb') as fp:
        fp.write(data)
        fp.flush()
        os.fsync(fp.fileno())

def get_stats(base_path: Path) -> Tuple[int, int, int, int]:
    # get number of blocks, bytes used and bytes available
    s = os.statvfs(base_path)
    free_bytes = s.f_bavail * s.f_bsize
    free_n = s.f_favail
    used_bytes = 0
    used_n = 0
    for i in range(256):
        path = base_path / i.to_bytes(2, 'little').hex()[:2]
        if not path.exists():
            continue
        with os.scandir(path) as it:
            for entry in it:
                used_bytes += entry.stat().st_size
                used_n += 1
    return used_bytes, free_bytes, used_n, free_n

###########################################
# serialisation / deserialisation
###########################################

def deser_erase_block(m: bytes, rk: crypto.ExpandedKey) -> int:
    # prefix=e, length=17
    m2 = crypto.remove_mac(m, rk)
    assert m2 is not None
    kind, block_id = struct.unpack('<cQ', m2)
    assert kind == b'e'
    return cast(int, block_id)

def deser_fetch_block(m: bytes) -> int:
    # prefix=f, length=9
    kind, block_id = struct.unpack('<cQ', m)
    assert kind == b'f'
    return cast(int, block_id)

def deser_crc_block(m: bytes) -> int:
    # prefix=c, length=9
    kind, block_id = struct.unpack('<cQ', m)
    assert kind == b'c'
    return cast(int, block_id)

def deser_write_block(m: bytes, rk: crypto.ExpandedKey) -> Tuple[int, bytes, int]:
    # write block with specific CRC and size
    # prefix=w, length=25
    m2 = crypto.remove_mac(m, rk)
    assert m2 is not None
    kind, block_id, crc, size = struct.unpack('<cQ4sI', m2)
    assert kind == b'w'
    return cast(int, block_id), cast(bytes, crc), cast(int, size)

def deser_stats(m: bytes) -> int:
    # prefix=s, length=9
    kind, token = struct.unpack('<cQ', m)
    assert kind == b's'
    return cast(int, token)

def ser_erase_cert(block_id: int, rk: crypto.ExpandedKey) -> bytes:
    # confirm block_id was erased
    # prefix=E, length=9
    m = struct.pack('<cQ', b'E', block_id)
    return crypto.add_mac(m, rk)

def ser_fetch_response(size: int) -> bytes:
    # just return the four byte size followed by the data
    # prefix=F, length=5
    return struct.pack('<cI', b'F', size)

def ser_crc_response(size: int, crc: bytes) -> bytes:
    # return size and crc
    # prefix=C, length=9
    return struct.pack('<cI4s', b'C', size, crc)

def ser_write_cert(block_id: int, rk: crypto.ExpandedKey) -> bytes:
    # confirm block was written
    # prefix=W, length=9
    m = struct.pack('<cQ', b'W', block_id)
    return crypto.add_mac(m, rk)

def ser_stats(token: int, used_bytes: int, free_bytes: int, used_n: int, free_n: int, rk: crypto.ExpandedKey) -> bytes:
    # prefix=S, length=41
    m = struct.pack('<cQQQQQ', b'S', token, used_bytes, free_bytes, used_n, free_n)
    return crypto.add_mac(m, rk)

###########################################
# main client handling code
###########################################

MAX_OBJECT_SIZE = 100e6

ONE_HOUR_IN_NS = 60 * 60 * 1000 * 1000 * 1000
PAST_CUTOFF = ONE_HOUR_IN_NS * 22
FUTURE_CUTOFF = ONE_HOUR_IN_NS * 2

async def handle_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, rk: crypto.ExpandedKey, base_path: Path) -> None:
    writer.get_extra_info('socket').setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b'\x00'*8)
    try:
        while True:
            kind = await reader.read(1)
            if kind == b'e':
                # erase block
                # (block_id, mac) -> data
                block_id = deser_erase_block(kind + (await reader.readexactly(16)), rk)
                erase_block(base_path, block_id) # can never fail
                m = ser_erase_cert(block_id, rk)
                writer.write(m)

            elif kind == b'f':
                # fetch block (no MAC is required)
                # block_id -> data
                block_id = deser_fetch_block(kind + (await reader.readexactly(8)))
                data = fetch_block(base_path, block_id)
                m = ser_fetch_response(len(data))
                writer.write(m)
                writer.write(data)

            elif kind == b'w':
                # write block
                # (block_id, crc, size, mac) -> data
                block_id, crc, size = deser_write_block(kind + (await reader.readexactly(24)), rk)
                # check that block_id is inside expected range
                # this ensures we don't have "reply attack"-esque issues
                now = common.eggs_time()
                if not (now - PAST_CUTOFF) <= block_id <= (now + FUTURE_CUTOFF):
                    logging.error(f'Block {block_id} in the past')
                    return
                assert size <= MAX_OBJECT_SIZE
                data = await reader.readexactly(size)
                assert crypto.crc32c(data) == crc
                write_block(base_path, block_id, data)
                m = ser_write_cert(block_id, rk)
                writer.write(m)

            elif kind == b'c':
                # CRC block (return CRC instead of data, no MAC is required)
                # block_id -> (size, crc)
                block_id = deser_crc_block(kind + (await reader.readexactly(8)))
                data = fetch_block(base_path, block_id)
                m = ser_crc_response(len(data), crypto.crc32c(data))
                writer.write(m)

            elif kind == b's':
                # status report (return a MAC so they can verify we are the correct process)
                token = deser_stats(kind + (await reader.readexactly(8)))
                used_bytes, free_bytes, used_n, free_n = get_stats(base_path)
                m = ser_stats(token, used_bytes, free_bytes, used_n, free_n, rk)
                writer.write(m)

            elif kind == b'':
                # EOF
                return

            else:
                # unknown request type
                assert False
    finally:
        writer.close()


###########################################
# initialization
###########################################

async def periodically_register(key: bytes, port: int, storage_class: int) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    m = struct.pack('<16sHB', key, port, storage_class)
    shuckle = ('localhost', 5000)
    while True:
        sock.sendto(m, socket.MSG_DONTWAIT, shuckle)
        await asyncio.sleep(1)

async def async_main(*, path: str, port: int, storage_class: int) -> None:
    assert (2 <= storage_class <= 255), f'Storage class {storage_class} out of range'

    # open the key file and ensure we are exclusive
    fp = open(Path(path) / 'secret.key', 'a+b')
    fp.seek(0)
    try:
        fcntl.flock(fp, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as e:
        logging.error(f'Another block service is already bound to {path}')
        sys.exit(1)

    # fetch the secret key, creating it if needed
    key = fp.read()
    if len(key) == 0:
        key = secrets.token_bytes(16)
        fp.write(key)
        fp.flush()
        os.fsync(fp)
    else:
        assert len(key) == 16
    logging.info(f'Using secret key {key.hex()}')
    rk = crypto.aes_expand_key(key)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(('localhost', port))
    _, port = sock.getsockname()
    logging.info(f'Block service {path} bound to port {port}')

    # create the server (starts listening for requests immediately)
    server = await asyncio.start_server(lambda rs, ws: handle_client(rs, ws, rk, Path(path)), sock=sock)

    # run forever
    await asyncio.gather(
        periodically_register(key, port, storage_class),
        server.serve_forever())

def main(*, path: str, port: int, storage_class: int) -> None:
    asyncio.run(async_main(path=path, port=port, storage_class=storage_class))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Block service')
    parser.add_argument('path', help='Path to the root of the storage partition', type=Path)
    parser.add_argument('--storage_class', help='Storage class byte', type=int, default=2)
    config = parser.parse_args()

    # Arbitrary port here
    main(path=config.path, port=0, storage_class=config.storage_class)