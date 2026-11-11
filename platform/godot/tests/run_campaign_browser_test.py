#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Serve real choice/two-player GPKs and exercise the staged hub's GUI controls."""
import argparse
import copy
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--app', type=Path, required=True, help='Imported application with current native extension')
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--capture', type=Path, help='Render a real browser screenshot; requires a display')
    args = parser.parse_args()
    app = args.app.resolve()
    with tempfile.TemporaryDirectory(prefix='browser-http-', dir=app.parent) as temporary:
        work = Path(temporary)
        source = json.loads((ROOT / 'games/steel_and_cedar/source/project.json').read_text())
        for mode in ('choice', 'two'):
            project = copy.deepcopy(source)
            project['campaigns'][0]['playerMode'] = mode
            path = work / (mode + '.json')
            path.write_text(json.dumps(project, ensure_ascii=False))
            subprocess.run([str(args.compiler.resolve()), str(path), str(work / (mode + '.gpk'))], check=True)

        class Handler(SimpleHTTPRequestHandler):
            def __init__(self, *positional, **keywords):
                super().__init__(*positional, directory=str(work), **keywords)

            def log_message(self, *_args):
                pass

        server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        base = 'http://127.0.0.1:%d' % server.server_port

        def entry(identifier, mode):
            package = (work / (mode + '.gpk')).read_bytes()
            return dict(id=identifier, title='Steel and Cedar' + (' · Two players' if mode == 'two' else ''),
                        description='Real authored campaign package.', author='Grandleon contributors', version='0.5.0',
                        campaign='steel_and_cedar', playerMode=mode, minimumPlayerVersion='0.5.0',
                        languages=['en', 'es', 'pt-BR'], assets=dict(kind='builtin', pack='grandleon-core', version=1),
                        package=dict(url=base + '/' + mode + '.gpk', sizeBytes=len(package), sha256=hashlib.sha256(package).hexdigest()))

        choice = entry('steel', 'choice')
        custom = copy.deepcopy(choice)
        custom.update(id='custom', title='Custom artwork unavailable', assets=dict(kind='custom'))
        catalog = dict(format='grandleon-campaign-catalog', formatVersion=1, id='ui-test', title='Browser integration',
                       campaigns=[choice, custom, entry('fixed-two', 'two')])
        (work / 'catalog.json').write_text(json.dumps(catalog))
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        command = [str(args.godot.resolve())]
        if args.capture:
            command += ['--rendering-method', 'gl_compatibility']
        else:
            command += ['--headless']
        command += ['--path', str(app), '--script', str(ROOT / 'platform/godot/tests/campaign_browser_test.gd'),
                    '--', '--catalog-test-url=' + base + '/catalog.json']
        if args.capture:
            command += ['--catalog-test-capture=' + str(args.capture.resolve())]
        try:
            return subprocess.run(command, timeout=90).returncode
        finally:
            server.shutdown()
            server.server_close()
            worker.join(timeout=5)


if __name__ == '__main__':
    raise SystemExit(main())
