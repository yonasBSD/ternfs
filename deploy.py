#!/usr/bin/env python3
import sys
import os
import subprocess

this_dir = os.path.dirname(__file__)

metadata_hosts = ['REDACTED']
metadata_binaries = ['cpp/build/alpine/shard/eggsshard', 'cpp/build/alpine/cdc/eggscdc', 'go/eggsshuckle/eggsshuckle']
storage_hosts = [f'REDACTED{i}' for i in range(1,17)]
storage_binaries = ['go/eggsblockservice/eggsblockservice']

print('building')
subprocess.run(['./cpp/build.py alpine'], check=True)
for binary in (metadata_binaries+storage_binaries):
    if binary.startswith('go/'):
        subprocess.run(['go', 'build', '.'], check=True, cwd=os.path.dirname(f'{this_dir}/{binary}'))

print(f'uploading to {", ".join(metadata_hosts + storage_hosts)}')

processes = []
for host in metadata_hosts:
    processes.append((
        host,
        subprocess.Popen(
            ['rsync', '--mkpath', '--quiet'] + [f'{this_dir}/{binary}' for binary in metadata_binaries] + [f'REDACTED'],
        )
    ))
for host in storage_hosts:
    processes.append((
        host,
        subprocess.Popen(
            ['rsync', '--mkpath', '--quiet'] + [f'{this_dir}/{binary}' for binary in storage_binaries] + [f'REDACTED'],
        ),
    ))

for host, process in processes:
    if process.wait() != 0:
        print(f'uploading to {host} failed!')