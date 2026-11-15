#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Match tagged Ares grabs to a bounded native VI-origin presentation trace.

This decoder is calibrated for the pinned 1024x768 Ares capture window and the
review-only marker used in n64-native-movement-06.json. It crops window chrome
and the marker, preserves every observed presentation, and refuses missing
frames. It does not create motion or change campaign/runtime state.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import re

from PIL import Image


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=Path)
    parser.add_argument("--log", type=Path, default=Path("ares/final.log"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.capture_dir.resolve()
    lines = (root / args.log).read_text().splitlines()
    header = next(line for line in lines if line.startswith("GRANDLEON_N64_NATIVE_TRACE "))
    fields = dict(re.findall(r"(\w+)=(\d+)", header))
    if fields["overflow"] != "0":
        raise SystemExit("Presentation trace overflowed; refusing to infer lost frames")
    trace = [dict(zip(("id", "vi", "us", "contexts"), map(int, item.split(":"))))
             for line in lines if line.startswith("GRANDLEON_N64_NATIVE_FRAMES ")
             for item in line.split()[1:]]
    if len(trace) != int(fields["count"]) or len(trace) < 2:
        raise SystemExit("Incomplete presentation trace")
    if any(b["id"] <= a["id"] or b["vi"] <= a["vi"] or b["us"] <= a["us"]
           for a, b in zip(trace, trace[1:])):
        raise SystemExit("Presentation trace is not monotonic")
    wanted = {frame["id"] for frame in trace}
    images: dict[int, list[tuple[str, Path]]] = defaultdict(list)
    rejects = 0
    for path in sorted((root / "ares/frames").glob("*.png")):
        with Image.open(path) as raw:
            if raw.size != (1024, 768):
                raise SystemExit("Capture size does not match marker calibration")
            image = raw.convert("RGB")
            pixels = image.load()
            r, g, b = pixels[240, 85]
            yr, yg, yb = pixels[247, 85]
            cr, cg, cb = pixels[119, 85]
            if not (r > 180 and g < 90 and b < 90 and yr > 180 and yg > 180 and
                    yb < 90 and cr < 90 and cg > 180 and cb > 180):
                rejects += 1
                continue
            frame_id = sum(1 << bit for bit in range(16)
                           if sum(pixels[round(125 + 7.2 * bit), 85]) > 384)
            if frame_id in wanted:
                crop = image.crop((118, 91, 868, 657))
                images[frame_id].append((hashlib.sha256(crop.tobytes()).hexdigest(), path))
    missing = sorted(wanted - images.keys())
    if missing:
        raise SystemExit(f"Missing {len(missing)} presented frames: {missing}")
    args.output.mkdir(parents=True, exist_ok=False)
    selection = []
    for at, frame in enumerate(trace):
        candidates = images[frame["id"]]
        best = Counter(value for value, _ in candidates).most_common(1)[0][0]
        source = next(path for value, path in candidates if value == best)
        with Image.open(source) as image:
            image.convert("RGB").crop((118, 91, 868, 657)).save(args.output / f"frame-{at:04d}.png")
        selection.append({"native": frame, "source": str(source.relative_to(root)),
                          "raw_png_sha256": digest(source), "pixel_sha256": best,
                          "samples": len(candidates)})
    report = {"trace_records": len(trace), "matched_ids": len(images), "missing_ids": [],
              "guard_rejects": rejects, "duration_us": trace[-1]["us"] - trace[0]["us"],
              "hold_vi_histogram": dict(Counter(b["vi"] - a["vi"] for a, b in zip(trace, trace[1:])))}
    if len(report["hold_vi_histogram"]) == 1:
        # A constant hold count permits a CFR clip at the measured VI cadence.
        # The final matched picture supplies the endpoint; encode preceding pictures.
        rate = Fraction((len(trace) - 1) * 1000000, report["duration_us"])
        report["constant_cadence_rate"] = str(rate)
        (args.output / "rate.txt").write_text(str(rate) + "\n")
    (args.output / "selection.json").write_text(json.dumps(selection, indent=2) + "\n")
    (args.output / "coverage.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
