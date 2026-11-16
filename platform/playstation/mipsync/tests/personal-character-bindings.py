#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Read exact linked ELF layouts/symbols and packaged authored identities for Lua.

No runtime symbols, source, executable bytes, or game state are changed. DWARF
supplies target offsets; this deliberately refuses stripped/ambiguous builds.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess


def dwarf_layouts(elf, wanted=None):
    wanted = set(wanted or {'UnitSnapshot', 'EncounterSnapshot', 'ObjectiveResult'})
    found, stack = {}, []
    process = subprocess.Popen(['readelf', '--debug-dump=info', str(elf)], stdout=subprocess.PIPE, text=True)
    def finish():
        node = stack.pop()
        if stack and node['tag'] == 'member' and 'name' in node and 'offset' in node:
            stack[-1]['members'][node['name']] = node['offset']
        if node.get('name') in wanted and 'size' in node and node['members']:
            layout = dict(node['members'], size=node['size'])
            if node['name'] in found:
                assert found[node['name']] == layout, f'conflicting DWARF layout: {node["name"]}'
            found[node['name']] = layout
    for line in process.stdout:
        match = re.match(r'\s*<(\d+)><[0-9a-f]+>: Abbrev Number: \d+(?: \(DW_TAG_(\w+)\))?', line)
        if match:
            depth, tag = int(match[1]), match[2]
            while stack and stack[-1]['depth'] >= depth:
                finish()
            if tag:
                stack.append({'depth': depth, 'tag': tag, 'members': {}})
        elif stack:
            node = stack[-1]
            if 'DW_AT_name ' in line:
                node['name'] = line.strip().split(': ')[-1]
            elif 'DW_AT_byte_size ' in line:
                node['size'] = int(line.split(':')[-1].strip(), 0)
            elif 'DW_AT_data_member_location:' in line:
                value = line.split(':')[-1].strip()
                if value.isdecimal():
                    node['offset'] = int(value)
    while stack:
        finish()
    assert process.wait() == 0 and wanted <= found.keys(), f'missing target DWARF layouts: {elf}'
    return found


def lua(value):
    if isinstance(value, dict):
        return '{' + ','.join('[' + json.dumps(str(k)) + ']=' + lua(v) for k, v in value.items()) + '}'
    if isinstance(value, list):
        return '{' + ','.join(map(lua, value)) + '}'
    if isinstance(value, bytes):
        return '"' + ''.join(f'\\{byte:03d}' for byte in value) + '"'
    return json.dumps(value)


def stable_id(key):
    value = 14695981039346656037
    for byte in key.encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(value)


def prepare(build, output, nm):
    stages, bindings = [], {'update': {}, 'ui': {}}
    for directory in sorted((build / 'stages').iterdir()):
        if not directory.is_dir():
            continue
        manifest = json.loads((directory / 'one.json').read_text())
        two = json.loads((directory / 'two.json').read_text())
        campaign = manifest['campaign']
        current = next(n for n in campaign['nodes'] if n['id'] == campaign['current_node_id'])
        key = {'I. Casualties': 'casualties', 'II. Required Hero': 'required_loss', 'III. Return': 'recovery'}[current['name']]
        assert current['id'] == stable_id(key)
        assert current['encounter_id'] == stable_id('personal_rules/' + key)
        elf = directory / 'build/grandleon_mipsync.elf'
        image = (directory / 'build/grandleon_mipsync.exe').read_bytes()
        assert image[:8] == b'PS-X EXE'
        base, length = struct.unpack_from('<II', image, 0x18)
        symbols = [row.split() for row in subprocess.check_output([nm, '-S', str(elf)], text=True).splitlines()]
        def symbol(name, private=False):
            matches = [r for r in symbols if len(r) == 4 and (re.search(r'\d+' + re.escape(name) + r'E(?:\.lto_priv\.\d+)?$', r[-1]) if private else r[-1] == name)]
            assert len(matches) == 1, f'{elf}: ambiguous/missing {name}: {matches}'
            return int(matches[0][0], 16) & 0xffffffff, int(matches[0][1], 16)
        for kind, name in [('update', 'grandleon_runtime_update'), ('ui', 'grandleon_game_ui_state')]:
            address, size = symbol(name)
            offset = address - base
            assert 0 <= offset and 0 < size <= length - offset
            code = image[0x800 + offset:0x800 + offset + size]
            assert len(code) == size
            bindings[kind][(address, code)] = dict(address=address, code=code)
        members = {m['source_id']: m for m in campaign['members']}
        assert {m['name'] for m in members.values()} == {'Scholar', 'Perman', 'Willow', 'Rhea'}
        for name, source_key, policy in [('Scholar', 'scholar', 2), ('Perman', 'permanent', 1), ('Willow', 'withdraw', 2), ('Rhea', 'required', 3)]:
            member = next(m for m in members.values() if m['name'] == name)
            assert member['source_id'] == stable_id(source_key) and member['defeat_policy'] == policy
        scholar = next(m for m in members.values() if m['name'] == 'Scholar')
        assert list(map(str, scholar['starting_weapons'])) == [stable_id('traveller_blade')]
        assert set(map(str, scholar['weapon_affinities'])) == {stable_id('blade'), stable_id('focus')}
        assert scholar['states_weapon_affinities'] and len(scholar['weapon_affinities']) == 2
        assert scholar['states_starting_weapons'] and len(scholar['starting_weapons']) == 1
        units = {}
        for unit in manifest['simulation']['units']:
            name = members.get(unit['source_id'], {}).get('name', 'enemy_' + unit['source_id'])
            source_keys = {'Scholar': 'scholar', 'Perman': 'permanent', 'Willow': 'withdraw', 'Rhea': 'required'}
            if name in source_keys:
                assert unit['id'] == stable_id('personal_rules/' + key + '/' + source_keys[name])
            units[name] = dict(id=f'{int(unit["id"]):016x}', source=f'{int(unit["source_id"]):016x}', x=unit['position'][0], y=unit['position'][1])
        # Raw authored roles remain unchanged; personal kit is applied at runtime.
        raw_scholar = next(u for u in manifest['simulation']['units'] if u['source_id'] == scholar['source_id'])
        assert raw_scholar['weapon_ids'] != scholar['starting_weapons']
        memory = json.loads((directory / 'build-manifest.json').read_text())['main_ram']
        assert memory['passes_playable_slice_budget']
        assert memory['playable_slice_committed_bytes'] <= memory['main_ram_bytes']
        assert memory['minimum_gameplay_heap_bytes'] >= 524288
        assert memory['platform_audio_reserve_bytes'] >= 65536 and memory['stack_reserve_bytes'] >= 32768
        stages.append(dict(name=current['name'], node=f'{int(current["id"]):016x}', units=units,
                           snapshot=symbol('s_snapshot', True)[0], cursor=symbol('s_cursor', True)[0],
                           selected=symbol('s_selected', True)[0], layout=dwarf_layouts(elf),
                           sword=f'{int(scholar["starting_weapons"][0]):016x}',
                           raw_hashes=[manifest['canonical_hash'], two['canonical_hash']]))
    assert len(stages) == 3
    header = (Path(__file__).parent / '../runtime/game_bridge.h').read_text()
    state = re.search(r'typedef struct grandleon_ui_state \{.*?\} grandleon_ui_state;', header, re.S).group()
    state = re.sub(r'const char\s*\*', 'uint32_t', state)
    document = dict(stages=stages, **{k: list(v.values()) for k,v in bindings.items()})
    output.write_text("require('ffi').cdef[[\n" + state + '\n]]\nreturn ' + lua(document) + '\n')
    print('PERSONAL_CHARACTER_BINDINGS stages=3 packaged_source_ids=verified target_abi=DWARF')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('build', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--nm', default='nm')
    args = parser.parse_args()
    prepare(args.build, args.output, args.nm)
