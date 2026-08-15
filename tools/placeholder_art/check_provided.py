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
  pixels a side, a model document holding a `NaN`, one whose accessor points
  outside its buffer, and one whose buffer is a data URI. A rule that could not
  be provoked is a rule this file refuses to claim, so the run fails if any code
  goes unexercised.
* **Both worked examples are accepted**: a sheet and a model. Standing either
  in moves exactly what it names and nothing else.
* **The same model, formatted differently, is the same integers**, which is the
  model half of "not one byte of a submitted file reaches a generated artefact".
* **The ordering rule is stricter than the generator's own**, measured on a
  figure `meshes/rules.py` accepts with a tightest gap of exactly 0.00 and the
  console inverts.

``--end-to-end`` adds the expensive half: three whole builds, one with nothing
provided, one with the sheet example, one with the model example. It reads the
provided figure's own pixels back out of every profile tree and the console
character header, and the provided figure's own part integers back out of the
console mesh header and the re-exported model.

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

from placeholder_art.meshes import rules
from placeholder_art import build as build_module  # noqa: E402
from placeholder_art import (characters, figures, frames, gltf,  # noqa: E402
                             meshes, palette, playstation_header, profiles,
                             provided, styles)

REPOSITORY = ROOT.parent.parent
EXAMPLE = REPOSITORY / provided.EXAMPLES_DIRECTORY / "lantern-keeper"
MODEL_EXAMPLE = REPOSITORY / provided.EXAMPLES_DIRECTORY / "banded-cutpurse"

#: The key the worked example replaces. A `pirates` sprite on purpose: no
#: project in this repository names that style, so nothing that is pinned by a
#: golden value is downstream of it, and the end-to-end run can build the real
#: thing without moving a ROM.
#:
#: The *archetype* is chosen by the `mesh-silhouette` rule and not by taste.
#: With the mesh library complete there is no archetype anywhere that a sprite
#: can be replaced at without meeting a solid, so the example has to land on one
#: whose solid it already satisfies.
#: The example's opaque box is 18 texels, which asks a mesh for 36 world units,
#: and this style's `mage` is authored at exactly 36: the one figure in the
#: library the drawing fits without a pixel being moved.
STANDING = "characters/mage_blue_pirates.png"
STRIP = "characters/mage_blue_pirates_frames.png"

#: The figure the worked **mesh** example stands in for, and the two keys it
#: arrives as. The same style as the sheet example and for the same reason:
#: nothing pinned by a golden value is downstream of `pirates`. A different
#: archetype, so that the two examples are independent submissions rather than
#: one, and the diff each of them moves stays exactly countable.
MODEL_STYLE = "pirates"
MODEL_ARCHETYPE = "rogue"
MODEL_DOCUMENT = (f"{gltf.key_directory(MODEL_STYLE)}/"
                  f"{gltf.model_name(MODEL_ARCHETYPE)}.gltf")
MODEL_BUFFER = (f"{gltf.key_directory(MODEL_STYLE)}/"
                f"{gltf.model_name(MODEL_ARCHETYPE)}.bin")

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


# ---------------------------------------------------------------------------
# Making models, hostile and otherwise.
#
# Every submission below is written from a part list by `gltf.document`, this
# repository's own writer. That is the shortest way to be sure a case fails for
# the reason it claims to and not because the fixture was malformed by
# accident. The two arguments the writer wants that a *reader* never looks at,
# the resolved palette words and the sprite's silhouette, are stubbed: a
# submitted model carries no colour that anything reads and cannot carry the
# silhouette rule at all, which is the whole point of measuring the sprite.
# ---------------------------------------------------------------------------

def _stub_channels(word: int) -> Tuple[int, int, int]:
    return (0, 0, 0)


def _model(parts: Tuple[meshes.Part, ...],
           archetype: str = MODEL_ARCHETYPE) -> Tuple[bytes, bytes]:
    """One model as the two files it is: the document and its buffer."""
    words = [[0] * meshes.RUNG_COUNT for _ in range(meshes.RAMP_COUNT)]
    text, buffer = gltf.document(
        MODEL_STYLE, archetype, parts, meshes.Silhouette(0, 0, 0), words,
        characters.FACTION_COLOURS[gltf.BAKED_FACTION].name, _stub_channels)
    return text.encode("utf-8"), buffer


def _example_parts() -> Tuple[meshes.Part, ...]:
    """The worked example's own part list, read back out of its own files."""
    return gltf.read_model(
        (MODEL_EXAMPLE / MODEL_DOCUMENT).read_bytes(),
        (MODEL_EXAMPLE / MODEL_BUFFER).read_bytes(),
        MODEL_STYLE, MODEL_ARCHETYPE)


def _good_model() -> Tuple[bytes, bytes]:
    return ((MODEL_EXAMPLE / MODEL_DOCUMENT).read_bytes(),
            (MODEL_EXAMPLE / MODEL_BUFFER).read_bytes())


def _edited_document(edit) -> bytes:
    """The worked example's document with ``edit`` let at its JSON."""
    document = json.loads((MODEL_EXAMPLE / MODEL_DOCUMENT).read_text(
        encoding="utf-8"))
    edit(document)
    return json.dumps(document).encode("utf-8")


def _moved(parts: Tuple[meshes.Part, ...], index: int, **shifts: int
           ) -> Tuple[meshes.Part, ...]:
    """One part of a figure moved, and the rest left where they were."""
    part = parts[index]
    values = dict(zip(meshes.PART_FIELDS, part.values()))
    values.update(shifts)
    return (parts[:index]
            + (meshes.Part(name=part.name, **values),)
            + parts[index + 1:])


def _model_cases() -> List[Tuple[str, str, Dict[str, object]]]:
    """(refusal code, description, tree) for every rule a mesh is held to."""
    document, buffer = _good_model()
    both: Dict[str, object] = {MODEL_DOCUMENT: document,
                               MODEL_BUFFER: buffer}
    parts = _example_parts()

    # A pair the *generator's* own rule accepts and the machine does not. Its
    # exact depth-key gap on part centres is 0.0, which `meshes.rules` reads as
    # far-to-near; the console truncates each of eight projected corner depths
    # and puts the two four units the wrong way round. This is the case the
    # whole `mesh-order` rule exists for, and nothing in `meshes/rules.py` can
    # catch it.
    machine_only = _moved(parts, 12, y0=parts[12].y0 + 3, y1=parts[12].y1 + 3,
                          z0=parts[12].z0 + 12, z1=parts[12].z1 + 12)
    #: Every part on the neutral ramp, so the six factions would draw one figure.
    colourless = tuple(
        meshes.Part(*part.values()[:6], meshes.RAMP_NEUTRAL, part.rung,
                    part.name)
        for part in parts)
    #: Corners 0 and 1 of the first face swapped, which reverses that face's
    #: winding and leaves the box's own bounds untouched, so the refusal is
    #: about the solid rather than about the declared minimum and maximum.
    unwound = bytearray(buffer)
    unwound[0:12], unwound[12:24] = buffer[12:24], buffer[0:12]

    return [
        ("not-a-gltf", "a text file at a model's key",
         {**both, MODEL_DOCUMENT: b"this is not a model, it is an apology\n"}),
        ("not-a-gltf", "JSON that declares no glTF version",
         {**both, MODEL_DOCUMENT: b'{"nodes": []}'}),
        ("malformed-gltf", "a document holding the JSON literal NaN",
         {**both, MODEL_DOCUMENT: _edited_document(lambda document: None)
             .replace(b'"scale": [0.015625', b'"scale": [NaN')}),
        ("malformed-gltf", "an accessor pointing at a buffer view that is not "
                           "there",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["accessors"][0].__setitem__(
                 "bufferView", 99))}),
        ("malformed-gltf", "a part node claiming a position it is not at",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["nodes"][1]["extras"].__setitem__(
                 "part", 7))}),
        ("malformed-gltf", "a part name carrying an escape sequence",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["nodes"][1]["extras"].__setitem__(
                 "partName", "left\x1b[31m boot"))}),
        ("unsupported-gltf", "an extension this reader would have to implement",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document.__setitem__(
                 "extensionsRequired", ["KHR_materials_unlit"]))}),
        ("unsupported-gltf", "a buffer inlined as a data URI",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["buffers"][0].__setitem__(
                 "uri", "data:application/octet-stream;base64,AAAA"))}),
        ("unsupported-gltf", "a transform on a part node",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["nodes"][1].__setitem__(
                 "translation", [1.0, 0.0, 0.0]))}),
        ("mesh-not-a-box", "a face wound against its own normal",
         {MODEL_DOCUMENT: document, MODEL_BUFFER: bytes(unwound)}),
        ("mesh-extent", "a corner further from the origin than the figure is "
                        "tall",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER),
                  _model(_moved(parts, 0, z1=200))))),
        ("mesh-ramp", "a material naming no ramp and no rung",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["materials"][0].__setitem__(
                 "name", "shiny_red"))}),
        ("mesh-ramp", "a material whose name and extras disagree",
         {**both, MODEL_DOCUMENT: _edited_document(
             lambda document: document["materials"][0]["extras"].__setitem__(
                 "rung", 9))}),
        ("mesh-ramp", "no part wearing the faction ramp",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER), _model(colourless)))),
        ("mesh-height", "a figure that does not reach its build height",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER),
                  _model(_moved(parts, len(parts) - 1,
                                y1=meshes.MESH_WORLD_HEIGHT - 8))))),
        ("mesh-triangle-band", "a figure below the triangle floor",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER), _model(parts[:10])))),
        ("mesh-order", "an order the generator's arithmetic accepts and the "
                       "console does not",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER), _model(machine_only)))),
        ("mesh-silhouette", "a figure wider than its own sprite asks for",
         dict(zip((MODEL_DOCUMENT, MODEL_BUFFER),
                  _model(_moved(parts, 0, x0=-60))))),
        ("incomplete-model", "a document without the buffer it describes",
         {MODEL_DOCUMENT: document}),
        ("incomplete-model", "a buffer without the document that reads it",
         {MODEL_BUFFER: buffer}),
        ("unknown-key", "a model at an archetype the roster does not hold",
         {**both, f"{gltf.key_directory(MODEL_STYLE)}/wizard.gltf": document}),
        ("unsupported-kind", "a model in a format this repository cannot read",
         {**both,
          f"{gltf.key_directory(MODEL_STYLE)}/{MODEL_ARCHETYPE}.blend":
              b"BLENDER-v300"}),
    ]


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
        # The worked example fits the pirates hexer exactly: 18 texels, which
        # asks for the 36 world units that mesh is authored at. Dropped on the
        # medieval knight, which is authored 45, the same silhouette leaves the
        # solid behind. That is the whole of the rule.
        ("mesh-silhouette",
         "a sprite whose silhouette leaves its archetype's mesh behind",
         {"characters/knight_blue.png": good,
          "characters/knight_blue_frames.png": strip}),
    ] + _model_cases()


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


def check_model_example_accepted() -> Tuple[meshes.Part, ...]:
    """The worked model is accepted, and both of its files are.

    Returns the part list it came back as, which is what everything below
    compares against: the point of the mechanism is that a submitted model
    becomes *integers*, and every claim after this one is about those.
    """
    with tempfile.TemporaryDirectory(prefix="provided-model-") as scratch:
        root = Path(scratch) / "provided"
        document, buffer = _good_model()
        _plant(root, {MODEL_DOCUMENT: document, MODEL_BUFFER: buffer})
        accepted, refusals = provided.read(root)
    if refusals:
        raise Failure("the worked model was refused: "
                      + "; ".join(str(refusal) for refusal in refusals))
    if sorted(accepted) != sorted((MODEL_BUFFER, MODEL_DOCUMENT)):
        raise Failure(f"the worked model accepted {sorted(accepted)}")
    parts = accepted[MODEL_DOCUMENT].parts
    if parts is None:
        raise Failure("the worked model was accepted without a part list")
    shipped = meshes.parts_for(styles.STYLES_BY_NAME[MODEL_STYLE],
                               MODEL_ARCHETYPE)
    if [part.values() for part in parts] == [part.values()
                                             for part in shipped]:
        raise Failure(
            "the worked model is the commissioned figure unchanged, so "
            "providing it would prove nothing about where a replacement ends "
            "up")
    return parts


def check_a_submission_is_read_not_copied(parts: Tuple[meshes.Part, ...]
                                          ) -> None:
    """The same figure, spelled differently, is the same integers.

    This is the model half of "not one byte of a submitted file reaches a
    generated artefact". A sheet gets that property by being decoded to palette
    indices and re-encoded; a model gets it by being read to a part list and
    re-emitted, and the way to see the difference is to submit a document that
    is *formatted* differently and watch the same integers come out. A reader
    that copied anything would carry the formatting with it.
    """
    document, buffer = _good_model()
    payload = json.loads(document.decode("utf-8"))
    respelled = json.dumps(payload, indent=8, sort_keys=False,
                           separators=(" ,", " : ")).encode("utf-8")
    if respelled == document:
        raise Failure("the respelled document is the original, so this check "
                      "compares nothing")
    with tempfile.TemporaryDirectory(prefix="provided-respelled-") as scratch:
        root = Path(scratch) / "provided"
        _plant(root, {MODEL_DOCUMENT: respelled, MODEL_BUFFER: buffer})
        accepted, refusals = provided.read(root)
    if refusals:
        raise Failure(
            "the same model, formatted differently, was refused: "
            + "; ".join(str(refusal) for refusal in refusals))
    again = accepted[MODEL_DOCUMENT].parts or ()
    if [part.values() for part in again] != [part.values() for part in parts]:
        raise Failure("a respelled document read back as different integers")


def check_the_machine_is_stricter_than_the_rule() -> None:
    """The ordering case is one `meshes/rules.py` accepts and the console does not.

    Worth measuring rather than asserting in prose, because it is the reason
    `mesh-order` exists at all. `check_commission` compares
    :func:`.meshes.depth_key`, which is exact arithmetic on part **centres**;
    the console adds up eight *projected corner* depths, each truncated by the
    shift the coprocessor applies. The figure below is far-to-near by the first
    measure and inverted by the second.
    """
    for code, _, tree in _model_cases():
        if code != "mesh-order":
            continue
        with tempfile.TemporaryDirectory(prefix="provided-order-") as scratch:
            root = Path(scratch) / "provided"
            _plant(root, tree)
            _, refusals = provided.read(root)
        parts = gltf.read_model(tree[MODEL_DOCUMENT], tree[MODEL_BUFFER],
                                MODEL_STYLE, MODEL_ARCHETYPE)
        exact = [meshes.depth_key(before) - meshes.depth_key(after)
                 for before, after in zip(parts, parts[1:])]
        if min(exact) < 0:
            raise Failure(
                "the ordering case is refused by the generator's own rule too, "
                "so it does not show that the machine's is stricter")
        if not any(refusal.code == "mesh-order" for refusal in refusals):
            raise Failure("the ordering case was not refused by mesh-order")
        margins = [rules.machine_order_margin_at_focus(before, after)
                   for before, after in zip(parts, parts[1:])]
        print(f"  the generator's own rule puts every pair far-to-near "
              f"(tightest {min(exact):.2f}); the console inverts one by "
              f"{-min(margins)} units of its eight-corner depth sum")
        return
    raise Failure("no ordering case to measure")


def check_model_substitution_is_local() -> None:
    """Standing a model in moves exactly the figure it names and no other.

    The comparison is on the part lists rather than on written files, which is
    where the property lives: the console header, the export, the round trip
    and the roster page are all functions of :func:`.meshes.parts_for`, so a
    figure whose parts are untouched here cannot differ anywhere.
    """
    with tempfile.TemporaryDirectory(prefix="provided-model-") as scratch:
        root = Path(scratch) / "provided"
        document, buffer = _good_model()
        _plant(root, {MODEL_DOCUMENT: document, MODEL_BUFFER: buffer})
        accepted, _ = provided.read(root)
    before = {(style.name, archetype): meshes.parts_for(style, archetype)
              for style in styles.STYLES
              for archetype in characters.ARCHETYPE_ORDER}
    with meshes.provided(provided.mesh_replacements(accepted)):
        after = {(style.name, archetype): meshes.parts_for(style, archetype)
                 for style in styles.STYLES
                 for archetype in characters.ARCHETYPE_ORDER}
    moved = sorted(key for key in before if before[key] != after[key])
    if moved != [(MODEL_STYLE, MODEL_ARCHETYPE)]:
        raise Failure(f"standing one model in moved {len(moved)} figures: "
                      + ", ".join("/".join(key) for key in moved))
    restored = {(style.name, archetype): meshes.parts_for(style, archetype)
                for style in styles.STYLES
                for archetype in characters.ARCHETYPE_ORDER}
    if restored != before:
        raise Failure("the commission was not put back when the build ended")


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


def _check_model_reached_its_clients(plain: Path, modelled: Path,
                                     document: bytes, buffer: bytes) -> None:
    """The provided figure's own integers, in the bytes a client compiles.

    Three claims, and the third is the one worth the build:

    1. the generated PlayStation mesh header for that style carries the provided
       part array, integer for integer, which is the array a console build
       compiles and the renderer walks;
    2. the exported model reads back as the same part list, through the reader
       an outside converter would use;
    3. what was written is the **canonical** spelling of those integers and not
       the submission, which was deliberately respelled before it was handed in.
       That is "not one byte of a submitted file reaches a generated artefact",
       measured rather than asserted.
    """
    style = styles.STYLES_BY_NAME[MODEL_STYLE]
    parts = gltf.read_model(document, buffer, MODEL_STYLE, MODEL_ARCHETYPE)

    name = f"assets/{playstation_header.meshes_header_name(style)}"
    after = (modelled / name).read_text(encoding="utf-8")
    if after == (plain / name).read_text(encoding="utf-8"):
        raise Failure(f"{name} is unchanged; the provided model did not reach "
                      "the console header")
    payload = ",".join(str(value) for part in parts for value in part.values())
    if payload not in after:
        raise Failure(
            f"{name} does not carry the provided figure's own part array. The "
            f"header is what a PlayStation build compiles, so a replacement "
            f"that is not in it is not a replacement")

    exported = modelled / gltf.relative_directory(MODEL_STYLE)
    stem = gltf.model_name(MODEL_ARCHETYPE)
    read_back = gltf.read_model(
        (exported / f"{stem}.gltf").read_bytes(),
        (exported / f"{stem}.bin").read_bytes(), MODEL_STYLE, MODEL_ARCHETYPE)
    if [part.values() for part in read_back] != [part.values()
                                                 for part in parts]:
        raise Failure("the exported model does not read back as the provided "
                      "part list")
    if (exported / f"{stem}.gltf").read_bytes() != document \
            or (exported / f"{stem}.bin").read_bytes() != buffer:
        raise Failure(
            "the exported model is not the canonical spelling of the provided "
            "integers, so something of the submission survived into it")


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
    """Build three times and read each provided figure back out of a client.

    This is the half of the claim that cannot be argued: a replacement has to
    *appear*. One build with nothing provided is the ground both are compared
    against; one stands a sheet in and one stands a **model** in, kept separate
    so each claim's diff stays exactly countable. The assertions are made
    against the same files the Nintendo 64's ``mksprite`` step, the
    PlayStation's ``psx_art.h`` and its mesh header compile.
    """
    with tempfile.TemporaryDirectory(prefix="provided-e2e-") as scratch:
        base = Path(scratch)
        accepted = _accepted({STANDING: _good_standing(),
                              STRIP: _good_strip()})
        document, buffer = _good_model()
        # Deliberately *not* the example's own bytes: the same figure, respelled
        # as JSON, so that what the build writes cannot have come from the
        # submission. What it must write is the canonical spelling, below.
        respelled = json.dumps(json.loads(document.decode("utf-8")),
                               indent=8, sort_keys=False,
                               separators=(" ,", " : ")).encode("utf-8")
        modelled_in = _accepted({MODEL_DOCUMENT: respelled,
                                 MODEL_BUFFER: buffer})

        plain = base / "plain"
        replaced = base / "replaced"
        modelled = base / "modelled"
        print("  building with nothing provided ...")
        build_module.build(plain, quiet=True)
        print("  building with the sheet example provided ...")
        build_module.build(replaced, quiet=True, replacements=accepted)
        print("  building with the model example provided ...")
        build_module.build(modelled, quiet=True, replacements=modelled_in)

        _check_model_reached_its_clients(plain, modelled, document, buffer)

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
        #    on the native canvas, as `check_substitution_is_local` asserts,
        #    but one thing downstream is computed from a *group* rather than
        #    from one asset, and it was found by reading this diff rather than
        #    by reasoning about it: the mesh header is fitted to the sprite's
        #    own measured silhouette, on both screen axes, so a
        #    replacement moves that header and the exported model's own
        #    self-describing note without touching one authored box.
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
            # Emitted for every style, commissioned or not, because it carries
            # the *measured silhouettes* of that style's sprites: the numbers
            # a solid is fitted to. It moves whether or not the style has
            # a commission, which is the general form of the fact the
            # `mesh-silhouette` rule was written for.
            f"assets/{playstation_header.meshes_header_name(style)}":
                "the mesh header, which carries the sprite's measured "
                "silhouette",
            # The glTF export writes the same three measurements into the
            # model's `extras` so the file is self-describing, so replacing a
            # sprite moves the exported model of the archetype it stands in for
            # while every authored integer in it stays put.
            f"assets/gltf/{style.name}/"
            f"{STANDING.rsplit('/', 1)[-1].split('_')[0]}.gltf":
                "the exported model's note of its sprite's silhouette",
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

        _account_for_the_model_diff(plain, modelled)


def _account_for_the_model_diff(plain: Path, modelled: Path) -> None:
    """Every file a provided model moves, sorted into a named category.

    The same discipline the sheet half applies, and the answer is a much shorter
    list, because a mesh has none of a sprite's group properties: it is not
    quantised into a shared colour bank, it is not converted per profile, and no
    other figure is computed from it. What moves is the figure's own outputs,
    the header that carries it and the indexes that mention it.
    """
    style = styles.STYLES_BY_NAME[MODEL_STYLE]
    named = {
        f"assets/{playstation_header.meshes_header_name(style)}":
            "the mesh header a PlayStation build compiles",
        f"{gltf.relative_directory(MODEL_STYLE)}/{MODEL_ARCHETYPE}.gltf":
            "the re-exported model",
        f"{gltf.relative_directory(MODEL_STYLE)}/{MODEL_ARCHETYPE}.bin":
            "the re-exported model's buffer",
        # Required to move, and it is the check `verify.check_gltf_round_trip`
        # structurally cannot make: the round trip proves the integers survived
        # and cannot notice a figure that survives perfectly and *looks* wrong.
        # A drawing notices, and this is the drawing.
        f"gallery/mesh_{MODEL_STYLE}_{MODEL_ARCHETYPE}.png":
            "the roster panel, which draws the figure",
    }
    differing = sorted(
        path.relative_to(modelled).as_posix()
        for path in modelled.rglob("*") if path.is_file()
        and (plain / path.relative_to(modelled)).exists()
        and path.read_bytes()
        != (plain / path.relative_to(modelled)).read_bytes())

    def category(path: str) -> str:
        if path in named:
            return named[path]
        if path.endswith("manifest.json"):
            return "a manifest, which records the provenance"
        if path.endswith(("ROSTER.md", "GALLERY.md")) \
                or path.startswith("gallery/"):
            return "a page of the library that shows the figure"
        return ""

    counts: Dict[str, int] = {}
    unexplained = []
    for path in differing:
        reason = category(path)
        if reason:
            counts[reason] = counts.get(reason, 0) + 1
        else:
            unexplained.append(path)
    if unexplained:
        raise Failure("a provided model moved generated files that no rule "
                      "accounts for: " + ", ".join(unexplained))
    for name in named:
        if name not in differing:
            raise Failure(f"{name} did not move, so the provided model did not "
                          f"reach it")
    print(f"  the model example moves {len(differing)} generated files, every "
          f"one of them accounted for:")
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
                        help="also build three times and read the provided "
                             "pixels and the provided part integers "
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

        parts = check_model_example_accepted()
        print(f"The worked model example is accepted "
              f"({len(parts)} parts, read as integers).")

        check_a_submission_is_read_not_copied(parts)
        print("The same model, formatted differently, is the same integers.")

        check_the_machine_is_stricter_than_the_rule()

        check_substitution_is_local()
        print("Substitution moves exactly the assets the provided files name.")

        check_model_substitution_is_local()
        print("Standing a model in moves exactly the figure it names.")

        if arguments.end_to_end:
            check_end_to_end()
            print("The provided pixels reach every profile and the console "
                  "character header, and the provided part integers reach "
                  "the console mesh header and the re-exported model.")
    except Failure as error:
        print(f"\nProvided-art check failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
