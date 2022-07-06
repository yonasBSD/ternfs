#!/usr/bin/env python

# TODO provide a nice ui
#      provide an endpoint where we pretend to be dead devices and fake deletion certs
#      provide an endpoint to return write weights for devices
#      don't insert things into the db until we have validated them
#      udp registration

from quart import Quart, request, g, jsonify, render_template
import asyncio
import datetime
import sqlite3
import struct

app = Quart(__name__)

###########################################
# device polling
###########################################

async def check_one_device(ip, port):
    reader, writer = await asyncio.open_connection(ip, port)
    writer.write(b's')
    m = await reader.readexactly(33)
    kind, used_bytes, free_bytes, used_n, free_n = struct.unpack('<cQQQQ', m)
    assert kind == b'S'
    return used_bytes, free_bytes, used_n, free_n

async def check_all_devices():
    c = app.db.execute('select id from block_services')
    all_ids = [row[0] for row in c.fetchall()]
    for i in all_ids:
        c = app.db.execute('select ip, port from block_services where id=? and status!="terminal"', (i,))
        try:
            ip, port = c.fetchone()
            now = int((datetime.datetime.utcnow() - datetime.datetime(2020, 1, 1)).total_seconds())
            used_bytes, free_bytes, used_n, free_n = await check_one_device(ip, port)
        except Exception as e:
            continue
        app.db.execute('update block_services set last_check=?, used_bytes=?, free_bytes=?, used_n=?, free_n=? where id=?', (now, used_bytes, free_bytes, used_n, free_n, i))

async def check_devices_forever():
    while not app.is_shutting_down:
        await check_all_devices()
        for _ in range(5):
            await asyncio.sleep(1)

###########################################
# new device registration
###########################################

async def register_device_impl(secret_key: bytes, ip: str, port: int) -> int:
    secret_key = secret_key.hex()
    now = int((datetime.datetime.utcnow() - datetime.datetime(2020, 1, 1)).total_seconds())
    c = app.db.execute('select id from block_services where secret_key=?', (secret_key,))
    row = c.fetchone()
    if row is None:
        # we need to insert it
        c = app.db.execute('insert into block_services (secret_key, last_register, ip, port) values (?, ?, ?, ?)', (secret_key, now, ip, port))
        app.db.commit()
        return c.lastrowid
    else:
        # there is an existing entry for it, just update it
        app.db.execute('update block_services set last_register=?, ip=?, port=? where id=?', (now, ip, port, row[0]))
        app.db.commit()
        return row[0]

@app.route('/register_new_device', methods=['POST'])
async def register_new_device():
    secret_key = bytes.fromhex(request.args.get('key'))
    assert len(secret_key) == 16
    port = int(request.args.get('port'))
    assert port < 65536
    ip = request.remote_addr
    unique_id = await register_device_impl(secret_key, ip, port)
    app.logger.info(f'registered device ip={ip} port={port} key={secret_key.hex()}, assigned id is {unique_id}')
    return jsonify(unique_id=unique_id)










@app.route('/list_devices')
async def list_devices():
    # TODO provide ip, port, write_weight for each device
    pass

@app.route('/')
async def index():
    c = app.db.execute('select * from block_services')
    return await render_template('index.html', devices=list(c.fetchall()))

def init_db():
    db = sqlite3.connect('database.db')
    db.execute("""
        create table if not exists block_services (
            id integer primary key,
            secret_key text unique not null,
            status integer not null default "ok",
            last_register integer not null,
            ip text not null,
            port int not null,
            last_check integer,
            used_bytes integer,
            free_bytes integer,
            used_n integer,
            free_n integer
        )
    """)
    db.commit()

@app.before_serving
async def startup():
    app.db = sqlite3.connect('database.db')
    app.db.row_factory = sqlite3.Row
    app.is_shutting_down = False
    app.add_background_task(check_devices_forever)

@app.after_serving
async def shutdown():
    app.is_shutting_down = True
    app.db.close()
    app.db = None

@app.template_filter('dateformat')
def date_format(value):
    dt = datetime.datetime(2020, 1, 1) + datetime.timedelta(seconds=value)
    return dt.strftime('%Y-%m-%d %H:%M:%S')

if __name__ == '__main__':
    init_db()
    app.run('0.0.0.0', 5000, debug=True)
