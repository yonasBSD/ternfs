#!/usr/bin/env python

from quart import Quart, request, g, jsonify, render_template, Response
from typing import Dict, Tuple, Deque, Optional
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

app = Quart(__name__)

###########################################
# device polling and registration
###########################################

# queue of keys that we should check and upsert in the db
# push None to shutdown the cororoutine
KEYS_TO_CHECK: Deque[bytes] = collections.deque()
KEY_TO_IP_PORT: Dict[bytes, Tuple[str, int]] = {}

DB = sqlite3.connect('database.db')
DB.row_factory = sqlite3.Row

EPOCH = datetime.datetime(2020, 1, 1)
MAX_STALE_SECS = 600

# verify we can contact a device, if we can update the database
async def check_one_device(secret_key: bytes, ip: str, port: int) -> None:
    now = int((datetime.datetime.utcnow() - EPOCH).total_seconds())

    # connect to the device and fetch the stats
    rk = crypto.aes_expand_key(secret_key)
    try:
        reader, writer = await asyncio.open_connection(ip, port)
    except ConnectionRefusedError:
        return
    token = secrets.token_bytes(8)
    writer.write(b's' + token)
    m = await reader.readexactly(49)
    m2 = crypto.remove_mac(m, rk)
    assert m2 is not None, 'bad mac'
    kind, got_token, used_bytes, free_bytes, used_n, free_n = struct.unpack('<c8sQQQQ', m2)
    assert kind == b'S', 'bad response kind'
    assert got_token == token, 'token mismatch'

    # put the information into the database
    # (either creating a new record or updating an old one
    DB.execute("""
        insert into block_services (secret_key, ip_port, last_check, used_bytes, free_bytes, used_n, free_n)
        values (?, ?, ?, ?, ?, ?, ?)
        on conflict (secret_key) do update set ip_port=?, last_check=?, used_bytes=?, free_bytes=?, used_n=?, free_n=?
    """, (secret_key.hex(), f'{ip}:{port}', now, used_bytes, free_bytes, used_n, free_n, f'{ip}:{port}', now, used_bytes, free_bytes, used_n, free_n))
    DB.commit()

    # given we have updated the database we should ensure this is getting covered by periodic checks
    # and that it's being checked with the right ip/port
    if secret_key not in KEY_TO_IP_PORT:
        KEYS_TO_CHECK.append(secret_key) # it's brand new
    KEY_TO_IP_PORT[secret_key] = (ip, port)


async def check_devices_forever() -> None:
    # first fetch all existing devices from the database
    c = DB.execute('select secret_key, ip_port from block_services')
    for key, ip_port in c.fetchall():
        ip, port = ip_port.split(':')
        port = int(port)
        key = bytes.fromhex(key)
        KEYS_TO_CHECK.append(key)
        KEY_TO_IP_PORT[key] = (ip, port)
    app.logger.info(f'Started periodic check routine, loaded {len(KEYS_TO_CHECK)} devices')
    # now repeatedly take a device from the front of the queue and check it
    while True:
        await asyncio.sleep(1)
        if not KEYS_TO_CHECK: continue
        KEYS_TO_CHECK.rotate(-1)
        key = KEYS_TO_CHECK[-1]
        ip, port = KEY_TO_IP_PORT[key]
        try:
            #app.logger.info(f'Running periodic check for {ip}:{port}')
            await check_one_device(key, ip, port)
        except asyncio.CancelledError:
            return
        except Exception as e:
            #app.logger.error(f'Failed to check device at {ip}:{port}', exc_info=True)
            pass

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
            key, tcp_port = struct.unpack('<16sH', data)
            if key not in KEYS_TO_CHECK or KEY_TO_IP_PORT[key] != (ip, tcp_port):
                # it's new or changed ip - need to check it
                await check_one_device(key, ip, tcp_port)
        except asyncio.CancelledError:
            return
        except Exception as e:
            app.logger.error(f'Error processing registration for {ip}:{tcp_port}', exc_info=True)


###########################################
# web ui
###########################################

HOST_CACHE: Dict[str, str] = {}

@app.template_filter('resolve')
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

@app.route('/')
async def index() -> str:
    # TODO need to render in red if stale
    c = DB.execute('select * from block_services')
    return await render_template('index.html', devices=list(c.fetchall()))

@app.route('/change_status', methods=['POST'])
async def change_status() -> Response:
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
        # * -> terminal
        # TODO ensure the device is stale before we allow this
        DB.execute("update block_services set status='terminal' where id=?", (id,))
        DB.commit()
    else:
        assert False
    return jsonify({})

def calc_weights(used_bytes: np.array, total_bytes: np.array, simulate_add_frac: float = 0.2) -> np.array:
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
    w = bytes_to_add * (1.0 / simulate_add_frac)
    return w

@app.route('/show_me_what_you_got')
async def list_devices() -> Response:
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
    c = DB.execute("select id, ip_port, last_check, status, secret_key from block_services")
    ret = []
    for id, ip_port, last_check, status, secret_key in c.fetchall():
        is_stale = last_check <= now - MAX_STALE_SECS
        if is_stale or status != 'ok': assert id not in id_to_w
        ret.append({
            'ip': ip_port.split(':')[0],
            'port': int(ip_port.split(':')[1]),
            'is_terminal': status == 'terminal',
            'is_stale': is_stale,
            'write_weight': id_to_w.get(id, 0.0),
            'secret_key': secret_key,
        })
    return jsonify(ret)


###########################################
# initialization
###########################################

def init_db() -> None:
    db = sqlite3.connect('database.db')
    db.execute("""
        create table if not exists block_services (
            id integer primary key,
            secret_key text unique not null,
            status integer not null default "ok",
            ip_port text,
            last_check integer not null,
            used_bytes integer not null,
            free_bytes integer not null,
            used_n integer not null,
            free_n integer not null
        )
    """)
    db.commit()

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

if __name__ == '__main__':
    init_db()
    app.run('0.0.0.0', 5000, debug=True)
