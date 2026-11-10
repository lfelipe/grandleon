#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Write machine-readable provenance and memory gates for a Mipsync disc."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            hasher.update(chunk)
    return hasher.hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--cue", type=Path, required=True)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--scene-metrics", type=Path, required=True)
    parser.add_argument("--size-tool", type=Path, required=True)
    parser.add_argument("--nm-tool", type=Path, required=True)
    parser.add_argument("--upstream-base", required=True)
    parser.add_argument("--upstream-patched", required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    artifacts = (args.elf, args.exe, args.cue, args.bin)
    for path in artifacts + (args.scene_metrics, args.size_tool, args.nm_tool):
        if not path.is_file():
            raise ValueError(f"required build input is missing: {path}")
    marker = b"GRANDLEON_PS1_RENDERER=MIPSYNC"
    if marker not in args.exe.read_bytes() or marker not in args.bin.read_bytes():
        raise ValueError("Mipsync renderer identity is absent from the executable or disc")

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
        [str(args.nm_tool), "-n", str(args.elf)], text=True
    ).splitlines()
    end_address = None
    for line in nm_output:
        fields = line.split()
        if len(fields) >= 3 and fields[-1] in ("_end", "__heap_base"):
            end_address = int(fields[0], 16) & 0xFFFFFFFF
    if end_address is None:
        raise ValueError("ELF has no _end or __heap_base symbol")

    scene = json.loads(args.scene_metrics.read_text(encoding="utf-8"))
    audio_reserve_bytes = 65536
    renderer_probe_budget_bytes = 524288
    stack_reserve_bytes = 32768
    minimum_gameplay_heap_bytes = 524288
    main_ram_bytes = 2097152
    committed_bytes = (
        resident_bytes + audio_reserve_bytes + stack_reserve_bytes +
        minimum_gameplay_heap_bytes
    )
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
        "main_ram": {
            "text_bytes": text_bytes,
            "data_bytes": data_bytes,
            "bss_bytes": bss_bytes,
            "resident_bytes": resident_bytes,
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
        },
        "scene": scene,
    }
    if not result["main_ram"]["passes_playable_slice_budget"]:
        raise ValueError(
            "simulation, renderer, platform-audio, stack and minimum heap "
            f"exceed PS1 main RAM: {committed_bytes} > {main_ram_bytes}"
        )
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(
        "GRANDLEON_PS1_RENDERER=MIPSYNC "
        f"resident={resident_bytes} cue={args.cue} bin={args.bin}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        raise SystemExit(f"write_build_manifest.py: {error}") from error

