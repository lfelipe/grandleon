#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Normalize PSn00bSDK ELF symbol tables for GNU binutils 2.42.

The SDK's archives are valid to its own linker, but some members interleave
local and global symbols while recording an incompatible ``sh_info`` boundary.
This groups locals first, remaps symbol-index references in relocations, groups
and extended section-index tables, updates ``sh_info`` and rebuilds the archive
in its original member order.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def normalize_elf(path: Path) -> None:
    data = bytearray(path.read_bytes())
    if data[:6] != b"\x7fELF\x01\x01":
        raise ValueError(f"archive member is not ELF32 little-endian: {path.name}")
    section_offset = struct.unpack_from("<I", data, 32)[0]
    section_size = struct.unpack_from("<H", data, 46)[0]
    section_count = struct.unpack_from("<H", data, 48)[0]
    sections = [section_offset + index * section_size for index in range(section_count)]
    for index in range(section_count):
        header = sections[index]
        section_type = struct.unpack_from("<I", data, header + 4)[0]
        if section_type != 2:  # SHT_SYMTAB
            continue
        symbols_offset = struct.unpack_from("<I", data, header + 16)[0]
        symbols_size = struct.unpack_from("<I", data, header + 20)[0]
        entry_size = struct.unpack_from("<I", data, header + 36)[0]
        if entry_size == 0 or symbols_size % entry_size:
            raise ValueError(f"malformed symbol table in {path.name}")
        count = symbols_size // entry_size
        locals_ = [
            symbol for symbol in range(count)
            if data[symbols_offset + symbol * entry_size + 12] >> 4 == 0
        ]
        globals_ = [symbol for symbol in range(count) if symbol not in set(locals_)]
        order = locals_ + globals_
        mapping = {old: new for new, old in enumerate(order)}
        original = bytes(data[symbols_offset:symbols_offset + symbols_size])
        for new, old in enumerate(order):
            begin = old * entry_size
            data[symbols_offset + new * entry_size:
                 symbols_offset + (new + 1) * entry_size] = original[begin:begin + entry_size]
        struct.pack_into("<I", data, header + 28, len(locals_))

        for related in sections:
            related_type = struct.unpack_from("<I", data, related + 4)[0]
            related_link = struct.unpack_from("<I", data, related + 24)[0]
            if related_link != index:
                continue
            if related_type in (4, 9):  # SHT_RELA, SHT_REL
                offset = struct.unpack_from("<I", data, related + 16)[0]
                size = struct.unpack_from("<I", data, related + 20)[0]
                stride = struct.unpack_from("<I", data, related + 36)[0]
                for relocation in range(0, size, stride):
                    info_offset = offset + relocation + 4
                    info = struct.unpack_from("<I", data, info_offset)[0]
                    symbol, kind = info >> 8, info & 0xff
                    struct.pack_into("<I", data, info_offset, mapping[symbol] << 8 | kind)
            elif related_type == 17:  # SHT_GROUP: sh_info is its signature symbol.
                signature = struct.unpack_from("<I", data, related + 28)[0]
                struct.pack_into("<I", data, related + 28, mapping[signature])
            elif related_type == 18:  # SHT_SYMTAB_SHNDX parallels the symbols.
                offset = struct.unpack_from("<I", data, related + 16)[0]
                size = struct.unpack_from("<I", data, related + 20)[0]
                original_indices = bytes(data[offset:offset + size])
                for new, old in enumerate(order):
                    data[offset + new * 4:offset + new * 4 + 4] = \
                        original_indices[old * 4:old * 4 + 4]
    path.write_bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ar", type=Path, required=True)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    members = subprocess.check_output(
        [str(args.ar), "t", str(args.source)], text=True
    ).splitlines()
    if len(members) != len(set(members)):
        raise ValueError("archive contains duplicate member names")
    with tempfile.TemporaryDirectory(prefix="grandleon-psn00b-normalize-") as raw:
        directory = Path(raw)
        subprocess.run(
            [str(args.ar), "x", str(args.source.resolve())], cwd=directory,
            check=True,
        )
        for member in members:
            normalize_elf(directory / member)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [str(args.ar), "rcs", str(args.output.resolve()), *members],
            cwd=directory, check=True,
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"normalize_psn00b_archive.py: {error}") from error
