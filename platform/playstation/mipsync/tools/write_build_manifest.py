#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write machine-readable provenance and memory gates for a Mipsync disc."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            hasher.update(chunk)
    return hasher.hexdigest()


def ram_frontier(resident_bytes: int, end_address: int) -> int:
    """Include the executable's reserved prefix and linker alignment in RAM use."""
    if not 0x80000000 <= end_address <= 0x80200000:
        raise ValueError("linked heap base is outside PS1 main RAM")
    frontier = end_address - 0x80000000
    if not 0 <= resident_bytes <= frontier:
        raise ValueError("linked sections exceed the physical RAM frontier")
    return frontier


def disc_contains_executable(path: Path, executable: bytes) -> bool:
    """Check ISO file data, where raw-sector headers cannot split the marker."""
    sectors = path.stat().st_size // 2352
    with path.open("rb") as stream:
        def sector(index: int) -> bytes:
            if index < 0 or index >= sectors:
                raise ValueError("disc extent is outside the image")
            stream.seek(index * 2352)
            raw = stream.read(2352)
            if raw[:12] != b"\x00" + b"\xff" * 10 + b"\x00":
                raise ValueError("disc extent is not a data sector")
            if raw[15] == 1:
                return raw[16:16 + 2048]
            if raw[15] == 2 and raw[16:20] == raw[20:24] and not raw[18] & 0x20:
                return raw[24:24 + 2048]
            raise ValueError("disc extent is not a Mode 1 or Mode 2 Form 1 sector")

        def extent(start: int, length: int) -> bytes:
            count = (length + 2047) // 2048
            if start + count > sectors:
                raise ValueError("disc file extends outside the image")
            return b"".join(sector(start + i) for i in range(count))[:length]

        pvd = sector(16)
        if pvd[:7] != b"\x01CD001\x01":
            raise ValueError("disc has no ISO9660 primary volume")
        root = pvd[156:190]
        directory = extent(int.from_bytes(root[2:6], "little"), int.from_bytes(root[10:14], "little"))
        cursor = 0
        while cursor < len(directory):
            size = directory[cursor]
            if not size:
                cursor = (cursor // 2048 + 1) * 2048
                continue
            record = directory[cursor:cursor + size]
            if size < 34 or len(record) != size or 33 + record[32] > size:
                raise ValueError("invalid disc directory record")
            name = record[33:33 + record[32]]
            length = int.from_bytes(record[10:14], "little")
            if not record[25] & 2 and name.endswith(b".EXE;1") and length == len(executable):
                if extent(int.from_bytes(record[2:6], "little"), length) == executable:
                    return True
            cursor += size
    return False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--exe", type=Path)
    parser.add_argument("--cue", type=Path)
    parser.add_argument("--bin", type=Path)
    parser.add_argument("--scene-metrics", type=Path)
    parser.add_argument("--size-tool", type=Path, required=True)
    parser.add_argument("--nm-tool", type=Path, required=True)
    parser.add_argument("--upstream-base")
    parser.add_argument("--upstream-patched")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--budget-only", action="store_true",
                        help="Write the linked RAM budget before disc packing. Exit 3 if it exceeds RAM.")
    args = parser.parse_args()
    if not args.budget_only:
        for name in ("exe", "cue", "bin", "scene_metrics", "upstream_base", "upstream_patched"):
            if getattr(args, name) is None:
                parser.error("--" + name.replace("_", "-") + " is required to verify a disc")
    return args


def linked_ram_budget(args):
    for path in (args.elf, args.size_tool, args.nm_tool):
        if not path.is_file():
            raise ValueError(f"required build input is missing: {path}")
    size_output = subprocess.check_output(
        [str(args.size_tool), str(args.elf)], text=True
    ).strip().splitlines()
    if len(size_output) < 2:
        raise ValueError("unexpected size-tool output")
    columns = size_output[-1].split()
    if len(columns) < 4:
        raise ValueError("size-tool did not report text, data and bss")
    text_bytes, data_bytes, bss_bytes = map(int, columns[:3])
    resident_bytes = text_bytes + data_bytes + bss_bytes

    nm_output = subprocess.check_output(
        [str(args.nm_tool), "-n", "-S", str(args.elf)], text=True
    ).splitlines()
    end_address = None
    packet_arena_bytes = None
    ordering_table_bytes = None
    for line in nm_output:
        fields = line.split()
        if fields:
            fields[-1] = re.sub(r"\.lto_priv\.\d+$", "", fields[-1])
        if len(fields) >= 3 and fields[-1] in ("_end", "__heap_base"):
            end_address = int(fields[0], 16) & 0xFFFFFFFF
        if len(fields) == 4 and fields[-1] == "grandleon_packet_arena":
            packet_arena_bytes = int(fields[1], 16)
        if len(fields) == 4 and fields[-1] == "s_ordering_table":
            ordering_table_bytes = int(fields[1], 16)
    if end_address is None:
        raise ValueError("ELF has no _end or __heap_base symbol")

    packet_bytes_per_frame = 57344
    if packet_arena_bytes != packet_bytes_per_frame or ordering_table_bytes != 8192:
        raise ValueError("linked packet arenas do not match the scene command budget")
    audio_reserve_bytes = 65536
    renderer_probe_budget_bytes = 524288
    stack_reserve_bytes = 32768
    minimum_gameplay_heap_bytes = 524288
    main_ram_bytes = 2097152
    occupied_prefix_bytes = ram_frontier(resident_bytes, end_address)
    committed_bytes = (
        occupied_prefix_bytes + audio_reserve_bytes + stack_reserve_bytes +
        minimum_gameplay_heap_bytes
    )
    return {
        "text_bytes": text_bytes,
        "data_bytes": data_bytes,
        "bss_bytes": bss_bytes,
        "resident_bytes": resident_bytes,
        "occupied_ram_prefix_bytes": occupied_prefix_bytes,
        "reserved_prefix_and_alignment_bytes": occupied_prefix_bytes - resident_bytes,
        "packet_bytes_per_frame": packet_bytes_per_frame,
        "packet_arena_bytes": packet_arena_bytes,
        "packet_arena_count": 1,
        "ordering_table_bytes": ordering_table_bytes,
        "end_address": f"0x{end_address:08x}",
        "renderer_probe_budget_bytes": renderer_probe_budget_bytes,
        "platform_audio_reserve_bytes": audio_reserve_bytes,
        "stack_reserve_bytes": stack_reserve_bytes,
        "minimum_gameplay_heap_bytes": minimum_gameplay_heap_bytes,
        "main_ram_bytes": main_ram_bytes,
        "renderer_and_audio_reserve_bytes": resident_bytes + audio_reserve_bytes,
        "passes_renderer_only_probe_budget": (
            resident_bytes + audio_reserve_bytes <= renderer_probe_budget_bytes
        ),
        "playable_slice_committed_bytes": committed_bytes,
        "passes_playable_slice_budget": committed_bytes <= main_ram_bytes,
    }


def main() -> int:
    args = parse_args()
    memory = linked_ram_budget(args)
    if args.budget_only:
        args.output.write_text(json.dumps(memory, indent=2) + "\n", encoding="utf-8")
        return 0 if memory["passes_playable_slice_budget"] else 3
    if not memory["passes_playable_slice_budget"]:
        raise ValueError(
            "simulation, renderer, platform-audio, stack and minimum heap "
            f"exceed PS1 main RAM: {memory['playable_slice_committed_bytes']} > {memory['main_ram_bytes']}"
        )
    artifacts = (args.elf, args.exe, args.cue, args.bin)
    for path in artifacts + (args.scene_metrics,):
        if not path.is_file():
            raise ValueError(f"required build input is missing: {path}")
    marker = b"GRANDLEON_PS1_RENDERER=MIPSYNC"
    executable = args.exe.read_bytes()
    if marker not in executable:
        raise ValueError("Mipsync renderer identity is absent from the executable")
    if not disc_contains_executable(args.bin, executable):
        raise ValueError("Mipsync disc does not contain the linked executable")
    result = {
        "schema": "grandleon.mipsync.build.v1",
        "renderer": "mipsync",
        "identity": marker.decode("ascii"),
        "upstream": {
            "base_revision": args.upstream_base,
            "patched_revision": args.upstream_patched,
        },
        "artifacts": {
            path.name: {"bytes": path.stat().st_size, "sha256": digest(path)}
            for path in artifacts
        },
        "main_ram": memory,
        "scene": json.loads(args.scene_metrics.read_text(encoding="utf-8")),
    }
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        "GRANDLEON_PS1_RENDERER=MIPSYNC "
        f"resident={memory['resident_bytes']} cue={args.cue} bin={args.bin}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        raise SystemExit(f"write_build_manifest.py: {error}") from error

