#!/usr/bin/env python

from quart import Quart, request, g, jsonify, render_template, Response
from typing import cast, Dict, Tuple, Deque, Optional
import asyncio
import collections
import crypto
import datetime
import humanize
import numpy as np
import scipy.optimize
import secrets
import socket
import sqlite3
import struct
import argparse
import sys
import os
import logging
from pathlib import Path

from shard_msgs import STORAGE_CLASSES

app = Quart(__name__)

###########################################
# device polling and registration
###########################################

# queue of keys that we should check and upsert in the db
# push None to shutdown the cororoutine
KEYS_TO_CHECK: Deque[bytes] = collections.deque()
KEY_TO_IP_PORT_SC: Dict[bytes, Tuple[str, int, int, str]] = {}

DB: sqlite3.Connection

EPOCH = datetime.datetime(2020, 1, 1)
MAX_STALE_SECS = 600

# verify we can contact a device, if we can update the database
async def check_one_device(id: int, secret_key: bytes, ip: str, port: int, sc: int, failure_domain: str) -> None:
    global DB

    now = int((datetime.datetime.utcnow() - EPOCH).total_seconds())

    # connect to the device and fetch the stats
    rk = crypto.aes_expand_key(secret_key)
    token = secrets.token_bytes(8)
    try:
        reader, writer = await asyncio.open_connection(ip, port)
    except ConnectionRefusedError as err:
        app.logger.error(f'Could not connect to {(ip, port)}: {err}')
        return
    try:
        writer.write(struct.pack('<Q', id) + b's' + token)
        m = await reader.readexactly(8+49)
    finally:
        writer.close()
    m2 = crypto.remove_mac(m, rk)
    assert m2 is not None, 'bad mac'
    received_id, kind, got_token, used_bytes, free_bytes, used_n, free_n = struct.unpack('<Qc8sQQQQ', m2)
    assert id == received_id, 'bad received id'
    assert kind == b'S', 'bad response kind'
    assert got_token == token, 'token mismatch'

    # put the information into the database
    # (either creating a new record or updating an old one
    DB.execute("""
        insert into block_services (id, secret_key, ip_port, storage_class, failure_domain, last_check, used_bytes, free_bytes, used_n, free_n)
        values (:id, :key, :addr, :sc, :failure_domain, :now, :used_bytes, :free_bytes, :used_n, :free_n)
        on conflict (secret_key)
            do update set ip_port=:addr, storage_class=:sc, failure_domain=:failure_domain, last_check=:now, used_bytes=:used_bytes, free_bytes=:free_bytes, used_n=:used_n, free_n=:free_n
    """, {
        'id': id,
        'key': secret_key.hex(),
        'addr': f'{ip}:{port}',
        'sc': sc,
        'failure_domain': failure_domain,
        'now': now,
        'used_bytes': used_bytes,
        'free_bytes': free_bytes,
        'used_n': used_n,
        'free_n': free_n,
    })
    DB.commit()

    # given we have updated the database we should ensure this is getting covered by periodic checks
    # and that it's being checked with the right ip/port
    if secret_key not in KEY_TO_IP_PORT_SC:
        KEYS_TO_CHECK.append(secret_key) # it's brand new
    KEY_TO_IP_PORT_SC[secret_key] = (ip, port, sc, failure_domain)


async def check_devices_forever() -> None:
    global DB

    while True:
        c = DB.execute('select id, secret_key, ip_port, storage_class, failure_domain from block_services').fetchall()
        app.logger.info(f'Started periodic check routine, loaded {len(c)} devices')
        for id, key, ip_port, sc, failure_domain in c:
            ip, port = ip_port.split(':')
            port = int(port)
            key = bytes.fromhex(key)
            try:
                await check_one_device(id, key, ip, port, sc, failure_domain)
            except asyncio.CancelledError:
                return
            except Exception as e:
                app.logger.error(f'Failed to check device at {ip_port}: {e}', exc_info=True)
                pass
        await asyncio.sleep(60)

async def listen_for_registrations() -> None:
    # open a udp socket and listen for registration messages
    # if it's a new device or and ip/port change then we need to check it immediately
    loop = asyncio.get_running_loop()
    q: asyncio.Queue[Tuple[bytes, Tuple[str, int]]] = asyncio.Queue()
    class UdpTransport(asyncio.BaseProtocol):
        def connection_made(self, transport: asyncio.BaseTransport) -> None:
            pass
        def datagram_received(self, data: bytes, addr: Tuple[str, int]) -> None:
            q.put_nowait((data, addr))
    transport, protocol = await loop.create_datagram_endpoint(lambda: UdpTransport(), local_addr=('localhost', 5000))
    while True:
        try:
            data, (ip, port) = await q.get()
            id, key, tcp_port, sc, failure_domain = struct.unpack('<Q16sHB16s', data)
            failure_domain = failure_domain.decode('ascii').rstrip('\x00')
            if key not in KEYS_TO_CHECK or KEY_TO_IP_PORT_SC[key] != (ip, tcp_port, sc, failure_domain):
                # it's new or changed ip - need to check it
                await check_one_device(id, key, ip, tcp_port, sc, failure_domain)
        except asyncio.CancelledError:
            return
        except Exception as e:
            app.logger.error(f'Error processing registration for {ip}:{tcp_port}: {e}', exc_info=True) # type: ignore


###########################################
# web ui
###########################################

HOST_CACHE: Dict[str, str] = {}

# mypy doesn't like applying this decorator to an async def
@app.template_filter('resolve') # type: ignore[arg-type]
async def resolve_host(ip_port: str) -> str:
    ip, port = ip_port.split(':')
    if ip not in HOST_CACHE:
        loop = asyncio.get_running_loop()
        try:
            hostname, _, _ = await asyncio.wait_for(loop.run_in_executor(None, socket.gethostbyaddr, ip), timeout=1.0)
        except asyncio.TimeoutError:
            hostname = ip
        HOST_CACHE[ip] = hostname
    return f'{HOST_CACHE[ip]}:{port}'

@app.template_filter('dateformat')
def date_format(value: int) -> str:
    dt = EPOCH + datetime.timedelta(seconds=value)
    delta = datetime.datetime.utcnow() - dt
    return humanize.naturaldelta(delta) + ' ago'

@app.template_test('stale')
def is_stale(value: int) -> bool:
    now = int((datetime.datetime.utcnow() - EPOCH).total_seconds())
    return value <= now - MAX_STALE_SECS

@app.route('/')
async def index() -> str:
    global DB
    c = DB.execute('select * from block_services')
    items = [dict(row) | {'storage_class_str': STORAGE_CLASSES[row['storage_class']], 'id_str': f'0x{row["id"]:016X}'} for row in c]
    return await render_template('index.html', devices=items)

@app.route('/change_status', methods=['POST'])
async def change_status() -> Response:
    global DB
    body = await request.json
    action, id = body['action'], body['id']
    if action == 'drain':
        # normal -> drain
        DB.execute("update block_services set status='drain' where id=? and status='ok'", (id,))
        DB.commit()
    elif action == 'resume':
        # drain -> normal
        DB.execute("update block_services set status='ok' where id=? and status='drain'", (id,))
        DB.commit()
    elif action == 'terminate':
        # * -> terminal (only if it's stale!)
        now = int((datetime.datetime.utcnow() - EPOCH).total_seconds())
        DB.execute("update block_services set status='terminal' where id=? and last_check<=?", (id, now-MAX_STALE_SECS))
        DB.commit()
    else:
        assert False
    return jsonify({})

def calc_weights(used_bytes: np.ndarray, total_bytes: np.ndarray, simulate_add_frac: float = 0.2) -> np.ndarray:
    # linear program
    #   variables: target_fill_ratio, bytes_added (for each device) [both >= 0]
    #   objective: minimize sum(bytes_added)
    #   s.t. bytes_added_i >= target_fill_ratio * total_space_i - used_space_i
    #        sum(bytes_added_i) >= 0.2 * sum(capacity)
    # you might think this overflows once there is less than 20% space remaining, however
    # the alternative is pretty terrible as it would degenerate to extremely uneven fill
    # behavior
    assert total_bytes.shape == used_bytes.shape
    n, = used_bytes.shape
    scale = 1.0 / total_bytes.sum()

    c = np.zeros((n+1,))
    c[1:] = 1.0

    A_ub = np.zeros((n+1, n+1))
    A_ub[0,1:] = -1.0
    A_ub[1:,0] = total_bytes * scale
    np.fill_diagonal(A_ub[1:,1:], -1.0)

    b_ub = np.zeros((n+1,))
    b_ub[0] = -simulate_add_frac
    b_ub[1:] = used_bytes * scale

    ret = scipy.optimize.linprog(c, A_ub, b_ub)
    assert ret.success
    bytes_to_add = ret.x[1:]
    w = cast(np.ndarray, bytes_to_add * (1.0 / simulate_add_frac))
    return w

@app.route('/show_me_what_you_got')
async def list_devices() -> Response:
    global DB

    now = int((datetime.datetime.utcnow() - EPOCH).total_seconds())

    # first let's compute the weights
    c = DB.execute("select id, used_bytes, used_bytes + free_bytes from block_services where status='ok' and last_check>?", (now-MAX_STALE_SECS,))
    rows = list(c.fetchall())
    if rows:
        ids, used_bytes, total_bytes = zip(*rows)
        ws = calc_weights(np.array(used_bytes), np.array(total_bytes))
        id_to_w = {i: w for i, w in zip(ids, ws)}
    else:
        id_to_w = {}

    # build the response
    c = DB.execute("select id, ip_port, storage_class, failure_domain, last_check, status, secret_key from block_services")
    ret = []
    for id, ip_port, storage_class, failure_domain, last_check, status, secret_key in c.fetchall():
        is_stale = last_check <= now - MAX_STALE_SECS
        if is_stale or status != 'ok': assert id not in id_to_w
        ret.append({
            'id': id,
            'ip': ip_port.split(':')[0],
            'port': int(ip_port.split(':')[1]),
            'storage_class': storage_class,
            'failure_domain': failure_domain,
            'is_terminal': status == 'terminal',
            'is_stale': is_stale,
            'write_weight': id_to_w.get(id, 0.0),
            'secret_key': secret_key,
        })
    return jsonify(ret)


###########################################
# initialization
###########################################

def init_db(db_dir: str) -> None:
    global DB

    db_path = os.path.join(db_dir, f'shuckle.sqlite')
    logging.info(f'Running shuckle with db {db_path}')
    DB = sqlite3.connect(db_path)
    DB.row_factory = sqlite3.Row

    DB.execute("""
        create table if not exists block_services (
            id integer primary key,
            secret_key text unique not null,
            status integer not null default "ok",
            ip_port text unique not null,
            storage_class integer not null,
            failure_domain text not null,
            last_check integer not null,
            used_bytes integer not null,
            free_bytes integer not null,
            used_n integer not null,
            free_n integer not null
        )
    """)
    DB.commit()

CHECK_DEVICES_FOREVER_TASK: Optional['asyncio.Task[None]'] = None
LISTEN_FOR_REGISTRATIONS_TASK: Optional['asyncio.Task[None]'] = None

@app.before_serving
async def startup() -> None:
    global CHECK_DEVICES_FOREVER_TASK
    global LISTEN_FOR_REGISTRATIONS_TASK
    assert CHECK_DEVICES_FOREVER_TASK is None
    assert LISTEN_FOR_REGISTRATIONS_TASK is None
    CHECK_DEVICES_FOREVER_TASK = asyncio.create_task(check_devices_forever())
    LISTEN_FOR_REGISTRATIONS_TASK = asyncio.create_task(listen_for_registrations())

@app.after_serving
async def shutdown() -> None:
    assert CHECK_DEVICES_FOREVER_TASK is not None
    assert LISTEN_FOR_REGISTRATIONS_TASK is not None
    CHECK_DEVICES_FOREVER_TASK.cancel()
    LISTEN_FOR_REGISTRATIONS_TASK.cancel()

def main(db_path: str, port: int):
    init_db(db_path)
    logging.info(f'Running shuckle webapp at 0.0.0:{port}')
    app.run('0.0.0.0', port, debug=True, use_reloader=False)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Runs shuckle')
    parser.add_argument('db_dir', help='Directory to create or load db')
    parser.add_argument('--verbose', action='store_true')
    parser.add_argument('--log_file', type=Path)
    parser.add_argument('--port', type=int, default=5000)
    config = parser.parse_args(sys.argv[1:])

    log_level = logging.DEBUG if config.verbose else logging.WARNING
    if config.log_file:
        logging.basicConfig(filename=config.log_file, encoding='utf-8', level=log_level)
    else:
        logging.basicConfig(level=log_level)

    main(config.db_dir, config.port)
