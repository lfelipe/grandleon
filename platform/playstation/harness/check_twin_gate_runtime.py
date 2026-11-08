#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Check the production Twin Gate board, reveal, and camera-scroll captures."""

from __future__ import annotations

import argparse
import struct
from collections import Counter
from pathlib import Path


WIDTH = 320
HEIGHT = 240
FRAME_BYTES = WIDTH * HEIGHT * 2


def read_frame(path: Path) -> tuple[int, ...]:
    data = path.read_bytes()
    if len(data) != FRAME_BYTES:
        raise ValueError(f"{path}: {len(data)} bytes, expected {FRAME_BYTES}")
    return struct.unpack(f"<{WIDTH * HEIGHT}H", data)


def changed(lhs: tuple[int, ...], rhs: tuple[int, ...]) -> int:
    return sum(a != b for a, b in zip(lhs, rhs))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("frame_dir", type=Path)
    args = parser.parse_args()

    paths = sorted(args.frame_dir.glob("frame-*.bin"))
    # Opening, one ordinary cursor checkpoint, presentation-only cutaway and
    # the vertically scrolled last row. More ordinary client checkpoints are
    # harmless, but fewer means one required production state was never seen.
    if len(paths) < 4:
        raise SystemExit(
            f"error: only {len(paths)} production frames; expected at least 4"
        )
    frames = [read_frame(path) for path in paths]
    failed = False
    for index, pixels in enumerate(frames):
        counts = Counter(pixels)
        dominant = max(counts.values()) * 100 / len(pixels)
        representable = all((pixel & 0x8000) == 0 for pixel in pixels)
        print(
            f"RUNTIME FRAME {index} distinct={len(counts)} "
            f"dominant={dominant:.1f}% "
            f"representable={'yes' if representable else 'no'}"
        )
        # The complete board must retain the broad palette. A deliberately
        # closer crop holds fewer material families, but sixty-four colours is
        # still well above a flat primitive mock-up and catches a missing
        # texture/CLUT page without rejecting the focused western approach.
        minimum_colours = 128 if index == 0 else 64
        if len(counts) < minimum_colours or dominant >= 30.0 or not representable:
            failed = True

    from_opening = [changed(frames[0], frame) for frame in frames[1:]]
    for index, difference in enumerate(from_opening, 1):
        print(f"RUNTIME CHANGE 0->{index} pixels={difference}")
    # Cursor motion alone changes hundreds of pixels. Both the landmark
    # cutaway and the one-row camera scroll change thousands; requiring two
    # such states proves the full-map path and the production reveal both ran.
    substantial = sum(difference >= 5_000 for difference in from_opening)
    if substantial < 2:
        failed = True

    if failed:
        print("TWIN GATE PRODUCTION RESULT FAIL")
        return 1
    print("TWIN GATE PRODUCTION RESULT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
