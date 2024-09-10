#!/usr/bin/env python3
import sys
import os
from pathlib import Path
import subprocess

if len(sys.argv) < 2:
    print(f'Usage: {sys.argv[0]} <build-type> [NINJA_ARG ...]', file=sys.stderr)
    sys.exit(2)

if len(sys.argv) == 1:
    build_type = 'release'
else:
    build_type = sys.argv[1]

cpp_dir = Path(__file__).resolve().parent
repo_dir = cpp_dir.parent

build_dir = cpp_dir / 'build' / build_type
build_dir.mkdir(parents=True, exist_ok=True)

if build_type in ('ubuntu', 'ubuntudebug', 'ubuntusanitized', 'ubuntuvalgrind', 'alpine', 'alpinedebug') and 'IN_EGGS_BUILD_CONTAINER' not in os.environ:
    if build_type.startswith('alpine'):
        container = 'REDACTED'
    else:
        container = 'REDACTED'
    # See <https://groups.google.com/g/seastar-dev/c/r7W-Kqzy9O4>
    # for motivation for `--security-opt seccomp=unconfined`,
    # the `--pids-limit -1` is not something I hit but it seems
    # like a good idea.
    subprocess.run(
        ['docker', 'run', '--network', 'host', '--pids-limit', '-1', '--security-opt', 'seccomp=unconfined', '--rm', '-i', '--mount', f'type=bind,src={repo_dir},dst=/eggsfs', '-u', f'{os.getuid()}:{os.getgid()}', container, '/eggsfs/cpp/build.py', build_type] + sys.argv[2:],
        check=True,
    )
else:
    os.chdir(str(build_dir))
    build_types = {
        'ubuntu': 'release',
        'ubuntudebug': 'debug',
        'ubuntusanitized': 'sanitized',
        'ubuntuvalgrind': 'valgrind',
    }
    subprocess.run(['cmake', '-G', 'Ninja', f'-DCMAKE_BUILD_TYPE={build_types.get(build_type, build_type)}', '../..'], check=True)
    subprocess.run(['ninja'] + sys.argv[2:], check=True)
