#!/usr/bin/env python3
"""Check that all Cedar Watch canopy pages reach Mipsync's real draw path."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys

if len(sys.argv) != 3:
    raise SystemExit('usage: PSN00BSDK_ROOT=/path/to/sdk run-scenery-pages-probe.py build-directory empty-output-directory')
build, output = (Path(arg).resolve() for arg in sys.argv[1:])
if output.exists() and any(output.iterdir()):
    raise SystemExit('output directory must be empty')
output.mkdir(parents=True, exist_ok=True)
root = build.parent
sdk = Path(os.environ['PSN00BSDK_ROOT'])
probe = Path(__file__).resolve().parent
metrics = json.loads((root / 'source/generated/scene-metrics.json').read_text())
scene = (root / 'source/generated/scene_data.c').read_text()
count = metrics['entities']
surfaces = int(re.search(r'g_grandleon_board_surface_count = (\d+)u', scene)[1])
pages = metrics['terrain_chunks'] - surfaces
if count <= 64 or pages != 9 or metrics['map'] != {'width': 16, 'height': 12}:
    raise SystemExit('this regression needs the real nine-page Cedar Watch scene with more than 64 entries')
subprocess.run([sys.executable, str(probe / 'verify-probe-disc.py'), str(build)], check=True)
nm = subprocess.check_output([str(sdk / 'bin/mipsel-none-elf-nm'), '-S', str(build / 'grandleon_mipsync.elf')], text=True)
symbols = {}
for line in nm.splitlines():
    fields = line.split()
    if len(fields) == 4:
        symbols[fields[3]] = (int(fields[0][-8:], 16), int(fields[1], 16))
base, size = symbols['k_ps1_entities']
assert size % count == 0
header = (probe.parent / 'runtime/game_bridge.h').read_text()
abi = re.search(r'typedef struct grandleon_ui_state \{.*?\} grandleon_ui_state;', header, re.S)[0]
abi = re.sub(r'const char\s*\*', 'uint32_t', abi)
(output / 'campaign-ui-abi.lua').write_text("require('ffi').cdef[[\n" + abi + '\n]]\n')
environment = {
    'GRANDLEON_ENTITY_BASE': base, 'GRANDLEON_ENTITY_STRIDE': size // count,
    'GRANDLEON_ENTITY_COUNT': count, 'GRANDLEON_FOLIAGE_COUNT': pages,
    'GRANDLEON_FOLIAGE_ADDRESS': symbols['g_grandleon_foliage_entity_indices'][0],
    'GRANDLEON_DRAW_ADDRESS': symbols['draw_entity'][0],
    'GRANDLEON_UI_ADDRESS': symbols['grandleon_game_ui_state'][0],
    'GRANDLEON_UPDATE_ADDRESS': symbols['grandleon_game_update'][0],
    'GRANDLEON_MAP_ADDRESS': symbols['grandleon_game_map_controller'][0],
}
command = ['timeout', '--kill-after=10', '240', 'docker', 'run', '--rm', '--user', f'{os.getuid()}:{os.getgid()}', '--env', 'HOME=/tmp']
for name, value in environment.items():
    command.extend(['--env', f'{name}={value}'])
command.extend(['--volume', f'{build}:/disc:ro', '--volume', f'{probe}:/probe:ro', '--volume', f'{output}:/out', '--workdir', '/tmp',
                'grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687', 'stdbuf', '-oL', '-eL', 'pcsx-redux', '-no-ui', '-run', '-softgpu',
                '-bios', '/usr/local/share/pcsx-redux/openbios.bin', '-testmode', '-interpreter', '-debugger',
                '-dofile', '/probe/scenery_pages_probe.lua', '-iso', '/disc/grandleon-mipsync.cue'])
with (output / 'emulator.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
text = (output / 'emulator.log').read_text()
if result.returncode or 'MIPSYNC_SCENERY_FAIL ' in text or 'MIPSYNC_SCENERY_PASS pages=9' not in text:
    raise SystemExit(f'scenery page traversal failed; see {output / "emulator.log"}')
for name in ['south-west', 'south-middle', 'south-east', 'middle-east', 'middle-middle', 'middle-west', 'north-west', 'north-middle', 'north-east']:
    if (output / f'{name}.bin').stat().st_size != 320 * 240 * 2:
        raise SystemExit('incomplete framebuffer: ' + name)
report = {'entities': count, 'pages': pages, 'drawn_canopy_entities': sorted(map(int, re.findall(r'MIPSYNC_SCENERY_DRAWN entity=(\d+)', text))),
          'scope': 'Actual controller traversal and renderer draw calls; no entity, camera or simulation state writes.'}
(output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
