#!/usr/bin/env python3
import sys
import os
from pathlib import Path
import subprocess

go_dir = Path(__file__).parent
repo_dir = go_dir.parent

paths = sys.argv[1:]
race = "--race" in paths
paths = list(filter(lambda x: x != "--race", paths))

if len(paths) == 0:
    for path in os.listdir(str(go_dir)):
        if path == 'vendor':
            continue
        if os.path.isdir(os.path.join(str(go_dir), path)):
            paths.append(path)

if 'IN_EGGS_BUILD_CONTAINER' not in os.environ:
    subprocess.run(
        ['docker', 'run', '--rm', '-i', '--mount', f'type=bind,src={repo_dir},dst=/eggsfs', '-u', f'{os.getuid()}:{os.getgid()}', 'REDACTED', '/eggsfs/go/build.py'] + sys.argv[1:] + (["--race"] if race else []),
        check=True,
    )
else:
    # Otherwise go will try to create the cache in /.cache, which won't work
    # since we're not running as root.
    os.environ['GOCACHE'] = '/eggsfs/.cache'
    for path_str in paths:
        print(f'Building {path_str}')
        path = go_dir / Path(path_str)
        subprocess.run(
            ['go', 'build', '-ldflags=-extldflags=-static'] + (["-race"] if race else []) + ['.'],
            cwd=str(path),
            check=True,
        )
