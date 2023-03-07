#!/usr/bin/env python3
import sys
import os
from pathlib import Path
import subprocess

if len(sys.argv) < 2:
    print(f'Usage: {sys.argv[0]} release|alpine|sanitized|debug|valgrind [NINJA_ARG ...]', file=sys.stderr)
    sys.exit(2)

if len(sys.argv) == 1:
    build_type = 'alpine'
else:
    build_type = sys.argv[1]

cpp_dir = Path(__file__).parent

build_dir = cpp_dir / 'build' / build_type
build_dir.mkdir(parents=True, exist_ok=True)

if build_type == 'alpine' and 'IN_EGGS_BUILD_CONTAINER' not in os.environ:
    subprocess.run(
        ['docker', 'run', '--rm', '-i', '--mount', f'type=bind,src={cpp_dir},dst=/eggsfs', 'REDACTED', '/eggsfs/build.py', 'alpine'],
        check=True,
    )
else:
    os.chdir(str(build_dir))
    subprocess.run(['cmake', '-G', 'Ninja', f'-DCMAKE_BUILD_TYPE={build_type}', '../..'], check=True)
    subprocess.run(['ninja'] + sys.argv[2:], check=True)
