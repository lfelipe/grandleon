#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reject direct-battle probe discs containing an older linked executable."""
import hashlib
from pathlib import Path
import sys


def verify(build: Path) -> str:
    executable = build / 'grandleon_mipsync.exe'
    elf = build / 'grandleon_mipsync.elf'
    disc = build / 'grandleon-mipsync.bin'
    if elf.stat().st_mtime_ns > executable.stat().st_mtime_ns:
        raise ValueError('PS1 executable predates its ELF; rebuild the iso target')
    with disc.open('rb') as stream:
        def sector(index: int) -> bytes:
            stream.seek(index * 2352)
            raw = stream.read(2352)
            if len(raw) != 2352 or raw[15] not in (1, 2):
                raise ValueError('invalid data sector in probe disc')
            offset = 24 if raw[15] == 2 else 16
            return raw[offset:offset + 2048]

        def extent(start: int, length: int) -> bytes:
            return b''.join(sector(start + i) for i in range((length + 2047) // 2048))[:length]

        pvd = sector(16)
        if pvd[:7] != b'\x01CD001\x01':
            raise ValueError('probe disc has no ISO9660 primary volume')
        root = pvd[156:190]
        directory = extent(int.from_bytes(root[2:6], 'little'), int.from_bytes(root[10:14], 'little'))
        cursor = 0
        while cursor < len(directory):
            size = directory[cursor]
            if size == 0:
                cursor = (cursor // 2048 + 1) * 2048
                continue
            record = directory[cursor:cursor + size]
            if size < 34 or len(record) != size:
                raise ValueError('invalid probe disc directory record')
            name = record[33:33 + record[32]]
            if name == b'PSX.EXE;1':
                data = extent(int.from_bytes(record[2:6], 'little'), int.from_bytes(record[10:14], 'little'))
                if data != executable.read_bytes():
                    raise ValueError('probe disc contains a different PSX.EXE; rebuild the iso target')
                with disc.open('rb') as verified_disc:
                    return hashlib.file_digest(verified_disc, 'sha256').hexdigest()
            cursor += size
    raise ValueError('probe disc has no PSX.EXE')


if __name__ == '__main__':
    try:
        print('PS1_PROBE_DISC_SHA256=' + verify(Path(sys.argv[1])))
    except (OSError, ValueError, IndexError) as error:
        sys.exit(str(error))
