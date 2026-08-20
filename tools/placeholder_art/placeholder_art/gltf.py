# SPDX-License-Identifier: MIT
"""Character meshes as glTF 2.0, so the roster opens in a modelling tool.

This is an **export and nothing else**. :mod:`.meshes` is the source of truth
for what a figure is and stays the source of truth; the consoles keep reading
the generated integer headers exactly as they do today; nothing at runtime
opens a file this module writes. What is added is one more *generated artefact*
under the drift check that already governs the headers, written for a human
with Blender open and for whatever converter is one day written against it.

Why glTF, and why now
---------------------
glTF is the obvious candidate for an import format, and none of the importing
half is built. The half that can be built with no importer, no new source of
truth and no asset-import security groundwork is this one, and it is the half
that answers the question the other half rests on. Until a file has been
written and *read back*, which of the mesh rules glTF expresses natively, which
survive only by a convention this module
invents, and which it cannot say at all are three guesses. The reader in
:func:`.verify.check_gltf_round_trip` turns them into a measurement.

The axis mapping, stated once and applied in one place
------------------------------------------------------
The figure's space is the one :mod:`.meshes` authors in: **x right, y up, z
away** from the viewer, origin at the centre of the feet. Because ``x cross y``
points *toward* the viewer in a right-handed space and this space's z points
away, the figure's space is **left-handed**.

glTF 2.0 is **y up, right-handed, -z forward**, so its +z points toward the
viewer, and its x and y are already ours. One negation is therefore the whole
mapping::

    gltf_x =  x        gltf_y =  y        gltf_z = -z

and it is applied in exactly one function, :func:`_box`. Negating a single axis
reverses handedness, which is what makes the mapping correct; it also reverses
the sense of a triangle's winding, which is why this module derives its winding
from the glTF-space axes after the negation rather than carrying any winding
across. (The mesh source carries none to carry: the face winding is a property
of the *renderer* and :mod:`.meshes` says so.)

The scale, and why one over sixty-four
--------------------------------------
The buffer holds the figure's **own authored integers**, untouched apart from
the z negation, and the single root node carries a uniform scale of
``1 / UNIT_WORLD``. Three reasons, in order of weight:

* it is exact. 64 is a power of two, so every authored integer times 1/64 is
  representable in binary floating point with no rounding anywhere, and the
  file can be read back to the authored integers by multiplication rather than
  by rounding. Nothing else in this module has that property to spare.
* it puts **one board tile on one Blender unit**, which is the only mapping a
  reader of this repository would guess. Blender's grid unit is a metre, so a
  tile is a metre and a figure opens *two* units tall, which is not an
  accident to explain away but rule 1 made visible: a figure is built at
  ``unit_world / cos φ`` and drawn one tile tall.
* it leaves the authored integers legible in the file. A model whose buffer
  reads ``-13, 0, -7`` is one an author can check against the part table by
  eye; one that reads ``-0.203125`` is not.

The cost, stated rather than glossed: a Blender user who selects a *child* part
sees its untransformed dimensions in world units rather than in the metres the
viewport is drawing, and a user who clears the root's transform gets a figure
128 units tall. The figure opens at the right size and the file reads as the
part table; only a part inspected out of its parent reads oddly, and that is
the cheaper of the two prices.

Flat shading, by construction rather than by request
----------------------------------------------------
The renderer flat-shades, and that choice was measured against a stretched
texture. glTF has no "shade this flat" flag in core. Smoothness is a property
of whether faces *share* vertices and of what normals those vertices carry, so
the export makes smoothing impossible instead of asking for it to be off: each
of a box's six faces carries its **own four corners** and every one of those
corners carries that face's **own normal**. Twenty-four vertices a box where
eight would do, and the twenty-four are what guarantee a viewer draws the solid
the console draws. It also costs nothing that matters: a whole figure is under
fourteen kilobytes.

Colour, which is a courtesy and not the content
------------------------------------------------
A mesh carries no colour: a face names a ramp and a rung and never a colour.
This module does not give it one. A material's **name** and its **extras**
both carry the ramp index, the ramp's name and the rung, which is what a
converter reads; ``baseColorFactor`` carries
the first faction's resolved rung colour, which is what a *viewer* reads, so
the figure opens as a knight rather than as twenty grey boxes. The two cannot
drift apart because both come from the same pair of indices, and the round-trip
reader checks the name and the extras against each other rather than trusting
either.

Determinism
-----------
The rule every generated artefact here obeys. The JSON is dumped with sorted
keys and a fixed indent, the buffer is packed little-endian explicitly, every
float in the document is either an exact binary fraction or a value from
:data:`SRGB_LINEAR`, and nothing consults the clock, the filesystem or a
dictionary's insertion order. The sRGB transfer's ``pow`` is the one place a
platform library could have entered, and in its place is a checked-in table of
the thirty-two values a five-bit channel can take.
"""

from __future__ import annotations

import json
import struct
from typing import Dict, List, Mapping, Sequence, Tuple

from . import meshes

#: glTF's name for what this module writes. Emitted into every document so a
#: reader is looking at a version rather than guessing one.
VERSION = "2.0"

#: What ``asset.generator`` says. Deliberately carries no version number and no
#: path: either would put something in a checked-in file that depends on where
#: and when it was generated.
GENERATOR = "Grandleon placeholder art generator"

#: glTF component types, by their OpenGL enumerant, which is how the format
#: spells them.
COMPONENT_FLOAT = 5126
COMPONENT_UNSIGNED_SHORT = 5123

#: glTF buffer-view targets, same spelling.
TARGET_ARRAY_BUFFER = 34962
TARGET_ELEMENT_ARRAY_BUFFER = 34963

#: glTF primitive mode. Triangles, which is the only thing here.
MODE_TRIANGLES = 4

#: Corners a box contributes to the buffer. Four per face and six faces,
#: because no face shares a vertex with any other. See the module docstring.
VERTICES_PER_BOX = meshes.FACES_PER_PART * 4

#: Indices a box contributes: two triangles a face.
INDICES_PER_BOX = meshes.TRIANGLES_PER_PART * 3

#: Bytes a vector of three floats occupies in the buffer.
VECTOR_BYTES = 3 * 4

#: The uniform scale the root node carries: one board tile to one Blender unit.
#: Exact in binary, which is the point. See the module docstring.
ROOT_SCALE = 1.0 / meshes.UNIT_WORLD

#: The faction colour whose resolved ramp is baked into ``baseColorFactor``.
#: The first one, which is the colour every other measurement in this pipeline
#: is taken on: :func:`.playstation_header.silhouettes` measures it and the
#: scratch program measures it. A baked colour is a courtesy and picking the
#: colour everything else already picked keeps it from being a second opinion.
BAKED_FACTION = 0

#: The name each ramp index is spelled with in a material's name and extras.
#: Fixed here rather than derived, because it is part of the file format this
#: module defines and a renamed ramp would silently change every exported file.
RAMP_NAMES: Mapping[int, str] = {
    meshes.RAMP_NEUTRAL: "neutral",
    meshes.RAMP_FACTION: "faction",
}

#: The sRGB electro-optical transfer function at the thirty-two levels a
#: five-bit channel can hold, rounded to six decimals.
#:
#: glTF defines ``baseColorFactor`` in **linear** space while the console's
#: framebuffer word is a display value, so a viewer shows the console's colour
#: only if the transfer is applied here. The values are tabulated rather than
#: computed because computing them means ``pow`` from whichever libm the host
#: happens to have, and a checked-in generated artefact does not get to depend
#: on that. They are, for a five-bit level ``v`` widened to eight bits the way
#: the hardware widens it, ``e = (v << 3) | (v >> 2)`` and ``s = e / 255``::
#:
#:     s / 12.92                     if s <= 0.04045
#:     ((s + 0.055) / 1.055) ** 2.4  otherwise
SRGB_LINEAR: Tuple[float, ...] = (
    0.0, 0.002428, 0.005182, 0.009134,
    0.015209, 0.022174, 0.030713, 0.040915,
    0.05448, 0.068478, 0.084376, 0.102242,
    0.124772, 0.147027, 0.171441, 0.198069,
    0.23074, 0.262251, 0.296138, 0.332452,
    0.376262, 0.417885, 0.462077, 0.508881,
    0.564712, 0.617207, 0.672443, 0.730461,
    0.799103, 0.863157, 0.930111, 1.0,
)
assert len(SRGB_LINEAR) == 32, "a five-bit channel has thirty-two levels"


def model_name(archetype: str) -> str:
    """The file one archetype's exported model is written to, without a suffix.

    Named once here so the document, the buffer it references and the reader
    that opens both cannot disagree about it.
    """
    return archetype


def key_directory(style_name: str) -> str:
    """Where one style's models live, relative to ``assets/``.

    Which is also the key a *provided* model is addressed by, because the key
    is the path, the one sentence that covers both halves of the library. Said
    here once so the export and the import cannot disagree about it.
    """
    return f"gltf/{style_name}"


def relative_directory(style_name: str) -> str:
    """Where one style's exported models live, relative to the output root."""
    return f"assets/{key_directory(style_name)}"


def _slug(name: str) -> str:
    """A part's authored name as an identifier a tool will not rewrite.

    Blender is content with spaces and commas in an object name, but a name
    that survives every tool unchanged survives a round trip unchanged, and the
    authored name is only carried for a human to read anyway. The *index* is
    what carries the order.
    """
    out = []
    previous_underscore = False
    for character in name.lower():
        if character.isalnum():
            out.append(character)
            previous_underscore = False
        elif not previous_underscore:
            out.append("_")
            previous_underscore = True
    return "".join(out).strip("_")


def part_label(index: int, part: meshes.Part) -> str:
    """One part's node and mesh name: its authored position, then its name.

    The zero-padded index is the load-bearing half. Parts are authored
    far-to-near and that ordering is what buys the scene freedom from a depth
    buffer, so a file that lost it is a file no importer could round-trip. It
    is carried **twice**, here and in the order the nodes themselves appear,
    because a tool that reorders nodes usually keeps names and a tool that
    renames usually keeps order, and neither loses both.
    """
    return f"{index:02d}_{_slug(part.name)}"


def material_name(ramp: int, rung: int) -> str:
    """One material's name: the ramp it draws from and the rung on it.

    This is the convention "a face names a ramp and a rung, never a colour" has
    to survive a file by. glTF has
    nowhere in a material for "this is index 1 of 2 on a ramp", so the mapping
    lives in the name, which every tool preserves and shows, and again in the
    material's ``extras``, which is where the format says application data
    goes. Two routes, and the reader checks them against each other.
    """
    return f"{RAMP_NAMES[ramp]}_ramp_rung_{rung}"


def parse_material_name(name: str) -> Tuple[int, int]:
    """The ramp and rung a material name spells, or raise ``ValueError``.

    The inverse of :func:`material_name`, written here beside it so the
    convention has one definition rather than two.
    """
    suffix = "_ramp_rung_"
    head, separator, tail = name.partition(suffix)
    if not separator:
        raise ValueError(f"material {name!r} does not name a ramp and a rung")
    for ramp, ramp_name in RAMP_NAMES.items():
        if ramp_name == head:
            break
    else:
        raise ValueError(f"material {name!r} names no ramp this library has")
    if not tail.isdigit():
        raise ValueError(f"material {name!r} names no rung")
    return ramp, int(tail)


def _box(part: meshes.Part) -> Tuple[Tuple[int, int, int], Tuple[int, int, int]]:
    """One part's low and high corners **in glTF space**.

    The whole of the axis mapping is here: x and y pass through, z is negated,
    and negating z swaps which end of the range is the low one.
    """
    return ((part.x0, part.y0, -part.z1), (part.x1, part.y1, -part.z0))


def faces_of(part: meshes.Part) -> List[Tuple[Tuple[int, int, int],
                                              Tuple[Tuple[int, int, int], ...]]]:
    """A part's six faces in glTF space: an outward normal and four corners.

    The corners are wound **counter-clockwise seen from outside**, which is the
    front face glTF defines, so the export can cull back faces exactly as the
    renderer skips wrongly-wound ones.

    The winding is derived rather than typed. For an axis ``a``, the other two
    axes taken in cyclic order satisfy ``e_u cross e_v = e_a``, so walking u
    before v traces a face whose normal is +a; the opposite face is the same
    walk reversed. Six hand-written quads would be six chances to type one
    backwards, so this writes none of them, and asserts the result against the
    normal it claims rather than trusting the derivation.
    """
    low, high = _box(part)
    faces = []
    for axis in range(3):
        u = (axis + 1) % 3
        v = (axis + 2) % 3
        for sign in (1, -1):
            plane = high[axis] if sign > 0 else low[axis]
            square = ((low[u], low[v]), (high[u], low[v]),
                      (high[u], high[v]), (low[u], high[v]))
            if sign < 0:
                square = tuple(reversed(square))
            quad = []
            for along_u, along_v in square:
                corner = [0, 0, 0]
                corner[axis] = plane
                corner[u] = along_u
                corner[v] = along_v
                quad.append((corner[0], corner[1], corner[2]))
            normal = [0, 0, 0]
            normal[axis] = sign
            outward = (normal[0], normal[1], normal[2])
            _assert_wound_outward(part, outward, quad)
            faces.append((outward, tuple(quad)))
    return faces


def _assert_wound_outward(part: meshes.Part, normal: Tuple[int, int, int],
                          quad: Sequence[Tuple[int, int, int]]) -> None:
    """Refuse a face whose winding disagrees with the normal it is given.

    Integer arithmetic on integer corners, so this is exact.
    """
    first = tuple(quad[1][i] - quad[0][i] for i in range(3))
    second = tuple(quad[2][i] - quad[0][i] for i in range(3))
    cross = (first[1] * second[2] - first[2] * second[1],
             first[2] * second[0] - first[0] * second[2],
             first[0] * second[1] - first[1] * second[0])
    projected = sum(cross[i] * normal[i] for i in range(3))
    assert projected > 0, (
        f"part '{part.name}': a face wound {quad} does not face "
        f"{normal}, so a viewer would cull the outside of the solid")


def base_colour(ramp_words: Sequence[Sequence[int]], ramp: int, rung: int,
                channels_of) -> List[float]:
    """One material's baked ``baseColorFactor``: linear RGB, then alpha.

    ``ramp_words`` is the archetype's resolved ramps as CLUT halfwords and
    ``channels_of`` unpacks one into its three five-bit channels. Both are
    passed in rather than imported, because they are properties of the machine
    whose palette this is and this module is about a file format.
    """
    red, green, blue = channels_of(ramp_words[ramp][rung])
    return [SRGB_LINEAR[red], SRGB_LINEAR[green], SRGB_LINEAR[blue], 1.0]


def _accessor(buffer_view: int, byte_offset: int, component_type: int,
              count: int, kind: str) -> Dict[str, object]:
    return {
        "bufferView": buffer_view,
        "byteOffset": byte_offset,
        "componentType": component_type,
        "count": count,
        "type": kind,
    }


def document(style_name: str, archetype: str, parts: Sequence[meshes.Part],
             silhouette: meshes.Silhouette,
             ramp_words: Sequence[Sequence[int]],
             faction_name: str,
             channels_of) -> Tuple[str, bytes]:
    """One archetype's model: the JSON document, and the buffer it references.

    Reads the part table and adjusts nothing in it. Every integer in the buffer
    is an authored coordinate with at most its sign changed, and the reader in
    :mod:`.verify` requires exactly that.
    """
    assert parts, "an archetype with no parts has no model to export"

    positions: List[float] = []
    normals: List[float] = []
    indices: List[int] = []
    bounds: List[Tuple[List[int], List[int]]] = []
    used: List[Tuple[int, int]] = []
    for part in parts:
        pair = (part.ramp, part.rung)
        if pair not in used:
            used.append(pair)
        low, high = _box(part)
        bounds.append(([low[0], low[1], low[2]], [high[0], high[1], high[2]]))
        base = 0
        for outward, quad in faces_of(part):
            for corner in quad:
                positions.extend(float(value) for value in corner)
                normals.extend(float(value) for value in outward)
            indices.extend((base, base + 1, base + 2, base, base + 2, base + 3))
            base += 4
        assert base == VERTICES_PER_BOX, "a box is six faces of four corners"
    used.sort()

    position_bytes = struct.pack(f"<{len(positions)}f", *positions)
    normal_bytes = struct.pack(f"<{len(normals)}f", *normals)
    index_bytes = struct.pack(f"<{len(indices)}H", *indices)
    buffer = position_bytes + normal_bytes + index_bytes

    materials = [
        {
            "name": material_name(ramp, rung),
            "pbrMetallicRoughness": {
                "baseColorFactor": base_colour(ramp_words, ramp, rung,
                                               channels_of),
                # Nothing here is a metal and nothing here is polished. A face
                # is a flat shade of a palette rung on the console, and the
                # nearest thing glTF's material model has to that is a fully
                # rough dielectric.
                "metallicFactor": 0.0,
                "roughnessFactor": 1.0,
            },
            # Every face is wound outward, so a viewer may cull the back of a
            # convex solid exactly as the renderer does.
            "doubleSided": False,
            "extras": {
                "ramp": ramp,
                "rampName": RAMP_NAMES[ramp],
                "rung": rung,
            },
        }
        for ramp, rung in used
    ]
    material_index = {pair: index for index, pair in enumerate(used)}

    accessors: List[Dict[str, object]] = []
    gltf_meshes: List[Dict[str, object]] = []
    nodes: List[Dict[str, object]] = [{}]  # the root, filled in below
    for index, part in enumerate(parts):
        label = part_label(index, part)
        position = len(accessors)
        low, high = bounds[index]
        accessor = _accessor(0, index * VERTICES_PER_BOX * VECTOR_BYTES,
                             COMPONENT_FLOAT, VERTICES_PER_BOX, "VEC3")
        # POSITION is the one accessor glTF requires bounds on, and they are
        # the box's own corners: a part is its bounding box.
        accessor["min"] = [float(value) for value in low]
        accessor["max"] = [float(value) for value in high]
        accessors.append(accessor)
        accessors.append(
            _accessor(1, index * VERTICES_PER_BOX * VECTOR_BYTES,
                      COMPONENT_FLOAT, VERTICES_PER_BOX, "VEC3"))
        accessors.append(
            _accessor(2, index * INDICES_PER_BOX * 2,
                      COMPONENT_UNSIGNED_SHORT, INDICES_PER_BOX, "SCALAR"))
        gltf_meshes.append({
            "name": label,
            "primitives": [{
                "attributes": {"POSITION": position, "NORMAL": position + 1},
                "indices": position + 2,
                "material": material_index[(part.ramp, part.rung)],
                "mode": MODE_TRIANGLES,
            }],
        })
        nodes.append({
            "name": label,
            "mesh": index,
            "extras": {
                # Everything a converter needs to put this part back into a
                # part table, in the format's own place for application data.
                "part": index,
                "partName": part.name,
                "ramp": part.ramp,
                "rampName": RAMP_NAMES[part.ramp],
                "rung": part.rung,
            },
        })

    nodes[0] = {
        "name": archetype,
        "children": list(range(1, len(parts) + 1)),
        "scale": [ROOT_SCALE, ROOT_SCALE, ROOT_SCALE],
        "extras": {
            "archetype": archetype,
            "style": style_name,
            # The axis mapping, in the file rather than only in this module, so
            # a reader who never opens the generator still knows what was done.
            "axes": "figure space is x right, y up, z away and is "
                    "left-handed; glTF is y up, right-handed, -z forward, so "
                    "z is negated and nothing else is",
            "unitWorld": meshes.UNIT_WORLD,
            "worldHeight": meshes.MESH_WORLD_HEIGHT,
            "pitchDegrees": meshes.PITCH_DEGREES,
            "partCount": len(parts),
            "triangleCount": len(parts) * meshes.TRIANGLES_PER_PART,
            "rampNeutral": meshes.RAMP_NEUTRAL,
            "rampFaction": meshes.RAMP_FACTION,
            "rungCount": meshes.RUNG_COUNT,
            "bakedFaction": faction_name,
            "silhouetteWidth": silhouette.width,
            "silhouetteHeight": silhouette.height,
            "silhouetteArea": silhouette.area,
            "partOrder": "authored far-to-near at the shipped pitch; node "
                         "order and the index prefix on every node name both "
                         "carry it",
        },
    }

    payload = {
        "accessors": accessors,
        "asset": {"generator": GENERATOR, "version": VERSION},
        "bufferViews": [
            # Every part's positions share one view and every part's normals
            # share another, so both views carry `byteStride`: glTF requires it
            # the moment two accessors read the same view, and the Khronos
            # validator is where that requirement was found rather than
            # remembered. It is the tight packing either way: three floats a
            # corner and nothing between them.
            {
                "buffer": 0,
                "byteOffset": 0,
                "byteLength": len(position_bytes),
                "byteStride": VECTOR_BYTES,
                "target": TARGET_ARRAY_BUFFER,
            },
            {
                "buffer": 0,
                "byteOffset": len(position_bytes),
                "byteLength": len(normal_bytes),
                "byteStride": VECTOR_BYTES,
                "target": TARGET_ARRAY_BUFFER,
            },
            # The index view carries none: `byteStride` is a vertex-attribute
            # notion and the format forbids it on an element array.
            {
                "buffer": 0,
                "byteOffset": len(position_bytes) + len(normal_bytes),
                "byteLength": len(index_bytes),
                "target": TARGET_ELEMENT_ARRAY_BUFFER,
            },
        ],
        "buffers": [{
            "byteLength": len(buffer),
            "uri": f"{model_name(archetype)}.bin",
        }],
        "materials": materials,
        "meshes": gltf_meshes,
        "nodes": nodes,
        "scene": 0,
        "scenes": [{"name": archetype, "nodes": [0]}],
    }
    text = json.dumps(payload, indent=2, sort_keys=True) + "\n"
    return text, buffer


# ---------------------------------------------------------------------------
# Reading the format back, from a file this repository did not write
#
# Everything above is the export. This is the other direction, and accepting a
# provided mesh at all rests on it. The mesh rules want an *agreed interchange
# format* before they want a validator, and there is no format question left to
# settle: the format is the one this module writes, and
# `verify.check_gltf_round_trip` measures that every authored integer survives
# it. So the reading direction is a supported input rather than a refusal.
#
# This is the third reader of these files in the repository and that is
# deliberate rather than an oversight. `verify` reads them to prove the *export*
# lost nothing and asserts; `preview3d` reads them to draw a picture and wants
# triangles; this one reads a file **somebody else wrote** and so may assume
# nothing at all. Every index is bounds-checked, every number is checked to be
# the kind of number it claims to be, and every failure is a refusal carrying a
# stable code rather than an assertion carrying a stack trace. A reader written
# for a trusted file and pointed at an untrusted one is the mistake this avoids.
#
# What comes back is a part list of integers, and nothing else: no chunk of the
# submitted document, no float, no string of the caller's choosing reaches an
# artefact. Everything downstream is re-emitted by `document()` above, which is
# the same property the sprite path gets by decoding to palette indices and
# re-encoding with this repository's own writer.
# ---------------------------------------------------------------------------

#: How far from the origin a coordinate may sit, on any axis.
#: :data:`~.meshes.MESH_WORLD_HEIGHT` is the ceiling a figure is built under,
#: so nothing in one may reach further from the origin than that. That is measured against the
#: shipped library, whose widest coordinate is 38 in x and 34 in z against
#: exactly this number in y. It is also what keeps every emitted value inside
#: the ``short`` the generated console header declares.
MAX_COORDINATE = meshes.MESH_WORLD_HEIGHT

#: The longest a part's authored name may be. Twice the longest the shipped
#: library holds ("shirt, where the coat falls open", at 32). A name is carried
#: for a human to read and is never emitted, so the cap is about what a
#: diagnostic may be asked to print rather than about the art.
MAX_PART_NAME = 64

#: The characters a part name may hold: printable ASCII and nothing else. A name
#: out of a submitted file is quoted back in refusals, so it may not carry a
#: control character, an escape sequence or a newline.
_PRINTABLE = frozenset(chr(code) for code in range(0x20, 0x7F))

#: Top-level document keys that name a glTF feature this format does not carry.
#: Named one by one rather than by an allow-list of the keys the export writes,
#: so a refusal can say *what* was found rather than that something was.
UNSUPPORTED_SECTIONS: Tuple[Tuple[str, str], ...] = (
    ("extensionsRequired", "an extension this reader would have to implement"),
    ("extensionsUsed", "an extension"),
    ("extensions", "an extension"),
    ("animations", "animation, where a mesh here is a static part list"),
    ("skins", "skinning, where a part is a rigid box"),
    ("cameras", "a camera, where the console owns the camera"),
    ("images", "an image, where a mesh carries no colour at all"),
    ("textures", "a texture, where a mesh carries no colour at all"),
    ("samplers", "a sampler, where a mesh carries no colour at all"),
)


class ModelRefused(ValueError):
    """A submitted model this reader will not turn into a part list.

    Carries the refusal's stable code beside its sentence, for the reason
    :class:`.provided.Rejected` does: a code a caller has to pattern-match out
    of an English sentence is a code a rewording will one day misfile.
    """

    def __init__(self, code: str, reason: str) -> None:
        super().__init__(reason)
        self.code = code
        self.reason = reason


def _refuse(code: str, reason: str) -> "ModelRefused":
    return ModelRefused(code, reason)


def _no_constants(name: str) -> float:
    raise _refuse(
        "malformed-gltf",
        f"it holds the JSON literal {name}, which is not a number. Every "
        f"coordinate in a model is an authored integer")


def _table(document: Mapping[str, object], name: str) -> List[object]:
    """One of the document's arrays, or a refusal saying it is not one."""
    value = document.get(name)
    if value is None:
        raise _refuse("malformed-gltf",
                      f"it has no '{name}' array, and a model is not "
                      f"reconstructable without one")
    if not isinstance(value, list):
        raise _refuse("malformed-gltf", f"its '{name}' is not an array")
    return value


def _entry(table: Sequence[object], index: object, name: str,
           where: str) -> Dict[str, object]:
    """One entry of one array, by an index out of the file.

    Every index in a glTF document is an offset into another array, and every
    one of them is checked here rather than handed to the interpreter: an
    out-of-range index in a trusted file is a bug and in a submitted one is the
    whole attack.
    """
    if not isinstance(index, int) or isinstance(index, bool):
        raise _refuse("malformed-gltf",
                      f"{where}: {index!r} is not an index into '{name}'")
    if not 0 <= index < len(table):
        raise _refuse(
            "malformed-gltf",
            f"{where}: it names {name}[{index}] and the document holds "
            f"{len(table)}")
    value = table[index]
    if not isinstance(value, dict):
        raise _refuse("malformed-gltf", f"{where}: {name}[{index}] is not an "
                                        f"object")
    return value


def _integers(values: object, where: str) -> List[int]:
    if not isinstance(values, list):
        raise _refuse("malformed-gltf", f"{where} is not an array")
    out: List[int] = []
    for value in values:
        if not isinstance(value, int) or isinstance(value, bool):
            raise _refuse("malformed-gltf",
                          f"{where} holds {value!r}, which is not an index")
        out.append(value)
    return out


def _vectors(buffer: bytes, views: Sequence[object],
             accessor: Mapping[str, object], expected: int,
             where: str) -> List[Tuple[float, float, float]]:
    """One VEC3 accessor's payload, with every bound checked against the file.

    ``byteStride`` is honoured because glTF requires it the moment two accessors
    share a view, and every view this format writes is shared.
    """
    if accessor.get("componentType") != COMPONENT_FLOAT:
        raise _refuse("malformed-gltf",
                      f"{where}: its component type is not FLOAT")
    if accessor.get("type") != "VEC3":
        raise _refuse("malformed-gltf", f"{where}: its type is not VEC3")
    if "sparse" in accessor:
        raise _refuse("unsupported-gltf",
                      f"{where}: a sparse accessor, where this format writes "
                      f"every corner of every box")
    count = accessor.get("count")
    if count != expected:
        raise _refuse(
            "mesh-not-a-box",
            f"{where}: {count} elements where a box with no shared vertex has "
            f"{expected}")
    view = _entry(views, accessor.get("bufferView"), "bufferViews", where)
    stride = view.get("byteStride", VECTOR_BYTES)
    if not isinstance(stride, int) or stride < VECTOR_BYTES:
        raise _refuse("malformed-gltf",
                      f"{where}: a stride of {stride!r} cannot hold three "
                      f"floats")
    start = _offset(view, accessor, where)
    end = start + (expected - 1) * stride + VECTOR_BYTES
    if end > len(buffer):
        raise _refuse("malformed-gltf",
                      f"{where}: it reads to byte {end} of a {len(buffer)}-byte "
                      f"buffer")
    return [struct.unpack_from("<3f", buffer, start + element * stride)
            for element in range(expected)]


def _offset(view: Mapping[str, object], accessor: Mapping[str, object],
            where: str) -> int:
    for holder, name in ((view, "bufferView"), (accessor, "accessor")):
        value = holder.get("byteOffset", 0)
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise _refuse("malformed-gltf",
                          f"{where}: the {name}'s byteOffset is {value!r}")
    return int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))


def _indices(buffer: bytes, views: Sequence[object],
             accessor: Mapping[str, object], where: str) -> List[int]:
    if accessor.get("componentType") != COMPONENT_UNSIGNED_SHORT:
        raise _refuse("malformed-gltf",
                      f"{where}: its component type is not UNSIGNED_SHORT")
    if accessor.get("type") != "SCALAR":
        raise _refuse("malformed-gltf", f"{where}: its type is not SCALAR")
    if "sparse" in accessor:
        raise _refuse("unsupported-gltf", f"{where}: a sparse accessor")
    if accessor.get("count") != INDICES_PER_BOX:
        raise _refuse(
            "mesh-not-a-box",
            f"{where}: {accessor.get('count')} indices where a box is "
            f"{INDICES_PER_BOX}")
    view = _entry(views, accessor.get("bufferView"), "bufferViews", where)
    start = _offset(view, accessor, where)
    if start + INDICES_PER_BOX * 2 > len(buffer):
        raise _refuse("malformed-gltf",
                      f"{where}: it reads past the end of a {len(buffer)}-byte "
                      f"buffer")
    return list(struct.unpack_from(f"<{INDICES_PER_BOX}H", buffer, start))


def _coordinate(value: float, where: str) -> int:
    """One number out of the buffer as the authored integer it has to be."""
    try:
        whole = int(value)
    except (ValueError, OverflowError) as error:  # NaN and the infinities
        raise _refuse("malformed-gltf",
                      f"{where}: {value!r} is not a number") from error
    if float(whole) != value:
        raise _refuse(
            "malformed-gltf",
            f"{where}: {value!r} is not an integer. A mesh is authored in whole "
            "world units")
    if abs(whole) > MAX_COORDINATE:
        # Reported as a distance rather than as the signed value, which is in
        # glTF space here and would have the opposite sign in the figure's own
        # space on the z axis, a needless puzzle for the one person who has to
        # go and find the box.
        raise _refuse(
            "mesh-extent",
            f"{where}: a corner sits {abs(whole)} world units from the origin "
            f"on one axis, and a figure is only {meshes.MESH_WORLD_HEIGHT} "
            f"units tall, so nothing in it may reach further than that")
    if (value * ROOT_SCALE) * meshes.UNIT_WORLD != value:
        raise _refuse(
            "malformed-gltf",
            f"{where}: {value!r} does not survive the root node's scale "
            f"exactly, so this model's units are lossy")
    return whole


def _part_name(node: Mapping[str, object], index: int, where: str) -> str:
    """A part's authored name, checked before anything quotes it back."""
    extras = node.get("extras")
    if not isinstance(extras, dict):
        raise _refuse("malformed-gltf", f"{where}: the node carries no extras, "
                                        f"so it names no part")
    name = extras.get("partName")
    if not isinstance(name, str) or not name:
        raise _refuse("malformed-gltf",
                      f"{where}: its extras name no part")
    if len(name) > MAX_PART_NAME:
        raise _refuse(
            "malformed-gltf",
            f"{where}: its part name is {len(name)} characters and may be at "
            f"most {MAX_PART_NAME}")
    if not set(name) <= _PRINTABLE:
        raise _refuse("malformed-gltf",
                      f"{where}: its part name is not printable ASCII")
    label = node.get("name")
    expected = f"{index:02d}_{_slug(name)}"
    if label != expected:
        raise _refuse(
            "malformed-gltf",
            f"{where}: the node is named {label!r} where its own part name "
            f"asks for {expected!r}. The index prefix is what carries the "
            f"far-to-near order through a tool that reorders nodes")
    return name


def _material_of(document: Mapping[str, object], primitive: Mapping[str, object],
                 node_extras: Mapping[str, object],
                 where: str) -> Tuple[int, int]:
    """The ramp and the rung, by both routes the format carries them.

    This is the one rule glTF has no native place for: a face names a ramp
    and a rung and never a colour, and the format has nowhere to say "index 1 of
    2 on a ramp". So the mapping lives in the material's *name* and again in its
    ``extras``, and again in the node's. A submission is required to spell it
    the same way in all three rather than being read from whichever one is
    looked at first.
    """
    material = _entry(_table(document, "materials"),
                      primitive.get("material"), "materials", where)
    try:
        ramp, rung = parse_material_name(str(material.get("name", "")))
    except ValueError as error:
        raise _refuse("mesh-ramp", f"{where}: {error} (a face names a ramp and "
                                   f"a rung, never a colour)") from error
    if not 0 <= ramp < meshes.RAMP_COUNT:
        raise _refuse("mesh-ramp",
                      f"{where}: it names ramp {ramp} and there are "
                      f"{meshes.RAMP_COUNT}")
    if not 0 <= rung < meshes.RUNG_COUNT:
        raise _refuse("mesh-ramp",
                      f"{where}: it names rung {rung} and a ramp has "
                      f"{meshes.RUNG_COUNT}")
    shading = material.get("pbrMetallicRoughness")
    if isinstance(shading, dict) and "baseColorTexture" in shading:
        raise _refuse("unsupported-gltf",
                      f"{where}: its material carries a texture, and a mesh "
                      f"carries no colour at all")
    extras = material.get("extras")
    if not isinstance(extras, dict):
        raise _refuse("mesh-ramp",
                      f"{where}: its material carries no extras, so the ramp "
                      f"and the rung are spelled only once")
    for holder, whose in ((extras, "material"), (node_extras, "node")):
        if (holder.get("ramp"), holder.get("rung")) != (ramp, rung):
            raise _refuse(
                "mesh-ramp",
                f"{where}: the material is named {material.get('name')!r} and "
                f"the {whose}'s extras say ramp {holder.get('ramp')!r} rung "
                f"{holder.get('rung')!r}. The two routes have to agree, "
                f"because either alone is a convention half-lost")
    if extras.get("rampName") != RAMP_NAMES[ramp]:
        raise _refuse("mesh-ramp",
                      f"{where}: its material spells its ramp two ways")
    return ramp, rung


def _box_from(corners: Sequence[Tuple[int, int, int]],
              normals: Sequence[Tuple[float, float, float]],
              indices: Sequence[int], accessor: Mapping[str, object],
              where: str) -> Tuple[Tuple[int, int, int], Tuple[int, int, int]]:
    """The low and high corners of the box these twenty-four corners describe.

    Reconstructed rather than read: a part is required to be a convex
    axis-aligned box, and the way to check that a submitted primitive *is* one
    is to require its corners, its normals and its winding to be exactly what
    one would be.
    """
    axis_normals = [(1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0),
                    (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)]
    for outward in normals:
        if outward not in axis_normals:
            raise _refuse(
                "mesh-not-a-box",
                f"{where}: the normal {outward} is not an axis direction, so "
                f"this is not the flat-shaded box the renderer draws")
    for outward in axis_normals:
        if normals.count(outward) != 4:
            raise _refuse(
                "mesh-not-a-box",
                f"{where}: {normals.count(outward)} corners face {outward} "
                f"where a box's face has four. A shared vertex is a smoothed "
                f"solid, and the renderer flat-shades")
    low = tuple(min(corner[axis] for corner in corners) for axis in range(3))
    high = tuple(max(corner[axis] for corner in corners) for axis in range(3))
    for axis in range(3):
        if low[axis] >= high[axis]:
            raise _refuse(
                "mesh-not-a-box",
                f"{where}: it has no extent on axis {axis}, so it is a box "
                f"with no volume")
    for corner, outward in zip(corners, normals):
        axis = [abs(value) for value in outward].index(1.0)
        want = high[axis] if outward[axis] > 0 else low[axis]
        if corner[axis] != want:
            raise _refuse(
                "mesh-not-a-box",
                f"{where}: a corner facing {outward} is not on that face of "
                f"the box, so the part is not the box it claims to be")
    if accessor.get("min") != [float(value) for value in low] or \
            accessor.get("max") != [float(value) for value in high]:
        raise _refuse(
            "malformed-gltf",
            f"{where}: the POSITION accessor's declared bounds are not the box "
            f"its own corners describe")
    for at in range(0, len(indices), 3):
        triangle = []
        for step in range(3):
            index = indices[at + step]
            if not 0 <= index < len(corners):
                raise _refuse(
                    "malformed-gltf",
                    f"{where}: a triangle names corner {index} of "
                    f"{len(corners)}")
            triangle.append(corners[index])
        one, two, three = triangle
        first = tuple(two[axis] - one[axis] for axis in range(3))
        second = tuple(three[axis] - one[axis] for axis in range(3))
        cross = (first[1] * second[2] - first[2] * second[1],
                 first[2] * second[0] - first[0] * second[2],
                 first[0] * second[1] - first[1] * second[0])
        outward = normals[indices[at]]
        if sum(cross[axis] * outward[axis] for axis in range(3)) <= 0:
            raise _refuse(
                "mesh-not-a-box",
                f"{where}: a triangle is wound against the face it belongs to, "
                f"so the renderer would cull the outside of the solid")
    return low, high  # type: ignore[return-value]


def _read_part(document: Mapping[str, object], buffer: bytes, index: int,
               where: str) -> meshes.Part:
    """One part of a submitted model, as the authored integers it must be."""
    nodes = _table(document, "nodes")
    node = _entry(nodes, index + 1, "nodes", where)
    for forbidden in ("matrix", "rotation", "translation", "scale"):
        if forbidden in node:
            raise _refuse(
                "unsupported-gltf",
                f"{where}: the part node carries a {forbidden}. A part is "
                f"authored in the figure's own space and the only transform in "
                f"this format is the root node's scale")
    if "children" in node:
        raise _refuse("unsupported-gltf",
                      f"{where}: a part has children, and a figure is one "
                      f"level of boxes under one root")
    name = _part_name(node, index, where)
    extras = node.get("extras")
    assert isinstance(extras, dict)  # _part_name has already refused otherwise
    if extras.get("part") != index:
        raise _refuse(
            "malformed-gltf",
            f"{where}: its extras place it at position {extras.get('part')!r}, "
            f"not {index}")

    mesh = _entry(_table(document, "meshes"), node.get("mesh"), "meshes", where)
    if mesh.get("name") != node.get("name"):
        raise _refuse(
            "malformed-gltf",
            f"{where}: the node and its mesh disagree about which part this is")
    primitives = mesh.get("primitives")
    if not isinstance(primitives, list) or len(primitives) != 1:
        raise _refuse("mesh-not-a-box",
                      f"{where}: a part is one box and must be one primitive")
    primitive = primitives[0]
    if not isinstance(primitive, dict):
        raise _refuse("malformed-gltf", f"{where}: its primitive is not an "
                                        f"object")
    if primitive.get("mode", MODE_TRIANGLES) != MODE_TRIANGLES:
        raise _refuse("mesh-not-a-box",
                      f"{where}: a part is not drawn as triangles")
    ramp, rung = _material_of(document, primitive, extras, where)

    attributes = primitive.get("attributes")
    if not isinstance(attributes, dict) or set(attributes) != {"POSITION",
                                                               "NORMAL"}:
        raise _refuse(
            "mesh-not-a-box",
            f"{where}: a flat-shaded box carries positions and normals and "
            f"nothing else; this one carries "
            f"{sorted(attributes) if isinstance(attributes, dict) else attributes!r}")
    accessors = _table(document, "accessors")
    views = _table(document, "bufferViews")
    position = _entry(accessors, attributes["POSITION"], "accessors", where)
    positions = _vectors(buffer, views, position, VERTICES_PER_BOX, where)
    normals = _vectors(
        buffer, views, _entry(accessors, attributes["NORMAL"], "accessors",
                              where),
        VERTICES_PER_BOX, where)
    indices = _indices(buffer, views,
                       _entry(accessors, primitive.get("indices"), "accessors",
                              where), where)

    corners = [tuple(_coordinate(value, where) for value in corner)
               for corner in positions]
    low, high = _box_from(corners, normals, indices, position, where)
    # The axis mapping, inverted. x and y pass through and z was negated, so the
    # far corner in glTF is the near corner in the figure's own space.
    return meshes.Part(low[0], high[0], low[1], high[1], -high[2], -low[2],
                       ramp, rung, name)


def read_model(text: bytes, buffer: bytes, style_name: str,
               archetype: str) -> Tuple[meshes.Part, ...]:
    """A submitted model as the part list :mod:`.meshes` would have held.

    ``text`` and ``buffer`` are the two files a model is, the document and the
    ``.bin`` it references, as bytes off disk, and ``style_name`` and
    ``archetype`` are what the *path* said this model is. Nothing here trusts
    the file about which figure it is: the two are compared, because a model
    dropped at the wrong key is a mistake worth naming rather than a silent
    substitution.

    Raises :class:`ModelRefused`. :mod:`.provided` applies every rule
    :mod:`.meshes.rules` states about the figure as a whole on the list this
    returns: its height, its width against its own sprite's silhouette, its
    triangle count and its far-to-near order as the *machine* evaluates it.
    Three of those four are measurements of something other than the file.
    """
    try:
        decoded = text.decode("utf-8")
    except UnicodeDecodeError as error:
        raise _refuse("not-a-gltf",
                      "it is not UTF-8 text, and a glTF document is JSON") \
            from error
    try:
        document = json.loads(decoded, parse_constant=_no_constants)
    except ModelRefused:
        raise
    except ValueError as error:
        raise _refuse("not-a-gltf",
                      f"it is not JSON ({error}). A model is a glTF 2.0 "
                      f"document and the buffer it names") from error
    except RecursionError as error:
        raise _refuse("malformed-gltf",
                      "its JSON nests deeper than a document ever needs to") \
            from error
    if not isinstance(document, dict):
        raise _refuse("not-a-gltf", "its JSON is not an object")

    asset = document.get("asset")
    if not isinstance(asset, dict) or asset.get("version") != VERSION:
        raise _refuse(
            "not-a-gltf",
            f"it does not declare glTF {VERSION}. The interchange format is "
            f"the one tools/placeholder_art writes into assets/gltf/, and "
            f"exporting a figure from it is the shortest correct route")
    for section, what in UNSUPPORTED_SECTIONS:
        if section in document:
            raise _refuse(
                "unsupported-gltf",
                f"it carries '{section}': {what}. A figure in this format is a "
                f"root node, one node a part, and nothing else")

    buffers = document.get("buffers")
    stem = model_name(archetype)
    if not isinstance(buffers, list) or len(buffers) != 1 \
            or not isinstance(buffers[0], dict):
        raise _refuse("malformed-gltf",
                      "a model references exactly one buffer, which is the "
                      "'.bin' beside it")
    if buffers[0].get("uri") != f"{stem}.bin":
        raise _refuse(
            "unsupported-gltf",
            f"its buffer is {buffers[0].get('uri')!r} and this reader opens "
            f"{stem}.bin, the file beside it. A data URI, an absolute path or a "
            f"URL is a way out of the provided tree")
    if buffers[0].get("byteLength") != len(buffer):
        raise _refuse(
            "malformed-gltf",
            f"it declares a {buffers[0].get('byteLength')!r}-byte buffer and "
            f"{stem}.bin is {len(buffer)} bytes")

    scenes = _table(document, "scenes")
    if document.get("scene") != 0 or len(scenes) != 1:
        raise _refuse("malformed-gltf",
                      "a model is one scene, rooted at the figure's own node")
    if not isinstance(scenes[0], dict) or scenes[0].get("nodes") != [0]:
        raise _refuse("malformed-gltf",
                      "the scene does not root at the figure's own node")

    nodes = _table(document, "nodes")
    root = _entry(nodes, 0, "nodes", "the root node")
    if root.get("name") != archetype:
        raise _refuse(
            "malformed-gltf",
            f"its root node is named {root.get('name')!r} and it was provided "
            f"as the {archetype}. The path is the key: a model stands in for "
            f"the figure whose file it was written to")
    extras = root.get("extras")
    if isinstance(extras, dict):
        for field, expected in (("archetype", archetype),
                                ("style", style_name)):
            if field in extras and extras[field] != expected:
                raise _refuse(
                    "malformed-gltf",
                    f"it says it is the {extras[field]!r} {field} and it was "
                    f"provided as {expected!r}")
    if root.get("scale") != [ROOT_SCALE] * 3:
        raise _refuse(
            "malformed-gltf",
            f"its root node's scale is {root.get('scale')!r}, not the uniform "
            f"{ROOT_SCALE} that puts one board tile on one Blender unit. The "
            f"buffer holds the figure's own authored integers")
    for forbidden in ("matrix", "rotation", "translation"):
        if forbidden in root:
            raise _refuse(
                "unsupported-gltf",
                f"its root node carries a {forbidden}, and the only transform "
                f"in this format is that one uniform scale")
    children = _integers(root.get("children"), "the root node's children")
    if children != list(range(1, len(nodes))):
        raise _refuse(
            "malformed-gltf",
            "the root's children are not every part node in order, so the "
            "authored far-to-near order is not recoverable from this file")
    if not children:
        raise _refuse("mesh-triangle-band",
                      "it holds no parts at all, and a mesh with no parts is "
                      "not a mesh")
    return tuple(_read_part(document, buffer, index,
                            f"part {index}")
                 for index in range(len(children)))
