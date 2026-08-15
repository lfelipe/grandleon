# SPDX-License-Identifier: MIT
"""Turns what the scratch program photographed into pictures and animations.

Run by run-scratch3d-film.sh, never on its own path in any gate. It reads the
guest's own `SHOT` lines out of the run log rather than assuming an ordering, so
changing the scratch's phases changes what comes out of here without anything
having to be kept in step by hand. The label a `SHOT` line carries is the whole
file name, stem and all: this file invents no prefix, so the guest is the one
place that decides what a picture is called.

A captured frame is 320x240 halfwords of `SBBBBBGG GGGRRRRR`, which is the
PlayStation's own storage and not a claim about a digital-to-analogue converter;
five-bit channels are widened to eight the usual way, by repeating the top bits,
so a full channel stays full.
"""

import glob
import hashlib
import os
import re
import sys

from PIL import Image

WIDTH = 320
HEIGHT = 240

log_path = os.environ["GRANDLEON_SCRATCH3D_LOG"]
frame_dir = os.environ["GRANDLEON_SCRATCH3D_FRAME_DIR"]
evidence = os.environ["GRANDLEON_SCRATCH3D_EVIDENCE"]

paths = sorted(glob.glob(os.path.join(frame_dir, "frame-*.bin")))
if not paths:
    raise SystemExit("no frames were captured")


def load(index):
    with open(paths[index], "rb") as handle:
        raw = handle.read()
    if len(raw) != WIDTH * HEIGHT * 2:
        raise SystemExit(f"{paths[index]} is {len(raw)} bytes, not a 320x240 frame")
    out = bytearray(WIDTH * HEIGHT * 3)
    for pixel in range(WIDTH * HEIGHT):
        halfword = raw[pixel * 2] | (raw[pixel * 2 + 1] << 8)
        red = halfword & 0x1F
        green = (halfword >> 5) & 0x1F
        blue = (halfword >> 10) & 0x1F
        out[pixel * 3] = (red << 3) | (red >> 2)
        out[pixel * 3 + 1] = (green << 3) | (green >> 2)
        out[pixel * 3 + 2] = (blue << 3) | (blue >> 2)
    return Image.frombytes("RGB", (WIDTH, HEIGHT), bytes(out))


stills = {}
films = []
with open(log_path, encoding="utf-8", errors="replace") as handle:
    for line in handle:
        still = re.match(r"^SHOT (\d+) still (\S+)", line)
        if still:
            stills[int(still.group(1))] = still.group(2)
            continue
        film = re.match(
            r"^SHOT film \S+ begins at ordinal (\d+), (.+?), (\d+) frames", line
        )
        if film:
            films.append((int(film.group(1)), film.group(2), int(film.group(3))))

if not stills:
    raise SystemExit("the run log names no stills")

written = []
for ordinal, label in sorted(stills.items()):
    path = os.path.join(evidence, f"{label}.png")
    load(ordinal).save(path, optimize=True)
    written.append(path)

for first, label, count in films:
    if first + count > len(paths):
        raise SystemExit(
            f"the log claims {count} frames from ordinal {first} "
            f"but only {len(paths)} were captured"
        )
    frames = []
    held = []
    previous = None
    for index in range(first, first + count):
        image = load(index)
        digest = hashlib.sha256(image.tobytes()).digest()
        if digest == previous:
            held[-1] += 1
            continue
        previous = digest
        frames.append(image)
        held.append(1)

    # A frame the program held for n frames is held for n thirtieths of a
    # second, because that is exactly what the pad script says happened. The
    # scratch counts frames, never a clock. Floored at two frames so a viewer's
    # browser does not silently re-time the animation.
    durations = [max(66, round(n * 1000 / 30)) for n in held]

    sample = Image.new("RGB", (WIDTH, HEIGHT * 8))
    step = max(1, len(frames) // 8)
    for i, frame in enumerate(frames[::step][:8]):
        sample.paste(frame, (0, i * HEIGHT))
    master = sample.quantize(colors=256, method=Image.MEDIANCUT)
    quantised = [f.quantize(palette=master, dither=Image.NONE) for f in frames]

    slug = label.replace(" ", "-")
    path = os.path.join(evidence, f"{slug}.gif")
    quantised[0].save(
        path,
        save_all=True,
        append_images=quantised[1:],
        duration=durations,
        loop=0,
        optimize=True,
        disposal=1,
    )
    written.append(path)
    print(
        f"  {slug}: {len(frames)} distinct of {count} frames, "
        f"{sum(durations) / 1000:.1f}s"
    )

for path in written:
    print(f"  {path} ({os.path.getsize(path)} bytes)", file=sys.stdout)
