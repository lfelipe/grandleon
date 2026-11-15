#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Record the two-player Bell Orchard probe at native display timing, or encode its trace."""
from __future__ import annotations
import argparse
from collections import Counter
import csv
from fractions import Fraction
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

CPU_HZ = 33868800  # PSn00bSDK hwregs_c.h F_CPU; emulated CPU cycles, not host time.
ROOT = Path(__file__).resolve().parents[2]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_capture(capture):
    capture = Path(capture).resolve()
    with (capture / 'presentation.csv').open() as source:
        rows = list(csv.DictReader(source))
    require(len(rows) > 2, 'native trace needs consecutive VSync samples and an end sentinel')
    cycles, origins, frames = [], [], []
    for index, row in enumerate(rows):
        require(int(row['index']) == index, 'missing or reordered VSync sample')
        cycle, origin = int(row['cycle']), int(row['display_origin'])
        require(not cycles or cycle > cycles[-1], 'native cycle clock must increase')
        data = (capture / f'present-{index:05d}.bin').read_bytes()
        require(len(data) == 320 * 240 * 2, 'incomplete native display image')
        if origins and origin == origins[-1]:
            require(data == frames[-1], 'display pixels changed without an observed buffer change')
        cycles.append(cycle)
        origins.append(origin)
        frames.append(data)
    span = cycles[-1] - cycles[0]
    count = len(rows) - 1
    error = max(abs(c - cycles[0] - i * span / count) for i, c in enumerate(cycles)) / CPU_HZ
    require(error < 0.00001, 'missing or nonuniform VSync samples require variable-rate encoding')
    changes = [i for i in range(1, len(rows)) if origins[i] != origins[i - 1]]
    require(len(changes) >= 2, 'trace needs at least two observed display changes')
    holds = [b - a for a, b in zip(changes, changes[1:])]
    seconds = (cycles[changes[-1]] - cycles[changes[0]]) / CPU_HZ
    report = {
        'schema': 'grandleon.ps1-native-motion.v1',
        'source_cpu_clock_hz': CPU_HZ,
        'native_vsync_samples': len(rows), 'encoded_vsync_frames': count,
        'duration_seconds': span / CPU_HZ,
        'observed_logic_updates_per_second': (int(rows[-1]['update']) - int(rows[0]['update'])) * CPU_HZ / span,
        'uniform_clock_error_max_us': error * 1e6,
        'complete_frame_hold_histogram_vis': dict(Counter(holds)),
        'observed_display_flip_fps': len(holds) / seconds,
        'presentation_is_even_two_vi': all(n == 2 for n in holds),
        'same_origin_pixel_changes': 0,
        'source_frames': [{
            'file': f'present-{i:05d}.bin', 'sha256': hashlib.sha256(data).hexdigest(),
            'cycles': cycles[i], 'display_origin': origins[i], 'update': int(rows[i]['update']),
        } for i, data in enumerate(frames)],
        'qualification': (
            'Every emulated VSync is captured directly from display memory, including held images. '
            'Native CPU-cycle timing determines playback rate; the end sentinel is excluded. '
            'Encoded VSync rate is not game rendering FPS. Display changes come from decoded GP1 '
            'Display Area Start writes. No target memory, register or input changes are made by the observer. '
            'MP4 is a lossy nearest-scaled YUV420 view; GIF is a 15 FPS preview. '
            'A short emulator scene is not a hardware or whole-campaign benchmark.'),
        'references': ['https://pcsx-redux.consoledev.net/Lua/redux-basics/',
                       'https://pcsx-redux.consoledev.net/Lua/playbook-scripted-session/'],
    }
    return rows, frames, Fraction(CPU_HZ * count, span).limit_denominator(1000000), report


def empty_directory(path):
    require(not path.exists() or not any(path.iterdir()), f'output must be empty: {path}')
    path.mkdir(parents=True, exist_ok=True)


def encode(capture, output):
    rows, frames, rate, report = read_capture(capture)
    empty_directory(output)
    video = output / 'playstation-native-motion.mp4'
    subprocess.run([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo',
        '-pixel_format', 'bgr555le', '-video_size', '320x240', '-framerate', str(rate),
        '-i', 'pipe:0', '-vf', 'scale=640:480:flags=neighbor', '-c:v', 'libx264',
        '-preset', 'fast', '-crf', '16', '-pix_fmt', 'yuv420p',
        '-video_track_timescale', str(CPU_HZ), '-movflags', '+faststart', '-an', str(video),
    ], input=b''.join(frames[:-1]), check=True)
    probe = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-count_frames', '-select_streams', 'v:0',
        '-show_entries', 'stream=nb_read_frames,duration,width,height,avg_frame_rate',
        '-of', 'json', str(video),
    ], text=True))['streams'][0]
    require(int(probe['nb_read_frames']) == len(rows) - 1, 'encoder changed frame count')
    require(abs(float(probe['duration']) - report['duration_seconds']) < 0.00005,
            'encoded duration does not match the native clock')
    poster = next((i for i, row in enumerate(rows) if int(row['update']) >= 200), 0)
    subprocess.run([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'rawvideo',
        '-pixel_format', 'bgr555le', '-video_size', '320x240', '-i', 'pipe:0',
        '-vf', 'scale=640:480:flags=neighbor', '-frames:v', '1',
        str(output / 'playstation-native-poster.png'),
    ], input=frames[poster], check=True)
    subprocess.run([
        'ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', str(video),
        '-filter_complex', 'fps=15,split[a][b];[a]palettegen=stats_mode=diff[p];'
        '[b][p]paletteuse=dither=sierra2_4a', '-loop', '0',
        str(output / 'playstation-motion-preview.gif'),
    ], check=True)
    report['video_probe'] = probe
    report['artifacts'] = {p.name: {'bytes': p.stat().st_size, 'sha256': digest(p)}
                           for p in output.iterdir() if p.is_file()}
    (output / 'coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def record(build, output, first_update=90, last_update=480):
    require(os.environ.get('PSN00BSDK_ROOT'), 'PSN00BSDK_ROOT is required for recording')
    require(0 <= first_update < last_update < 1800, 'capture window must finish before the original probe exits at update 1800')
    empty_directory(output)
    probe = output / 'probe'
    shutil.copytree(ROOT / 'platform/playstation/mipsync/tests', probe,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    shutil.copy2(probe / 'combat_audio_probe.lua', probe / 'combat_audio_original.lua')
    observer = Path(__file__).with_name('ps1_native_motion_observer.lua').read_text()
    (probe / 'combat_audio_probe.lua').write_text(
        f'grandleon_capture_first_update={first_update}\ngrandleon_capture_last_update={last_update}\n' + observer)
    original = probe / 'combat_probe.lua'
    text = original.read_text()
    needle = '  runtime_frame = runtime_frame + 1\n'
    require(text.count(needle) == 1, 'the original probe needs a reviewed read-only observation hook')
    original.write_text(text.replace(needle, needle +
        '  if grandleon_presentation_tick then grandleon_presentation_tick(runtime_frame) end\n', 1))
    with (output / 'probe.log').open('w') as log:
        result = subprocess.run(['sh', str(probe / 'run-combat-probe.sh'), str(build), str(output / 'capture')],
                                env={**os.environ, 'GRANDLEON_CAPTURE_MOTION': '0'},
                                stdout=log, stderr=subprocess.STDOUT)
    log = (output / 'probe.log').read_text()
    functional = 'combat controls, camera and audio checks passed' in log
    pacing_failed = 'stable 30 fps combat frame result' in log
    require(functional and (result.returncode == 0 or pacing_failed), 'native functional probe failed; see probe.log')
    native = (output / 'capture/emulator.log').read_text()
    require('MIPSYNC_PRESENTATION_CAPTURE frames=' in native, 'presentation observer did not finish')
    report = encode(output / 'capture', output / 'media')
    report['native_probe_exit_code'] = result.returncode
    report['strict_frame_pacing_passed'] = result.returncode == 0
    (output / 'media/coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('record', 'encode'))
    parser.add_argument('input', type=Path, help='Bell Orchard two-player build, or existing capture directory')
    parser.add_argument('output', type=Path, help='empty output directory')
    parser.add_argument('--first-update', type=int, default=90, help='recording window start')
    parser.add_argument('--last-update', type=int, default=480, help='recording window end, before probe exit at 1800')
    args = parser.parse_args()
    report = (record(args.input.resolve(), args.output.resolve(), args.first_update, args.last_update)
              if args.action == 'record' else encode(args.input.resolve(), args.output.resolve()))
    print(json.dumps({k: report[k] for k in ('duration_seconds', 'observed_display_flip_fps',
                                            'presentation_is_even_two_vi')}))


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
