#!/usr/bin/env python

import argparse
import asyncio
import crypto
import fcntl
import os
import pathlib
import requests
import secrets
import socket
import struct
import sys

def block_id_to_path(base_path, block_id):
    # 256 subdirectories
    block_id = block_id.to_bytes(8, 'little').hex()
    subdir = block_id[:2]
    filename = f'{block_id}.dat'
    return base_path / subdir / block_id

def fetch_block(base_path, block_id):
    # if it doesn't exist the connection gets closed
    path = block_id_to_path(base_path, block_id)
    return path.read_bytes()

def erase_block(base_path, block_id):
    # this cannot fail
    # (need to fsync the directory for prod version)
    path = block_id_to_path(base_path, block_id)
    path.unlink(missing_ok=True)

def write_block(base_path, block_id, data):
    # (need to fsync the directory for prod version)
    path = block_id_to_path(base_path, block_id)
    path.parent.mkdir(exist_ok=True)
    with path.open('xb') as fp:
        fp.write(data)
        fp.flush()
        os.fsync(fp.fileno())

def get_stats(base_path):
    # get number of blocks, bytes used and bytes available
    s = os.statvfs(base_path)
    free_bytes = s.f_bavail * s.f_bsize
    free_n = s.f_favail
    used_bytes = 0
    used_n = 0
    for i in range(256):
        path = base_path / i.to_bytes(2, 'little').hex()
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

def deser_erase_block(m, rk):
    # prefix=e, length=17
    m = crypto.remove_mac_fixed_length(m, rk)
    assert m is not None
    kind, block_id = struct.unpack('<cQ', m)
    assert kind == b'e'
    return block_id

def deser_fetch_block(m):
    # prefix=f, length=9
    kind, block_id = struct.unpack('<cQ', m)
    assert kind == b'f'
    return block_id

def deser_crc_block(m):
    # prefix=c, length=9
    kind, block_id = struct.unpack('<cQ', m)
    assert kind == b'c'
    return block_id

def deser_write_block(m, rk):
    # write block with specific CRC and size
    # prefix=w, length=25
    m = crypto.remove_mac_fixed_length(m, rk)
    assert m is not None
    kind, block_id, crc, size = struct.unpack('<cQII')
    assert kind == b'w'
    return block_id, crc, size

def deser_stats(m):
    # prefix=s, length=9
    kind, token = struct.unpack('<cQ', m)
    assert kind == b's'
    return token

def ser_erase_cert(block_id, rk):
    # confirm block_id was erased
    # prefix=E, length=9
    m = struct.pack('<cQ', b'E', block_id)
    return crypto.add_mac(m, rk)

def ser_fetch_response(size):
    # just return the four byte size followed by the data
    # prefix=F, length=5
    return struct.pack('<cI', b'F', size)

def ser_crc_response(size, crc):
    # return size and crc
    # prefix=C, length=9
    return struct.pack('<cII', b'C', size, crc)

def ser_write_cert(block_id):
    # confirm block was written
    # prefix=W, length=9
    m = struct.pack('<cQ', b'W', block_id)
    return crypto.add_mac(m, rk)

def ser_stats(token, used_bytes, free_bytes, used_n, free_n, rk):
    # prefix=S, length=41
    m = struct.pack('<cQQQQQ', b'S', token, used_bytes, free_bytes, used_n, free_n)
    return crypto.add_mac(m, rk)

###########################################
# main client handling code
###########################################

MAX_OBJECT_SIZE = 100e6

async def handle_client(reader, writer, rk, base_path):
    writer.get_extra_info('socket').setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b'\x00'*8)
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
            assert size <= MAX_OBJECT_SIZE
            data = reader.readexactly(size)
            assert crypto.crc32c(data) == crc
            write_block(base_path, block_id, data)
            m = ser_write_cert(block_id)
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

###########################################
# initialization
###########################################

async def periodically_register(key, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    m = struct.pack('<16sH', key, port)
    shuckle = ('localhost', 5000)
    while True:
        sock.sendto(m, socket.MSG_DONTWAIT, shuckle)
        await asyncio.sleep(60)

async def main():
    parser = argparse.ArgumentParser(description='Block service')
    parser.add_argument('path', help='Path to the root of the storage partition', type=pathlib.Path)
    parser.add_argument('--storage_class', help='Storage class byte', type=int, default=1)
    parser.add_argument('--coordinator', help='Address and port of the block service coordinator')
    config = parser.parse_args()

    # open the key file and ensure we are exclusive
    fp = open(config.path / 'secret.key', 'a+b')
    fp.seek(0)
    try:
        fcntl.flock(fp, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as e:
        print(f'Another block service is already bound to {config.path}', file=sys.stderr)
        sys.exit(1)

    # fetch the secret key, creating it if needed
    key = fp.read()
    if len(key) == 0:
        key = secrets.token_bytes(16)
        fp.write(key)
    else:
        assert len(key) == 16
    print(f'Using secret key {key.hex()}', file=sys.stderr)
    rk = crypto.aes_expand_key(key)

    # create a TCP socket on an arbitrary port
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.bind(('localhost', 0))
    _, port = sock.getsockname()
    print(f'Bound to port {port}', file=sys.stderr)

    # create the server (starts listening for requests immediately)
    server = await asyncio.start_server(lambda rs, ws: handle_client(rs, ws, rk, config.path), sock=sock)

    # run forever
    await asyncio.gather(
        periodically_register(key, port),
        server.serve_forever())

if __name__ == '__main__':
    asyncio.run(main())
