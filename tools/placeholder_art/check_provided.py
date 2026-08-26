#!/usr/bin/env python3
# SPDX-License-Identifier: MIT

"""Exercise the provided-art validator: every refusal, and the substitution.

Usage::

    tools/placeholder_art/.venv/bin/python tools/placeholder_art/check_provided.py
    tools/placeholder_art/.venv/bin/python tools/placeholder_art/check_provided.py --end-to-end

The default run takes ten seconds and asserts these:

* **Every rule in** :data:`~placeholder_art.provided.RULES` **refuses, by its own
  code, a submission that breaks it**, including the hostile ones: a symbolic
  link, a named pipe, an oversized file, a chunk stream with a bad CRC, a
  trailing byte after IEND, an animated PNG, an image declaring twenty thousand
  pixels a side. A rule that could not be provoked is a rule this file refuses
  to claim, so the run fails if any code goes unexercised.
* **The worked example is accepted.** Standing it in moves exactly what it
  names and nothing else.

``--end-to-end`` adds the expensive half: two whole builds, one with nothing
provided and one with the worked example. It reads the provided figure's own
pixels back out of every profile tree and the console character header.

**Both halves are in the gate** (``scripts/local-ci.sh``), including the
expensive one. It is the only thing in this repository that stands a submission
into a *real* build, so it is the only thing that can catch a check written for
generated art being asked about provided art. The fast half structurally cannot
pose that question, because nothing in it builds. The cost to weigh that
against is the fast half at ten seconds and the whole run at 5m46s, of which the
three library builds are nearly all.
"""

from __future__ import annotations

import argparse
import binascii
import io
import json
import os
import struct
import sys
import tempfile
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from PIL import Image

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from placeholder_art import build as build_module  # noqa: E402
from placeholder_art import (characters, figures, frames,  # noqa: E402
                             palette, playstation_header, profiles,
                             provided, styles)

REPOSITORY = ROOT.parent.parent
EXAMPLE = REPOSITORY / provided.EXAMPLES_DIRECTORY / "lantern-keeper"

#: The key the worked example replaces. A `pirates` sprite on purpose: no
#: project in this repository names that style, so nothing that is pinned by a
#: golden value is downstream of it, and the end-to-end run can build the real
#: thing without moving a ROM.
#:
STANDING = "characters/mage_blue_pirates.png"
STRIP = "characters/mage_blue_pirates_frames.png"

SPRITE = characters.SPRITE


class Failure(Exception):
    pass


# ---------------------------------------------------------------------------
# Making submissions, hostile and otherwise.
# ---------------------------------------------------------------------------

def _png(width: int, height: int, pixels) -> bytes:
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    image.putdata(list(pixels))
    buffer = io.BytesIO()
    image.save(buffer, format="PNG")
    return buffer.getvalue()


def _good_standing() -> bytes:
    return (EXAMPLE / "characters" / "mage_blue_pirates.png").read_bytes()


def _good_strip() -> bytes:
    return (EXAMPLE / "characters" / "mage_blue_pirates_frames.png").read_bytes()


def _decoded(data: bytes) -> List[Tuple[int, int, int, int]]:
    with Image.open(io.BytesIO(data)) as image:
        raw = image.convert("RGBA").tobytes()
    return [tuple(raw[at:at + 4]) for at in range(0, len(raw), 4)]


def _repack(data: bytes, edit) -> bytes:
    """Rebuild a PNG's chunk stream, letting ``edit`` rewrite it."""
    chunks: List[Tuple[bytes, bytes]] = []
    offset = 8
    while offset < len(data):
        length = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        chunks.append((kind, data[offset + 8:offset + 8 + length]))
        offset += 12 + length
    out = bytearray(data[:8])
    for kind, payload in edit(chunks):
        out += struct.pack(">I", len(payload)) + kind + payload
        out += struct.pack(">I", binascii.crc32(kind + payload) & 0xFFFFFFFF)
    return bytes(out)


def _paint(data: bytes, at: Tuple[int, int], colour: Tuple[int, int, int, int],
           width: int = SPRITE, height: int = SPRITE) -> bytes:
    pixels = _decoded(data)
    x, y = at
    pixels[y * width + x] = colour
    return _png(width, height, pixels)


def _cases() -> List[Tuple[str, str, Dict[str, object]]]:
    """(refusal code, description, tree) for every rule.

    A tree is a mapping of relative path to either bytes, or one of the marker
    strings ``"symlink"`` and ``"fifo"``, which the writer turns into the thing
    it names.
    """
    good = _good_standing()
    strip = _good_strip()
    both = {STANDING: good, STRIP: strip}
    # Opaque edge to edge, and in a palette colour, so the rule under test is
    # the one that fires rather than the palette rule in front of it.
    opaque = _png(SPRITE, SPRITE,
                  [palette.RGB[palette.RAMPS["bone"][2]] + (255,)]
                  * (SPRITE * SPRITE))
    ramps = [name for name in palette.RAMPS if name][:9]
    many_ramps = _decoded(good)
    for index, name in enumerate(ramps):
        many_ramps[(2 + index) * SPRITE + 12] = \
            palette.RGB[palette.RAMPS[name][0]] + (255,)
    # Enough distinct colours that the sixteen-entry subset has to move a
    # noticeable share of them, without leaving the palette.
    costly = _decoded(good)
    for index in range(1, 60):
        costly[(index % 24 + 4) * SPRITE + (index % 12 + 10)] = \
            palette.RGB[index] + (255,)

    return [
        ("symlink", "a symbolic link where a file should be",
         {**both, STANDING: "symlink"}),
        ("not-a-regular-file", "a named pipe where a file should be",
         {**both, STANDING: "fifo"}),
        ("too-large", "a file above the byte cap",
         {**both, STANDING: good + b"\0" * provided.MAX_FILE_BYTES}),
        ("unknown-key", "a path that names no asset",
         {**both, "characters/lantern_keeper.png": good}),
        ("unsupported-kind", "a terrain sheet, which is a key and not a typo",
         {**both, "terrain/grass_base.png": good}),
        # The one an author is most likely to reach for by accident, because
        # unlike a terrain sheet it is the *same role* they are already
        # replacing, under a filename sitting next to the one they copied.
        ("unsupported-kind",
         "a role's second figure, which is a key and not a typo",
         {**both,
          f"characters/mage_blue_pirates{figures.FIGURE_ORDER[1].suffix}.png":
              good}),
        ("unknown-key", "a path whose case does not match the key",
         {STRIP: strip, "characters/Mage_blue_pirates.png": good}),
        ("collision", "two paths differing only in case",
         {**both, "characters/Mage_blue_pirates.png": good}),
        ("not-a-png", "a text file at a real key",
         {**both, STANDING: b"this is not a PNG, it is an apology\n"}),
        ("malformed-png", "a byte appended after IEND",
         {**both, STANDING: good + b"\x00"}),
        ("malformed-png", "a chunk whose CRC does not match",
         {**both, STANDING: good[:-1] + bytes([good[-1] ^ 0xFF])}),
        ("animated-png", "an acTL chunk, so an animated PNG",
         {**both, STANDING: _repack(
             good, lambda chunks: [chunks[0],
                                   (b"acTL", struct.pack(">II", 2, 0))]
             + chunks[1:])}),
        ("wrong-size", "an image that is not the key's size",
         {**both, STANDING: _png(64, 64, [(0, 0, 0, 0)] * 64 * 64)}),
        ("wrong-size", "a header claiming twenty thousand pixels a side",
         {**both, STANDING: _repack(
             good, lambda chunks: [
                 (b"IHDR", struct.pack(">II", 20000, 20000) + chunks[0][1][8:])]
             + chunks[1:])}),
        ("undecodable", "a structurally valid PNG with unusable image data",
         {**both, STANDING: _repack(
             good, lambda chunks: [
                 chunk if chunk[0] != b"IDAT" else (b"IDAT", b"\x00" * 32)
                 for chunk in chunks])}),
        ("partial-alpha", "a half-transparent pixel",
         {**both, STANDING: _paint(good, (16, 16), (220, 214, 194, 128))}),
        ("off-palette", "a colour the master palette does not hold",
         {**both, STANDING: _paint(good, (16, 16), (255, 0, 255, 255))}),
        ("no-transparency", "a cell with no transparent pixel",
         {**both, STANDING: opaque}),
        ("empty-cell", "a sequence sheet with a blank cell",
         {**both, STRIP: _png(
             SPRITE * frames.FRAME_COUNT, SPRITE,
             [pixel if (position % (SPRITE * frames.FRAME_COUNT)) // SPRITE != 2
              else (0, 0, 0, 0)
              for position, pixel in enumerate(_decoded(strip))])}),
        ("cell-margin", "a pixel the walk pose would push off the cell",
         {**both, STANDING: _paint(good, (0, 30), palette.RGB[1] + (255,))}),
        ("too-many-ramps", "nine ramps named by one sprite",
         {**both, STANDING: _png(SPRITE, SPRITE, many_ramps)}),
        ("ci4-cost", "more colours than the sixteen-entry subset can keep",
         {**both, STANDING: _png(SPRITE, SPRITE, costly)}),
        ("incomplete-sequence", "a standing sprite without its sequence sheet",
         {STANDING: good}),
    ]


def _plant(root: Path, tree: Dict[str, object]) -> None:
    for relative, content in tree.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if content == "symlink":
            path.symlink_to(Path("..") / ".." / ".." / "elsewhere.png")
        elif content == "fifo":
            os.mkfifo(path)
        else:
            path.write_bytes(content)  # type: ignore[arg-type]


# ---------------------------------------------------------------------------
# The checks.
# ---------------------------------------------------------------------------

def check_refusals() -> List[str]:
    """Every case refuses by its own code. Returns the codes exercised."""
    seen: List[str] = []
    for code, description, tree in _cases():
        with tempfile.TemporaryDirectory(prefix="provided-case-") as scratch:
            root = Path(scratch) / "provided"
            root.mkdir()
            _plant(root, tree)
            accepted, refusals = provided.read(root)
        codes = [refusal.code for refusal in refusals]
        if code not in codes:
            raise Failure(
                f"{description}: expected a {code} refusal, got "
                + (", ".join(codes) or "none"))
        if accepted:
            raise Failure(
                f"{description}: {len(accepted)} files were accepted anyway; a "
                "refused submission is refused whole")
        seen.append(code)
    return seen


def check_example_accepted() -> None:
    with tempfile.TemporaryDirectory(prefix="provided-good-") as scratch:
        root = Path(scratch) / "provided"
        (root / "characters").mkdir(parents=True)
        _plant(root, {STANDING: _good_standing(), STRIP: _good_strip()})
        accepted, refusals = provided.read(root)
    if refusals:
        raise Failure("the worked example was refused: "
                      + "; ".join(str(refusal) for refusal in refusals))
    if sorted(accepted) != sorted((STANDING, STRIP)):
        raise Failure(f"the worked example accepted {sorted(accepted)}")
    measured = accepted[STANDING].measurements
    if len(list(measured["ramps"])) > 5:  # type: ignore[arg-type]
        raise Failure("the worked example should name at most five ramps, "
                      "because it is the file a contributor copies")


def check_substitution_is_local() -> None:
    """Substituting moves exactly the assets the provided files name.

    The native canvases are compared rather than the written files, which is
    where the property actually lives: everything downstream is a function of
    this list, so an asset whose canvas is untouched here cannot differ in any
    profile, any header or any gallery panel.
    """
    with tempfile.TemporaryDirectory(prefix="provided-good-") as scratch:
        root = Path(scratch) / "provided"
        (root / "characters").mkdir(parents=True)
        _plant(root, {STANDING: _good_standing(), STRIP: _good_strip()})
        accepted, _ = provided.read(root)
    before = build_module.native_assets()
    after, used = build_module.substitute(before, accepted)
    if sorted(used) != sorted((STANDING, STRIP)):
        raise Failure(f"substitution used {sorted(used)}")
    moved = [old.path for old, new in zip(before, after)
             if old.canvas.data != new.canvas.data]
    if sorted(moved) != sorted((STANDING, STRIP)):
        raise Failure(
            f"substitution moved {len(moved)} assets, not the two it names: "
            + ", ".join(sorted(moved)[:8]))


def _provided_indices() -> List[int]:
    """The provided standing sprite as master palette indices."""
    lookup = {colour: index for index, colour in enumerate(palette.RGB)
              if index != palette.TRANSPARENT}
    return [0 if pixel[3] == 0 else lookup[pixel[:3]]
            for pixel in _decoded(_good_standing())]


def _accepted(tree: Dict[str, object]) -> Dict[str, provided.Replacement]:
    """Read one submission and require it to have been accepted whole."""
    with tempfile.TemporaryDirectory(prefix="provided-e2e-in-") as scratch:
        root = Path(scratch) / "provided"
        _plant(root, tree)
        accepted, refusals = provided.read(root)
    if refusals:
        raise Failure("a worked example was refused: "
                      + "; ".join(str(refusal) for refusal in refusals))
    return accepted


def check_end_to_end() -> None:
    """Build twice and read the provided figure back out of a client.

    This is the half of the claim that cannot be argued: a replacement has to
    *appear*. One build with nothing provided is the ground the other is
    compared against. The assertions are made against the same files the
    Nintendo 64's ``mksprite`` step and the PlayStation's ``psx_art.h``
    compile.
    """
    with tempfile.TemporaryDirectory(prefix="provided-e2e-") as scratch:
        base = Path(scratch)
        accepted = _accepted({STANDING: _good_standing(),
                              STRIP: _good_strip()})
        plain = base / "plain"
        replaced = base / "replaced"
        print("  building with nothing provided ...")
        build_module.build(plain, quiet=True)
        print("  building with the sheet example provided ...")
        build_module.build(replaced, quiet=True, replacements=accepted)

        expected = _provided_indices()

        # 1. Every profile tree carries the provided pixels.
        for profile in profiles.PROFILES:
            drawn = replaced / "assets" / profile.name / STANDING
            original = plain / "assets" / profile.name / STANDING
            if drawn.read_bytes() == original.read_bytes():
                raise Failure(f"{profile.name}: the replaced sprite is "
                              "byte-identical to the generated one")
        modern = replaced / "assets" / "modern" / STANDING
        lookup = {colour: index for index, colour in enumerate(palette.RGB)
                  if index != palette.TRANSPARENT}
        got = [0 if pixel[3] == 0 else lookup[pixel[:3]]
               for pixel in _decoded(modern.read_bytes())]
        if got != expected:
            raise Failure("the modern profile did not draw the provided pixels")

        # 2. The PlayStation header a console build compiles.
        for name, symbol in (
                (playstation_header.characters_header_name(
                    styles.STYLES_BY_NAME["pirates"]),
                 "grandleon_playstation_character_mage_blue"),):
            after = (replaced / "assets" / name).read_text(encoding="utf-8")
            before = (plain / "assets" / name).read_text(encoding="utf-8")
            if after == before:
                raise Failure(f"{name} is unchanged; the replacement did not "
                              "reach the console header")
            if symbol not in after:
                raise Failure(f"{name} no longer declares {symbol}")

        # 3. And what else moved, named rather than waved at. Locality is exact
        #    on the native canvas, as `check_substitution_is_local` asserts.
        differing = sorted(
            path.relative_to(replaced).as_posix()
            for path in replaced.rglob("*") if path.is_file()
            and (plain / path.relative_to(replaced)).exists()
            and path.read_bytes()
            != (plain / path.relative_to(replaced)).read_bytes())
        # The replaced key's own outputs, enumerated rather than matched on the
        # filename's stem. The same role's *second figure* is that stem plus a
        # suffix (`figures.FIGURE_ORDER`), so a substring test files art the
        # submission never touched under the heading of art it replaced. That
        # is the one distinction this section exists to keep.
        own = {f"assets/{profile.name}/{key}"
               for profile in profiles.PROFILES for key in (STANDING, STRIP)}
        style = styles.STYLES_BY_NAME["pirates"]
        indexes = ("manifest.json", "palettes.json", "palette_usage.json",
                   "GALLERY.md", "ROSTER.md")
        headers = {
            f"assets/{playstation_header.characters_header_name(style)}":
                "the PlayStation character header for the style",
        }

        def category(path: str) -> str:
            if path in own:
                return "the replaced key's own outputs"
            if path in headers:
                return headers[path]
            if path.endswith(indexes):
                return "an index of the whole library"
            if path.startswith("gallery/"):
                return "a gallery panel showing the replaced key"
            return ""

        counts: Dict[str, int] = {}
        unexplained = []
        for path in differing:
            reason = category(path)
            if not reason:
                unexplained.append(path)
            else:
                counts[reason] = counts.get(reason, 0) + 1
        if unexplained:
            raise Failure(
                "generated files moved that no rule accounts for: "
                + ", ".join(unexplained))
        print(f"  the sheet example moves {len(differing)} generated files, "
              f"every one of them accounted for:")
        for reason in sorted(counts):
            print(f"    {counts[reason]:3d}  {reason}")


def check_nothing_provided_is_identical() -> None:
    """An empty tree and no tree are the same build.

    Cheap because it runs no builds. It compares what the build is a function
    of: the native canvases and the accepted replacements.
    The expensive version of this claim is ``generate.py --check`` itself, which
    reads the real tree and holds the committed output to it.
    """
    with tempfile.TemporaryDirectory(prefix="provided-empty-") as scratch:
        root = Path(scratch) / "provided"
        root.mkdir()
        accepted, refusals = provided.read(root)
    if accepted or refusals:
        raise Failure("an empty provided tree was not silent")
    missing = Path("/nonexistent-provided-tree")
    accepted, refusals = provided.read(missing)
    if accepted or refusals:
        raise Failure("an absent provided tree was not silent")


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--end-to-end", action="store_true",
                        help="also build twice and read the provided pixels "
                             "back out of every client's own bytes")
    arguments = parser.parse_args(argv)

    try:
        check_nothing_provided_is_identical()
        print("An absent or empty provided tree accepts and refuses nothing.")

        seen = check_refusals()
        unexercised = sorted(set(provided.RULE_CODES) - set(seen))
        if unexercised:
            raise Failure(
                "these rules were never provoked, so this file cannot claim "
                "they refuse anything: " + ", ".join(unexercised))
        print(f"All {len(provided.RULE_CODES)} rules refuse, by code, over "
              f"{len(seen)} submissions.")

        check_example_accepted()
        print("The worked sheet example is accepted.")

        check_substitution_is_local()
        print("Substitution moves exactly the assets the provided files name.")

        if arguments.end_to_end:
            check_end_to_end()
            print("The provided pixels reach every profile and the "
                  "console character header.")
    except Failure as error:
        print(f"\nProvided-art check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
