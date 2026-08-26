# SPDX-License-Identifier: MIT
"""Provided art: replacements this repository did not draw.

The generated library is the default set and not the destination. It stays
complete (every archetype, every style, every faction, every frame) precisely
so that a project can replace part of it, and a replaced asset is a normal
outcome rather than a compromise. This module is the mechanism.

Where a replacement lives, and why it lives there
-------------------------------------------------
``generate.py`` owns ``assets/``, ``gallery/``, ``GALLERY.md`` and ``ROSTER.md``,
and its drift check treats a file *present* in those trees but *absent* from a
fresh build as drift. That strictness is correct and is not relaxed here: a
replacement dropped beside generated art would be indistinguishable from an
edited one. So a replacement lives **outside** the generated trees, in
``art/provided/`` at the repository root, and it is an **input** to the
generator rather than an output beside it.

That single decision is what makes everything else fall out:

* The generated trees stay exactly as byte-checked as they were. A replacement
  is not a hole cut in an assertion; it is another thing the assertion covers,
  because ``--check`` reads the same provided tree the build read.
* A replacement reaches every client the generated art reaches, without one of
  them learning a new lookup. The substitution happens on the native canvas,
  before the first profile has touched it, so the four profile trees, the
  PlayStation headers, the Nintendo 64's CI4 sheets, the editor's board copies
  and the gallery all follow from it.
* **Not one byte of a submitted file reaches a generated artefact.** The file is
  decoded to master-palette indices and re-encoded by this repository's own
  writer; its chunks, its metadata, its compression and its colour type are all
  discarded at the door. This is the load-bearing safety property, and it is why
  the checks below can be about art rather than about file formats.

The key is the manifest's own
-----------------------------
The unit of replacement is already decided by the key and needs no new
addressing scheme: sprites are keyed by archetype, style and faction colour,
so what a submission stands in for is one archetype's sheet in one style: the
manifest key the consoles and the editor already resolve art through. So the path of a provided file *is* the
manifest ``path`` of the asset it stands in for:
``art/provided/characters/knight_blue_pirates.png`` replaces the manifest entry
whose path is ``characters/knight_blue_pirates.png``. There is nothing to
declare, nothing to register, and no second name for anything.

Path handling is whitelisting rather than sanitising, which is the strongest
form of normalisation available: a relative path that is not *exactly* a key the
manifest publishes is refused. ``..`` never has to be stripped because it never
matches a key.

What is checkable today, and what is not
----------------------------------------
Character sprites are. A provided ``character`` or ``character-frames`` sheet is
held to every sheet rule in :data:`RULES`, and both files of a key must arrive
together.

Terrain sheets, the shadow and the sample map are still **not** accepted, not
because the tree could not hold them but because the checks that would make
them safe are not written: a terrain tile needs the seam measurement
``verify.check_seamless`` makes. They are refused by name and told so, rather
than accepted and half-checked.
"""

from __future__ import annotations

import binascii
import errno
import hashlib
import io
import os
import stat
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

from PIL import Image

from . import (characters, figures, frames, palette,
               profiles, styles, terrain, themes)
from .raster import Canvas

#: Where a contributor puts a replacement, relative to the repository root.
#: Absent by default: this repository provides nothing, and an absent directory
#: says that more plainly than an empty one with a placeholder file in it.
PROVIDED_DIRECTORY = "art/provided"

#: Worked examples, ready to copy into the tree above. Deliberately *not*
#: scanned: an example that took effect would be a replacement, and the shipped
#: default set has to stay the shipped default set.
EXAMPLES_DIRECTORY = "art/examples"

#: The largest a submitted file may be, before anything reads past its first
#: byte. The largest character sheet this repository generates is 1,082 bytes,
#: so 64 KiB is sixty times the honest need. Small enough that a hostile file
#: cannot cost memory worth measuring.
MAX_FILE_BYTES = 64 * 1024

#: How deep the scan will descend. A sheet's key is two components, a directory
#: and a filename. Four is well past the deepest honest need, and it is what
#: stops a pathological tree from being walked forever.
MAX_TREE_DEPTH = 4

#: A ceiling on decoded pixels, as defence in depth behind the exact dimension
#: check below. The dimension check is the real defence: a submission whose
#: IHDR does not state its key's exact size is refused before any decoder is
#: handed the bytes. This is what catches a decoder that ignored it.
MAX_PIXELS = 256 * 256

#: The row at or below which a walk's contact pose pushes a pixel *outward*, and
#: therefore off the cell. The standing art fills the cell, so every generated
#: pose displaces inward; ``walk_contact`` moves everything at or below this row
#: outward by one column, which states the rule as arithmetic: nothing on the
#: first or last column at or below this row. Measured across the 336 shipped
#: standing sprites, every one of them satisfies it.
MARGIN_ROW = frames.KNEE_Y

#: The most palette ramps a sprite may name. A commission is briefed at "about
#: five", because a sprite's sixteen colours are a budget spent on materials
#: rather than on colours and each shading ramp costs three to five of the
#: sixteen. Five is the advice; eight is the refusal, because the shipped
#: library's own ceiling is seven (``medieval``'s commander names steel, skin,
#: leather, wood, gold, ink and its faction) and a replacement is not held to a
#: rule the art it replaces would fail.
MAX_RAMPS = 8

#: The most opaque texels, as a fraction, that the Nintendo 64's CI4 subset may
#: move to a neighbouring colour. The sixteen-colour cap is not a rule a sprite
#: can break, since the profile always produces sixteen, so what there is to
#: check is what obeying it *costs*. Measured over the shipped library, the
#: worst single sheet is ``undead``'s stormcaller at 2.46% and the worst whole
#: style is ``pirates`` at 0.63%. Five per cent is twice the worst thing
#: shipped.
MAX_CI4_LOSS = 0.05

#: Every rule, in the order it is applied, with the code its refusal carries.
#: Published as data rather than restated anywhere, so that the command line and
#: every other consumer state the one list the code applies.
RULES: Tuple[Tuple[str, str], ...] = (
    ("symlink", "a provided file is a regular file, never a symbolic link"),
    ("not-a-regular-file",
     "a provided file is a regular file, never a directory entry of another kind"),
    ("too-large", f"a provided file is at most {MAX_FILE_BYTES} bytes"),
    ("unknown-key",
     "a provided file's path is exactly the path of the asset it replaces: "
     "the manifest's own"),
    ("unsupported-kind",
     "only character sheets are replaceable today"),
    ("collision",
     "two provided files whose paths differ only in case claim one asset"),
    ("not-a-png", "a provided file is a PNG"),
    ("malformed-png",
     "a provided file's chunk stream is well formed and ends at IEND"),
    ("animated-png", "a provided file holds one image and no animation"),
    ("wrong-size",
     "a provided file's dimensions are exactly the dimensions of the asset it "
     "replaces"),
    ("undecodable", "a provided file decodes to pixels"),
    ("partial-alpha",
     "every pixel is fully opaque or fully transparent; there is one "
     "transparent index and no blend"),
    ("off-palette",
     "every opaque pixel is a master palette colour; a replacement may not "
     "grow the palette"),
    ("no-transparency",
     "every cell has at least one transparent pixel, because a sprite is "
     "drawn over terrain"),
    ("empty-cell", "every cell has at least one opaque pixel"),
    ("cell-margin",
     f"no opaque pixel on the first or last column at or below row "
     f"{MARGIN_ROW} of a cell, or the walk pose would push it off"),
    ("too-many-ramps", f"a sprite names at most {MAX_RAMPS} palette ramps"),
    ("ci4-cost",
     f"the Nintendo 64's sixteen-colour subset moves at most "
     f"{MAX_CI4_LOSS:.0%} of a sheet's opaque texels"),
)

RULE_CODES: Tuple[str, ...] = tuple(code for code, _ in RULES)

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

#: Master palette colour to index, for the opaque entries. Index 0's RGB is
#: meaningless, so it is deliberately absent: a submitted pixel is transparent
#: because its alpha says so and never because it happens to be black.
_INDEX_OF_RGB: Dict[Tuple[int, int, int], int] = {
    colour: index for index, colour in enumerate(palette.RGB)
    if index != palette.TRANSPARENT
}


@dataclass(frozen=True)
class Refusal:
    """One reason one file was not accepted."""

    file: str
    code: str
    reason: str

    def __str__(self) -> str:
        return f"{self.file}: {self.reason} [{self.code}]"


class Rejected(Exception):
    """A refusal raised from inside the reader, carrying its own code.

    The code travels with the reason rather than being inferred from it: a
    refusal a caller has to pattern-match out of an English sentence is a
    refusal that will one day be misfiled by a rewording.
    """

    def __init__(self, code: str, reason: str) -> None:
        super().__init__(reason)
        self.code = code
        self.reason = reason


@dataclass(frozen=True)
class Specification:
    """What a replacement for one key has to be.

    Every field is read off the asset it replaces, which is the point: the
    generated set is the specification for its own replacement, so the two
    cannot drift and nobody has to keep a second table of sizes.
    """

    key: str
    kind: str
    width: int
    height: int
    columns: int
    #: The other key that must be provided with this one, if any.
    partner: Optional[str] = None
    #: Set for the kinds this mechanism cannot check yet, with the reason.
    unsupported: str = ""
    #: Which drawing this key is, where the key names one. Carried so a rule
    #: can ask the rest of the library about it.
    style: str = ""
    archetype: str = ""
    faction: str = ""

    @property
    def cell_width(self) -> int:
        return self.width // self.columns


@dataclass(frozen=True)
class Replacement:
    """One accepted file, and what was measured while accepting it.

    :attr:`canvas` holds this repository's own representation, master-palette
    indices, because that is the property the whole mechanism rests on: **not
    one byte of a submitted file reaches a generated artefact.**
    """

    key: str
    kind: str
    digest: str
    size_bytes: int
    #: A sheet's pixels, for a ``character`` or ``character-frames`` key.
    canvas: Optional[Canvas] = None
    style: str = ""
    archetype: str = ""
    measurements: Dict[str, object] = field(default_factory=dict)


def canvas_replacements(accepted: Mapping[str, Replacement]
                        ) -> Dict[str, Replacement]:
    """The accepted sheets, keyed by the manifest path they stand in for."""
    return {key: replacement for key, replacement in accepted.items()
            if replacement.canvas is not None}


def specifications() -> Dict[str, Specification]:
    """Every key a replacement may name, and what it must be.

    Built from the registries rather than by rendering, so the command line
    answers in milliseconds where a build takes minutes. The paths are the same
    strings :func:`~.build.native_assets` writes into the manifest, and the
    Nintendo 64 build, the PlayStation header and the editor's board copies all
    resolve through them.
    """
    table: Dict[str, Specification] = {}
    cell = characters.SPRITE
    for style in styles.STYLES:
        suffix = styles.asset_suffix(style)
        for archetype in characters.ARCHETYPE_ORDER:
            for colour in characters.FACTION_COLOURS:
                stem = f"characters/{archetype}_{colour.name}{suffix}"
                standing = f"{stem}.png"
                strip = f"{stem}_frames.png"
                table[standing] = Specification(
                    standing, "character", cell, cell, 1, partner=strip,
                    style=style.name, archetype=archetype,
                    faction=colour.name)
                table[strip] = Specification(
                    strip, "character-frames",
                    cell * frames.FRAME_COUNT, cell, frames.FRAME_COUNT,
                    partner=standing, style=style.name, archetype=archetype,
                    faction=colour.name)

    # The keys that exist and are not replaceable yet, named rather than left
    # to fall through to "it does not name an asset". A contributor who drops a
    # terrain sheet here has done something reasonable and should be told what
    # is missing, not told they mistyped.
    terrain_reason = (
        "terrain sheets are not replaceable yet. A tile has to tile: the "
        "generator measures a terrain's seams against its own neighbours "
        "(verify.check_seamless) and against a whole-scene conversion, and "
        "until a provided sheet is held to those two measurements it would be "
        "accepted and half-checked")
    for theme_suffix in _theme_suffixes():
        for name in terrain.TERRAIN_ORDER:
            for stem in (f"terrain/{name}_base{theme_suffix}.png",
                         f"terrain/{name}_blob{theme_suffix}.png"):
                table[stem] = Specification(stem, "terrain", 0, 0, 1,
                                            unsupported=terrain_reason)
        for name, under in terrain.TRANSITION_PAIRS:
            stem = f"terrain/{name}_over_{under}{theme_suffix}.png"
            table[stem] = Specification(stem, "terrain", 0, 0, 1,
                                        unsupported=terrain_reason)
    # A role's second figure. Its files sit in `assets/` beside the first
    # figure's under names a contributor can read off a directory listing, so
    # leaving them to fall through to "it does not name an asset" would tell
    # somebody they had mistyped a filename they had copied correctly.
    #
    # Driven by the figure registry rather than by a literal suffix, so a third
    # figure is named here the day it exists instead of the day somebody
    # notices.
    figure_reason = (
        "a role's second figure is not replaceable yet. Nothing in any client "
        "can select a figure, so a submitted one would be art no project could "
        "use. The four bounds that compare a role's own two figures "
        "(verify.check_figure_separation, verify.check_figure_room) mean "
        "something only where one hand made both, which a provided figure "
        "beside a generated one is not: a second figure redistributes the "
        "first's mass and never enlarges it, so its occupied box has to sit "
        "inside the first figure's on the standing cell. Replace the first "
        "figure, whose key is this one without the suffix")
    for style in styles.STYLES:
        suffix = styles.asset_suffix(style)
        for archetype in characters.ARCHETYPE_ORDER:
            for colour in characters.FACTION_COLOURS:
                for shape in figures.FIGURE_ORDER[1:]:
                    stem = (f"characters/{archetype}_{colour.name}"
                            f"{suffix}{shape.suffix}")
                    for key in (f"{stem}.png", f"{stem}_frames.png"):
                        table[key] = Specification(
                            key, "character", 0, 0, 1,
                            unsupported=figure_reason)

    for stem, kind, reason in (
            ("characters/shadow.png", "shadow",
             "the drop shadow is not replaceable yet: it is one asset shared by "
             "every archetype, every style and every faction, so replacing it "
             "is a change to all 336 units at once and wants a rule of its own"),
            ("sample_map.png", "scene",
             "the sample map is a composition of terrain tiles rather than a "
             "drawing, so replacing it would replace nothing an author sees")):
        table[stem] = Specification(stem, kind, 0, 0, 1, unsupported=reason)
    return table


def _theme_suffixes() -> List[str]:
    return [themes.asset_suffix(theme) for theme in themes.THEMES]


def replaceable_keys() -> List[str]:
    """Every key a replacement may name today, sorted.

    The specification table is wider than this, because a key that exists and
    cannot be replaced yet has to be refused *by name* rather than as a typo.
    This is the list a contributor wants.
    """
    return sorted(key for key, spec in specifications().items()
                  if not spec.unsupported)


def _walk(root: Path, prefix: str = "") -> List[Tuple[str, Path]]:
    """Every entry under ``root``, as (key candidate, path), depth first.

    Written as an explicit stack rather than ``os.walk`` so that the treatment
    of a symbolic link is visible in one place, and so that a pathological
    nesting meets a depth cap rather than the interpreter's recursion limit: a
    link is **reported as an entry and never followed**, whether it points at a
    file or at a directory. That is what keeps the scan inside the tree without
    any path arithmetic. Nothing here calls ``resolve()`` and then reasons about
    the answer, because a scanner that resolves before it decides has already
    left.

    The key is built from the names walked, never from the resolved path, so a
    key is exactly what a reviewer sees in ``git status``.
    """
    found: List[Tuple[str, Path]] = []
    pending: List[Tuple[Path, str, int]] = [(root, prefix, 0)]
    while pending:
        directory, at, depth = pending.pop()
        try:
            with os.scandir(directory) as entries:
                listed = sorted(entries, key=lambda item: item.name)
        except OSError:
            # A directory that cannot be listed is reported as the entry it is,
            # so it is refused by name rather than silently skipped.
            found.append((at.rstrip("/"), directory))
            continue
        for entry in listed:
            key = f"{at}{entry.name}"
            path = Path(entry.path)
            if entry.is_symlink() or not entry.is_dir(follow_symlinks=False):
                found.append((key, path))
            elif depth < MAX_TREE_DEPTH:
                pending.append((path, f"{key}/", depth + 1))
            else:
                # Every key is two components deep. A tree nested past the cap
                # is reported at the cap and refused there, so a pathological
                # one cannot be walked forever.
                found.append((key, path))
    return sorted(found, key=lambda entry: entry[0])


def _chunks(data: bytes) -> List[Tuple[bytes, bytes]]:
    """Split a PNG into its chunks, or raise ``ValueError`` saying why not.

    Written here rather than delegated because this is the one place a hostile
    file is still bytes. Every length is checked against what is left, every
    CRC is verified, the stream must begin at IHDR and end at IEND, and a single
    trailing byte is a refusal, so the decoder below is never handed a file
    whose shape has not already been agreed.
    """
    if not data.startswith(_PNG_SIGNATURE):
        raise ValueError("it does not begin with the PNG signature")
    found: List[Tuple[bytes, bytes]] = []
    offset = len(_PNG_SIGNATURE)
    ended = False
    while offset < len(data):
        if ended:
            raise ValueError(f"{len(data) - offset} bytes follow the IEND chunk")
        if offset + 8 > len(data):
            raise ValueError("a chunk header runs past the end of the file")
        length = int.from_bytes(data[offset:offset + 4], "big")
        kind = data[offset + 4:offset + 8]
        if length > MAX_FILE_BYTES:
            raise ValueError(
                f"the {kind.decode('ascii', 'replace')} chunk declares "
                f"{length} bytes, more than the whole file may hold")
        end = offset + 8 + length + 4
        if end > len(data):
            raise ValueError(
                f"the {kind.decode('ascii', 'replace')} chunk declares "
                f"{length} bytes and the file has {len(data) - offset - 12} left")
        payload = data[offset + 8:offset + 8 + length]
        stated = int.from_bytes(data[end - 4:end], "big")
        if binascii.crc32(kind + payload) & 0xFFFFFFFF != stated:
            raise ValueError(
                f"the {kind.decode('ascii', 'replace')} chunk's CRC does not "
                "match its contents")
        if not found and kind != b"IHDR":
            raise ValueError("the first chunk is not IHDR")
        found.append((kind, payload))
        ended = kind == b"IEND"
        offset = end
    if not ended:
        raise ValueError("the chunk stream does not end at IEND")
    return found


def _decode(data: bytes, chunks: Sequence[Tuple[bytes, bytes]],
            expected: Specification) -> Canvas:
    """Decode an already-structurally-checked PNG into palette indices.

    Raises :class:`Rejected` with the refusal's code and sentence. The returned
    canvas holds master palette indices and nothing else, which is what makes
    the submitted bytes end here.

    The dimension check is what makes this safe rather than merely careful: the
    key states the exact size, so the decoder is only ever handed a file that
    has already agreed to be 32 by 32 or 128 by 32. There is no size a hostile
    submission can claim that would allocate anything.
    """
    header = chunks[0][1]
    if len(header) < 13:
        raise Rejected("malformed-png",
                       "its IHDR chunk is shorter than a PNG header")
    width = int.from_bytes(header[0:4], "big")
    height = int.from_bytes(header[4:8], "big")
    if (width, height) != (expected.width, expected.height):
        raise Rejected(
            "wrong-size",
            f"it is {width}x{height} and {expected.key} is "
            f"{expected.width}x{expected.height}. The cell size is "
            f"{characters.SPRITE} and a sequence sheet is "
            f"{frames.FRAME_COUNT} cells in one row")
    if width * height > MAX_PIXELS:
        raise Rejected(
            "wrong-size",
            f"it declares {width * height} pixels, more than the "
            f"{MAX_PIXELS} a provided file may hold")

    previous_limit = Image.MAX_IMAGE_PIXELS
    Image.MAX_IMAGE_PIXELS = MAX_PIXELS
    try:
        with Image.open(io.BytesIO(data), formats=("PNG",)) as image:
            if getattr(image, "n_frames", 1) != 1:
                raise Rejected("animated-png", "it holds more than one image")
            if image.size != (width, height):
                raise Rejected(
                    "malformed-png",
                    f"it decodes to {image.size[0]}x{image.size[1]} and its "
                    f"IHDR declares {width}x{height}")
            raw = image.convert("RGBA").tobytes()
    except Rejected:
        raise
    except Exception as error:
        raise Rejected("undecodable",
                       f"the PNG decoder refused it ({error})") from error
    finally:
        Image.MAX_IMAGE_PIXELS = previous_limit

    if len(raw) != width * height * 4:
        raise Rejected("malformed-png",
                       "it decoded to a different number of pixels than it "
                       "declares")
    canvas = Canvas(width, height)
    for position in range(width * height):
        red, green, blue, alpha = raw[position * 4:position * 4 + 4]
        if alpha == 0:
            continue
        x, y = position % width, position // width
        if alpha != 255:
            raise Rejected(
                "partial-alpha",
                f"the pixel at ({x}, {y}) is {alpha}/255 opaque. Every pixel "
                "is fully opaque or fully transparent: the indexed profiles "
                "have one transparent entry and no way to blend")
        index = _INDEX_OF_RGB.get((red, green, blue))
        if index is None:
            raise Rejected(
                "off-palette",
                f"the pixel at ({x}, {y}) is #{red:02x}{green:02x}{blue:02x}, "
                "which is not one of the master palette's 123 opaque colours. "
                "A replacement may not grow the palette: the n64_ci8 profile "
                "writes the whole palette into every asset, so one appended "
                "entry would rewrite art nobody asked to change")
        canvas.data[position] = index
    return canvas


def _cells(canvas: Canvas, spec: Specification) -> List[Tuple[int, Canvas]]:
    width = spec.cell_width
    out: List[Tuple[int, Canvas]] = []
    for column in range(spec.columns):
        cell = Canvas(width, canvas.height)
        for y in range(canvas.height):
            source = y * canvas.width + column * width
            cell.data[y * width:(y + 1) * width] = \
                canvas.data[source:source + width]
        out.append((column, cell))
    return out


def _measure(canvas: Canvas, spec: Specification) -> Dict[str, object]:
    """Everything the sheet rules ask about a sheet, measured once.

    Reported whether or not it refuses: a contributor who is inside every bound
    still wants the numbers. These bounds are budgets: ramps and CI4 cost are
    spent, not merely obeyed, and a budget nobody can see is advice.
    """
    used = {index for index in canvas.data if index != palette.TRANSPARENT}
    ramps = sorted({palette.RAMP_OF_INDEX[index][0] for index in used})
    opaque = sum(1 for index in canvas.data if index != palette.TRANSPARENT)
    # The profile's own subset routine, reached through its private name on
    # purpose: a second implementation of "which sixteen survive" would be a
    # second opinion, and the number reported here has to be the number the
    # Nintendo 64 sheet will actually spend.
    ci4 = profiles.PROFILES_BY_NAME["n64_ci4"]
    mapping = profiles._subset(canvas.data, ci4.max_colours)
    moved = sum(1 for index in canvas.data
                if index != palette.TRANSPARENT and mapping[index] != index)
    return {
        "colours": len(used),
        "ramps": ramps,
        "opaque_texels": opaque,
        "ci4_moved_texels": moved,
        "ci4_loss": (moved / opaque) if opaque else 0.0,
        "cells": spec.columns,
    }


def _contract_refusals(file_label: str, canvas: Canvas,
                       spec: Specification,
                       measured: Mapping[str, object]) -> List[Refusal]:
    """The sheet rules, applied to an already-decoded sheet."""
    out: List[Refusal] = []
    for column, cell in _cells(canvas, spec):
        where = f"cell {column}" if spec.columns > 1 else "the cell"
        opaque = [index for index in cell.data if index != palette.TRANSPARENT]
        if not opaque:
            out.append(Refusal(file_label, "empty-cell",
                               f"{where} has no opaque pixel"))
            continue
        if len(opaque) == len(cell.data):
            out.append(Refusal(
                file_label, "no-transparency",
                f"{where} is opaque edge to edge; a sprite is drawn over "
                "terrain and needs a transparent index"))
        last = cell.width - 1
        outside = [
            (x, y)
            for y in range(MARGIN_ROW, cell.height)
            for x in (0, last)
            if cell.data[y * cell.width + x] != palette.TRANSPARENT
        ]
        if outside:
            x, y = outside[0]
            out.append(Refusal(
                file_label, "cell-margin",
                f"{where} has an opaque pixel at ({x}, {y}), on column {x} at "
                f"or below row {MARGIN_ROW}. The walk's contact pose moves "
                f"everything at or below that row outward by one column, so "
                f"that pixel would be pushed off the cell. Draw it inward, or "
                f"leave both edge columns clear from that row down"))
    ramps = list(measured["ramps"])  # type: ignore[arg-type]
    if len(ramps) > MAX_RAMPS:
        out.append(Refusal(
            file_label, "too-many-ramps",
            f"it names {len(ramps)} palette ramps ({', '.join(ramps)}); a "
            f"sprite may name at most {MAX_RAMPS}, and a commission is briefed "
            f"at about five, because each shading ramp costs three to five of "
            f"the sixteen colours the Nintendo 64's subset leaves it. Name the "
            f"materials before drawing and spend the budget on those"))
    loss = float(measured["ci4_loss"])  # type: ignore[arg-type]
    if loss > MAX_CI4_LOSS:
        out.append(Refusal(
            file_label, "ci4-cost",
            f"the Nintendo 64's sixteen-colour subset moves {loss:.2%} of its "
            f"{measured['opaque_texels']} opaque texels to another colour, "
            f"above the {MAX_CI4_LOSS:.0%} a replacement may spend. It names "
            f"{measured['colours']} colours; the cap is sixteen, and the "
            f"shipped library's worst sheet spends 2.46%"))
    return out


def _label(key: Optional[str]) -> str:
    """A key as the path a refusal names, or the empty string for no file."""
    return f"{PROVIDED_DIRECTORY}/{key}" if key else ""


def read(root: Path) -> Tuple[Dict[str, Replacement], List[Refusal]]:
    """Read every provided file under ``root``.

    Returns the accepted replacements by key and every refusal, with its reason.
    A refused file is never partially accepted: nothing it holds reaches the
    build, and the build does not run at all while a refusal stands.
    """
    accepted: Dict[str, Replacement] = {}
    refusals: List[Refusal] = []
    if root.is_symlink() or (root.exists() and not root.is_dir()):
        # Silence is the right answer to a tree that is not there, and the wrong
        # answer to one that is there and is not a tree.
        refusals.append(Refusal(
            PROVIDED_DIRECTORY, "not-a-regular-file",
            "it exists and is not a directory. Provided art is a tree of files "
            "whose paths are the keys they replace"))
        return accepted, refusals
    if not root.is_dir():
        return accepted, refusals

    table = specifications()
    lowered: Dict[str, str] = {}
    for key in table:
        lowered.setdefault(key.lower(), key)

    entries = _walk(root)
    # Collision handling, and the only shape a collision can take here. The key
    # *is* the path, so two files can never claim one key on this filesystem.
    # But a checkout on a case-insensitive one could not hold both of these at
    # once, and would silently keep whichever was written last. Refuse the pair
    # rather than accept a tree that means something different elsewhere.
    folded: Dict[str, List[str]] = {}
    for key, _ in entries:
        folded.setdefault(key.lower(), []).append(key)
    colliding = {key for group in folded.values() if len(group) > 1
                 for key in group}

    for key, path in entries:
        label = f"{PROVIDED_DIRECTORY}/{key}"
        if key in colliding:
            others = sorted(set(folded[key.lower()]) - {key})
            refusals.append(Refusal(
                label, "collision",
                f"it differs only in case from {', '.join(others)}. A checkout "
                f"on a case-insensitive filesystem could hold only one of "
                f"them, so which asset this tree replaces would depend on the "
                f"machine reading it"))
            continue
        if path.is_symlink():
            refusals.append(Refusal(
                label, "symlink",
                "it is a symbolic link. A provided file is read as bytes from "
                "inside the provided tree, and a link is a way out of it"))
            continue
        try:
            info = path.lstat()
        except OSError as error:
            refusals.append(Refusal(label, "not-a-regular-file",
                                    f"it could not be read ({error.strerror})"))
            continue
        if not stat.S_ISREG(info.st_mode):
            refusals.append(Refusal(
                label, "not-a-regular-file",
                f"it is a directory nested more than {MAX_TREE_DEPTH} deep, or "
                f"one that could not be listed. The deepest key is two "
                f"components (a directory and a filename), so the scan stops "
                f"there"
                if stat.S_ISDIR(info.st_mode) else
                "it is not a regular file. A provided file is a PNG on disk, "
                "not a device, a socket or a pipe"))
            continue
        if info.st_size > MAX_FILE_BYTES:
            refusals.append(Refusal(
                label, "too-large",
                f"it is {info.st_size} bytes and a provided file may be at "
                f"most {MAX_FILE_BYTES}. The largest sheet this repository "
                f"generates is 1,082"))
            continue

        spec = table.get(key)
        if spec is None:
            near = lowered.get(key.lower())
            hint = (f" Did you mean {near}? A key is exactly the manifest path, "
                    "letter for letter." if near else
                    " Run generate.py --list-keys for every key a replacement "
                    "may name.")
            refusals.append(Refusal(
                label, "unknown-key",
                f"it does not name an asset. The path under "
                f"{PROVIDED_DIRECTORY}/ is the manifest path of the asset a "
                f"file replaces.{hint}"))
            continue
        if spec.unsupported:
            refusals.append(Refusal(label, "unsupported-kind", spec.unsupported))
            continue

        # Opened with O_NOFOLLOW and read to one byte past the cap. The size
        # check above is on the directory entry, which is a fact about a moment
        # ago; this is a fact about the bytes actually read, and it closes the
        # window in which a file could grow or become a link between the two.
        try:
            handle = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
        except OSError as error:
            refusals.append(Refusal(
                label, "symlink" if error.errno in (errno.ELOOP, errno.EMLINK)
                else "not-a-regular-file",
                f"it could not be opened as a plain file ({error.strerror})"))
            continue
        try:
            with os.fdopen(handle, "rb") as stream:
                data = stream.read(MAX_FILE_BYTES + 1)
        except OSError as error:
            refusals.append(Refusal(label, "not-a-regular-file",
                                    f"it could not be read ({error.strerror})"))
            continue
        if len(data) > MAX_FILE_BYTES:
            refusals.append(Refusal(
                label, "too-large",
                f"it is more than {MAX_FILE_BYTES} bytes. The largest sheet "
                f"this repository generates is 1,082"))
            continue

        if not data.startswith(_PNG_SIGNATURE):
            refusals.append(Refusal(
                label, "not-a-png",
                "it does not begin with the PNG signature. A replacement is a "
                "PNG at the master palette, which is what the modern profile "
                "already writes"))
            continue
        try:
            chunks = _chunks(data)
        except ValueError as error:
            refusals.append(Refusal(label, "malformed-png",
                                    f"it is not a well-formed PNG: {error}"))
            continue
        if any(kind == b"acTL" for kind, _ in chunks):
            refusals.append(Refusal(
                label, "animated-png",
                "it carries an acTL chunk, so it is an animated PNG. A "
                "sequence is one row of cells in a fixed order that a client "
                "indexes by position, not an animation container"))
            continue
        try:
            canvas = _decode(data, chunks, spec)
        except Rejected as error:
            refusals.append(Refusal(label, error.code, error.reason))
            continue

        measured = _measure(canvas, spec)
        broken = _contract_refusals(label, canvas, spec, measured)
        if broken:
            refusals.extend(broken)
            continue
        accepted[key] = Replacement(
            key=key, kind=spec.kind, canvas=canvas,
            digest=hashlib.sha256(data).hexdigest(),
            size_bytes=len(data), measurements=dict(measured),
            style=spec.style, archetype=spec.archetype)

    submitted = {key for key, _ in entries}

    # Only a partner that was never *submitted* is missing. One that arrived and
    # was refused has already been reported by its own rule, and saying it is
    # absent as well would send its author looking for a file they can see.
    for key in sorted(accepted):
        partner = table[key].partner
        if not partner or partner in submitted:
            continue
        refusals.append(Refusal(
            f"{PROVIDED_DIRECTORY}/{key}", "incomplete-sequence",
            f"it is provided without {partner}. A character is replaced as "
            f"a pair (the standing sprite and its four-cell sequence "
            f"sheet) because a pose is applied to the body before the ink "
            f"outline is traced and over the faction disc, so this "
            f"pipeline cannot derive one from the other"))

    # A tree with a refusal in it accepts nothing. What one file gets, refused
    # and told why and never silently degraded, applies to the tree rather
    # than to the file: a build that drew the three replacements that passed and
    # the generated art for the fourth would be a build whose output nobody
    # could describe. It is also what makes the refusals worth reading, because
    # every one of them is reported before anything stops.
    if refusals:
        accepted = {}
    return accepted, refusals


def manifest_block(replacements: Mapping[str, Replacement]) -> Dict[str, object]:
    """What the manifest says about the replacements this build read.

    Emitted only when something was provided. A project that provides nothing
    gets the manifest it always got, byte for byte, which is the property the
    whole mechanism is arranged around.
    """
    return {
        "root": PROVIDED_DIRECTORY,
        "count": len(replacements),
        "replaces": [
            {
                "key": replacement.key,
                "kind": replacement.kind,
                "file": f"{PROVIDED_DIRECTORY}/{replacement.key}",
                "sha256": replacement.digest,
                "bytes": replacement.size_bytes,
            }
            for replacement in sorted(replacements.values(),
                                      key=lambda item: item.key)
        ],
    }


def describe(replacement: Replacement) -> str:
    """One line of measurement for an accepted file."""
    measured = replacement.measurements
    ramps = list(measured["ramps"])  # type: ignore[arg-type]
    return (f"{replacement.key}: {measured['colours']} colours, "
            f"{len(ramps)} ramps ({', '.join(ramps)}), "
            f"CI4 moves {float(measured['ci4_loss']):.2%} of "
            f"{measured['opaque_texels']} opaque texels")
