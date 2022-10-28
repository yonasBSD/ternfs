#!/usr/bin/env python3

import argparse
import collections
from datetime import timedelta
import socket
import struct
import sys
from typing import Callable, Dict, List, Optional, Tuple, Union, TypeVar
import typing
import logging
from pathlib import Path
import inspect
import re

import bincode
import crypto
from cdc_msgs import *
from shard_msgs import *
from common import *
from error import *

@dataclass
class CommandArg:
    name: str
    type: type
    default: Optional[Any]

@dataclass
class Command:
    fun: Any
    args: List[CommandArg]
    trailing_args: Optional[str]

commands: Dict[str, Command] = {}
raw_commands: Dict[str, Command] = {}

def command(cmd_name: str):
    global commands
    assert cmd_name not in commands
    def decorator(f):
        args: List[CommandArg] = []
        trailing_arsgs: Optional[str] = None
        for name, param in inspect.signature(f).parameters.items():
            if param.kind == param.VAR_POSITIONAL:
                trailing_arsgs = name
                continue
            assert param.kind == param.POSITIONAL_OR_KEYWORD
            assert param.annotation != param.empty
            typ: type = param.annotation
            # Strip optional
            if isinstance(typ, typing._UnionGenericAlias):
                typ = typ.__args__[0]
            default: Optional[Any] = None
            if param.default != param.empty:
                default = param.default
            args.append(CommandArg(name=name, type=typ, default=default))
        commands[cmd_name] = Command(fun=f, args=args, trailing_args=trailing_arsgs)
        def wrapper(*args, **kwargs):
            return f(*args, **kwargs)
        return wrapper
    return decorator

LOCAL_HOST = '127.0.0.1'

def send_shard_request(shard: int, req_body: ShardRequestBody, key: Optional[crypto.ExpandedKey] = None, timeout_secs: float = 2.0) -> Union[EggsError, ShardResponseBody]:
    assert (key is not None) == kind_is_privileged(req_body.KIND)
    port = shard_to_port(shard)
    request_id = eggs_time()
    target = (LOCAL_HOST, port)
    req = ShardRequest(request_id=request_id, body=req_body)
    packed_req = req.pack(key)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(timeout_secs)
        try:
            packed_resp = sock.recv(UDP_MTU)
        except socket.timeout:
            return EggsError(ErrCode.TIMEOUT)
    response = bincode.unpack(ShardResponse, packed_resp)
    assert request_id == response.request_id
    logging.debug(f'Sent shard request {req}')
    logging.debug(f'Got shard response {response}')
    return response.body

def send_shard_request_or_raise(shard: int, req_body: ShardRequestBody, key: Optional[crypto.ExpandedKey] = None, timeout_secs: float = 2.0) -> ShardResponseBody:
    resp = send_shard_request(shard, req_body, key, timeout_secs)
    if isinstance(resp, EggsError):
        raise resp
    return resp

def send_cdc_request(req_body: CDCRequestBody, timeout_secs: float = 2.0) -> Union[EggsError, CDCResponseBody]:
    request_id = eggs_time()
    target = (LOCAL_HOST, CDC_PORT)
    req = CDCRequest(request_id=request_id, body=req_body)
    packed_req = bincode.pack(req)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.bind(('', 0))
        sock.sendto(packed_req, target)
        sock.settimeout(timeout_secs)
        try:
            packed_resp = sock.recv(UDP_MTU)
        except socket.timeout:
            return EggsError(ErrCode.TIMEOUT)
    response = bincode.unpack(CDCResponse, packed_resp)
    assert request_id == response.request_id
    if isinstance(response.body, EggsError):
        print(f'Got error {response.body} for req {req_body}')
    return response.body

def send_cdc_request_or_raise(req_body: CDCRequestBody) -> CDCResponseBody:
    resp = send_cdc_request(req_body)
    if isinstance(resp, EggsError):
        raise resp
    return resp

def lookup_internal(dir_id: int, name: str) -> int:
    resp = send_shard_request_or_raise(inode_id_shard(dir_id), LookupReq(dir_id=dir_id, name=name.encode('ascii')))
    assert isinstance(resp, LookupResp)
    return resp.target_id

@command('lookup_raw')
def lookup_raw(dir_id: int, name: str):
    inode = lookup_internal(dir_id, name)
    print(f'inode id:    0x{inode:016X}')
    print(f'type:        {repr(inode_id_type(inode))}')
    print(f'shard:       {inode_id_shard(inode)}')

def print_dir_info(info: DirectoryInfoBody):
    print(f'  delete_after_time:      {timedelta(microseconds=info.delete_after_time/1000)}')
    print(f'  delete_after_versions:  {info.delete_after_versions}')
    print(f'  span policies:')
    print(f'    255: INLINE')
    for policy in info.span_policies:
        print(f'    {span_size_str(policy.max_size)}: {STORAGE_CLASSES.get(policy.storage_class, policy.storage_class)}, {parity_str(policy.parity)}')

def resolve_dir_info(id) -> Tuple[int, DirectoryInfoBody]:
    resp = send_shard_request_or_raise(inode_id_shard(id), StatDirectoryReq(id))
    assert isinstance(resp, StatDirectoryResp)
    if resp.info.body:
        return id, resp.info.body[0]
    else:
        return resolve_dir_info(resp.owner)

@command('stat_raw')
def stat_raw(id: int):
    if inode_id_type(id) == InodeType.DIRECTORY:
        resp = send_shard_request_or_raise(inode_id_shard(id), StatDirectoryReq(id))
        assert isinstance(resp, StatDirectoryResp)
        print(f'mtime:       {eggs_time_str(resp.mtime)}')
        print(f'owner:       0x{resp.owner:016X}')
        if resp.info.body:
            print(f'info:')
            print_dir_info(resp.info.body[0])
        else:
            print(f'info (inherited):')
            print(resp.info)
            inherited_from, info = resolve_dir_info(id)
            print(f'  inherited from:         0x{inherited_from:016X}')
            print_dir_info(info)
    else:
        resp = send_shard_request_or_raise(inode_id_shard(id), StatFileReq(id))
        assert isinstance(resp, StatFileResp)
        print(f'mtime:       {eggs_time_str(resp.mtime)}')
        print(f'size:        {resp.size}')
        print(f'transient:   {resp.transient}')
        if resp.transient:
            print(f'note:        {resp.note!r}')

@command('stat')
def do_stat(path: Path):
    return stat_raw(lookup(path))

def lookup_abs_path(path: Path):
    if path.parts[0] != '/':
        raise ValueError('Path must be absolute')
    cur_inode = ROOT_DIR_INODE_ID
    for part in path.parts[1:]:
        cur_inode = lookup_internal(cur_inode, part)
    return cur_inode

@command('lookup')
def lookup(path: Path) -> int:
    inode = lookup_abs_path(path)
    print(f'inode id:    0x{inode:016X}')
    print(f'type:        {repr(inode_id_type(inode))}')
    print(f'shard:       {inode_id_shard(inode)}')
    return inode

def mkdir_raw(owner_id: int, name: str) -> None:
    send_cdc_request_or_raise(MakeDirectoryReq(owner_id=owner_id, name=name.encode('ascii'), info=INHERIT_DIRECTORY_INFO))

@command('mkdir')
def mkdir(path: Path) -> None:
    owner_id = lookup_abs_path(path.parent)
    # before talking to the cross-dir co-ordinator, check whether this
    # subdirectory already exists.
    # this ensures we're not wasting the rename co-ordinator's time
    try:
        lookup_internal(owner_id, path.name)
        raise EggsError(ErrCode.CANNOT_OVERRIDE_NAME)
    except EggsError as err:
        if err.error_code != ErrCode.DIRECTORY_NOT_FOUND:
            raise err
    return mkdir_raw(owner_id, path.name)

@command('rm_file_raw')
def rm_file_raw(owner_id: int, file_id: int, name: str) -> None:
    send_shard_request_or_raise(inode_id_shard(owner_id), SoftUnlinkFileReq(owner_id, file_id, name.encode('ascii')))

@command('rm_file')
def rm_file(path: Path) -> None:
    dir_id = lookup_abs_path(path.parent)
    file_id = lookup_internal(dir_id, path.name)
    rm_file_raw(dir_id, file_id, path.name)

@command('rm_dir_raw')
def rm_dir_raw(owner_id: int, dir_id: int, name: str) -> None:
    send_cdc_request_or_raise(SoftUnlinkDirectoryReq(owner_id=owner_id, target_id=dir_id, name=name.encode('ascii')))

@command('rm_dir')
def rm_dir(path: Path) -> None:
    owner_id = lookup_abs_path(path.parent)
    dir_id = lookup_internal(owner_id, path.name)
    rm_dir_raw(owner_id, dir_id, path.name)

@command('same_dir_mv')
def same_dir_mv(target_id: int, dir_id: int, old: str, new: str) -> None:
    send_shard_request_or_raise(inode_id_shard(dir_id), SameDirectoryRenameReq(dir_id=dir_id, target_id=target_id, old_name=old.encode('ascii'), new_name=new.encode('ascii')))

@command('mv_raw')
def mv_raw(target_id: int, old_owner_id: int, old_name: str, new_owner_id: int, new_name: str):
    if inode_id_type(target_id) == InodeType.FILE:
        send_cdc_request_or_raise(RenameFileReq(target_id, old_owner_id, old_name.encode('ascii'), new_owner_id, new_name.encode('ascii')))
    else:
        send_cdc_request_or_raise(RenameDirectoryReq(target_id, old_owner_id, old_name.encode('ascii'), new_owner_id, new_name.encode('ascii')))

@command('mv')
def mv(a: Path, b: Path) -> None:
    owner_a = lookup_abs_path(a.parent)
    a_id = lookup_internal(owner_a, a.name)
    owner_b = lookup_abs_path(b.parent)
    if owner_a == owner_b:
        same_dir_mv(dir_id=owner_a, target_id=a_id, old=a.name, new=b.name)
    elif inode_id_type(a_id) == InodeType.FILE:
        send_cdc_request_or_raise(RenameFileReq(a_id, owner_a, a.name.encode('ascii'), owner_b, b.name.encode('ascii')))
    else:
        send_cdc_request_or_raise(RenameDirectoryReq(a_id, owner_a, a.name.encode('ascii'), owner_b, b.name.encode('ascii')))

@command('readdir_single')
def readdir_single(id: int, start_hash: int):
    print(send_shard_request_or_raise(inode_id_shard(id), ReadDirReqNow(id, start_hash)))

@command('readdir')
def readdir(id: int):
    continuation_key = 0
    while True:
        resp = send_shard_request_or_raise(inode_id_shard(id), ReadDirReqNow(id, continuation_key))
        assert isinstance(resp, ReadDirResp)
        for result in resp.results:
            s = result.name.decode("ascii") + ("/" if inode_id_type(result.target_id) == InodeType.DIRECTORY else "")
            print(f'{s: <20} 0x{result.target_id:016X}')
        continuation_key = resp.next_hash
        if continuation_key == 0:
            break

@command('full_readdir')
def full_readdir(id: int):
    start_hash = 0
    start_name = b''
    start_time = 0
    all_results: Dict[str, List[EdgeWithOwnership]] = collections.defaultdict(list)
    while True:
        resp = send_shard_request_or_raise(inode_id_shard(id), FullReadDirReq(id, start_hash, start_name, start_time))
        assert isinstance(resp, FullReadDirResp)
        for result in resp.results:
            all_results[result.name.decode('ascii')].append(result)
        if resp.finished:
            break
        start_hash = resp.results[-1].name_hash
        start_name = resp.results[-1].name
        start_time = resp.results[-1].creation_time+1
    for name, results in all_results.items():
        print(name)
        for result in results:
            typ_str = {
                InodeType.DIRECTORY: 'D',
                InodeType.FILE: 'F',
                InodeType.SYMLINK: 'S',
            }
            id = inode_id_strip_extra(result.target_id)
            ownership = 'O' if inode_id_extra(result.target_id) else ' '
            id_str = f'0x{id:016X}'
            if id == NULL_INODE_ID:
                print(f'    {ownership} {eggs_time_str(result.creation_time)} <deleted>')
            else:
                print(f'  {typ_str[inode_id_type(id)]} {ownership} {eggs_time_str(result.creation_time)} {id_str}')
        
@command('ls')
def ls(dir: Path) -> None:
    id = lookup(dir)
    print('')
    readdir(id)

@command('full_ls')
def full_ls(dir: Path) -> None:
    id = lookup(dir)
    print('')
    full_readdir(id)

def parse_duration(time_str: str) -> timedelta:
    regex = re.compile(r'((?P<weeks>\d+?)w)?((?P<days>\d+?)d)?((?P<hours>\d+?)hr)?((?P<minutes>\d+?)m)?((?P<seconds>\d+?)s)?')
    parts = regex.match(time_str)
    if parts is None:
        raise ValueError(f'Bad duration string {time_str}, expecting something of the form <weeks>w<days>d<hours>hr<minutes>m<seconds>s, all fields optional.')
    time_params = {}
    for name, param in parts.groupdict().items():
        if param:
            time_params[name] = int(param)
    return timedelta(**time_params)

def span_size_str(size: int) -> str:
    if size % (1<<20) == 0:
        return f'{size>>20}M'
    if size % (1<<10) == 0:
        return f'{size>>10}K'
    return str(size)

def parse_span_size(size_str: str) -> int:
    regex = re.compile(r'(?P<digits>[0-9]+)(?P<unit>M|K)?')
    parts = regex.match(size_str)
    if parts is None:
        raise ValueError(f'Bad size string {size_str}, expecting <size>M|K, where M stands for MiB and K for KiB. The M|K suffix is optional.')
    size = int(parts.groupdict()['digits'])
    unit = parts.groupdict().get('unit')
    if unit == 'M':
        size = size << 20
    if unit == 'K':
        size = size << 10
    if size % (1<<16) != 0:
        raise ValueError(f'size {size_str} is not a multiple of 64KiB')
    return size

def parity_str(parity: int) -> str:
    return f'{num_data_blocks(parity)}+{num_parity_blocks(parity)}'

def parse_parity(s: str) -> int:
    data, parity = s.split('+')
    return create_parity_mode(int(data), int(parity))

@command('set_directory_info_raw')
def set_directory_info_raw(id: int, delete_after_time: timedelta, delete_after_versions: int, *span_policies: str) -> None:
    if len(span_policies) < 1:
        raise ValueError('Expecting at least one span policy!')
    def bad_span_policy():
        raise ValueError(f'Span policies must be of the form <max-size> <storage-class> <data-blocks>+<parity-blocks>')
    if len(span_policies)%3 != 0:
        bad_span_policy()
    policies: List[SpanPolicy] = []
    last_size = 255
    for i in range(0, len(span_policies), 3):
        size_str, storage_class_str, parity_str = span_policies[i:i+3]
        size = parse_span_size(size_str)
        if size <= last_size:
            raise ValueError(f'Unsorted span policies')
        last_size = size
        storage_class = STORAGE_CLASSES_BY_NAME[storage_class_str]
        if storage_class in (ZERO_FILL_STORAGE, INLINE_STORAGE):
            raise ValueError(f'Reserved storage class {storage_class}')
        policies.append(SpanPolicy(size, storage_class, parse_parity(parity_str)))
    delete_after_time_ns = int(delete_after_time.total_seconds()) * 1000 * 1000 * 1000
    info = DirectoryInfo(False, [DirectoryInfoBody(delete_after_time_ns, delete_after_versions, policies)])
    send_shard_request_or_raise(inode_id_shard(id), SetDirectoryInfoReq(id, info))
    stat_raw(id)

@command('inherit_directory_info_raw')
def inherit_directory_info_raw(id: int) -> None:
    info = DirectoryInfo(False, [])
    send_shard_request_or_raise(inode_id_shard(id), SetDirectoryInfoReq(id, info))
    stat_raw(id)

@command('inherit_directory_info')
def inherit_directory_info(path: Path) -> None:
    id = lookup_abs_path(path)
    inherit_directory_info_raw(id)

@command('set_directory_info')
def set_directory_data(path: Path, delete_after_time: timedelta, delete_after_versions: int, *span_policies: str) -> None:
    id = lookup_abs_path(path)
    set_directory_info_raw(id, delete_after_time, delete_after_versions, *span_policies)

# Writes block, returns proof
def write_block(*, block: BlockInfo, data: bytes, crc32: bytes) -> bytes:
    header = struct.pack('<cQ4sI8s', b'w', block.block_id, crc32, len(data), block.certificate)
    with socket.create_connection((socket.inet_ntoa(block.ip), block.port)) as conn:
        conn.send(header + data)
        resp = conn.recv(17)
        assert len(resp) == 17
    proof: bytes
    rkind, rblock_id, proof = struct.unpack('<cQ8s', resp)
    if rkind != b'W':
        raise RuntimeError(f'Unexpected response kind {rkind} {resp!r}')
    if rblock_id != block.block_id:
        raise RuntimeError(f'Bad block id, expected {block.block_id} got {rblock_id}')
    return proof

def read_block(block: FetchedBlock) -> bytes:
    ip = socket.inet_ntoa(block.ip)
    msg = struct.pack('<cQ', b'f', block.block_id)
    with socket.create_connection((ip, block.port)) as conn:
        conn.send(msg)
        reply = conn.recv(5)
        assert len(reply) == 5
        kind, ret_block_sz = struct.unpack('<cI', reply)
        if kind != b'F':
            raise Exception(f'Bad reply {reply!r}')
        if ret_block_sz != block.size:
            raise Exception(f'Block {block.block_id} inconsistent size: metadata says {block.size}, block service says {ret_block_sz}')
        # TODO check crc32
        data = b''
        while len(data) < block.size:
            data += conn.recv(block.size - len(data))
        return data

# No mirroring for now, 4k blocks, just so that we create many blocks.
PARITY = create_parity_mode(1, 0)
STORAGE_CLASS = 2

def create_file(name: Path, blob: bytes):
    dir_id = lookup_abs_path(name.parent)
    shard = inode_id_shard(dir_id)
    transient_file = send_shard_request_or_raise(shard, ConstructFileReq(InodeType.FILE, str(name).encode('ascii')))
    assert isinstance(transient_file, ConstructFileResp)
    file_id = transient_file.id
    cookie = transient_file.cookie
    for ix in range(0, len(blob), 1<<12):
        data = blob[ix:ix+(1<<12)]
        crc32 = crypto.crc32c(data)
        size = len(data)
        span = send_shard_request_or_raise(
            shard,
            AddSpanInitiateReq(
                file_id=file_id,
                cookie=cookie,
                byte_offset=ix,
                storage_class=STORAGE_CLASS,
                parity=PARITY,
                crc32=crc32,
                size=size,
                body_blocks=[NewBlockInfo(crc32, size)],
                body_bytes=b'',
            )
        )
        assert isinstance(span, AddSpanInitiateResp)
        assert len(span.blocks) == 1
        block = span.blocks[0]
        block_proof = write_block(block=block, data=data, crc32=crc32)
        send_shard_request_or_raise(
            shard,
            AddSpanCertifyReq(
                file_id=file_id,
                cookie=cookie,
                byte_offset=ix,
                proofs=[BlockProof(block.block_id, block_proof)],
            )
        )
    send_shard_request_or_raise(shard, LinkFileReq(file_id, cookie, dir_id, name.name.encode('ascii')))

@command('echo_into')
def create_file_from_str(name: Path, str: str):
    return create_file(name, str.encode('utf-8'))

@command('copy_into')
def create_file_from_file(dest_path: Path, source_file: Path):
    with open(source_file, mode='rb') as f:
        return create_file(dest_path, f.read())

@command('cat')
def cat(name: Path):
    file_id = lookup_abs_path(name)
    byte_offset = 0
    while True:
        resp = send_shard_request_or_raise(inode_id_shard(file_id), FileSpansReq(file_id=file_id, byte_offset=byte_offset))
        assert isinstance(resp, FileSpansResp)
        for span in resp.spans:
            if span.storage_class == INLINE_STORAGE:
                sys.stdout.buffer.write(span.body_bytes)
            elif span.storage_class == ZERO_FILL_STORAGE:
                sys.stdout.buffer.write(b'\x00'*span.size)
            else:
                assert num_data_blocks(span.parity) == 1
                sys.stdout.buffer.write(read_block(span.body_blocks[0]))
        byte_offset = resp.next_offset
        if byte_offset == 0:
            break
    sys.stdout.flush()
            

@command('transient_files')
def transient_files():
    for shard in range(256):
        begin_id = 0
        while True:
            resp = send_shard_request_or_raise(shard, VisitTransientFilesReq(begin_id))
            assert isinstance(resp, VisitTransientFilesResp)
            for f in resp.files:
                print(f'inode id:    0x{f.id:016X}')
                print(f'cookie:      0x{f.cookie:016X}')
                print(f'deadline:    {eggs_time_str(f.deadline_time)}')
                print(f'note:        {f.note.decode("ascii")}')
                print(f'type:        {repr(inode_id_type(f.id))}')
                print(f'shard:       {shard}')
                print()
            begin_id = resp.next_id
            if begin_id == 0:
                break

def storage_class_str(i: int) -> str:
    if i == INLINE_STORAGE:
        return 'INLINE'
    elif i == ZERO_FILL_STORAGE:
        return 'ZERO_FILL'
    else:
        return STORAGE_CLASSES[i]

@command('file_spans_raw')
def file_spans_raw(file_id: int):
    byte_offset = 0
    while True:
        resp = send_shard_request_or_raise(inode_id_shard(file_id), FileSpansReq(file_id=file_id, byte_offset=byte_offset))
        assert isinstance(resp, FileSpansResp)
        for span in resp.spans:
            print(f'{span.byte_offset}:')
            print(f'  size:    {span_size_str(span.size)}')
            print(f'  storage: {storage_class_str(span.storage_class)}')
            print(f'  crc32c:  {span.crc32!r}')
            if span.storage_class == INLINE_STORAGE:
                print(f'  body:    {span.body_bytes!r}')
            elif span.storage_class != ZERO_FILL_STORAGE:
                print(f'  parity:  {parity_str(span.parity)}')
                print(f'  blocks:')
                for block in span.body_blocks:
                    print(f'    - ip:       {socket.inet_ntoa(block.ip)}')
                    print(f'      port:     {block.port}')
                    print(f'      block id: 0x{block.block_id:016X}')
                    print(f'      crc32c:   {block.crc32}')
                    print(f'      size:     {span_size_str(block.size)}')
                    print(f'      flags:    {format(block.flags, "08b")}')
        byte_offset = resp.next_offset
        if byte_offset == 0:
            break
    sys.stdout.flush()

@command('file_spans')
def file_spans(f: Path):
    file_id = lookup(f)
    print()
    file_spans_raw(file_id)

def main() -> None:
    args = sys.argv[1:]

    parser = argparse.ArgumentParser(description='EggsFS CLI')

    subparsers = parser.add_subparsers(dest='command')
    subparsers.required = True

    for name, cmd in commands.items():
        cmd_parser = subparsers.add_parser(name)
        for arg in cmd.args:
            extra = {}
            if arg.default:
                extra['default'] = arg.default
            if arg.type == int:
                cmd_parser.add_argument(arg.name, type=lambda x: int(x, 0), **extra)
            elif arg.type == timedelta:
                cmd_parser.add_argument(arg.name, type=parse_duration, **extra)
            elif arg.type == bool:
                cmd_parser.add_argument(f'--{arg.name}', action='store_true')
            else:
                cmd_parser.add_argument(arg.name, type=arg.type, **extra)
        if cmd.trailing_args:
            cmd_parser.add_argument(cmd.trailing_args, nargs='*')

    config = parser.parse_args(args)
    print(config)
    command = commands[config.command]
    args = [getattr(config, arg.name) for arg in command.args]
    if command.trailing_args:
        args += getattr(config, command.trailing_args)
    command.fun(*args)

if __name__ == '__main__':
    main()
