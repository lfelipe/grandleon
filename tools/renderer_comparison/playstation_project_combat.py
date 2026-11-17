#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Record real Steel and Cedar campaign combat, including its authored hero packs.

The normal title/player/story path precedes the existing two-controller Bell
Orchard replay. The display observer and audio/camera assertions are unchanged.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import playstation_native_motion as native


def record(build, output):
    native.empty_directory(output)
    with (output / 'disc-verification.txt').open('w') as log:
        subprocess.run(['python3', str(native.ROOT / 'platform/playstation/mipsync/tests/verify-campaign-disc.py'),
                        str(build)], check=True, stdout=log)
    stage = build / 'stages/01/build'
    rows = [line.split() for line in subprocess.check_output(
        ['nm', '-S', str(stage / 'grandleon_mipsync.elf')], text=True).splitlines()]

    def symbol(name, private=False):
        pattern = (r'\d+' + re.escape(name) + r'E' if private else '^' + re.escape(name)) + r'(?:\.lto_priv\.\d+)?$'
        found = [r for r in rows if len(r) == 4 and re.search(pattern, r[-1])]
        native.require(len(found) == 1, f'ambiguous/missing linked symbol: {name}')
        return int(found[0][0], 16) & 0xffffffff

    module_path = native.ROOT / 'platform/playstation/mipsync/tests/personal-character-bindings.py'
    spec = importlib.util.spec_from_file_location('ps1_bindings', module_path)
    bindings = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bindings)
    layout = bindings.dwarf_layouts(stage / 'grandleon_mipsync.elf', {'grandleon_ui_state'})['grandleon_ui_state']
    values = {'UPDATE':symbol('grandleon_game_update'), 'AUDIO_CUE':symbol('grandleon_audio_cue'),
              'RESULT_DURATION':symbol('s_result_duration', True), 'RESULT_FRAMES':symbol('s_result_frames', True),
              'CURSOR':symbol('s_cursor', True), 'PLAYER_CURSORS':symbol('s_player_cursors', True),
              'AI_ACTIVE':symbol('s_ai_active', True)}
    probe = output / 'probe'
    shutil.copytree(native.ROOT / 'platform/playstation/mipsync/tests', probe,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    shutil.copy2(probe / 'combat_audio_probe.lua', probe / 'combat_audio_original.lua')
    prefix = (f'grandleon_project_ui_address=0x{symbol("grandleon_game_ui_state"):08x}\n'
              f'grandleon_project_settled_offset={layout["camera_settled"]}\n'
              f'grandleon_campaign_menu_address=0x{symbol("s_campaign_menu_index", True):08x}\n'
              "dofile('/probe/project_combat_gate.lua')\n"
              'grandleon_capture_first_update=90\ngrandleon_capture_last_update=1000\n')
    (probe / 'combat_audio_probe.lua').write_text(prefix +
        (native.ROOT / 'tools/renderer_comparison/ps1_native_motion_observer.lua').read_text())
    controller = probe / 'combat_probe.lua'
    text = controller.read_text()
    needle = '  runtime_frame = runtime_frame + 1\n'
    native.require(text.count(needle) == 1, 'existing controller observation seam changed')
    # The UI gate advances the actual opening dialogue. Do not repeat the
    # standalone replay's fixed opening presses once the board is ready.
    text = re.sub(r"^actions\[(?:10|20|40|60|80|90)\] = 0x4000\n", "", text, flags=re.M)
    # Personal packs add a Pack row before Attack/Ability. Drive the real
    # menu to those actions instead of treating its first row as Attack.
    attack = '  0x4000, 0x0000, 0x4000, -- Attack auto-focuses legal target 5,1; commit'
    ability = '  0x4000,                 -- Ability 1 (Attack absent without legal targets)'
    native.require(text.count(attack) == 1 and text.count(ability) == 1,
                   'review the replay when its action-menu sequence changes')
    text = text.replace(attack, '  0x0040, 0x4000, 0x4000, -- Down to Attack, auto-focus, commit')
    text = text.replace(ability, '  0x0040, 0x4000,         -- Down past Pack, choose Ability 1')
    controller.write_text(text.replace(needle, needle +
        '  if grandleon_presentation_tick then grandleon_presentation_tick(runtime_frame) end\n'))
    (output / 'linked-addresses.json').write_text(json.dumps(values, indent=2) + '\n')
    capture = output / 'capture'
    capture.mkdir()
    image = os.environ.get('GRANDLEON_PLAYSTATION_EMULATOR_IMAGE',
                           'grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687')
    command = ['timeout', '--kill-after=10', '180', 'docker', 'run', '--rm', '--user',
               f'{os.getuid()}:{os.getgid()}', '--env', 'HOME=/tmp']
    for name, address in values.items(): command += ['--env', f'GRANDLEON_{name}_ADDRESS=0x{address:08x}']
    command += ['-v', f'{build}:/disc:ro', '-v', f'{probe}:/probe:ro', '-v', f'{capture}:/out',
                '-w', '/tmp', image, 'stdbuf', '-oL', '-eL', 'pcsx-redux', '-no-ui', '-run', '-softgpu',
                '-bios', '/usr/local/share/pcsx-redux/openbios.bin', '-testmode', '-interpreter', '-debugger',
                '-memcard1', '/out/campaign.mcd', '-dofile', '/probe/combat_audio_probe.lua', '-iso', '/disc/game.cue']
    with (output / 'emulator.log').open('w') as log:
        subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
    text = (output / 'emulator.log').read_text()
    for marker in ['MIPSYNC_PROJECT_COMBAT_STARTED ordinary_campaign=1',
                   'MIPSYNC_CAMPAIGN_READY card=1 continue=0',
                   'MIPSYNC_SIM_PROOF', 'p_attack=1 p_ability=1',
                   'MIPSYNC_COMBAT_AUDIO_PASS', 'MIPSYNC_PLAYER_CAMERA_PASS',
                   'MIPSYNC_PRESENTATION_CAPTURE frames=']:
        native.require(marker in text, f'actual campaign combat did not report {marker}')
    native.require('SCRIPT ERROR' not in text and 'Lua error' not in text and 'threw an exception' not in text, 'native probe raised an error')
    report = native.encode(capture, output / 'media')
    report['scope'] = 'Normal exported Steel and Cedar campaign: real title, 2P selection, authored story, personal hero equipment, ordinary combat/controller input and unchanged audio/camera assertions.'
    (output / 'media/coverage.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({key: report[key] for key in ('observed_display_flip_fps', 'complete_frame_hold_histogram_vis')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    try:
        record(args.build.resolve(), args.output.resolve())
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        raise SystemExit(str(error)) from error
