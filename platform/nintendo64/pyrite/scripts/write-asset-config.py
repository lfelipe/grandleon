#!/usr/bin/env python3
"""Write one deterministic Pyrite bitmap sidecar into a staged project."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("uuid", type=int)
    parser.add_argument("--format", type=int, default=4)
    args = parser.parse_args()
    payload = {
        "baseScale": 16,
        "baseScaleOverride": 0,
        "compression": 0,
        "data": None,
        "exclude": False,
        "fontCharset": "",
        "fontId": 0,
        "format": args.format,
        "gltfBVH": False,
        "uuid": args.uuid,
        "wavCompression": 0,
        "wavForceMono": False,
        "wavResampleRate": 0,
    }
    args.path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
