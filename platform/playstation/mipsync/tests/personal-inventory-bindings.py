#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read linked production ELF layouts for the two-stage inventory probe.

The fixture is exported by personal_inventory_fixture.gd. Addresses and sizes
come from the actual linked artifacts; the probe only writes controller inputs.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--nm', default='nm')
parser.add_argument('--weapon-choice', action='store_true')
parser.add_argument('--hero', default='aren', help='Authored roster member ID')
parser.add_argument('--stages', nargs='+', default=['01', '02'])
args = parser.parse_args()
path = Path(__file__).with_name('personal-character-bindings.py')
spec = importlib.util.spec_from_file_location('bindings', path)
bindings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bindings)
modules = []
for stage in args.stages:
    directory = args.build / 'stages' / stage
    elf = directory / 'build/grandleon_mipsync.elf'
    image = (directory / 'build/grandleon_mipsync.exe').read_bytes()
    assert image[:8] == b'PS-X EXE'
    base, length = struct.unpack_from('<II', image, 0x18)
    rows = [line.split() for line in subprocess.check_output(
        [args.nm, '-S', str(elf)], text=True).splitlines()]

    def symbol(name, private=False):
        found = [row for row in rows if len(row) == 4 and (
            re.search(r'\d+' + re.escape(name) + r'E(?:\.lto_priv\.\d+)?$', row[-1])
            if private else re.fullmatch(re.escape(name) + r'(?:\.lto_priv\.\d+)?', row[-1]))]
        assert len(found) == 1, (name, found)
        return int(found[0][0], 16), int(found[0][1], 16)

    private_names = ('s_campaign_mode', 's_mode', 's_snapshot', 's_selected',
                     's_menu_index', 's_cursor', 's_result_frames',
                     's_camera_return_frames', 's_campaign_save', 's_motion')
    symbols = {name: symbol(name, True)[0] for name in private_names}
    if args.weapon_choice:
        for name in ('s_attack_weapon', 's_last_result_damage'):
            symbols[name] = symbol(name, True)[0]
    symbols['grandleon_runtime_update'] = symbol('grandleon_runtime_update')[0]
    address, size = symbol('g_grandleon_unit_ids')
    offset = address - base
    assert 0 <= offset and size <= length - offset
    manifest = json.loads((directory / 'one.json').read_text())
    heroes = [unit for unit in manifest['simulation']['units']
              if str(unit['source_id']) == bindings.stable_id(args.hero)]
    assert len(heroes) == 1
    modules.append({
        'name': stage,
        'layout': bindings.dwarf_layouts(elf, {
            'UnitSnapshot', 'EncounterSnapshot', 'CarriedStack', 'GroundItem',
            'CampaignSave', 'CampaignState', 'InventoryStack', 'ContentRef',
            'BoardMotion',
        }),
        'symbols': symbols,
        'hero_id': format(int(heroes[0]['id']), '016x'),
        'signature': {'address': address,
                      'code': image[0x800 + offset:0x800 + offset + size]},
    })
args.output.write_text('return ' + bindings.lua(modules) + '\n')
print('PS1_PACK_PROBE_BINDINGS stages=' + str(len(modules)))
