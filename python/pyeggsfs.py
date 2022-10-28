#!/usr/bin/env python3
from multiprocessing import Process
import argparse
import sys
from time import time
from typing import List
import os
from pathlib import Path
import logging.config
import os
import signal
import time

import shard
import shuckle
import block_service
import cdc

class Daemon(Process):
    def __init__(self, parent, *args, **kwargs):
        super().__init__(daemon=True, *args, **kwargs)
        self._parent = parent

    def run(self):
        try:
            Process.run(self)
        except Exception as err:
            os.kill(self._parent, signal.SIGINT)
            raise err

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Runs every component of EggsFS')
    parser.add_argument('path', help='Location to store DBs and blocks')
    parser.add_argument('--hdd_block_services', help='Number of HDD block services', type=int, default=10)
    parser.add_argument('--flash_block_services', help='Number of FLASH block services', type=int, default=5)
    parser.add_argument('-v', '--verbose', action='store_true')
    config = parser.parse_args(sys.argv[1:])

    logging.config.dictConfig({
        'version': 1,
        'loggers': {
            'quart.app': {'level': 'WARNING'},
            'quart.serving': {'level': 'WARNING'},
            'root': {'level': 'DEBUG' if config.verbose else 'WARNING'},
        },
    })

    parent = os.getpid()

    funs: List[Daemon] = []
    for i in range(config.hdd_block_services+config.flash_block_services):
        storage_class = 'HDD' if i < config.hdd_block_services else 'FLASH'
        bs_path = Path(config.path) / f'bs_{i}'
        bs_path.mkdir(exist_ok=True)
        funs.append(Daemon(parent, target=block_service.main, kwargs={'path': str(bs_path), 'port': 40000+i, 'storage_class': storage_class, 'failure_domain': str(i), 'time_check': False}))
    funs.append(Daemon(parent, target=cdc.main, args=(config.path,)))
    for i in range(256):
        funs.append(Daemon(parent, target=shard.main, kwargs={'db_dir': config.path, 'shard': i, 'wait_for_shuckle': True}))
    for fun in funs:
        fun.start()
    
    shuckle.main(config.path)
