#!/usr/bin/env python3
import os
import sys
import subprocess
import atexit
import shlex
import socket
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--functional', action='store_true')
parser.add_argument('--integration', action='store_true')
parser.add_argument('--short', action='store_true')
parser.add_argument('--quiet', action='store_true')
parser.add_argument('--kmod', default=None, type=str, help='Run the kmod tests with the provided base image')
parser.add_argument('--no-build', action='store_true')
args = parser.parse_args()

if sys.stdout.isatty():
    def bold_print(fmt, *args, **kwargs):
        print('\033[1m' + fmt + '\033[0m', *args, **kwargs)
else:
    def bold_print(*args, **kwargs):
        print(*args, **kwargs)

os.environ['PATH'] = f'/opt/go1.18.4/bin:{os.environ["PATH"]}'

script_dir = os.path.dirname(os.path.realpath(__file__))
os.chdir(script_dir)

# all running pids
processes = []

def process_status(p, fmt, *args, **kwargs):
    print(repr(shlex.join(p.args)) + ': ' + fmt, *args, **kwargs)

def terminate_all_processes():
    for process in processes:
        if process.poll() is None: # not done yet
            process_status(process, 'terminating')
            process.terminate()
            try:
                process.wait(timeout=10) # wait at most 10 seconds
            except subprocess.TimeoutExpired:
                print('Process not terminating after 10 seconds, killing it')
                process.kill()

atexit.register(terminate_all_processes)

def run_cmd(*args, **kwargs):
    p = subprocess.Popen(*args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kwargs)
    process_status(p, 'start')
    processes.append(p)
    return p

def wait_cmd(p, timeout=None):
    try:
        output, _ = p.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        return False
    process_status(p, f'finished ({p.returncode})')
    if p.returncode != 0 or not args.quiet:
        if len(output) > 0:
            process_status(p, 'output:')
            sys.stdout.flush()
            sys.stdout.buffer.write(output)
            sys.stdout.flush()
        else:
            process_status(p, 'no output')
    if p.returncode != 0:
        sys.exit(p.returncode)
    return True

def wait_cmds(ps):
    done = [False]*len(ps)
    while not all(done):
        for i, p in enumerate(ps):
            if done[i]:
                continue
            done[i] = wait_cmd(p, timeout=0.1)

if not args.no_build:
    bold_print('building')
    for r in ['release', 'sanitized', 'valgrind']:
        wait_cmd(run_cmd(['./build.sh', r]))
    wait_cmd(run_cmd(['make', 'bincode_tests'], cwd='kmod'))

if args.functional:
    bold_print('functional tests')
    wait_cmds(
        [run_cmd(['./cpp/tests.sh']), run_cmd(['./bincode_tests'], cwd='kmod'), run_cmd(['go', 'test', './...'], cwd='go')],
    )

if args.integration:
    if args.short:
        fuse_tests = 'history|direct|mounted|cp'
    else:
        # The mounted ones are long, and we don't rely on the FUSE driver critically,
        # but we still want to test it in the short run.
        fuse_tests = 'history|direct|cp'

    bold_print('integration tests')
    short = ['-short'] if args.short else []
    # -block-service-killer does not work with FUSE driver (the duplicated FDs
    # of the child processes confuse the FUSE driver).
    tests = [
        ['./go/eggstests/eggstests', '-build-type', 'sanitized', '-binaries-dir', 'build/sanitized', '-verbose', '-repo-dir', '.', '-tmp-dir', '.', '-filter', fuse_tests, '-outgoing-packet-drop', '0.02'] + short,
        ['./go/eggstests/eggstests', '-build-type', 'release', '-binaries-dir', 'build/release', '-preserve-data-dir', '-verbose', '-block-service-killer', '-filter', 'direct', '-repo-dir', '.', '-tmp-dir', '.'] + short,
    ]
    if not args.short:
        # valgrind is super slow, it still surfaced bugs in the past but run the short
        # versions only in the long tests.
        tests.append(['./go/eggstests/eggstests', '-build-type', 'valgrind', '-binaries-dir', 'build/valgrind', '-verbose', '-repo-dir', '.', '-tmp-dir', '.', '-short', '-filter', fuse_tests])
    # we need three free ports, we get them here upfront rather than in shuckle to reduce
    # the chance of races -- if we got it from the integration tests it'll be while
    # tons of things are started in another integration test
    ports = []
    for _ in range(len(tests)):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))
            s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)  # Ensure the port is immediately reusable
            ports.append(s.getsockname()[1])
    wait_cmds(
        [run_cmd(test + ['-shuckle-port', str(port)]) for test, port in zip(tests, ports)],
    )

if args.kmod:
    bold_print('kmod tests')
    wait_cmd(run_cmd(['./kmod/ci.sh', args.kmod] + (['-short'] if args.short else [])))