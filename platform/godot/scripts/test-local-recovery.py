#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Test native recovery across forcibly terminated Godot processes, locally."""
import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[3]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--godot', type=Path, required=True)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='new evidence directory')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    godot, project, compiler = args.godot.resolve(), args.project.resolve(), args.compiler.resolve()
    original = ROOT / 'games/steel_and_cedar/source/project.json'
    package = output / 'campaign.gpk'
    subprocess.run([str(compiler), str(original), str(package)], check=True)
    # A genuine recompile with changed presentation and the same game/revision,
    # not a patched package or changed simulation intended to satisfy the test.
    source = json.loads(original.read_text())
    source['title'] += ' — updated presentation'
    changed_source = output / 'compatible-project.json'
    changed_source.write_text(json.dumps(source, ensure_ascii=False))
    compatible = output / 'compatible.gpk'
    subprocess.run([str(compiler), str(changed_source), str(compatible)], check=True)
    assert package.read_bytes() != compatible.read_bytes()
    env = {**os.environ, 'XDG_DATA_HOME': str(output / 'userdata'),
           'XDG_CONFIG_HOME': str(output / 'config'), 'XDG_CACHE_HOME': str(output / 'cache')}
    script = ROOT / 'platform/godot/tests/local_recovery_process.gd'
    def command(phase, root, expected, players, selected=package):
        return [str(godot), '--headless', '--path', str(project), '--script', str(script), '--',
                f'--phase={phase}', f'--root={root}', f'--expected={expected}',
                f'--package={selected}', f'--players={players}', '--language=pt-BR']
    def verify(name, root, expected, players, selected=package, phase='verify'):
        result = subprocess.run(command(phase, root, expected, players, selected), env=env,
                                capture_output=True, text=True, timeout=45)
        text = result.stdout + result.stderr
        (output / f'{name}.log').write_text(text)
        marker = {'verify': 'PROCESS', 'fallback': 'FALLBACK', 'poison-base': 'POISON'}[phase]
        if result.returncode or 'SCRIPT ERROR:' in text or '\nERROR:' in text or f'RECOVERY_{marker}_PASS' not in text:
            raise RuntimeError(f'{name} failed; see {output / (name + ".log")}')
    reports = []
    for players in (1, 2):
        root = output / f'{players}p' / 'São João — ação'
        expected = output / f'{players}p-expected.var'
        lines = queue.Queue()
        process = subprocess.Popen(command('write', root, expected, players), env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        collected = []
        def collect():
            for line in process.stdout:
                collected.append(line)
                lines.put(line)
            lines.put(None)
        worker = threading.Thread(target=collect, daemon=True)
        worker.start()
        try:
            deadline = time.monotonic() + 45
            while True:
                line = lines.get(timeout=max(0.1, deadline - time.monotonic()))
                if line is None or time.monotonic() >= deadline:
                    raise RuntimeError(f'{players}p writer failed before checkpoint')
                if 'SCRIPT ERROR:' in line or line.startswith('ERROR:'):
                    raise RuntimeError(f'{players}p writer reported an error: {line.strip()}')
                if line.startswith('RECOVERY_READY_FOR_KILL '): break
            process.kill()  # No Godot stop(), _exit_tree(), or shutdown save.
            process.wait(timeout=10)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            worker.join(timeout=5)
            process.stdout.close()
            (output / f'{players}p-killed-writer.log').write_text(''.join(collected))
        auto = next(root.rglob('auto.resume'))
        # An interrupted replacement must not replace the valid automatic file.
        Path(str(auto) + '.tmp').write_bytes(b'interrupted replacement')
        verify(f'{players}p-after-kill', root, expected, players)
        verify(f'{players}p-compatible-update', root, expected, players, compatible)
        damaged = output / f'{players}p-damaged'
        shutil.copytree(root, damaged)
        damaged_auto = next(damaged.rglob('auto.resume'))
        damaged_auto.write_bytes(b'damaged automatic recovery')
        verify(f'{players}p-corrupt-recovery', damaged, expected, players, phase='fallback')
        assert any(p.read_bytes() == b'damaged automatic recovery'
                   for p in damaged.rglob('*.rejected-*'))
        invalid_base = output / f'{players}p-invalid-base'
        shutil.copytree(root, invalid_base)
        verify(f'{players}p-invalid-base-fixture', invalid_base, expected, players, phase='poison-base')
        verify(f'{players}p-invalid-base-fallback', invalid_base, expected, players, phase='fallback')
        reports.append({'players': players, 'forced_termination': True, 'accented_paths': True,
                        'interrupted_replacement_ignored': True, 'compatible_package_update': True,
                        'corrupt_recovery_retained': True, 'invalid_native_boundary_fallback': True})
    (output / 'coverage.json').write_text(json.dumps({'scope': 'Real native Godot processes; not Android device lifecycle validation.',
                                                    'cases': reports}, indent=2) + '\n')
    print('LOCAL_RECOVERY_PROCESS_PASS solo=true local_coop=true compatible_update=true')

if __name__ == '__main__':
    main()
