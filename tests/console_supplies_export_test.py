#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Legacy starting stock and chapter grants survive both console export paths."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import tempfile

REPO = Path(__file__).resolve().parents[1]

def run(*args):
    return subprocess.run(list(map(str, args)), capture_output=True, text=True, check=False)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content-compiler', type=Path, required=True)
    parser.add_argument('--scene-export', type=Path, required=True)
    args = parser.parse_args()
    base = json.loads((REPO / 'platform/playstation/mipsync/tests/fixtures/personal-character-campaign.json').read_text())
    with tempfile.TemporaryDirectory(prefix='grandleon-console-supplies-') as temporary:
        work = Path(temporary)
        source, package, output = (work / name for name in ['project.json', 'game.gpk', 'scene.json'])
        source.write_text(json.dumps(base))
        art = work / 'art'
        made = run('python3', REPO / 'platform/console_art/generate-character-art.py', '--project', source,
                   '--platform', 'n64', '--output', art)
        assert made.returncode == 0, made.stderr
        manifest = art / 'manifest.json'
        prepare = REPO / 'platform/nintendo64/pyrite/scripts/prepare-project.py'
        for case in ['baseline', 'empty', 'starting_weapon', 'stage_weapon', 'starting_item', 'stage_item']:
            project = copy.deepcopy(base)
            campaign = project['campaigns'][0]
            if case == 'empty':
                campaign['startingStore'] = []
                for node in campaign['flow']['nodes']: node['grants'] = []
            elif case not in ['baseline', 'empty']:
                project['items'].append({'id': 'supply', 'name': 'Supply', 'stackLimit': 3})
                grant = {'quantity': 1, **({'weaponId': 'raider_blade'} if case.endswith('weapon') else {'itemId': 'supply'})}
                if case.startswith('starting'): campaign['startingStore'] = [grant]
                else: campaign['flow']['nodes'][0]['grants'] = [grant]
            source.write_text(json.dumps(project))
            source_bytes = source.read_bytes()
            compiled = run(args.content_compiler, source, package)
            assert compiled.returncode == 0, compiled.stderr
            for players in [1, 2]:
                exported = run(args.scene_export, package, 'personal_rules/casualties', players, output)
                assert exported.returncode == 0, exported.stderr
                manifest_data = json.loads(output.read_text())
                grants = manifest_data['campaign']['grants']
                if case in ['baseline', 'empty']:
                    assert not grants
                else:
                    grant, = grants
                    assert grant['quantity'] == 1
                    assert bool(int(grant['node_id'])) == case.startswith('stage')
                    category = 'weapon' if case.endswith('weapon') else 'item'
                    assert int(grant[category + '_id'])
                    assert any(grant[category + '_id'] == definition['id']
                               for definition in manifest_data['simulation'][category + 's'])
            target = work / ('n64-' + case)
            for directory in ['assets', 'generated', 'src/user']: (target / directory).mkdir(parents=True, exist_ok=True)
            exported = run('python3', prepare, '--project', source, '--art-manifest', manifest, '--project-root', target)
            assert exported.returncode == 0, exported.stderr
            assert (target / 'src/user/GeneratedProject.hpp').is_file()
            assert source.read_bytes() == source_bytes, 'Export changed author source'
            print('PASS console supplies:', case)

if __name__ == '__main__': main()
