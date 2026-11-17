#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify every packaged campaign executable against its linked stage output."""
import hashlib
import json
from pathlib import Path
import re
import sys

build = Path(sys.argv[1])
planned = [
    line.split('\t')[1]
    for line in (build / 'campaign-plan.tsv').read_text().splitlines()
    if line.startswith('stage\t')
]
if not planned or len(set(planned)) != len(planned):
    raise ValueError('campaign plan must list unique stages')

with (build / 'game.bin').open('rb') as stream:
    def sector(index):
        stream.seek(index * 2352)
        raw = stream.read(2352)
        if len(raw) != 2352 or raw[15] not in (1, 2):
            raise ValueError('invalid campaign data sector')
        offset = 24 if raw[15] == 2 else 16
        return raw[offset:offset + 2048]

    def extent(start, length):
        return b''.join(sector(start + i) for i in range((length + 2047) // 2048))[:length]

    pvd = sector(16)
    if pvd[:7] != b'\x01CD001\x01':
        raise ValueError('campaign disc has no ISO9660 primary volume')
    record = pvd[156:190]
    directory = extent(int.from_bytes(record[2:6], 'little'), int.from_bytes(record[10:14], 'little'))
    entries = {}
    cursor = 0
    while cursor < len(directory):
        size = directory[cursor]
        if not size:
            cursor = (cursor // 2048 + 1) * 2048
            continue
        record = directory[cursor:cursor + size]
        name = record[33:33 + record[32]].decode('ascii').removesuffix(';1')
        if name in entries:
            raise ValueError(f'duplicate disc entry: {name}')
        entries[name] = (int.from_bytes(record[2:6], 'little'), int.from_bytes(record[10:14], 'little'))
        cursor += size
    verified = {}
    boot_stages = []
    for index in planned:
        stage = build / 'stages' / index
        name = (stage / 'source/generated/stage-filename.txt').read_text().strip()
        if not re.fullmatch(r'STG[0-9]{2}\.EXE', name) or name in verified:
            raise ValueError(f'invalid or duplicate executable name for stage {index}: {name}')
        if (stage / 'source/generated/stage-is-boot.txt').read_text().strip() == '1':
            boot_stages.append(name)
        executable = stage / 'build/grandleon_mipsync.exe'
        if (stage / 'build/grandleon_mipsync.elf').stat().st_mtime_ns > executable.stat().st_mtime_ns:
            raise ValueError(f'{name} executable predates its ELF')
        actual = extent(*entries[name])
        if actual != executable.read_bytes():
            raise ValueError(f'campaign disc contains a different {name}')
        verified[name] = hashlib.sha256(actual).hexdigest()
    if set(verified) != {name for name in entries if re.fullmatch(r'STG[0-9]{2}\.EXE', name)}:
        raise ValueError('campaign disc stage list differs from the export plan')
    config = extent(*entries['SYSTEM.CNF']).decode('ascii')
    boot = re.findall(r'(?m)^BOOT\s*=\s*cdrom:\\(STG[0-9]{2}\.EXE);1\s*$', config)
    if len(boot_stages) != 1 or boot != boot_stages:
        raise ValueError('SYSTEM.CNF does not boot the authored opening stage')
with (build / 'game.bin').open('rb') as source:
    digest = hashlib.file_digest(source, 'sha256').hexdigest()
print(json.dumps({'campaign_bin_sha256': digest, 'boot_stage': boot[0], 'verified_stages': verified}, indent=2))
