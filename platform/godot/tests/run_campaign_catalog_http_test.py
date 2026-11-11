#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real-loopback HTTP + native-GPK integration, with an isolated Godot project."""
import argparse
import copy
import hashlib
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import shutil
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--extension', type=Path, required=True)
    parser.add_argument('--package', type=Path, default=ROOT / 'out/windows06-steel-and-cedar.gpk')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--live', action='store_true', help='also verify the public HTTPS catalog/release using Godot HTTPRequest')
    args = parser.parse_args()
    temporary = tempfile.TemporaryDirectory(prefix='grandleon-catalog-http-') if not args.output else None
    work = args.output.resolve() if args.output else Path(temporary.name)
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        parser.error('output must be empty')
    project = work / 'project'
    (project / 'scripts').mkdir(parents=True)
    (project / 'fixtures').mkdir()
    (project / 'bin').mkdir()
    shutil.copy2(ROOT / 'platform/godot/project/scripts/campaign_catalog.gd', project / 'scripts/')
    shutil.copy2(ROOT / 'platform/godot/tests/campaign_catalog_http_test.gd', project / 'test.gd')
    shutil.copy2(ROOT / 'platform/godot/tests/campaign_catalog_live_test.gd', project / 'live.gd')
    shutil.copy2(ROOT / 'games/steel_and_cedar/source/project.json', project / 'fixtures/source.json')
    library_name = args.extension.name
    shutil.copy2(args.extension, project / 'bin' / library_name)
    library_key = 'windows' if library_name.endswith('.dll') else 'linux'
    (project / 'bin/test.gdextension').write_text(
        '[configuration]\nentry_symbol="grandleon_library_init"\ncompatibility_minimum="4.7"\n'
        '[libraries]\n%s.debug.x86_64="res://bin/%s"\n%s.release.x86_64="res://bin/%s"\n'
        % (library_key, library_name, library_key, library_name))
    (project / 'project.godot').write_text('config_version=5\n[application]\nconfig/name="Campaign Catalog HTTP Integration"\n')
    # Register the known fixture extension at engine startup, as an exported
    # project does. Late discovery during the first minimal-editor scan aborts
    # Godot 4.7.2 on exit even in a native-only control with no catalog scripts.
    (project / '.godot').mkdir()
    (project / '.godot/extension_list.cfg').write_text('res://bin/test.gdextension\n')
    package = args.package.read_bytes()
    corrupt = bytes([package[0] ^ 255]) + package[1:]
    digest = hashlib.sha256(package).hexdigest()
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            control_path = work / 'control.json'
            state = json.loads(control_path.read_text()) if control_path.exists() else {'mode': 'valid'}
            mode = state['mode']
            if mode == 'down':
                self.send_error(503)
                return
            if self.path.endswith('/catalog.json'):
                if mode == 'slow':
                    time.sleep(0.25)
                is_a = self.path.startswith('/a/')
                entry = {'id': 'steel-and-cedar', 'title': 'Steel and Cedar',
                         'description': 'A real native package served over HTTP.', 'author': 'Grandleon',
                         'version': '0.5.0', 'campaign': 'steel_and_cedar', 'playerMode': 'choice',
                         'languages': ['en', 'es', 'pt-BR'], 'minimumPlayerVersion': '0.5.0',
                         'assets': {'kind': 'builtin', 'pack': 'grandleon-core', 'version': 1},
                         'package': {'url': base + ('/redirect' if is_a else '/builtin.gpk'),
                                     'sizeBytes': len(package), 'sha256': digest}}
                if is_a:
                    if mode in ['one', 'two']:
                        entry['id'] = 'con' if mode == 'one' else 'aux'
                        entry['playerMode'] = mode
                        entry['package'] = {'url': base + '/' + mode + '.gpk', 'sizeBytes': state['size'], 'sha256': state['sha']}
                    elif mode == 'update':
                        entry['version'] = '0.5.1'
                        entry['package'] = {'url': base + '/updated.gpk', 'sizeBytes': state['size'], 'sha256': state['sha']}
                    elif mode == 'size':
                        entry['package']['sizeBytes'] += 1
                    elif mode == 'corrupt':
                        entry['package']['sha256'] = hashlib.sha256(corrupt).hexdigest()
                    elif mode == 'missing_campaign':
                        entry['campaign'] = 'absent_campaign'
                    elif mode == 'incompatible':
                        entry['minimumPlayerVersion'] = '99.0.0'
                    elif mode == 'external':
                        entry['assets'] = {'kind': 'custom', 'url': base + '/untrusted.zip'}
                    elif mode == 'malformed_assets':
                        entry['assets'] = {}
                    elif mode == 'invalid_version':
                        entry['version'] = '0.5'
                    elif mode == 'traversal':
                        entry['id'] = '../escape'
                    elif mode == 'oversized':
                        entry['package']['sizeBytes'] = 16 * 1024 * 1024 + 1
                entries = [entry, copy.deepcopy(entry)] if is_a and mode == 'duplicate' else [entry]
                document = {'format': 'grandleon-campaign-catalog', 'formatVersion': 1,
                            'id': 'test.a' if is_a else 'test.b', 'title': 'HTTP fixture', 'campaigns': entries}
                if is_a and mode == 'boolean_format_version':
                    document['formatVersion'] = True
                body = b'not json' if is_a and mode == 'invalid_json' else json.dumps(document).encode()
                if is_a and mode == 'catalog_too_large':
                    body = b' ' * (1024 * 1024 + 1)
            elif self.path == '/redirect':
                self.send_response(302)
                self.send_header('Location', 'http://192.168.1.1/private' if mode == 'unsafe_redirect' else '/redirect' if mode == 'redirect_loop' else '/package.gpk')
                self.end_headers()
                return
            elif self.path in ['/updated.gpk', '/one.gpk', '/two.gpk']:
                body = (work / self.path[1:]).read_bytes()
            elif self.path in ['/package.gpk', '/builtin.gpk']:
                body = corrupt if self.path == '/package.gpk' and mode in ['checksum', 'corrupt'] else package
                if self.path == '/package.gpk' and mode == 'body_oversized':
                    body += b'X'
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    base = 'http://127.0.0.1:%d' % server.server_address[1]
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    import os
    env = dict(os.environ, GRANDLEON_CATALOG_TEST_URL=base, GRANDLEON_CATALOG_TEST_WORK=str(work))
    try:
        imported = subprocess.run([str(args.godot.resolve()), '--headless', '--path', str(project), '--import'],
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60, env=env)
        (work / 'import.log').write_text(imported.stdout)
        tested = subprocess.run([str(args.godot.resolve()), '--headless', '--path', str(project), '--script', 'res://test.gd'],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, env=env)
        (work / 'test.log').write_text(tested.stdout)
        (work / 'requests.json').write_text(json.dumps(requests, indent=2) + '\n')
        if imported.returncode or tested.returncode or 'CAMPAIGN_CATALOG_HTTP_PASS ' not in tested.stdout:
            raise SystemExit('Godot exit import=%d test=%d\n' % (imported.returncode, tested.returncode) + imported.stdout + '\n' + tested.stdout)
        assert '/redirect' in requests and '/package.gpk' in requests and '/updated.gpk' in requests
        print(tested.stdout.strip())
        print('Real HTTP requests:', len(requests), 'package sha256:', digest)
        if args.live:
            live = subprocess.run([str(args.godot.resolve()), '--headless', '--path', str(project), '--script', 'res://live.gd'],
                                  text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120, env=env)
            (work / 'live.log').write_text(live.stdout)
            if live.returncode or 'CAMPAIGN_CATALOG_LIVE_PASS ' not in live.stdout:
                raise SystemExit(live.stdout)
            print(live.stdout.strip())
        print('Evidence:', work)
    finally:
        server.shutdown()
        server.server_close()
        if temporary:
            temporary.cleanup()


if __name__ == '__main__':
    main()
