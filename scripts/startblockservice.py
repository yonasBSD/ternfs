#!/usr/bin/env python3
import socket
import subprocess
import os

os.chdir(os.path.dirname(__file__))

hostname = socket.gethostname()
failure_domain, _, _, _ = hostname.split('.')
own_ip = socket.gethostbyname(socket.gethostname())
storage_paths = [f'/storage/{i}/blockservice' for i in range(10, 20)]
storage_paths_flags = [x for path in storage_paths for x in (path, 'HDD')]
subprocess.run(
    ['./eggsblockservice', '-failure-domain', failure_domain, '-log-file', f'{storage_paths[0]}/log', '-own-ip', own_ip] + storage_paths_flags,
    check=True,
)
