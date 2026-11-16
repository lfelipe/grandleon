#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native checkpoint and real HTTP update checks against an already staged app."""
import argparse
import functools
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os
from pathlib import Path
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        parser.error('output must be empty')
    requests = []

    class Handler(SimpleHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            super().do_GET()

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(Handler, directory=str(work)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = dict(os.environ, GRANDLEON_INVENTORY_COMPAT_WORK=str(work),
               GRANDLEON_INVENTORY_COMPAT_URL='http://127.0.0.1:%d' % server.server_port)
    for name, folder in [('XDG_DATA_HOME', 'data'), ('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache')]:
        env[name] = str(work / folder)
        (work / folder).mkdir()
    try:
        result = subprocess.run([
            str(args.godot.resolve()), '--headless', '--path', str(args.project.resolve()),
            '--script', str(ROOT / 'platform/godot/tests/inventory_checkpoint_compatibility_test.gd'),
            '--audio-driver', 'Dummy'], env=env, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=120)
        (work / 'test.log').write_text(result.stdout)
        (work / 'requests.txt').write_text('\n'.join(requests) + '\n')
        print(result.stdout, end='')
        assert result.returncode == 0 and 'INVENTORY_CHECKPOINT_COMPATIBILITY_PASS' in result.stdout
        assert not any(marker in result.stdout for marker in ['SCRIPT ERROR:', '\nERROR:'])
        assert all('/0.6.%d.gpk' % version in requests for version in range(4)), requests
    finally:
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    main()
