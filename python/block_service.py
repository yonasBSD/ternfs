#!/usr/bin/env python

from asyncio.log import logger
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
import traceback

import common
from shard_msgs import STORAGE_CLASSES_BY_NAME

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
    _, kind, block_id = struct.unpack('<QcQ', m2)
    assert kind == b'e'
    return cast(int, block_id)

def deser_fetch_block(m: bytes) -> int:
    # prefix=f, length=9
    _, kind, block_id = struct.unpack('<QcQ', m)
    assert kind == b'f'
    return cast(int, block_id)

def deser_crc_block(m: bytes) -> int:
    # prefix=c, length=9
    _, kind, block_id = struct.unpack('<QcQ', m)
    assert kind == b'c'
    return cast(int, block_id)

def deser_write_block(m: bytes, rk: crypto.ExpandedKey) -> Tuple[int, bytes, int]:
    # write block with specific CRC and size
    # prefix=w, length=25
    m2 = crypto.remove_mac(m, rk)
    assert m2 is not None
    _, kind, block_id, crc, size = struct.unpack('<QcQ4sI', m2)
    assert kind == b'w'
    return cast(int, block_id), cast(bytes, crc), cast(int, size)

def deser_stats(m: bytes) -> int:
    # prefix=s, length=9
    _, kind, token = struct.unpack('<QcQ', m)
    assert kind == b's'
    return cast(int, token)

def ser_erase_cert(block_service_id: int, block_id: int, rk: crypto.ExpandedKey) -> bytes:
    # confirm block_id was erased
    # prefix=E, length=9
    m = struct.pack('<QcQ', block_service_id, b'E', block_id)
    resp = crypto.add_mac(m, rk)
    return resp

def ser_fetch_response(block_service_id: int, size: int) -> bytes:
    # just return the four byte size followed by the data
    # prefix=F, length=5
    return struct.pack('<QcI', block_service_id, b'F', size)

def ser_crc_response(block_service_id: int, size: int, crc: bytes) -> bytes:
    # return size and crc
    # prefix=C, length=9
    return struct.pack('<QcI4s', block_service_id, b'C', size, crc)

def ser_write_cert(block_service_id: int, block_id: int, rk: crypto.ExpandedKey) -> bytes:
    # confirm block was written
    # prefix=W, length=9
    m = struct.pack('<QcQ', block_service_id, b'W', block_id)
    return crypto.add_mac(m, rk)

def ser_stats(block_service_id: int, token: int, used_bytes: int, free_bytes: int, used_n: int, free_n: int, rk: crypto.ExpandedKey) -> bytes:
    # prefix=S, length=41
    m = struct.pack('<QcQQQQQ', block_service_id, b'S', token, used_bytes, free_bytes, used_n, free_n)
    return crypto.add_mac(m, rk)

###########################################
# main client handling code
###########################################

MAX_OBJECT_SIZE = 100e6

ONE_HOUR_IN_NS = 60 * 60 * 1000 * 1000 * 1000
PAST_CUTOFF = ONE_HOUR_IN_NS * 22
FUTURE_CUTOFF = ONE_HOUR_IN_NS * 2

async def handle_client(*, reader: asyncio.StreamReader, writer: asyncio.StreamWriter, key: bytes, base_path: Path, time_check: bool) -> None:
    id = block_service_id(key)
    rk = crypto.aes_expand_key(key)
    writer.get_extra_info('socket').setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b'\x00'*8)
    try:
        while True:
            block_service_str = await reader.read(8)
            if block_service_str == b'':
                # EOF
                return
            received_block_service_id = struct.unpack('<Q', block_service_str)[0]
            # Right now we do not support multiplexing
            assert id == received_block_service_id, f'Expected id 0x{id:016X}, got 0x{received_block_service_id:016X}'
            kind = await reader.readexactly(1)
            if kind == b'e':
                # erase block
                # (block_id, mac) -> data
                block_id = deser_erase_block(block_service_str + kind + (await reader.readexactly(16)), rk)
                now = common.eggs_time()
                if time_check and block_id <= (now + FUTURE_CUTOFF):
                    logging.error(f'Block {block_id} is too recent to be deleted.')
                    return
                erase_block(base_path, block_id) # can never fail
                m = ser_erase_cert(id, block_id, rk)
                writer.write(m)

            elif kind == b'f':
                # fetch block (no MAC is required)
                # block_id -> data
                block_id = deser_fetch_block(block_service_str + kind + (await reader.readexactly(8)))
                data = fetch_block(base_path, block_id)
                m = ser_fetch_response(id, len(data))
                writer.write(m)
                writer.write(data)

            elif kind == b'w':
                # write block
                # (block_id, crc, size, mac) -> data
                block_id, crc, size = deser_write_block(block_service_str + kind + (await reader.readexactly(24)), rk)
                # check that block_id is inside expected range
                # this ensures we don't have "reply attack"-esque issues
                now = common.eggs_time()
                if time_check and (not (now - PAST_CUTOFF) <= block_id <= (now + FUTURE_CUTOFF)):
                    logging.error(f'Block {block_id} in the past')
                    return
                assert size <= MAX_OBJECT_SIZE
                data = await reader.readexactly(size)
                assert crypto.crc32c(data) == crc
                write_block(base_path, block_id, data)
                m = ser_write_cert(id, block_id, rk)
                writer.write(m)

            elif kind == b'c':
                # CRC block (return CRC instead of data, no MAC is required)
                # block_id -> (size, crc)
                block_id = deser_crc_block(block_service_str + kind + (await reader.readexactly(8)))
                data = fetch_block(base_path, block_id)
                m = ser_crc_response(id, len(data), crypto.crc32c(data))
                writer.write(m)

            elif kind == b's':
                # status report (return a MAC so they can verify we are the correct process)
                token = deser_stats(block_service_str + kind + (await reader.readexactly(8)))
                used_bytes, free_bytes, used_n, free_n = get_stats(base_path)
                m = ser_stats(id, token, used_bytes, free_bytes, used_n, free_n, rk)
                writer.write(m)

            else:
                # unknown request type
                assert False
    except Exception as err:
        logging.error(f'Got exception while processing request')
        traceback.print_exc()
    finally:
        writer.close()


###########################################
# initialization
###########################################

def block_service_id(secret_key: bytes) -> int:
    # we don't really care about leaking part or all of the key -- the whole key business is to defend
    # against bugs, not malicious agents.
    #
    # That said, we prefer to encode the knowledge of how to generate block service ids once, 
    # in the block service.
    #
    # Also, we remove the highest bit for the sake of SQLite, at least for now
    return struct.unpack('<Q', secret_key[:8])[0] & 0x7FFFFFFFFFFFFFFF

async def periodically_register(*, key: bytes, port: int, storage_class: int, failure_domain: str) -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    failure_domain_bs = failure_domain.encode('ascii')
    m = struct.pack('<Q16sHB16s', block_service_id(key), key, port, storage_class, failure_domain_bs)
    shuckle = ('localhost', 5000)
    while True:
        sock.sendto(m, socket.MSG_DONTWAIT, shuckle)
        await asyncio.sleep(1)

async def async_main(*, path: str, port: int, failure_domain: str, storage_class_str: str, time_check: bool) -> None:
    storage_class = STORAGE_CLASSES_BY_NAME[storage_class_str]
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

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(('localhost', port))
    _, port = sock.getsockname()
    logging.info(f'Block service {path} bound to port {port}')

    # create the server (starts listening for requests immediately)
    server = await asyncio.start_server(lambda rs, ws: handle_client(reader=rs, writer=ws, key=key, base_path=Path(path), time_check=time_check), sock=sock)

    # run forever
    await asyncio.gather(
        periodically_register(key=key, port=port, storage_class=storage_class, failure_domain=failure_domain),
        server.serve_forever())

def main(*, path: str, port: int, storage_class: str, failure_domain: str, time_check: bool) -> None:
    asyncio.run(async_main(path=path, port=port, failure_domain=failure_domain, storage_class_str=storage_class, time_check=time_check))

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Block service')
    parser.add_argument('path', help='Path to the root of the storage partition', type=Path)
    parser.add_argument('--storage_class', help='Storage class byte', type=str, default='HDD')
    parser.add_argument('--failure_domain', help='Failure domain', type=str)
    parser.add_argument('--no_time_check', action='store_true', type=bool, help='Do not perform block deletion time check (to prevent replay attacks). Useful for testing.')
    config = parser.parse_args()

    # Arbitrary port here
    main(path=config.path, port=0, storage_class=config.storage_class, failure_domain=config.failure_domain, time_check=not (config.no_time_check))