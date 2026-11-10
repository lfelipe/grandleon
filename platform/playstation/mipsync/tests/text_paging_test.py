#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise production PS1 wrapping/navigation on every shipping catalog string."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

repo = Path(__file__).resolve().parents[4]

def strings(value):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from strings(item)
    elif isinstance(value, list):
        for item in value:
            yield from strings(item)

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path)
args = parser.parse_args()

project = json.loads((repo / 'games/steel_and_cedar/source/project.json').read_text())
payload = ('\0'.join(strings(project)) + '\0').encode()
if args.binary:
    subprocess.run([str(args.binary.resolve())], input=payload, check=True)
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='grandleon-ps1-paging-') as directory:
    binary = Path(directory) / 'test'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(repo / 'platform/view/include'),
                    '-I' + str(repo / 'platform/playstation/mipsync/runtime'),
                    str(Path(__file__).with_suffix('.cpp')), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], input=payload, check=True)
