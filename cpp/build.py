#!/usr/bin/env python3
import sys
import os
import pathlib
import subprocess

if len(sys.argv) < 2 or sys.argv[1] not in ('release', 'sanitized', 'debug', 'valgrind'):
    print(f'Usage: {sys.argv[0]} release|sanitized|debug|valgrind [NINJA_ARG ...]', file=sys.stderr)
    sys.exit(2)

typ = sys.argv[1]

if typ == 'release':
    cmake_build = 'Release'
    build_dir = 'build/release'
elif typ == 'valgrind':
    cmake_build = 'Valgrind'
    build_dir = 'build/valgrind'
elif typ == 'sanitized':
    cmake_build = 'Sanitized'
    build_dir = 'build/sanitized'
elif typ == 'debug':
    cmake_build = 'Debug'
    build_dir = 'build/debug'
else:
    assert False

build_dir = f'{os.path.dirname(__file__)}/{build_dir}'
pathlib.Path(build_dir).mkdir(parents=True, exist_ok=True)
os.chdir(build_dir)

subprocess.run(['cmake', '-G', 'Ninja', f'-DCMAKE_BUILD_TYPE={cmake_build}', '../..'])
subprocess.run(['ninja'] + sys.argv[2:])
