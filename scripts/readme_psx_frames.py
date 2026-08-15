# SPDX-License-Identifier: MIT
"""What the PlayStation harnesses photographed, as pictures.

Imported by `scripts/readme-screenshots.sh`'s three PlayStation arms. It exists
because those three read the same thing, raw 15-bit frames written by
`platform/playstation/harness/playstation_turn_probe.lua`, and a decoder
copied three times is three chances to widen a channel differently.

A captured frame is `width * height` little-endian halfwords of
`SBBBBBGG GGGRRRRR`, which is the PlayStation's own storage and not a claim
about a digital-to-analogue converter. Five-bit channels are widened to eight
the usual way, by repeating the top bits, so a full channel stays full. That is
the same widening `platform/playstation/scratch/scratch3d_evidence.py` does.

Nothing here assumes a display size. The harness prints the size it captured on
its own `FRAMES` line and `playstation_turn_probe.c` reads the size from there
rather than assuming it; so does this, for the same reason.

The module name carries underscores where every other helper in this directory
carries hyphens, because this one is imported rather than executed.
"""

import glob
import os
import re

from PIL import Image

# 32768 rather than 65536: bit 15 is the semi-transparency flag, and the turn
# harness already fails a run whose frame sets it. Masked off here so a table
# lookup cannot be the thing that hides one.
_WIDENED = bytes(
    ((component << 3) | (component >> 2))
    for component in range(32)
)
_TABLE = [
    bytes((
        _WIDENED[halfword & 0x1F],
        _WIDENED[(halfword >> 5) & 0x1F],
        _WIDENED[(halfword >> 10) & 0x1F],
    ))
    for halfword in range(32768)
]


def frame_size(log_path):
    """The display size the harness says it captured, off its `FRAMES` line."""
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            named = re.match(r"^HARNESS FRAMES (\d+) (\d+)x(\d+)\s*$", line)
            if named:
                return int(named.group(2)), int(named.group(3))
    raise SystemExit(f"{log_path} carries no HARNESS FRAMES line")


def checkpoint_names(log_path):
    """The run's own checkpoint names, in the order the run reached them.

    Frame *n* is checkpoint *n + 1*: the client prints `CHECKPOINT` and then
    signals the capture, so the two lists are the same list. The names are read
    out of the run rather than invented here, which is what lets the trail's
    pacing follow what a checkpoint *is* without anything being kept in step by
    hand.
    """
    names = []
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            named = re.match(r"^CHECKPOINT (\S+)\s*$", line)
            if named:
                names.append(named.group(1))
    return names


def load(path, width, height):
    """One raw frame, as an RGB image."""
    with open(path, "rb") as handle:
        raw = handle.read()
    if len(raw) != width * height * 2:
        raise SystemExit(
            f"{path} is {len(raw)} bytes, not a {width}x{height} frame"
        )
    table = _TABLE
    return Image.frombytes(
        "RGB",
        (width, height),
        b"".join(
            table[(raw[i] | (raw[i + 1] << 8)) & 0x7FFF]
            for i in range(0, len(raw), 2)
        ),
    )


def frames_in(directory, width, height, pattern="frame-*.bin"):
    """Every frame in a capture directory, in the order it was captured."""
    paths = sorted(glob.glob(os.path.join(directory, pattern)))
    if not paths:
        raise SystemExit(f"no frames in {directory}")
    return [load(path, width, height) for path in paths]
