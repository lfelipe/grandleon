#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the five real-framebuffer states of the Twin Gate quality proof."""

from __future__ import annotations

import argparse
import struct
from collections import Counter
from pathlib import Path


WIDTH = 320
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2
STATE_COUNT = 5


def read_frame(path: Path) -> tuple[int, ...]:
    data = path.read_bytes()
    if len(data) != FRAME_BYTES:
        raise ValueError(
            f"{path}: {len(data)} bytes, expected {FRAME_BYTES} for "
            f"{WIDTH}x{HEIGHT} 15-bit output"
        )
    return struct.unpack(f"<{WIDTH * HEIGHT}H", data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("frame_dir", type=Path)
    args = parser.parse_args()

    paths = sorted(args.frame_dir.glob("frame-*.bin"))
    if len(paths) != STATE_COUNT:
        raise SystemExit(
            f"error: {args.frame_dir} has {len(paths)} checkpoint frames; "
            f"the reveal must produce exactly {STATE_COUNT}"
        )

    frames = [read_frame(path) for path in paths]
    failed = False
    for index, pixels in enumerate(frames):
        counts = Counter(pixels)
        dominant = max(counts.values()) * 100 / len(pixels)
        representable = all((pixel & 0x8000) == 0 for pixel in pixels)
        print(
            f"FRAME {index} distinct={len(counts)} dominant={dominant:.1f}% "
            f"representable={'yes' if representable else 'no'}"
        )
        # The measured proof currently holds 200+ colours and a 17-19%
        # dominant colour. These deliberately looser floors catch lost texture,
        # an empty keep page, or a blank framebuffer without pinning dithering
        # noise or a particular palette revision.
        if len(counts) < 128 or dominant >= 25.0 or not representable:
            failed = True

    changes = []
    for index in range(1, len(frames)):
        changed = sum(a != b for a, b in zip(frames[index - 1], frames[index]))
        changes.append(changed)
        print(f"TRANSITION {index - 1}->{index} changed={changed}")
        # Each authored threshold state changes at least 15k pixels in the
        # measured production run. Ten thousand distinguishes a staged reveal
        # from a blinking label while allowing palette and camera refinement.
        if changed < 10_000:
            failed = True

    exterior_to_interior = sum(a != b for a, b in zip(frames[0], frames[-1]))
    print(f"REVEAL exterior->interior changed={exterior_to_interior}")
    if exterior_to_interior < 20_000:
        failed = True

    if failed:
        print("TWIN GATE FRAMEBUFFER RESULT FAIL")
        return 1
    print("TWIN GATE FRAMEBUFFER RESULT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
