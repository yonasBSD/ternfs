#!/usr/bin/env python3

import trio
import pyfuse3
import socket
import argparse
import logging
from typing import cast
import errno
import os
import struct

from common import *
from shard_msgs import *
from cdc_msgs import *
from msgs import *
from error import *
import crypto as crypto


LOCAL_HOST = '127.0.0.1'

# No mirroring for now, 4k blocks, just so that we create many blocks.
PARITY = create_parity_mode(1, 0)
STORAGE_CLASS = 2

async def send_shard_request(shard: int, req_body: ShardRequestBody, timeout_secs: float = 2.0) -> Union[EggsError, ShardResponseBody]:
    port = shard_to_port(shard)
    request_id = eggs_time()
    target = (LOCAL_HOST, port)
    req = ShardRequest(request_id=request_id, body=req_body)
    packed_req = req.pack()
    with trio.socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        await sock.bind(('', 0))
        await sock.sendto(packed_req, target)
        timed_out = True
        with trio.move_on_after(timeout_secs):
            packed_resp = await sock.recv(UDP_MTU)
            timed_out = False
        if timed_out:
            return EggsError(ErrCode.TIMEOUT)
    logging.debug(f'Sent shard request {req}')
    response = bincode.unpack(ShardResponse, packed_resp)
    logging.debug(f'Got shard response {response}')
    assert request_id == response.request_id
    return response.body

async def send_cdc_request(req_body: CDCRequestBody, timeout_secs: float = 2.0) -> Union[EggsError, CDCResponseBody]:
    request_id = eggs_time()
    target = (LOCAL_HOST, CDC_PORT)
    req = CDCRequest(request_id=request_id, body=req_body)
    packed_req = bincode.pack(req)
    with trio.socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        await sock.bind(('', 0))
        await sock.sendto(packed_req, target)
        timed_out = True
        with trio.move_on_after(timeout_secs):
            packed_resp = await sock.recv(UDP_MTU)
            timed_out = False
        if timed_out:
            return EggsError(ErrCode.TIMEOUT)
    response = bincode.unpack(CDCResponse, packed_resp)
    assert request_id == response.request_id
    return response.body

async def read_block(block: FetchedBlock) -> bytes:
    ip = socket.inet_ntoa(block.ip)
    msg = struct.pack('<cQ', b'f', block.block_id)
    conn = await trio.open_tcp_stream(ip, block.port) # TODO how promptly is this closed?
    await conn.send_all(msg)
    reply = await conn.receive_some(5)
    assert len(reply) == 5
    kind, ret_block_sz = struct.unpack('<cI', reply)
    if kind != b'F':
        raise Exception(f'Bad reply {reply!r}')
    if ret_block_sz != block.size:
        raise Exception(f'Block {block.block_id} inconsistent size: metadata says {block.size}, block service says {ret_block_sz}')
    # TODO check crc32
    data = b''
    while len(data) < block.size:
        data += await conn.receive_some(block.size - len(data))
    return data

# Writes block, returns proof
async def write_block(*, block: BlockInfo, data: bytes, crc32: bytes) -> bytes:
    header = struct.pack('<cQ4sI8s', b'w', block.block_id, crc32, len(data), block.certificate)
    conn = await trio.open_tcp_stream(socket.inet_ntoa(block.ip), block.port)
    await conn.send_all(header + data)
    resp = await conn.receive_some(17)
    assert len(resp) == 17
    proof: bytes
    rkind, rblock_id, proof = struct.unpack('<cQ8s', resp)
    if rkind != b'W':
        raise RuntimeError(f'Unexpected response kind {rkind} {resp!r}')
    if rblock_id != block.block_id:
        raise RuntimeError(f'Bad block id, expected {block.block_id} got {rblock_id}')
    return proof

def inode_type_to_mode(type: InodeType) -> int:
    assert type != InodeType.RESERVED
    mode = 0
    mode |= stat.S_IFREG if (type == InodeType.FILE) else 0
    mode |= stat.S_IFLNK if (type == InodeType.SYMLINK) else 0
    mode |= stat.S_IFDIR if (type == InodeType.DIRECTORY) else 0
    # This filesystem is read only, and permissionless.
    mode |= stat.S_IRUSR | stat.S_IXUSR
    mode |= stat.S_IRGRP | stat.S_IXGRP
    mode |= stat.S_IROTH | stat.S_IXOTH
    return mode

TIMEOUT_SECS = 2.0

def entry_attribute(inode_id: int, size: int, mtime: int) -> pyfuse3.EntryAttributes:
    type = inode_id_type(inode_id)
    assert type != InodeType.RESERVED

    entry = pyfuse3.EntryAttributes()
    entry.st_ino = pyfuse3.ROOT_INODE if inode_id == ROOT_DIR_INODE_ID else inode_id
    entry.generation = 0
    entry.entry_timeout = 0
    entry.attr_timeout = 0
    entry.st_mode = inode_type_to_mode(type)
    entry.st_nlink = 1
    entry.st_uid = 0
    entry.st_gid = 0
    entry.st_rdev = 0
    entry.st_size = (type != InodeType.DIRECTORY) and size

    entry.st_blksize = 512
    entry.st_blocks = 1 # TODO can we return something reasonable here?
    entry.st_atime_ns = 0
    entry.st_mtime_ns = EGGS_EPOCH + mtime
    entry.st_ctime_ns = EGGS_EPOCH + mtime

    return entry

def fuse_id_to_eggs_id(inode):
    if inode == pyfuse3.ROOT_INODE:
        inode = ROOT_DIR_INODE_ID
    return inode

def file_info(file_id):
    return pyfuse3.FileInfo(
        fh=file_id,
        direct_io=True, # Better for debugging
        keep_cache=False, # Better for debugging
        nonseekable=False,
    )

@dataclass
class FileUnderConstruction:
    dir_id: int
    name: bytes
    cookie: int

class Operations(pyfuse3.Operations):
    # From inode, to eventual destination.
    _files_under_construction: Dict[int, FileUnderConstruction] = {}

    def __init__(self):
        super().__init__()

    async def _send_shard_req(self, shard: int, req: ShardRequestBody) -> ShardResponseBody:
        resp = await send_shard_request(shard, req)
        if isinstance(resp, EggsError):
            raise pyfuse3.FUSEError(ERR_CODE_TO_ERRNO[resp.error_code])
        return resp
    
    async def _send_cdc_req(self, req: CDCRequestBody) -> CDCResponseBody:
        resp = await send_cdc_request(req)
        if isinstance(resp, EggsError):
            raise pyfuse3.FUSEError(ERR_CODE_TO_ERRNO[resp.error_code])
        return resp

    async def getattr(self, inode_id, ctx=None):
        inode_id = fuse_id_to_eggs_id(inode_id)
        resp = cast(StatResp, await self._send_shard_req(inode_id_shard(inode_id), StatReq(inode_id)))
        return entry_attribute(inode_id=inode_id, size=resp.size_or_owner, mtime=resp.mtime)

    async def opendir(self, inode_id, ctx=None):
        inode_id = fuse_id_to_eggs_id(inode_id)
        assert inode_id_type(inode_id) == InodeType.DIRECTORY
        return inode_id
    
    async def releasedir(self, dir_id):
        pass

    async def readdir(self, dir_id, continuation_key, token):
        assert inode_id_type(dir_id) == InodeType.DIRECTORY
        while True:
            resp = cast(ReadDirResp, await self._send_shard_req(inode_id_shard(dir_id), ReadDirReqNow(dir_id, continuation_key)))
            for result in resp.results:
                # FUSE (or at least pyfuse) expects the mtime/ctime here
                entry = await self.getattr(result.target_id)
                more = pyfuse3.readdir_reply(
                    token,
                    result.name,
                    entry,
                    # TODO this is not quite right, since there might be collisions.
                    # See contract for readdir here: http://www.rath.org/pyfuse3-docs/fuse_api.html#pyfuse3.readdir_reply
                    # But probably good enough for this toy implementation
                    result.name_hash + 1,
                )
                if not more:
                    return
            continuation_key = resp.next_hash
            if continuation_key == 0:
                return

    async def lookup(self, dir_id, name, ctx):
        dir_id = fuse_id_to_eggs_id(dir_id)
        assert inode_id_type(dir_id) == InodeType.DIRECTORY
        inode_id = cast(LookupResp, await self._send_shard_req(inode_id_shard(dir_id), LookupReq(dir_id, name))).target_id
        return await self.getattr(inode_id)
    
    async def read(self, file_id, offset, size):
        assert inode_id_type(file_id) in (InodeType.FILE, InodeType.SYMLINK)
        data = b''
        while len(data) < size:
            spans = cast(FileSpansResp, await self._send_shard_req(inode_id_shard(file_id), FileSpansReq(file_id, offset)))
            for span in spans.spans:
                span_data: bytes
                if len(data) >= size: break
                if span.storage_class == ZERO_FILL_STORAGE:
                    assert not span.body_bytes
                    assert not span.body_blocks
                    span_data = b'\0' * span.size
                elif span.storage_class == INLINE_STORAGE:
                    assert not span.body_blocks
                    span_data = span.body_bytes
                else:
                    assert not span.body_bytes
                    assert len(span.body_blocks) == 1
                    if span.body_blocks[0].flags & (BlockFlags.STALE | BlockFlags.TERMINAL):
                        raise pyfuse3.FUSEError(errno.EIO) # TODO better error code?
                    span_data = await read_block(span.body_blocks[0])
                # we might be in the middle of a span
                data += span_data[max(0, offset-span.byte_offset):]
            offset = spans.next_offset
            if offset == 0:
                break
        data = data[:size] # We might have overshoot
        return data

    async def open(self, file_id, flags, ctx=None):
        # FUSE splits open/create for us
        assert not (flags & os.O_CREAT)
        file_id = fuse_id_to_eggs_id(file_id)
        return file_info(file_id)

    async def _create(self, dir_id: int, type: InodeType, name: bytes):
        assert type in (InodeType.FILE, InodeType.SYMLINK)
        resp = cast(ConstructFileResp, await self._send_shard_req(inode_id_shard(dir_id), ConstructFileReq(type, name)))
        self._files_under_construction[resp.id] = FileUnderConstruction(dir_id=dir_id, name=name, cookie=resp.cookie)
        return resp.id

    async def create(self, dir_id, name, mode, flags, ctx=None):
        # TODO do something with mode?
        dir_id = fuse_id_to_eggs_id(dir_id)
        assert (flags & os.O_CREAT)
        file_id = await self._create(dir_id, InodeType.FILE, name)
        return (file_info(file_id), entry_attribute(file_id, size=0, mtime=0))
    
    async def write(self, file_id, offset, buf):
        under_construction = self._files_under_construction.get(file_id)
        if under_construction is None:
            # This is when we try to open an existing file for writing
            raise pyfuse3.FUSEError(errno.EROFS)
        # Split in 4k chunks
        for ix in range(0, len(buf), 1<<12):
            data = buf[ix:ix+(1<<12)]
            crc32 = crypto.crc32c(data)
            size = len(data)
            span = cast(AddSpanInitiateResp, await self._send_shard_req(inode_id_shard(file_id), AddSpanInitiateReq(
                file_id=file_id,
                cookie=under_construction.cookie,
                byte_offset=offset+ix,
                storage_class=STORAGE_CLASS,
                parity=PARITY,
                crc32=crc32,
                size=size,
                body_blocks=[NewBlockInfo(crc32, size)],
                body_bytes=b'',
            )))
            assert len(span.blocks) == 1
            block = span.blocks[0]
            block_proof = await write_block(block=block, data=data, crc32=crc32)
            await self._send_shard_req(
                inode_id_shard(file_id),
                AddSpanCertifyReq(
                    file_id=file_id,
                    cookie=under_construction.cookie,
                    byte_offset=offset+ix,
                    proofs=[block_proof],
                )
            )
        return len(buf)

    async def flush(self, file_id):
        under_construction = self._files_under_construction.get(file_id)
        if under_construction is None:
            return
        # Create the transient file
        await self._send_shard_req(
            inode_id_shard(under_construction.dir_id),
            LinkFileReq(
                file_id=file_id,
                cookie=under_construction.cookie,
                owner_id=under_construction.dir_id,
                name=under_construction.name
            )
        )

    async def release(self, file_id):
        pass

    async def mkdir(self, owner_id, name, mode, ctx=None):
        owner_id = fuse_id_to_eggs_id(owner_id)
        # TODO do something with the noode?
        resp = cast(MakeDirectoryResp, await self._send_cdc_req(MakeDirectoryReq(owner_id, name)))
        return entry_attribute(resp.id, size=0, mtime=0)
    
    async def setattr(self, inode_id, attr, fields, fh, ctx=None):
        inode_id = fuse_id_to_eggs_id(inode_id)
        return entry_attribute(inode_id, size=0, mtime=0)

    async def rename(self, parent_inode_old, name_old, parent_inode_new, name_new, flags, ctx=None):
        if flags & (pyfuse3.RENAME_EXCHANGE | pyfuse3.RENAME_NOREPLACE):
            raise pyfuse3.FUSEError(errno.EINVAL)
        parent_inode_old = fuse_id_to_eggs_id(parent_inode_old)
        parent_inode_new = fuse_id_to_eggs_id(parent_inode_new)
        target_id = cast(LookupResp, await self._send_shard_req(inode_id_shard(parent_inode_old), LookupReq(parent_inode_old, name_old))).target_id
        if parent_inode_old == parent_inode_new:
            await self._send_shard_req(inode_id_shard(parent_inode_old), SameDirectoryRenameReq(target_id, parent_inode_old, name_old, name_new))
        elif inode_id_type(target_id) == InodeType.DIRECTORY:
            await self._send_cdc_req(RenameDirectoryReq(target_id, parent_inode_old, name_old, parent_inode_new, name_new))
        else:
            await self._send_cdc_req(RenameFileReq(target_id, parent_inode_old, name_old, parent_inode_new, name_new))
        
    async def unlink(self, parent_inode, name, ctx=None):
        parent_inode = fuse_id_to_eggs_id(parent_inode)
        target_id = cast(LookupResp, await self._send_shard_req(inode_id_shard(parent_inode), LookupReq(parent_inode, name))).target_id
        await self._send_shard_req(inode_id_shard(parent_inode), SoftUnlinkFileReq(parent_inode, target_id, name))
    
    async def rmdir(self, parent_inode, name, ctx=None):
        parent_inode = fuse_id_to_eggs_id(parent_inode)
        target_id = cast(LookupResp, await self._send_shard_req(inode_id_shard(parent_inode), LookupReq(parent_inode, name))).target_id
        await self._send_cdc_req(SoftUnlinkDirectoryReq(parent_inode, target_id, name))
    
    async def symlink(self, parent_inode, name, target, ctx=None):
        parent_inode = fuse_id_to_eggs_id(parent_inode)
        file_id = await self._create(parent_inode, InodeType.SYMLINK, name)
        await self.write(file_id, 0, target)
        await self.flush(file_id)
        return entry_attribute(file_id, size=0, mtime=0)        
    
    async def readlink(self, file_id, ctx=None):
        file_id = fuse_id_to_eggs_id(file_id)
        assert inode_id_type(file_id) == InodeType.SYMLINK
        size = cast(StatResp, await self._send_shard_req(inode_id_shard(file_id), StatReq(file_id))).size_or_owner
        data = b''
        while len(data) < size:
            data += await self.read(file_id, len(data), size - len(data))
        return data

def init_logging(debug=False):
    formatter = logging.Formatter('%(asctime)s.%(msecs)03d %(threadName)s: [%(name)s] %(message)s', datefmt="%Y-%m-%d %H:%M:%S")
    handler = logging.StreamHandler()
    handler.setFormatter(formatter)
    root_logger = logging.getLogger()
    if debug:
        handler.setLevel(logging.DEBUG)
        root_logger.setLevel(logging.DEBUG)
    else:
        handler.setLevel(logging.INFO)
        root_logger.setLevel(logging.INFO)
    root_logger.addHandler(handler)

def parse_args():
    '''Parse command line'''

    parser = argparse.ArgumentParser()

    parser.add_argument('mountpoint', type=str, help='Where to mount the file system')
    parser.add_argument('--debug', action='store_true', default=False, help='Enable debugging output')
    parser.add_argument('--debug-fuse', action='store_true', default=False, help='Enable FUSE debugging output')

    return parser.parse_args()

if __name__ == '__main__':
    options = parse_args()
    init_logging(options.debug)
    operations = Operations()

    fuse_options = set(pyfuse3.default_options)
    fuse_options.add('fsname=eggsfs')
    fuse_options.discard('default_permissions')
    if options.debug_fuse:
        fuse_options.add('debug')
    pyfuse3.init(operations, options.mountpoint, fuse_options)

    try:
        trio.run(pyfuse3.main)
    except:
        pyfuse3.close()
        raise

    pyfuse3.close()
