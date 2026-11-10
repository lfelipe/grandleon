#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compile generated localized multi-speaker data, preserving order and cast IDs."""
import argparse
import json
import re
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
spec = spec_from_file_location('grandleon_simulation_generator', root / 'tools/generate_simulation.py')
module = module_from_spec(spec)
spec.loader.exec_module(module)
parser = argparse.ArgumentParser()
parser.add_argument('--compiler', default='g++')
parser.add_argument('--manifest', action='append', type=Path, default=[])
parser.add_argument('--scene-data', type=Path)
args = parser.parse_args()

languages = []
for code in ('en', 'es', 'pt-BR'):
    languages.append({'code': code, 'nodes': [
        {'id': 7, 'name': 'Parley', 'dialogue': 'obsolete first-line-only fallback',
         'dialogue_lines': [
             {'speaker': 'Vere', 'text': code + ': the envoy speaks first.', 'unit_type_id': '901'},
             {'speaker': 'Rowan', 'text': code + ': the commander answers.', 'unit_type_id': '902'},
             {'speaker': 'Masatora', 'text': code + ': the lord answers last.', 'unit_type_id': '903'}]},
        {'id': 8, 'name': 'Legacy', 'dialogue': code + ': compatible flat text'},
    ], 'moments': [{'dialogue_id': 9, 'lines': [
        {'speaker': 'Masatora', 'text': code + ': battle speech', 'unit_type_id': '903'}]}]})

emitted = '\n'.join(module.emit_localizations({'languages': languages}))
assert 'obsolete first-line-only fallback' not in emitted
prefix = '''#include <cstdint>
#include <vector>
#include <string_view>
#include <cassert>
struct grandleon_generated_dialogue_line {
 const char* speaker{}; const char* text{}; std::uint64_t unit_type_id{};
};
'''
checks = []
for index, code in enumerate(('en', 'es', 'pt-BR')):
    checks += [f'{{ const auto& lines = grandleon_generated_campaign_node_lines({index},7);',
               'assert(lines.size()==3);']
    for offset, (speaker, cast, text) in enumerate((('Vere',901,'the envoy speaks first.'),
                                                    ('Rowan',902,'the commander answers.'),
                                                    ('Masatora',903,'the lord answers last.'))):
        checks += [f'assert(std::string_view(lines[{offset}].speaker)=="{speaker}");',
                   f'assert(lines[{offset}].unit_type_id=={cast});',
                   f'assert(std::string_view(lines[{offset}].text)=="{code}: {text}");']
    checks += [f'assert(std::string_view(grandleon_generated_campaign_node_dialogue_for({index},7))==lines[0].text);',
               f'const auto& legacy=grandleon_generated_campaign_node_lines({index},8);',
               'assert(legacy.size()==1 && legacy[0].unit_type_id==0 && !legacy[0].speaker[0]);',
               f'assert(std::string_view(legacy[0].text)=="{code}: compatible flat text");',
               f'const auto& battle=grandleon_generated_moment_lines({index},9);',
               'assert(battle.size()==1 && battle[0].unit_type_id==903); }']
with tempfile.TemporaryDirectory(prefix='grandleon-structured-dialogue-') as directory:
    source = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    source.write_text(prefix + module.dialogue_span_declaration() + emitted + '\nint main(){\n' + '\n'.join(checks) + '\n}\n')
    subprocess.run([args.compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('PS1_STRUCTURED_DIALOGUE_PASS languages=3 speakers=3 ordered_lines=9 typed_battle_cast=3 legacy_flat=3')


def stable_id(key):
    result = 14695981039346656037
    for value in key.encode():
        result = ((result ^ value) * 1099511628211) & ((1 << 64) - 1)
    return result


project = json.loads((Path(__file__).resolve().parents[4] / 'games/steel_and_cedar/source/project.json').read_text())
source_dialogues = {value['id']: value for value in project['dialogues']}
source_nodes = project['campaigns'][0]['flow']['nodes']
translations = {value['language']: {entry['id']: entry['text'] for entry in value['entries']}
                for value in project.get('translations', [])}
for manifest in args.manifest:
    document = json.loads(manifest.read_text())
    languages = document['languages']
    for language in languages:
        translated = translations.get(language['code'], {})
        nodes = {int(node['id']): node for node in language['nodes']}
        expected_count = 0
        for node in source_nodes:
            expected = []
            for dialogue_id in node.get('dialogueIds', []):
                dialogue = source_dialogues[dialogue_id]
                cast = {entry['speaker']: stable_id(entry['unitTypeId']) for entry in dialogue.get('cast', [])}
                for index, line in enumerate(dialogue['lines']):
                    key = f'dialogues.{dialogue_id}.lines.{index}'
                    expected.append((translated.get(key + '.speaker', line['speaker']),
                                     translated.get(key + '.text', line['text']), cast.get(line['speaker'], 0)))
            actual = [(line['speaker'], line['text'], int(line.get('unit_type_id', 0)))
                      for line in nodes[stable_id(node['id'])]['dialogue_lines']]
            assert actual == expected, (manifest, language['code'], node['id'], actual, expected)
            expected_count += len(expected)
        assert expected_count >= 18
    print(f'PS1_STORY_SOURCE_AUDIT_PASS manifest={manifest.name} languages={len(languages)} lines_per_language={expected_count}')
if args.scene_data:
    source = args.scene_data.read_text()
    def values(symbol):
        match = re.search(r'const (?:uint64_t|unsigned int) ' + symbol + r'\[\] = \{(.*?)\};', source, re.S)
        assert match, symbol
        return [int(number) for number in re.findall(r'(\d+)(?:ULL|u)', match.group(1))]
    registry = dict(zip(values('g_grandleon_speaker_unit_type_ids'), values('g_grandleon_speaker_texture_indices')))
    for dialogue in source_dialogues.values():
        for cast in dialogue.get('cast', []):
            if cast['speaker'] in ('Vere', 'Masatora'):
                assert registry.get(stable_id(cast['unitTypeId']), 0), cast
    print('PS1_NPC_PORTRAIT_AUDIT_PASS Vere=present Masatora=present')
