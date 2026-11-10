#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Prepare isolated generated content for the authored lethal presentation fixture."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import subprocess
import tempfile

source, output = map(Path, sys.argv[1:])
if output.exists():
    raise SystemExit('fixture destination must not exist')
spec = json.loads((Path(__file__).parent / 'fixtures/lethal-presentation.json').read_text())
original = (source / 'generated/simulation_data.cpp').read_text()
pattern = rf"(value.id={spec['unit_id']}ULL;[^{{}}]*?value.position=\{{5,1\}};\s*value.health=)15;"
modified, count = re.subn(pattern, rf"\g<1>{spec['health']};", original)
if count != 2:
    raise SystemExit(f'expected authored Bell raider in both player modes, found {count}')
shutil.copytree(source, output, symlinks=True)
(output / 'generated/simulation_data.cpp').write_text(modified)
# Derive both canonical hashes with the unmodified simulation. Preserve the
# runtime's strict content identity gate for this separate authored fixture.
repo = Path(__file__).resolve().parents[4]
with tempfile.TemporaryDirectory(prefix='grandleon-lethal-hash-') as scratch:
    scratch = Path(scratch)
    main = scratch / 'hash.cpp'
    main.write_text('#include "grandleon_simulation.hpp"\n#include <cstdio>\nint main(){for(unsigned p=1;p<=2;++p){auto c=grandleon::simulation::create_encounter(grandleon_generated_definition(p));if(!c)return 1;std::printf("%llu\\n",(unsigned long long)c.encounter.canonical_hash());}}\n')
    command = ['g++', '-std=c++17', '-O1', '-ffunction-sections', '-fdata-sections',
               '-Wl,--gc-sections', '-I' + str(output / 'generated')]
    command += ['-I' + str(repo / part) for part in
                ('engine/core/include', 'engine/campaign/include', 'engine/simulation/include', 'engine/tactics/include')]
    command += [str(main), str(output / 'generated/simulation_data.cpp'),
                str(repo / 'engine/simulation/src/encounter.cpp'),
                str(repo / 'engine/core/src/random.cpp'), '-o', str(scratch / 'hash')]
    subprocess.run(command, check=True)
    hashes = subprocess.check_output([str(scratch / 'hash')], text=True).splitlines()
header = output / 'generated/grandleon_simulation.hpp'
text, replacements = re.subn(r'return local_players > 1 \? \d+ULL : \d+ULL;',
    f'return local_players > 1 ? {hashes[1]}ULL : {hashes[0]}ULL;', header.read_text())
if replacements != 1:
    raise SystemExit('missing canonical identity declaration')
header.write_text(text)
spec['canonical_hashes'] = {'one_player': f'{int(hashes[0]):016x}', 'two_player': f'{int(hashes[1]):016x}'}

spec['base_simulation_sha256'] = hashlib.sha256(original.encode()).hexdigest()
spec['fixture_simulation_sha256'] = hashlib.sha256(modified.encode()).hexdigest()
(output / 'generated/lethal-fixture.json').write_text(json.dumps(spec, indent=2) + '\n')
print(json.dumps(spec, indent=2))
