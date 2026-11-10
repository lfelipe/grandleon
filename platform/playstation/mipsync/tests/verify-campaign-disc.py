#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Verify every packaged campaign executable against its linked stage output."""
import hashlib
import json
from pathlib import Path
import sys

build = Path(sys.argv[1])
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
        entries[name] = (int.from_bytes(record[2:6], 'little'), int.from_bytes(record[10:14], 'little'))
        cursor += size
    verified = {}
    for stage in sorted((build / 'stages').iterdir()):
        if not stage.is_dir():
            continue
        name = (stage / 'source/generated/stage-filename.txt').read_text().strip()
        executable = stage / 'build/grandleon_mipsync.exe'
        if (stage / 'build/grandleon_mipsync.elf').stat().st_mtime_ns > executable.stat().st_mtime_ns:
            raise ValueError(f'{name} executable predates its ELF')
        actual = extent(*entries[name])
        if actual != executable.read_bytes():
            raise ValueError(f'campaign disc contains a different {name}')
        verified[name] = hashlib.sha256(actual).hexdigest()
with (build / 'game.bin').open('rb') as source:
    digest = hashlib.file_digest(source, 'sha256').hexdigest()
print(json.dumps({'campaign_bin_sha256': digest, 'verified_stages': verified}, indent=2))
