#!/usr/bin/env python3
"""Generate the original soft I8 contact shadow used by the Pyrite slice."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    size = 32
    centre = (size - 1) * 0.5
    image = Image.new("L", (size, size))
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            dx = (x - centre) / centre
            dy = (y - centre) / (centre * 0.72)
            distance = math.sqrt(dx * dx + dy * dy)
            falloff = max(0.0, 1.0 - distance)
            pixels[x, y] = round(255.0 * falloff * falloff)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output, optimize=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
