#!/usr/bin/env python3
"""Bind read-only campaign observations to the exact loaded function code."""
from pathlib import Path
import struct
import subprocess
import sys

build, nm, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
bindings = {'update': set(), 'ui': set()}
for stage in ('bell', 'deep', 'watch', 'village', 'gate'):
    directory = build / f'{stage}-build'
    symbols = subprocess.check_output([nm, '-S', str(directory / 'grandleon_mipsync.elf')], text=True)
    image = (directory / 'grandleon_mipsync.exe').read_bytes()
    assert image[:8] == b'PS-X EXE'
    base, length = struct.unpack_from('<II', image, 0x18)
    for kind, name in [('update', 'grandleon_runtime_update'), ('ui', 'grandleon_game_ui_state')]:
        found = [row.split() for row in symbols.splitlines() if row.split()[-1:] == [name]]
        assert len(found) == 1, f'{stage}: missing {name}'
        address, size = int(found[0][0], 16) & 0xffffffff, int(found[0][1], 16)
        offset = address - base
        assert 0 <= offset and 0 < size <= length - offset
        code = image[0x800 + offset:0x800 + offset + size]
        assert len(code) == size
        bindings[kind].add((address, code))
lines = ['return {']
for kind, entries in bindings.items():
    lines.append(kind + ' = {')
    for address, code in sorted(entries):
        escaped = ''.join(f'\\{byte:03d}' for byte in code)
        lines.append(f'{{address=0x{address:08x},code="{escaped}"}},')
    lines.append('},')
lines.append('}')
output.write_text('\n'.join(lines) + '\n')
print('MIPSYNC_CAMPAIGN_BINDINGS ' + ' '.join(f'{kind}={len(entries)}' for kind, entries in bindings.items()))
