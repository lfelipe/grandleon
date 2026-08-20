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
addressing scheme: sprites are keyed by archetype, style and faction colour and
meshes by archetype and style, so what a submission stands in for is one
archetype's sheet in one style, or one mesh: the manifest key the consoles and
the editor already resolve art through. So the path of a provided file *is* the
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
Character sprites are, and so are character **meshes**. A provided
``character`` or ``character-frames`` sheet is held to every sheet rule in
:data:`RULES`, and both files of a key must arrive together; a provided
``mesh``, which is a glTF 2.0 document and the buffer beside it, is held to
every mesh rule and likewise arrives as a pair.

The interchange format was the open question and is not one any more: **it is
the format this repository already writes**. ``assets/gltf/<style>/`` holds
every commissioned figure as glTF 2.0, and ``verify.check_gltf_round_trip``
reads those files back and rebuilds every part's integers from geometry alone.
What was missing was the *reading* direction as a supported input, which
:func:`.gltf.read_model` now is.

One thing glTF cannot carry, and it is worth stating rather than discovering:
**the file carries the model and cannot carry the contract.** The silhouette
rule holds a mesh to the opaque box of *its own archetype's sprite*, a
measurement of a different artefact, so no model file can state it and this
module measures the sprite itself. Two more of the mesh rules survive a file
only by a naming convention this repository invented (the ramp and the rung in a
material's name and ``extras``), which is why both routes are required to agree
rather than either being read.

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

from . import (characters, figures, frames, gltf, meshes, palette,
               playstation_header, preview3d, profiles, styles, terrain, themes)
from .meshes import rules
from .raster import Canvas

#: File extensions that name a 3D model this repository does not read. glTF 2.0
#: is deliberately absent: it is the interchange format, because it is the one
#: ``assets/gltf/`` is already written in. The rest are refused **by name**,
#: because arriving with a ``.blend`` is a reasonable thing to have done and
#: "it does not name an asset" would be the wrong answer to it.
MODEL_SUFFIXES = (".glb", ".obj", ".fbx", ".blend", ".dae", ".stl", ".ply",
                  ".3ds", ".usd", ".usdz")

#: The two files one provided model is, by suffix: the document and the buffer
#: it references. Both are required, for the reason a character sheet requires
#: its sequence strip: half a submission is not one.
MODEL_DOCUMENT_SUFFIX = ".gltf"
MODEL_BUFFER_SUFFIX = ".bin"

#: Where a contributor puts a replacement, relative to the repository root.
#: Absent by default: this repository provides nothing, and an absent directory
#: says that more plainly than an empty one with a placeholder file in it.
PROVIDED_DIRECTORY = "art/provided"

#: Worked examples, ready to copy into the tree above. Deliberately *not*
#: scanned: an example that took effect would be a replacement, and the shipped
#: default set has to stay the shipped default set.
EXAMPLES_DIRECTORY = "art/examples"

#: The largest a submitted file may be, before anything reads past its first
#: byte. The largest character sheet this repository generates is 1,082 bytes
#: and the largest model document 28,351, so 64 KiB is sixty times the honest
#: need for a sheet and twice it for a model. A legal model is bounded, not
#: merely observed: the triangle band caps a figure at 25 parts and the widest
#: part this library writes costs 1,302 bytes of document, so 32,550 is the most
#: a model obeying the mesh rules can spend on geometry. Small enough, either
#: way, that a hostile file cannot cost memory worth measuring.
MAX_FILE_BYTES = 64 * 1024

#: How deep the scan will descend. A sheet's key is two components, a directory
#: and a filename, and a model's is three, because its directory is keyed by
#: style. Four is one past the deepest honest need, and it is what stops a
#: pathological tree from being walked forever.
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
     "the manifest's for a sheet, the export's for a model"),
    ("unsupported-kind",
     "only character sheets and character meshes are replaceable today, and a "
     "mesh only as glTF 2.0"),
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
    ("mesh-silhouette",
     "an archetype's sprite and its mesh are two drawings of one figure: the "
     "sprite's opaque box and the mesh's authored width stay within the "
     "tolerance of each other, whichever of the two was provided"),
    ("incomplete-sequence",
     "a character key is replaced as a pair: the standing sprite and its "
     "sequence sheet"),
    # ---- and the same discipline for a mesh, which is a part list and not a
    # sheet. The mesh rules, in the order the reader applies them.
    ("not-a-gltf",
     "a provided model is a glTF 2.0 document, which is the format "
     "assets/gltf/ is already written in"),
    ("malformed-gltf",
     "a provided model's document is structurally sound: every index inside "
     "its array, every accessor inside its buffer, every coordinate a whole "
     "world unit"),
    ("unsupported-gltf",
     "a provided model uses no glTF feature this format does not carry: no "
     "extension, animation, skin, camera, texture, sparse accessor or node "
     "transform but the root's one scale"),
    ("mesh-not-a-box",
     "every part is a convex axis-aligned box: twenty-four corners, six axis "
     "normals four corners each, every triangle wound outward"),
    ("mesh-extent",
     f"no coordinate sits further than {gltf.MAX_COORDINATE} world units from "
     f"the origin, which is a figure's own height"),
    ("mesh-ramp",
     "every face names a ramp and a rung and never a colour, spelled the same "
     "way in the material's name, the material's extras and the node's, and at "
     "least one part wears the faction ramp"),
    ("mesh-height",
     "a figure is built at MESH_WORLD_HEIGHT with its feet at y = 0"),
    ("mesh-triangle-band",
     "a figure lands inside the measured triangle band"),
    ("mesh-order",
     "parts are authored far-to-near as the *console* evaluates it: the sum of "
     "eight projected corner depths, each truncated by the coprocessor"),
    ("incomplete-model",
     "a mesh key is replaced as a pair: the glTF document and the buffer it "
     "references"),
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
    #: Which drawing this key is, where the key names one. Carried so a rule can
    #: ask the rest of the library about it, and the mesh rule below is the
    #: case that needed it.
    style: str = ""
    archetype: str = ""
    faction: str = ""

    @property
    def cell_width(self) -> int:
        return self.width // self.columns


@dataclass(frozen=True)
class Replacement:
    """One accepted file, and what was measured while accepting it.

    Exactly one of :attr:`canvas` and :attr:`parts` is set, and which one is
    what ``kind`` says. Both are this repository's own representations,
    master-palette indices and authored integers, because that is the property
    the whole mechanism rests on: **not one byte of a submitted file reaches a
    generated artefact.**
    """

    key: str
    kind: str
    digest: str
    size_bytes: int
    #: A sheet's pixels, for a ``character`` or ``character-frames`` key.
    canvas: Optional[Canvas] = None
    #: A figure's part list, for a ``mesh`` key.
    parts: Optional[Tuple[meshes.Part, ...]] = None
    #: Which figure a ``mesh`` key stands in for.
    style: str = ""
    archetype: str = ""
    measurements: Dict[str, object] = field(default_factory=dict)


def canvas_replacements(accepted: Mapping[str, Replacement]
                        ) -> Dict[str, Replacement]:
    """The accepted sheets, keyed by the manifest path they stand in for."""
    return {key: replacement for key, replacement in accepted.items()
            if replacement.canvas is not None}


def mesh_replacements(accepted: Mapping[str, Replacement]
                      ) -> Dict[Tuple[str, str], Tuple[meshes.Part, ...]]:
    """The accepted figures, keyed the way :func:`.meshes.provided` wants them.

    Keyed by style and archetype rather than by path, because that is what a
    mesh *is* addressed by everywhere downstream; the path is how it arrived.
    """
    return {(replacement.style, replacement.archetype): replacement.parts
            for replacement in accepted.values()
            if replacement.parts is not None}


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

    # The meshes. A key here is exactly the path the export writes under
    # `assets/`, so the same sentence covers both halves of the library: the
    # path of a provided file is the path of the thing it stands in for. The
    # table is built from the commission rather than from the roster, because a
    # model for an archetype nobody has commissioned would be an *addition* and
    # not a replacement, and additions want rules of their own.
    for style in styles.STYLES:
        for archetype in characters.ARCHETYPE_ORDER:
            if not meshes.parts_for(style, archetype):
                continue
            stem = (f"{gltf.key_directory(style.name)}/"
                    f"{gltf.model_name(archetype)}")
            document = f"{stem}{MODEL_DOCUMENT_SUFFIX}"
            buffer = f"{stem}{MODEL_BUFFER_SUFFIX}"
            table[document] = Specification(
                document, "mesh", 0, 0, 1, partner=buffer,
                style=style.name, archetype=archetype)
            table[buffer] = Specification(
                buffer, "mesh-buffer", 0, 0, 1, partner=document,
                style=style.name, archetype=archetype)

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


def _silhouette_refusal(style_name: str, archetype: str, canvas: Canvas,
                        parts: Sequence[meshes.Part],
                        sprite_label: str,
                        mesh_label: str) -> Optional[Refusal]:
    """A mesh is held to its own sprite's box, whichever side was provided.

    Found by replacing a sprite: the sprite is not the only drawing of an
    archetype. The solid is held to *that sprite's* own opaque box, measured
    off the CI4 texels, on faction colour zero, by the build itself, so
    replacing either drawing moves the other's target. Without this rule a
    sprite submission validated and the build then failed a hundred seconds
    later inside `meshes/rules.py`, with a message written for a mesh author
    telling them to reproportion boxes they never touched.

    **This is the rule no interchange format can carry**, and it is why the
    export wave's finding is load-bearing here rather than a remark: glTF
    carries the model and cannot carry the contract. The number a mesh is held
    to is a measurement of a *different artefact*, so it can be neither written
    into a model file nor read out of one; the sprite is measured, here, on the
    same texels the console uploads.

    The refusal is addressed to the file that arrived, and says the thing that
    file's author can act on: a sprite's author gets the range of silhouette
    widths their drawing may have, a mesh's author the range of world widths
    their figure may be. Only the faction-zero sprite is measured; every other
    faction recolours inside the same silhouette.
    """
    silhouette = _silhouette_of(canvas)
    if silhouette.width <= 0 or silhouette.height <= 0:
        return Refusal(
            mesh_label or sprite_label, "mesh-silhouette",
            f"the {style_name} {archetype} sprite has no opaque silhouette for "
            f"a solid to be held to")
    asked = meshes.target_width(silhouette)
    actual = meshes.authored_width(parts)
    if abs(actual - asked) <= meshes.WIDTH_TOLERANCE:
        # Width first, then the height half of the same rule on the same box.
        # In that order because a replaced sprite moves both targets at once,
        # and a mesh author told to reproportion a figure whose width is
        # already right has been told the wrong thing.
        #
        # A figure is built to be *drawn* its sprite's height, which is not the
        # same as being built that tall: the pitch sends world y to screen
        # through cos and world z through sin, so a figure's own depth buys
        # screen height and has to be paid for out of its build height.
        drawn = meshes.projected_height(parts)
        wants = meshes.target_height(silhouette)
        if abs(drawn - wants) > meshes.HEIGHT_TOLERANCE:
            return Refusal(
                mesh_label or sprite_label, "mesh-height",
                f"the {style_name} {archetype} is drawn {drawn:.1f} world "
                f"units tall where its own sprite's figure asks {wants}. Scale "
                f"the figure's y until meshes.projected_height matches; a "
                f"figure of no depth would be built "
                f"{meshes.MESH_WORLD_HEIGHT} tall and every unit of depth it "
                f"does have costs it tan({meshes.PITCH_DEGREES}) units of that")
        return None
    if mesh_label:
        return Refusal(
            mesh_label, "mesh-silhouette",
            f"the {style_name} {archetype} is authored {actual} world units "
            f"wide and its own sprite's opaque box is {silhouette.width} "
            f"texels, which asks for {asked}. That is past the "
            f"{meshes.WIDTH_TOLERANCE}-unit tolerance. This archetype is drawn "
            f"twice, as a sprite and as a solid, and the solid is held to the "
            f"sprite of its own style. Author it between "
            f"{asked - meshes.WIDTH_TOLERANCE} and "
            f"{asked + meshes.WIDTH_TOLERANCE} units wide. No model file can "
            f"state this rule, because it is a measurement of the sprite and "
            f"not of the model, so the sprite is measured here")
    allowed = [texels for texels in range(1, canvas.width + 1)
               if abs(actual - meshes.target_width(
                   meshes.Silhouette(texels, silhouette.height,
                                     silhouette.area,
                                     silhouette.figure_height)))
               <= meshes.WIDTH_TOLERANCE]
    span = (f"between {allowed[0]} and {allowed[-1]} texels wide"
            if allowed else "no width at all, at this mesh's proportions")
    return Refusal(
        sprite_label, "mesh-silhouette",
        f"its opaque box is {silhouette.width} texels wide, which asks the "
        f"{style_name} {archetype} mesh for {asked} world units where it "
        f"is authored {actual}. That is past the {meshes.WIDTH_TOLERANCE}-unit "
        f"tolerance. This archetype is drawn twice: as this sprite and as a "
        f"solid held to this sprite's own silhouette. Draw it {span}, or "
        f"provide the mesh with it (a model at "
        f"gltf/{style_name}/{archetype}.gltf is replaceable too), or the "
        f"commission in placeholder_art/meshes/{style_name}.py has to be "
        f"reproportioned. "
        f"Only this faction colour is measured; the other five recolour inside "
        f"the same silhouette")


# ---------------------------------------------------------------------------
# A provided mesh
#
# The mesh rules, applied to a part list that came out of a file somebody else
# wrote. They live here rather than in `meshes/rules.py` for the reason the
# sheet rules live here rather than in `characters.py`: that module states what
# the *shipped* art is and raises an AssertionError naming an authored table
# when it is wrong, which is exactly the message an outside contributor should
# never be shown. The rules are the same rules; the audience, and therefore the
# sentence, is not.
#
# Two of them are stronger here than there, and deliberately:
#
# * the order is checked the way the **machine** evaluates it rather than the
#   way the generator does. See `_order_refusal`;
# * the silhouette is checked in the direction a mesh author can act on, which
#   is the same rule `_mesh_refusal` above applies to a provided sprite from the
#   other side.
# ---------------------------------------------------------------------------

#: How much of the console's eight-corner depth sum the coprocessor's own
#: truncation can be worth. Each of the eight projected corner depths is
#: truncated to a whole unit by the shift the GTE applies, so eight truncations
#: are worth up to eight units of the sum, which is exactly the margin the
#: console refused a `pirates` commission over, on gaps of 1.4 and 2.6 units
#: that the generator's own exact arithmetic on part centres had accepted. A
#: margin of one or two units in exact arithmetic is therefore noise: give a
#: part that overlaps another on screen at least two units, and check the pair
#: rather than the figure.
def _order_refusal(file_label: str,
                   parts: Sequence[meshes.Part]) -> Optional[Refusal]:
    """Far-to-near order, asked of the machine rather than of exact arithmetic.

    Two readings, and a submission has to survive both. `meshes.rules` owns the
    arithmetic, the console's eight truncated corner depths, and offers it at
    the focus, where the scratch photographs, and as the worst of every phase a
    board can stand a figure at. A pair inverted **at the focus** is refused
    whatever it overlaps, because that is the picture a reader is shown. A pair
    inverted only at some other phase is refused when the two are **drawn over
    one another**, because an order between parts that never share a pixel is
    an order that decides nothing, and holding a contributor to more than that
    would hold them to a rule three of the seven shipped commissions break.
    """
    for index, (before, after) in enumerate(zip(parts, parts[1:])):
        focus = rules.machine_order_margin_at_focus(before, after)
        worst = rules.machine_order_margin(before, after)
        overlaps = rules.drawn_over_one_another(before, after)
        if focus >= 0 and not (worst < 0 and overlaps):
            continue
        margin = focus if focus < 0 else worst
        where = ("at the camera the scratch photographs"
                 if focus < 0 else
                 "at one of the elevations a board can stand it at, where the "
                 "two are drawn over one another")
        exact = meshes.depth_key(before) - meshes.depth_key(after)
        return Refusal(
            file_label, "mesh-order",
            f"part {index + 1} '{after.name}' draws in front of part {index} "
            f"'{before.name}' on the console {where}, by {-margin} units of "
            f"the sum of eight projected corner depths. So the array is not "
            f"far-to-near and the scene would need a depth buffer, an ordering "
            f"table and an AVSZ4. The generator's own rule is "
            f"exact arithmetic on part centres and puts these "
            f"{exact:+.2f} apart; the machine truncates each of the eight "
            f"corners, which is worth up to {meshes.VERTICES_PER_PART} units "
            f"of the sum, so a pair this close is decided by the truncation. "
            f"Move the nearer part further forward in z or lower in y, or "
            f"author it earlier in the list")
    return None


def _mesh_refusals(file_label: str, parts: Sequence[meshes.Part],
                   spec: Specification) -> List[Refusal]:
    """Every mesh rule that is about the figure rather than about the file.

    The silhouette rule is not here: it is a measurement of the archetype's
    *sprite*, so it is applied once per figure by :func:`_silhouette_refusal`,
    after both halves of a submission have been read.
    """
    out: List[Refusal] = []
    where = f"the {spec.style} {spec.archetype}"

    feet = min(part.y0 for part in parts)
    crown = max(part.y1 for part in parts)
    if feet != 0:
        out.append(Refusal(
            file_label, "mesh-height",
            f"{where} stands from y = {feet} rather than from y = 0, and a "
            f"figure's feet are its origin"))
    elif crown > meshes.MESH_WORLD_HEIGHT:
        out.append(Refusal(
            file_label, "mesh-height",
            f"{where} reaches y = {crown}, past the "
            f"{meshes.MESH_WORLD_HEIGHT}-unit ceiling, which is what a figure "
            f"of no depth at all would be built to to stand its sprite's "
            f"height"))


    triangles = len(parts) * meshes.TRIANGLES_PER_PART
    low, high = meshes.TRIANGLE_BAND
    if not low <= triangles <= high:
        out.append(Refusal(
            file_label, "mesh-triangle-band",
            f"{where} is {len(parts)} boxes, which is {triangles} triangles, "
            f"outside the measured {low}-{high} band. A part costs "
            f"{meshes.TRIANGLES_PER_PART} triangles, so a figure is between "
            f"{-(-low // meshes.TRIANGLES_PER_PART)} and "
            f"{high // meshes.TRIANGLES_PER_PART} parts. The floor is as real "
            f"as the ceiling: a figure below it is a box with a head"))

    if not any(part.ramp == meshes.RAMP_FACTION for part in parts):
        out.append(Refusal(
            file_label, "mesh-ramp",
            f"no part of {where} wears the faction ramp (ramp "
            f"{meshes.RAMP_FACTION}), so all six factions would draw the same "
            f"figure. A mesh carries no colour: a face names a ramp and a rung "
            f"and the drawing resolves it from that faction's own CLUT"))

    order = _order_refusal(file_label, parts)
    if order is not None:
        out.append(order)
    return out


def _measure_mesh(parts: Sequence[meshes.Part]) -> Dict[str, object]:
    """What an accepted model reports, whether or not anything was refused.

    ``order_margin`` and ``order_close_pairs`` are the numbers a mesh author
    most wants and has no other way to see: the tightest adjacent gap in the
    console's own quantity, and how many adjacent pairs sit inside the
    :data:`meshes.VERTICES_PER_PART` units the coprocessor's truncation is worth.
    A pair inside that band draws in the authored order at the camera this
    checks and is not *guaranteed* to at every other, which is why the number is
    reported rather than assumed away. The margins are the worst standing phase,
    which is the reading that answers "wherever the board puts this figure".
    """
    margins = [rules.machine_order_margin(before, after)
               for before, after in zip(parts, parts[1:])]
    return {
        "parts": len(parts),
        "triangles": len(parts) * meshes.TRIANGLES_PER_PART,
        "width": meshes.authored_width(parts),
        "rungs": sorted({(part.ramp, part.rung) for part in parts}),
        "order_margin": min(margins) if margins else 0,
        "order_close_pairs": sum(1 for margin in margins
                                 if margin < meshes.VERTICES_PER_PART),
    }


def _read_mesh(label: str, spec: Specification, document: bytes, buffer: bytes
               ) -> Tuple[Optional[Tuple[meshes.Part, ...]],
                          Dict[str, object], List[Refusal]]:
    """One submitted model, parsed and then held to the mesh rules.

    Parsing and rule-checking are two steps rather than one because the first
    answers "is this a model at all" about a file nobody trusts, and the second
    answers "is this figure inside the contract" about a part list of integers
    that no longer holds anything of the submission.
    """
    try:
        parts = gltf.read_model(document, buffer, spec.style, spec.archetype)
    except gltf.ModelRefused as error:
        return None, {}, [Refusal(label, error.code, error.reason)]
    refusals = _mesh_refusals(label, parts, spec)
    if refusals:
        return None, {}, refusals
    return parts, _measure_mesh(parts), []


def _silhouette_of(canvas: Canvas) -> meshes.Silhouette:
    """One sprite's opaque box, by the route the build itself measures it.

    The CI4 conversion of the faction-zero sprite, read through
    :func:`.playstation_header.silhouette_of`, which is the same texels the
    console uploads to VRAM, so a provided mesh is held to the number the
    generated header will carry rather than to a second opinion of it.
    """
    return playstation_header.silhouette_of(
        profiles.convert(canvas, profiles.PROFILES_BY_NAME["n64_ci4"],
                         is_sprite=True))


def _sprite_of(style_name: str, archetype: str,
               provided_sheets: Mapping[str, Canvas]) -> Canvas:
    """The faction-zero sprite a mesh of this archetype is held to.

    A **provided** sheet where one arrived in the same submission, and the
    generated one otherwise. Replacing a sprite and its own mesh together has to
    be a thing that works: the two are drawings of one figure, and holding a new
    solid to the old drawing's silhouette would refuse exactly the submission
    that got both right.
    """
    style = styles.STYLES_BY_NAME[style_name]
    faction = characters.FACTION_COLOURS[0].name
    key = (f"characters/{archetype}_{faction}"
           f"{styles.asset_suffix(style)}.png")
    provided_sheet = provided_sheets.get(key)
    if provided_sheet is not None:
        return provided_sheet
    return styles.sprite(style, archetype, faction)


def _silhouette_refusals(accepted: Mapping[str, Replacement],
                         table: Mapping[str, Specification]) -> List[Refusal]:
    """The silhouette rule over every figure this submission touches.

    A submission can move the sprite, the solid, or both, and the rule is one
    rule about the pair. So the figures are gathered first and each is measured
    once, with whichever drawings arrived standing in for the ones that did not.
    """
    sheets = {key: replacement.canvas
              for key, replacement in accepted.items()
              if replacement.canvas is not None}
    figures: Dict[Tuple[str, str], Dict[str, str]] = {}
    for key, replacement in accepted.items():
        if not replacement.style:
            continue
        spec = table[key]
        if spec.kind == "character" and \
                spec.faction == characters.FACTION_COLOURS[0].name:
            side = "sprite"
        elif spec.kind == "mesh":
            side = "mesh"
        else:
            continue
        figures.setdefault((replacement.style, replacement.archetype),
                           {})[side] = key

    out: List[Refusal] = []
    for (style_name, archetype), keys in sorted(figures.items()):
        style = styles.STYLES_BY_NAME.get(style_name)
        if style is None:
            continue
        mesh_key = keys.get("mesh")
        parts = (accepted[mesh_key].parts if mesh_key
                 else meshes.parts_for(style, archetype))
        if not parts:
            # A style with no commission has nothing to hold this sprite to.
            continue
        refusal = _silhouette_refusal(
            style_name, archetype,
            _sprite_of(style_name, archetype, sheets), parts,
            _label(keys.get("sprite")), _label(mesh_key))
        if refusal is not None:
            out.append(refusal)
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

    #: A model's two files, held until both have passed every rule that is
    #: about a file rather than about a figure. A document cannot be read
    #: without the buffer it describes, and the buffer cannot be read without
    #: the document that says what is in it.
    model_bytes: Dict[str, bytes] = {}

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
                f"one that could not be listed. The deepest key is three "
                f"components (a directory, a style and a filename), so the "
                f"scan stops there"
                if stat.S_ISDIR(info.st_mode) else
                "it is not a regular file. A provided file is a PNG or a model "
                "on disk, not a device, a socket or a pipe"))
            continue
        if info.st_size > MAX_FILE_BYTES:
            refusals.append(Refusal(
                label, "too-large",
                f"it is {info.st_size} bytes and a provided file may be at "
                f"most {MAX_FILE_BYTES}. The largest sheet this repository "
                f"generates is 1,082"))
            continue

        spec = table.get(key)
        if spec is None and key.lower().endswith(MODEL_SUFFIXES):
            refusals.append(Refusal(
                label, "unsupported-kind",
                f"a model is provided as glTF 2.0 and this is not one. The "
                f"interchange format is the one this repository already writes "
                f"and reads back: assets/gltf/<style>/<archetype>.gltf and the "
                f".bin beside it, which "
                f"verify.check_gltf_round_trip reconstructs every authored "
                f"integer from. Export from your tool to glTF 2.0 with a "
                f"separate buffer, or start from the exported model of the "
                f"figure you are replacing"))
            continue
        if spec is None:
            near = lowered.get(key.lower())
            hint = (f" Did you mean {near}? A key is exactly the manifest path, "
                    "letter for letter." if near else
                    " Run generate.py --list-keys for every key a replacement "
                    "may name.")
            if near is None and key.lower().endswith(
                    (MODEL_DOCUMENT_SUFFIX, MODEL_BUFFER_SUFFIX)):
                hint = (" A model's key is gltf/<style>/<archetype>.gltf and "
                        "the .bin beside it, which is where the export writes "
                        "it under assets/.")
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

        # A model is two files and neither is a sheet, so it leaves the sheet
        # path here with its bytes and is read below, once both halves of it
        # have arrived. Everything above this line applied to it exactly as it
        # applied to a PNG: the link, the file kind, the two byte caps and the
        # whitelisted key, because none of that was ever about pixels.
        if spec.kind in ("mesh", "mesh-buffer"):
            model_bytes[key] = data
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

    # The models, once both halves of each are in hand. A document without its
    # buffer is not read at all, because there is nothing to read it against;
    # and a buffer without its document is not read at all, because nothing says
    # what is in it. Both are named here rather than in the missing-partner pass
    # below, which only looks at what was accepted, and half a model never is.
    submitted = {key for key, _ in entries}
    for key, data in sorted(model_bytes.items()):
        spec = table[key]
        partner = str(spec.partner)
        if partner not in submitted:
            refusals.append(Refusal(
                _label(key), "incomplete-model",
                f"it is provided without {partner}. A model is replaced as a "
                f"pair (the glTF document and the buffer it references) "
                f"because the document is a description of bytes that are not "
                f"in it: every corner, normal and triangle lives in the .bin, "
                f"and neither file says anything without the other"))
            continue
        if partner not in model_bytes or spec.kind != "mesh":
            # Half of this model arrived and was refused by its own rule, which
            # has already said so. Repeating it as an absence would send its
            # author looking for a file they can see.
            continue
        buffer = model_bytes[partner]
        parts, measured, broken = _read_mesh(
            f"{PROVIDED_DIRECTORY}/{key}", spec, data, buffer)
        if broken:
            refusals.extend(broken)
            continue
        assert parts is not None
        accepted[key] = Replacement(
            key=key, kind=spec.kind, parts=parts,
            digest=hashlib.sha256(data).hexdigest(),
            size_bytes=len(data), measurements=dict(measured),
            style=spec.style, archetype=spec.archetype)
        # The buffer is accepted as the half of the model it is, and carries no
        # measurement of its own: everything it holds has already been read out
        # of it as integers, and none of its bytes goes anywhere.
        accepted[str(spec.partner)] = Replacement(
            key=str(spec.partner), kind="mesh-buffer",
            digest=hashlib.sha256(buffer).hexdigest(),
            size_bytes=len(buffer), style=spec.style,
            archetype=spec.archetype)

    # The silhouette rule, once per figure, after both drawings of it are known.
    # It is the one rule that spans two artefacts, since a solid is held to *its
    # own sprite's* opaque box, so it cannot be applied while reading either
    # one, and no interchange format can carry it. Every archetype either half
    # of a submission touches is measured, against the provided drawing where
    # one arrived and the generated one otherwise.
    refusals.extend(_silhouette_refusals(accepted, table))

    # Only a partner that was never *submitted* is missing. One that arrived and
    # was refused has already been reported by its own rule, and saying it is
    # absent as well would send its author looking for a file they can see. A
    # model's pair is settled above, where half of one is never accepted at all.
    for key in sorted(accepted):
        partner = table[key].partner
        if not partner or partner in submitted \
                or table[key].kind in ("mesh", "mesh-buffer"):
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
    if replacement.kind == "mesh-buffer":
        return (f"{replacement.key}: {replacement.size_bytes} bytes of "
                f"geometry, read as integers and re-emitted")
    if replacement.kind == "mesh":
        return (f"{replacement.key}: {measured['parts']} parts, "
                f"{measured['triangles']} triangles, {measured['width']} world "
                f"units wide, {len(list(measured['rungs']))} ramp/rung pairs, "
                f"tightest far-to-near margin {measured['order_margin']} of "
                f"the {meshes.VERTICES_PER_PART} units the console's truncation "
                f"is worth ({measured['order_close_pairs']} adjacent pairs "
                f"inside it)")
    ramps = list(measured["ramps"])  # type: ignore[arg-type]
    return (f"{replacement.key}: {measured['colours']} colours, "
            f"{len(ramps)} ramps ({', '.join(ramps)}), "
            f"CI4 moves {float(measured['ci4_loss']):.2%} of "
            f"{measured['opaque_texels']} opaque texels")
