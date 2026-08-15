# SPDX-License-Identifier: MIT
"""A picture of an exported mesh, for a page a human judges the art on.

:mod:`.gltf` writes geometry and geometry is not a picture. This module draws
one, so that ``ROSTER.md`` can put a unit's solid beside its sprite, and so
that the export gains the one check a round trip over its own integers cannot
give it. :func:`.verify.check_gltf_round_trip` proves the file reconstructs the
part table; it cannot notice a model that reconstructs perfectly and *looks*
wrong, because looking is not one of the things it does. A drawing notices.

Everything here reads the **exported file**
------------------------------------------
The corners, the per-face normals, the far-to-near part order and each face's
ramp and rung all come out of ``assets/gltf/<style>/<archetype>.gltf`` and its
buffer. Nothing about a mesh is read from :mod:`.meshes` to draw it. That is
what makes the picture evidence: a model that lost a part, smoothed a normal or
shuffled its order comes out as a wrong picture rather than as a passing check.

The buffer is walked here rather than by borrowing :mod:`.verify`'s reader, and
deliberately: two independent readers of the same file agreeing is worth more
than one reader used twice, and this one wants triangles where that one wants
integers.

What this module is not
-----------------------
It is a **preview**, and every image it produces is labelled one.
``platform/playstation/scratch/grandleon_playstation_scratch3d`` draws these
meshes for real and ``platform/playstation/scratch/evidence/`` holds its
photographs; those are the console's own output and these are not.

The second copy of the renderer, and how much of one this is
------------------------------------------------------------
:mod:`.meshes` states that the face winding order, the six per-normal light
factors and the ramp resolution are the *renderer's* properties, not the art's,
and that a mesh carrying them would be carrying a copy of the renderer. A
preview renderer is exactly the second copy that warns about, so the copy is
kept to what cannot be read from somewhere that already owns it:

* the winding is **not** copied: it comes out of the file, which derived it
  from the axes and asserted it against each face's own normal. What is
  restated is the back-face *test*, and :func:`faces_the_viewer` says exactly
  how its form differs from the console's and why the sign follows;
* the part order is **not** copied: it is the order the nodes appear in;
* the ramp resolution is **not** copied: the colours come from
  :func:`.playstation_header.mesh_ramp_words`, the same function the console's
  own header is built with;
* the **six light factors** and the **projection** are copied, in
  :data:`LIGHT_BY_NORMAL` and :func:`project`, each naming
  ``platform/playstation/scratch/scratch3d_exe.cpp`` as the file that owns it.

The generator does not read that file. Coupling the art gate to a scratch
program that is deliberately in no gate would trade one fragility for a worse
one. What pins the copy instead is measurement. The console's committed stills
are real framebuffer captures, so the projection and the colours can be checked
against them, and they were:

* redrawing ``evidence/mesh-beside-billboard.png``'s three knights with this
  projection puts the figure **25, 37 and 41 pixels tall** at the near, middle
  and far rows, which are the three numbers that still's own notes record;
* 88 per cent of the pixels it paints carry one of the model's own face colours
  in the capture, and every face colour it paints but one or two is a colour
  the capture holds. The one or two are the tops of the thighs, which the belt
  covers there and does not quite cover here. What the two do not share is the
  rasteriser's fill rule, which :func:`_fill` states and measures.

That check is also what found the one real defect in this module: it was
projecting glTF-space corners through a figure-space camera, drawing every
figure with its depth mirrored, and the picture looked plausible enough that
only the comparison caught it.

The camera, and the size a figure comes out
-------------------------------------------
The shipped one: sixty degrees off the board plane, focal length 300, the board
centre 744 units in front of the eye. A figure comes out between twenty-five and
forty-one pixels tall at that camera depending where on the board it stands, and
thirty-eight at the focus, which is where this frames it. That smallness is the
point: a mesh judged at another pitch or another lens is not evidence about the
one the game draws. So it is nearest-neighbour upscaled by
:data:`PREVIEW_ZOOM`, exactly as every sprite on these pages already is, rather
than re-rendered larger.

The arithmetic is integer throughout, from a fixed-point sine and cosine
computed once and asserted against the values the console's own table holds. A
rasteriser that rounded in floating point would be a generated artefact whose
bytes depended on a platform's libm.

What is deliberately absent
---------------------------
The per-unit silhouette match is the split scale that holds a drawn mesh to its
own sprite's opaque box. It is a property of how a *unit is placed on a board*,
not of the model, and it is recomputed per frame from the drawn tile. This
draws the model as authored. So the figure here is the mesh at its authored
proportions under the shipped camera, and the console additionally matches it
to its sprite at draw time.
"""

from __future__ import annotations

import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Tuple

from . import gltf, meshes

Rgb = Tuple[int, int, int]
Point = Tuple[int, int]

#: The fixed-point scale the console's trigonometry uses: 1.3.12.
FIXED_ONE = 4096

#: Sine and cosine of the shipped pitch in that fixed point. Computed rather
#: than transcribed, and then asserted against the two values
#: ``scratch3d_exe.cpp``'s own 91-entry table holds, so this file carries no
#: table of anybody else's numbers and still cannot disagree with them.
SINE = round(math.sin(math.radians(meshes.PITCH_DEGREES)) * FIXED_ONE)
COSINE = round(math.cos(math.radians(meshes.PITCH_DEGREES)) * FIXED_ONE)
assert (SINE, COSINE) == (3547, 2048), (
    "the shipped pitch's sine and cosine are the console's sine_table entries")

#: The shipped camera, from ``scratch3d_exe.cpp``: the board centre 744 world
#: units in front of the eye and a focal length of 300 pixels, which is the
#: framing at which a near tile is drawn at the art's own 32-pixel cell size.
CAMERA_DISTANCE = 744
CAMERA_FOCAL = 300

#: How much a preview is upscaled for the page. Nearest-neighbour, as every
#: sprite here is: :data:`.gallery.SPRITE_DISPLAY` shows a 32-pixel sprite at
#: 128, and this shows a thirty-pixel figure at four times its console size for
#: the same reason and to a matching height.
PREVIEW_ZOOM = 4

#: The native canvas a preview is rendered into, in console pixels, before that
#: upscale. Fixed rather than fitted so that every row of the page frames its
#: figure identically and two rows can be compared; :func:`render` refuses a
#: model that does not fit rather than silently cropping one.
#:
#: Sized for the mesh rules rather than for the two models that exist, because
#: further commissions are expected and a page that had to be re-tuned for each
#: one would not be generated in any useful sense. A figure is always
#: :data:`.meshes.MESH_WORLD_HEIGHT` tall and the silhouette rule holds its
#: width near its sprite's, so the drawn figure is about 38 by 23; the tile
#: under it is 26 by 23; and the slack above both is what a part carried well
#: forward or well back of the feet can spend.
#:
#: The figure is then centred in it on both axes rather than stood on a fixed
#: baseline. A baseline would be the obvious choice if figures varied in
#: height, and they do not: every mesh is built at exactly
#: :data:`.meshes.MESH_WORLD_HEIGHT`, so centring keeps a row's feet within a
#: pixel or two of every other row's *and* spends the canvas's slack only when
#: a model actually needs it, instead of leaving a dead band above every
#: figure that does not.
PREVIEW_WIDTH = 44
PREVIEW_HEIGHT = 56

#: The key light, which is the renderer's and not the art's: one fixed
#: directional light from the front, above and the left, as six factors in
#: eight-bit fixed point where 256 is the rung unchanged. Copied from
#: ``box_face_light`` in ``platform/playstation/scratch/scratch3d_exe.cpp``,
#: which is the file that owns them, and keyed by the outward normal in the
#: **figure's own space** (x right, y up, z *away*), which is the space that
#: table's rows are named in and the space :func:`load` puts the export back
#: into.
LIGHT_BY_NORMAL: Mapping[Tuple[int, int, int], int] = {
    (0, 0, -1): 178,  # front
    (0, 0, 1): 72,    # back
    (0, 1, 0): 255,   # top
    (0, -1, 0): 52,   # bottom
    (-1, 0, 0): 148,  # left
    (1, 0, 0): 102,   # right
}

#: The tile a figure stands on, drawn under it at the same camera so the model
#: can be judged against the ground it would stand on rather than in the air.
#: One board tile centred on the figure's feet, and drawn as an **outline**: a
#: filled tile at this pitch is a near-square slab that competes with the
#: figure for the eye, where an outline says the same thing and stays ground.
TILE_COLOUR: Rgb = (0x4A, 0x54, 0x63)

#: How far inside the tile the outline's inner edge sits, in world units. Three
#: of sixty-four, which is a little over a pixel at this camera.
TILE_INSET = 3


class PreviewError(RuntimeError):
    """An exported model this renderer cannot draw."""


@dataclass(frozen=True)
class Preview:
    """A rendered figure at console resolution, before any upscale.

    ``pixels`` holds one entry per pixel, row-major, with ``None`` where
    nothing was drawn. The caller composites it over whatever background its
    panel uses.
    """

    width: int
    height: int
    pixels: List[Optional[Rgb]]

    def at(self, x: int, y: int) -> Optional[Rgb]:
        return self.pixels[y * self.width + x]


# ---------------------------------------------------------------------------
# Reading the export
# ---------------------------------------------------------------------------


@dataclass(frozen=True)
class Face:
    """One quad of one part, in the **figure's own space**.

    The export maps the figure's space into glTF's by negating z and nothing
    else (:func:`.gltf._box`), and :func:`load` applies that mapping's inverse
    as it reads, in one place, for the same reason the exporter applies it in
    one place. Everything downstream is then in the space the console's own
    renderer names: the projection, the light table and the winding. Nothing
    has to remember which of the two spaces it is holding.
    """

    corners: Tuple[Tuple[int, int, int], ...]
    normal: Tuple[int, int, int]
    ramp: int
    rung: int


def _accessor_vectors(document: Dict[str, object], buffer: bytes,
                      index: int) -> List[Tuple[float, float, float]]:
    """One VEC3 accessor's payload, honouring its view's stride."""
    accessor = document["accessors"][index]  # type: ignore[index]
    if accessor.get("type") != "VEC3":
        raise PreviewError(f"accessor {index} is not VEC3")
    view = document["bufferViews"][int(accessor["bufferView"])]  # type: ignore[index]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    stride = int(view.get("byteStride", gltf.VECTOR_BYTES))
    return [struct.unpack_from("<3f", buffer, start + element * stride)
            for element in range(int(accessor["count"]))]


def _accessor_indices(document: Dict[str, object], buffer: bytes,
                      index: int) -> List[int]:
    accessor = document["accessors"][index]  # type: ignore[index]
    view = document["bufferViews"][int(accessor["bufferView"])]  # type: ignore[index]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    count = int(accessor["count"])
    return list(struct.unpack_from(f"<{count}H", buffer, start))


def _whole(value: float, where: str) -> int:
    if float(int(value)) != value:
        raise PreviewError(f"{where}: {value!r} is not an authored integer")
    return int(value)


def _figure_space(vector: Sequence[float], where: str) -> Tuple[int, int, int]:
    """A vector out of the file, back in the figure's own space.

    The inverse of the export's whole axis mapping, which is one negation: the
    figure's space is x right, y up, z **away** and left-handed, glTF's is y up
    right-handed with -z forward, so z changes sign and nothing else does. It
    is applied here and nowhere else, so no other function in this module has
    to know that two spaces were ever involved.
    """
    x, y, z = (_whole(value, where) for value in vector)
    return (x, y, -z)


def load(directory: Path, archetype: str) -> List[Face]:
    """Every face of one exported model, in the file's own part order.

    The faces come back flattened across parts because that is what a painter's
    renderer wants, and the flattening preserves node order, which is where the
    far-to-near authoring is carried.
    """
    stem = gltf.model_name(archetype)
    document = json.loads((directory / f"{stem}.gltf").read_text(encoding="utf-8"))
    buffer = (directory / f"{stem}.bin").read_bytes()

    materials = document["materials"]
    nodes = document["nodes"]
    root = nodes[0]
    faces: List[Face] = []
    for node_index in root["children"]:
        node = nodes[node_index]
        mesh = document["meshes"][int(node["mesh"])]
        primitive = mesh["primitives"][0]
        where = f"{archetype} part {node.get('name')}"
        positions = _accessor_vectors(document, buffer,
                                      int(primitive["attributes"]["POSITION"]))
        normals = _accessor_vectors(document, buffer,
                                    int(primitive["attributes"]["NORMAL"]))
        indices = _accessor_indices(document, buffer, int(primitive["indices"]))
        material = materials[int(primitive["material"])]
        # By the file's own convention, and by the route the format defines for
        # application data rather than by the material's baked colour: a mesh
        # names a ramp and a rung and never a colour.
        extras = material.get("extras") or {}
        ramp, rung = int(extras["ramp"]), int(extras["rung"])
        if (ramp, rung) != gltf.parse_material_name(str(material["name"])):
            raise PreviewError(
                f"{where}: material {material['name']!r} and its extras "
                f"disagree about the ramp and the rung")

        # Two triangles at a time, which is how a quad was written: the second
        # shares the first's opening and closing corners, so the four are the
        # first triangle's three and the second's middle one.
        if len(indices) % 6:
            raise PreviewError(f"{where}: indices do not divide into quads")
        for quad in range(len(indices) // 6):
            slice_ = indices[quad * 6:quad * 6 + 6]
            if slice_[0] != slice_[3] or slice_[2] != slice_[4]:
                raise PreviewError(
                    f"{where}: triangles {slice_} are not a quad's two")
            order = (slice_[0], slice_[1], slice_[2], slice_[5])
            corners = tuple(_figure_space(positions[corner], where)
                            for corner in order)
            normal = _figure_space(normals[order[0]], where)
            for corner in order[1:]:
                if _figure_space(normals[corner], where) != normal:
                    raise PreviewError(
                        f"{where}: a face's corners carry different normals, "
                        f"so a viewer would smooth what the renderer flattens")
            if normal not in LIGHT_BY_NORMAL:
                raise PreviewError(f"{where}: normal {normal} is not a box's")
            faces.append(Face(corners, normal, ramp, rung))
    if not faces:
        raise PreviewError(f"{archetype}: the exported model has no faces")
    return faces


# ---------------------------------------------------------------------------
# The camera
#
# The projection the console computes, in the arithmetic the console computes
# it in. `project_portably` in
# `platform/playstation/scratch/scratch3d_exe.cpp` is the file that owns it;
# this is that routine with the coprocessor's own divider left out, which the
# scratch measured as a one-pixel disagreement on two vertices out of nine
# hundred and which is below what a preview can show.
# ---------------------------------------------------------------------------


def corner_depth(y: int, z: int) -> int:
    """One vertex's depth at the shipped camera, truncated as the GTE truncates.

    Named and used on its own because the *ordering* rule is a question about
    this number and nothing else: ``sort_mesh_parts`` in ``scratch3d_exe.cpp``
    sorts a part by the sum of its eight corners' depths, and each of those
    eight has already lost its fraction to the ``>> 12`` below. Depth does not
    depend on x at all, which is why this takes two coordinates and
    :func:`project` takes three.
    """
    camera_z = ((CAMERA_DISTANCE * FIXED_ONE) + (-SINE) * y + COSINE * z) >> 12
    return min(max(camera_z, 0), 0xFFFF)


def project(x: int, y: int, z: int) -> Point:
    """One vertex of the figure, in **figure space**, as a screen pixel.

    Screen coordinates are relative to the view's centre; the caller decides
    where in a canvas that centre falls. x grows right and y grows *down*,
    which is the screen's own sense and the reason the visibility test below
    keeps a negative area.
    """
    camera_x = (FIXED_ONE * x) >> 12
    camera_y = ((-COSINE) * y + (-SINE) * z) >> 12
    depth = corner_depth(y, z)
    half = 0x10000
    if depth > 0:
        reciprocal = min((CAMERA_FOCAL << 17) // depth, 0x1FFFF)
        half = (reciprocal + 1) >> 1
    return ((half * camera_x) >> 16, (half * camera_y) >> 16)


def faces_the_viewer(quad: Sequence[Point]) -> bool:
    """Whether a face's projected winding shows its outside.

    The console's rule: a face wound outward whose projection comes out the
    other way is skipped. That is exact for a convex solid and is what buys
    the scene freedom from a depth buffer.

    The *sign* is the opposite of the console's, and for a reason worth writing
    down rather than discovering: this walks a **ring** of four corners, where
    ``box_face_corners`` in ``scratch3d_exe.cpp`` is a **Z** (top-left,
    top-right, bottom-left, bottom-right), and the console crosses two edges of
    that Z rather than taking a shoelace around it. Same rule, opposite sign,
    because the two corner orders differ by a swap.

    The negation :func:`load` applies does *not* enter this: it reverses the
    face's orientation in space and reverses which end of the depth axis is
    drawn higher on screen, and those two cancel exactly.
    """
    area = 0
    for index, (x0, y0) in enumerate(quad):
        x1, y1 = quad[(index + 1) % len(quad)]
        area += x0 * y1 - x1 * y0
    return area < 0


# ---------------------------------------------------------------------------
# Colour
# ---------------------------------------------------------------------------


def widen(channel: int) -> int:
    """A five-bit channel as eight bits, the way the hardware widens one.

    The top bits repeat into the bottom, so a full channel stays full. It is
    what ``scratch3d_evidence.py`` does to a captured framebuffer and what the
    scratch program does to a CLUT word, which is why a colour computed here
    can be looked for in a committed still.
    """
    return (channel << 3) | (channel >> 2)


def shade(colour: Rgb, light: int) -> Rgb:
    """A rung scaled by a face's light factor, per channel.

    Eight-bit fixed point, 256 being the rung unchanged. It is the console's
    ``shade``, integer for integer.
    """
    red, green, blue = colour
    return ((red * light) >> 8, (green * light) >> 8, (blue * light) >> 8)


def ramp_colours(ramp_words: Sequence[Sequence[int]],
                 channels_of) -> List[List[Rgb]]:
    """The resolved ramps as eight-bit colour, indexed ``[ramp][rung]``.

    ``ramp_words`` is what :func:`.playstation_header.mesh_ramp_words` returns
    and ``channels_of`` unpacks one word into its five-bit channels, both
    passed in for the reason :func:`.gltf.base_colour` passes them in: they are
    properties of the machine whose palette this is.
    """
    ramps: List[List[Rgb]] = []
    for rungs in ramp_words:
        row: List[Rgb] = []
        for word in rungs:
            red, green, blue = channels_of(word)
            row.append((widen(red), widen(green), widen(blue)))
        ramps.append(row)
    return ramps


# ---------------------------------------------------------------------------
# Rasterising
# ---------------------------------------------------------------------------


def _fill(preview: List[Optional[Rgb]], width: int, height: int,
          polygon: Sequence[Point], colour: Optional[Rgb]) -> None:
    """Scanline fill of a convex polygon, inclusive at both ends of a span.

    Deliberately conservative: two faces that share an edge both paint it, and
    the painter's order decides which wins.

    This is the one place where the preview is knowingly *not* the console, and
    it was measured rather than assumed. Redrawing the console's own
    ``mesh-beside-billboard.png`` with this renderer, the projected figure lands
    on the pixels the machine wrote: 88 per cent of the pixels painted carry
    one of the model's own face colours in the capture, and 65 to 68 per cent
    carry the *same* face's colour, the difference being a pixel of edge here
    and there. A half-open rule, which is what a hardware rasteriser uses,
    scores 70 to 87 per cent instead, and drops every feature a pixel wide,
    the knight's crest among them. On a page whose whole job is judging the
    art, losing a feature is the worse error of the two, so the fill is
    conservative and the difference is written down.
    """
    top = max(0, min(point[1] for point in polygon))
    bottom = min(height - 1, max(point[1] for point in polygon))
    for y in range(top, bottom + 1):
        crossings: List[int] = []
        for index, (x0, y0) in enumerate(polygon):
            x1, y1 = polygon[(index + 1) % len(polygon)]
            if y0 == y1:
                if y0 == y:
                    crossings.extend((x0, x1))
                continue
            low, high = (y0, y1) if y0 < y1 else (y1, y0)
            if not low <= y <= high:
                continue
            crossings.append(x0 + (x1 - x0) * (y - y0) // (y1 - y0))
        if not crossings:
            continue
        left = max(0, min(crossings))
        right = min(width - 1, max(crossings))
        for x in range(left, right + 1):
            preview[y * width + x] = colour


def render(faces: Sequence[Face], ramps: Sequence[Sequence[Rgb]]) -> Preview:
    """One model, drawn at the shipped camera into the fixed preview canvas.

    The faces arrive in the file's node order, which is the authored far-to-near
    order, and are drawn in it: that ordering is what the mesh rules buy, so a
    renderer that sorted them again would be testing its own sort instead of
    the file's order.
    """
    width, height = PREVIEW_WIDTH, PREVIEW_HEIGHT
    pixels: List[Optional[Rgb]] = [None] * (width * height)

    # The tile, first, so the figure stands on it. One board tile centred on
    # the feet, at the ground plane, and the same square again inset, which is
    # what turns the fill into an outline.
    half = meshes.UNIT_WORLD // 2
    inner = half - TILE_INSET
    tile = [project(-half, 0, -half), project(-half, 0, half),
            project(half, 0, half), project(half, 0, -half)]
    tile_inner = [project(-inner, 0, -inner), project(-inner, 0, inner),
                  project(inner, 0, inner), project(inner, 0, -inner)]

    drawn: List[Tuple[List[Point], Rgb]] = []
    for face in faces:
        quad = [project(*corner) for corner in face.corners]
        if not faces_the_viewer(quad):
            continue
        colour = shade(ramps[face.ramp][face.rung], LIGHT_BY_NORMAL[face.normal])
        drawn.append((quad, colour))
    if not drawn:
        raise PreviewError("no face of this model faces the viewer")

    # Where the drawing lands in the canvas: centred on both axes, figure and
    # tile together. The offset is applied after the projection because that is
    # exactly what the console's own screen offset does, adding it after the
    # divide, so framing here changes no projected pixel.
    points = [point for quad, _ in drawn for point in quad] + tile
    left = min(point[0] for point in points)
    right = max(point[0] for point in points)
    top = min(point[1] for point in points)
    bottom = max(point[1] for point in points)
    if right - left >= width or bottom - top >= height:
        raise PreviewError(
            f"a figure {right - left + 1}x{bottom - top + 1} pixels does not "
            f"fit the {width}x{height} preview canvas")
    offset_x = (width - (right - left + 1)) // 2 - left
    offset_y = (height - (bottom - top + 1)) // 2 - top

    def place(quad: Sequence[Point]) -> List[Point]:
        return [(x + offset_x, y + offset_y) for x, y in quad]

    _fill(pixels, width, height, place(tile), TILE_COLOUR)
    _fill(pixels, width, height, place(tile_inner), None)
    for quad, colour in drawn:
        _fill(pixels, width, height, place(quad), colour)
    return Preview(width, height, pixels)


def available(directory: Path, archetype: str) -> bool:
    """Whether this style's export holds a model for this archetype.

    The page asks the *files* rather than the mesh registry, so a commission
    that lands later fills its row with no edit here.
    """
    stem = gltf.model_name(archetype)
    return ((directory / f"{stem}.gltf").is_file()
            and (directory / f"{stem}.bin").is_file())
