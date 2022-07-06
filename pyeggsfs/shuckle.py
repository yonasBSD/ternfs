#!/usr/bin/env python

# TODO provide a nice ui
#      provide an endpoint where we pretend to be dead devices and fake deletion certs
#      provide an endpoint to return write weights for devices

from quart import Quart, request, g, jsonify, render_template
import asyncio
import crypto
import datetime
import secrets
import sqlite3
import struct
import collections

app = Quart(__name__)

###########################################
# device polling and registration
###########################################

# queue of keys that we should check and upsert in the db
# push None to shutdown the cororoutine
# TODO when it becomes terminal we need to remove it!
KEYS_TO_CHECK = collections.deque()
KEY_TO_IP_PORT = {}

# verify we can contact a device, if we can update the database
async def check_one_device(secret_key, ip, port):
    now = int((datetime.datetime.utcnow() - datetime.datetime(2020, 1, 1)).total_seconds())

    # connect to the device and fetch the stats
    rk = crypto.aes_expand_key(secret_key)
    reader, writer = await asyncio.open_connection(ip, port)
    token = secrets.token_bytes(8)
    writer.write(b's' + token)
    m = await reader.readexactly(49)
    m = crypto.remove_mac(m, rk)
    assert m is not None, 'bad mac'
    kind, got_token, used_bytes, free_bytes, used_n, free_n = struct.unpack('<c8sQQQQ', m)
    assert kind == b'S', 'bad response kind'
    assert got_token == token, 'token mismatch'

    # put the information into the database
    # (either creating a new record or updating an old one
    app.db.execute("""
        insert into block_services (secret_key, ip, port, last_check, used_bytes, free_bytes, used_n, free_n)
        values (?, ?, ?, ?, ?, ?, ?, ?)
        on conflict (secret_key) do update set ip=?, port=?, last_check=?, used_bytes=?, free_bytes=?, used_n=?, free_n=?
    """, (secret_key.hex(), ip, port, now, used_bytes, free_bytes, used_n, free_n, ip, port, now, used_bytes, free_bytes, used_n, free_n))
    app.db.commit()

    # given we have updated the database we should ensure this is getting covered by periodic checks
    # and that it's being checked with the right ip/port
    if secret_key not in KEY_TO_IP_PORT:
        KEYS_TO_CHECK.append(secret_key) # it's brand new
    KEY_TO_IP_PORT[secret_key] = (ip, port)


async def check_devices_forever():
    # first fetch all existing devices from the database
    c = app.db.execute('select secret_key, ip, port from block_services where status != "terminal"')
    for key, ip, port in c.fetchall():
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
            app.logger.info(f'Running periodic check for {ip}:{port}')
            await check_one_device(key, ip, port)
        except asyncio.CancelledError:
            return
        except Exception as e:
            app.logger.error(f'Failed to check device at {ip}:{port}', exc_info=True)

async def listen_for_registrations():
    # open a udp socket and listen for registration messages
    # if it's a new device or and ip/port change then we need to check it immediately
    loop = asyncio.get_running_loop()
    q = asyncio.Queue()
    class UdpTransport:
        def connection_made(self, transport):
            pass
        def datagram_received(self, data, addr):
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

@app.template_filter('dateformat')
def date_format(value):
    dt = datetime.datetime(2020, 1, 1) + datetime.timedelta(seconds=value)
    return dt.strftime('%Y-%m-%d %H:%M:%S')

@app.route('/list_devices')
async def list_devices():
    # TODO provide ip, port, write_weight for each device
    pass

@app.route('/')
async def index():
    c = app.db.execute('select * from block_services')
    return await render_template('index.html', devices=list(c.fetchall()))


###########################################
# initialization
###########################################

def init_db():
    db = sqlite3.connect('database.db')
    db.execute("""
        create table if not exists block_services (
            id integer primary key,
            secret_key text unique not null,
            status integer not null default "ok",
            ip text not null,
            port int not null,
            last_check integer not null,
            used_bytes integer not null,
            free_bytes integer not null,
            used_n integer not null,
            free_n integer not null
        )
    """)
    db.commit()

@app.before_serving
async def startup():
    app.db = sqlite3.connect('database.db')
    app.db.row_factory = sqlite3.Row
    app.check_devices_forever_task = asyncio.create_task(check_devices_forever())
    app.listen_for_registrations_task = asyncio.create_task(listen_for_registrations())

@app.after_serving
async def shutdown():
    app.check_devices_forever_task.cancel()
    app.listen_for_registrations_task.cancel()

if __name__ == '__main__':
    init_db()
    app.run('0.0.0.0', 5000, debug=True)
