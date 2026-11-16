#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Drive the separate Android UI-check APK through real system input/pickers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = 'org.grandleon.app.qa'
PROFILES = {'tablet': ('1280x800', 160), 'compact-phone': ('1280x720', 320),
            'wide-phone': ('1920x864', 420)}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--sdk', type=Path, default=Path.home() / '.cache/android-sdk-local')
    parser.add_argument('--apk', type=Path, help='Install a UI-check APK first; never installs the player application')
    parser.add_argument('--profile', choices=PROFILES, default='tablet')
    parser.add_argument('--language', default='pt-BR')
    parser.add_argument('--cutout', choices=['corner', 'hole'], help='Temporarily enable an Android cutout overlay')
    parser.add_argument('--output', type=Path, required=True, help='New evidence directory')
    args = parser.parse_args()
    if not args.serial.startswith('emulator-'):
        parser.error('This unattended input/picker runner is restricted to an emulator')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    adb = args.sdk / 'platform-tools/adb'
    def call(*command, check=True, timeout=20):
        return subprocess.run([str(adb), '-s', args.serial, *map(str, command)],
                              capture_output=True, check=check, timeout=timeout)
    def write_app(path, data):
        # Only the dedicated QA application's private test configuration.
        command = f'run-as {PACKAGE} sh -c {shlex.quote("cat > " + shlex.quote(path))}'
        subprocess.run([str(adb), '-s', args.serial, 'shell', command], input=data,
                       check=True, timeout=20, capture_output=True)
    def read_app(path):
        return call('exec-out', f'run-as {PACKAGE} cat {shlex.quote(path)}', check=False)
    def screenshot(name):
        (output / (name + '.png')).write_bytes(call('exec-out', 'screencap', '-p').stdout)
    def tap_matching(test):
        call('shell', 'uiautomator', 'dump', '/sdcard/grandleon-qa-window.xml', check=False)
        data = call('exec-out', 'cat', '/sdcard/grandleon-qa-window.xml').stdout
        (output / 'picker.xml').write_bytes(data)
        tree = ET.fromstring(data)
        for node in tree.iter('node'):
            if test(node.attrib):
                bounds = list(map(int, re.findall(r'\d+', node.attrib['bounds'])))
                call('shell', 'input', 'tap', (bounds[0] + bounds[2]) // 2,
                     (bounds[1] + bounds[3]) // 2)
                return True
        return False
    if args.apk:
        badging = subprocess.check_output([str(args.sdk / 'build-tools/36.1.0/aapt'),
                                          'dump', 'badging', str(args.apk)], text=True)
        if f"name='{PACKAGE}'" not in badging:
            raise ValueError('Refusing to install an APK without the separate QA package identity')
        call('install', '--no-incremental', '-r', args.apk, timeout=180)
    size, density = PROFILES[args.profile]
    # Set natural portrait dimensions; the application requests landscape.
    # This emulator image caps either forced dimension at 1920 pixels.
    width, height = map(int, size.split('x'))
    call('shell', 'wm', 'size', f'{height}x{width}')
    call('shell', 'wm', 'density', density)
    call('shell', 'am', 'force-stop', PACKAGE)
    call('shell', 'run-as', PACKAGE, 'mkdir', '-p', 'files/android-ui')
    call('shell', 'run-as', PACKAGE, 'rm', '-f', 'files/android-ui/status.json', 'files/android-ui/continue.json', 'files/android-ui/capture-continue.json')
    write_app('files/android-ui-config.json', json.dumps({'profile': args.profile,
                                                       'language': args.language}).encode())
    call('logcat', '-c')
    call('shell', 'am', 'start', '-n', PACKAGE + '/com.godot.game.GodotAppLauncher')
    seen = set()
    overlay = 'com.android.internal.display.cutout.emulation.' + args.cutout if args.cutout else ''
    overlay_was_enabled = bool(overlay and '[x] ' + overlay in call('shell', 'cmd', 'overlay', 'list').stdout.decode())
    archive_name = ''
    deadline = time.monotonic() + 240
    try:
        if overlay:
            call('shell', 'am', 'force-stop', PACKAGE)
            call('shell', 'cmd', 'overlay', 'enable', '--user', '0', overlay)
            time.sleep(1)
            call('shell', 'am', 'start', '-n', PACKAGE + '/com.godot.game.GodotAppLauncher')
        while time.monotonic() < deadline:
            raw = read_app('files/android-ui/status.json')
            try:
                status = json.loads(raw.stdout)
            except (ValueError, UnicodeDecodeError):
                time.sleep(.3)
                continue
            phase = status['phase']
            if phase in seen:
                time.sleep(.3)
                continue
            seen.add(phase)
            print(phase, status.get('failures'), flush=True)
            (output / (phase + '.json')).write_text(json.dumps(status, ensure_ascii=False, indent=2))
            screenshot(phase)
            if phase.startswith(('editor_', 'panel_')):
                write_app('files/android-ui/capture-continue.json', b'{}')
            elif phase.startswith('dismiss_panel_keyboard_'):
                call('shell', 'input', 'keyevent', 'KEYCODE_BACK')
            elif phase == 'ready_ime':
                x, y = status['field_pixel']
                call('shell', 'input', 'tap', round(x), round(y))
            elif phase == 'ime_seen':
                call('shell', 'input', 'keycombination', 'KEYCODE_CTRL_LEFT', 'KEYCODE_V')
                time.sleep(.5)
                call('shell', 'input', 'keyevent', 'KEYCODE_BACK')
            elif phase == 'ready_tab':
                call('shell', 'input', 'keyevent', 'KEYCODE_TAB')
            elif phase == 'ready_reverse_tab':
                call('shell', 'input', 'keycombination', 'KEYCODE_SHIFT_LEFT', 'KEYCODE_TAB')
            elif phase == 'ready_mouse':
                x, y = status['mouse_pixel']
                call('shell', 'input', 'mouse', 'tap', round(x), round(y))
            elif phase == 'save_picker_open':
                archive_name = status['archive_name']
                # Select Downloads in the actual system document provider.
                time.sleep(.5)
                if tap_matching(lambda node: node.get('content-desc') == 'Show roots'):
                    time.sleep(.4)
                    tap_matching(lambda node: node.get('text') == 'Downloads')
                    time.sleep(.4)
                limit = time.monotonic() + 30
                while time.monotonic() < limit:
                    if tap_matching(lambda node: node.get('text', '').upper() == 'SAVE' and node.get('enabled') == 'true'): break
                    time.sleep(.4)
                else:
                    raise RuntimeError('Native Android save picker did not offer Save')
            elif phase == 'ready_import':
                archive_path = status['archive_path']
                if not archive_path.startswith((f'/data/user/0/{PACKAGE}/files/android-ui/', f'/data/data/{PACKAGE}/files/android-ui/')):
                    raise ValueError('Unexpected QA archive path')
                archive_name = Path(archive_path).name
                data = read_app(archive_path)
                data.check_returncode()
                local = output / archive_name
                local.write_bytes(data.stdout)
                write_app('files/android-ui/continue.json', b'{}')
            elif phase == 'picker_open':
                limit = time.monotonic() + 45
                opened_roots = False
                # Landscape system pickers can initially expose only thumbnail
                # previews; list mode makes the actual filename selectable.
                tap_matching(lambda node: node.get('content-desc') == 'List view')
                while time.monotonic() < limit:
                    if tap_matching(lambda node: node.get('text') == archive_name): break
                    if tap_matching(lambda node: node.get('text') == 'Downloads'):
                        time.sleep(.5)
                        continue
                    if not opened_roots:
                        opened_roots = tap_matching(lambda node: node.get('content-desc') == 'Show roots')
                    time.sleep(.5)
                else:
                    raise RuntimeError('Native Android picker did not expose the exported QA archive')
            elif phase in ['passed', 'failed']:
                if phase == 'failed': raise RuntimeError(str(status['failures']))
                if list(status['window']) != [width, height]:
                    raise RuntimeError('Android did not apply the requested profile: ' + str(status['window']))
                safe = status['safe_pixels']
                if args.cutout and safe == [0, 0, width, height]:
                    raise RuntimeError('Android enabled the overlay but exposed no cutout inset; not counting cutout coverage')
                metadata = {'profile': args.profile, 'size': size, 'density': density,
                            'language': args.language, 'emulator': args.serial, 'cutout': args.cutout,
                            'scope': 'Actual Android UI and native authoring; injected Android input, not physical peripherals.'}
                if args.apk:
                    metadata['apk_sha256'] = hashlib.sha256(args.apk.read_bytes()).hexdigest()
                else:
                    package_path = call('shell', 'pm', 'path', PACKAGE).stdout.decode().strip().removeprefix('package:')
                    metadata['apk_sha256'] = call('shell', 'sha256sum', package_path).stdout.decode().split()[0]
                (output / 'coverage.json').write_text(json.dumps(metadata, indent=2) + '\n')
                print('ANDROID_UI_PASS profile=' + args.profile)
                return
            time.sleep(.3)
        raise TimeoutError('Android UI checks timed out')
    finally:
        if overlay and not overlay_was_enabled:
            call('shell', 'cmd', 'overlay', 'disable', '--user', '0', overlay, check=False)
        (output / 'logcat.txt').write_bytes(call('logcat', '-d', '-s', 'godot', check=False).stdout)
        screenshot('final')

if __name__ == '__main__':
    main()
