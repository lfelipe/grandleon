# SPDX-License-Identifier: MIT
"""The PlayStation's art, as a C++ header, out of the ``n64_ci4`` profile.

This module adds **no profile and no palette mode.** That is the whole point
of it, and it is worth saying first.

The generated ``n64_ci4`` assets already *are* a PlayStation texture page plus
CLUT. The colour half of that claim is checked here rather than believed:
:func:`~.verify.check_playstation` asserts that the master palette's 124 entries
map to 124 *distinct* 15-bit triples, so nothing merges when a colour is
rounded to the five bits per channel this machine stores, and no re-quantising
profile is needed to survive the rounding. And because the map is
injective and master entry 0 is the transparent one, entry 0 is the *only*
colour that becomes the PlayStation's all-zero halfword, which is the value the
GPU skips. An opaque colour can never silently turn into a hole.

The byte half of the claim does not hold, and neither part of the difference
loses information:

* **Nibble order is reversed.** A 4bpp PNG, and the Nintendo 64's CI4, puts
  the left pixel of a pair in the *high* nibble. The PlayStation puts it in the
  *low* nibble, because VRAM is a halfword array and a halfword holds four
  texels from the least significant nibble rightwards.
* **The palette word is a different layout.** The Nintendo 64's TLUT is
  ``RRRRRGGG GGBBBBBA`` big-endian, alpha in the low bit. A PlayStation CLUT
  entry is a little-endian halfword ``SBBBBBGG GGGRRRRR``: red in the low five
  bits, and the top bit is the semi-transparency flag rather than an alpha.

So the conversion is a repack, done here at generation time, and the header
carries the result. The other reason it has to exist at all is that the assets
are PNGs, and there is no PNG decoder in the PlayStation toolchain image and no
vendor converter this repository is willing to depend on. Emitting from the same :class:`~.profiles.Converted` objects the
``n64_ci4`` PNGs are written from is what keeps the header and the PNGs from
disagreeing.

The format
----------
A cell is :data:`~.terrain.TILE` texels square at 4bpp, so a row is sixteen
halfwords and a cell is 512 of them: exactly the rectangle a ``GP0(0xA0)``
CPU-to-VRAM transfer wants, row major, no padding and no swizzle. Halfwords
rather than bytes because that is the unit VRAM is addressed in, and because
combining two of them into the 32-bit word the GP0 port takes is then a shift
rather than an assumption about byte order.

Each cell carries its own sixteen-entry CLUT, which is what the ``n64_ci4``
profile already chose per asset and what the hardware already allows per draw
call: a textured-rectangle command names a CLUT, so two cells drawn one after
the other need not share a palette, and no board-wide palette arithmetic is
needed.

Why one array per asset
-----------------------
Each cell is its own ``inline constexpr`` array so an executable naming a
handful of them links only those under
``-fdata-sections -Wl,--gc-sections``. Referencing a pointer table pulls in
every array it points at, so a size-constrained client should index by symbol
and let the tables be collected.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Sequence, Tuple

from . import (characters, figures, meshes, profiles, styles, terrain,
               themes)
from .profiles import Converted

#: The profile whose converted assets this header repacks. The PlayStation has
#: no profile of its own, and naming the one it borrows is what makes that a
#: statement rather than an omission. :func:`~.verify.check_playstation`
#: asserts the properties the borrowing depends on against this name.
SOURCE_PROFILE = "n64_ci4"

#: Texels packed into one VRAM halfword at four bits each.
TEXELS_PER_HALFWORD = 4

#: Entries in a 4bpp CLUT. The hardware reads sixteen contiguous halfwords
#: whatever the art spends, so a shorter palette is padded rather than trusted.
CLUT_SIZE = 16


def clut_word(colour: profiles.Rgb) -> int:
    """One master-palette colour as a PlayStation CLUT halfword.

    ``SBBBBBGG GGGRRRRR``: five bits per channel with red lowest, and the top
    bit the semi-transparency flag. It is left clear here, because every draw
    this repository issues is an opaque textured rectangle, for which the
    hardware ignores the flag. So the transparent master entry, which is black,
    becomes the all-zero halfword the GPU skips, and nothing else can.
    """
    red, green, blue = colour
    return ((blue >> 3) << 10) | ((green >> 3) << 5) | (red >> 3)


def cell_halfwords(source: Converted, left: int, top: int, size: int) -> List[int]:
    """One ``size``-square cell of ``source`` as VRAM halfwords.

    Row major, four texels per halfword, the leftmost in the low nibble.
    """
    assert size % TEXELS_PER_HALFWORD == 0, "a row is a whole number of halfwords"
    out: List[int] = []
    for row in range(size):
        base = (top + row) * source.width + left
        for column in range(0, size, TEXELS_PER_HALFWORD):
            word = 0
            for step in range(TEXELS_PER_HALFWORD):
                word |= source.indices[base + column + step] << (4 * step)
            out.append(word)
    return out


def clut_entries(source: Converted) -> List[int]:
    """One asset's CLUT, padded to the sixteen halfwords the hardware reads."""
    words = [clut_word(colour) for colour in source.colours]
    assert len(words) <= CLUT_SIZE, "a 4bpp asset has at most sixteen colours"
    return words + [0] * (CLUT_SIZE - len(words))


def silhouette_of(source: Converted) -> meshes.Silhouette:
    """The opaque box and drawn mass of one converted sprite, in texels.

    "Opaque" is decided the way the hardware decides it rather than the way the
    drawing intended it: a texel counts when the colour its palette entry
    resolves to is not the all-zero halfword the GPU skips. That is exactly the
    test `platform/playstation/scratch/scratch3d_exe.cpp` runs against the very
    arrays it uploaded, so this number and the machine's are the same number by
    construction rather than by agreement, and the scratch program asserts it
    against this one rather than measuring its own.

    It lives here rather than in :mod:`.meshes` because it is a question about
    *this machine's* colour word, and :func:`clut_word` above is the one place
    that word is defined.
    """
    words = [clut_word(colour) for colour in source.colours]
    left, right = source.width, -1
    top, bottom = source.height, -1
    area = 0
    for y in range(source.height):
        row = y * source.width
        for x in range(source.width):
            if words[source.indices[row + x]] == 0:
                continue
            left = min(left, x)
            right = max(right, x)
            top = min(top, y)
            bottom = max(bottom, y)
            area += 1
    if right < left:
        return meshes.Silhouette(0, 0, 0)
    return meshes.Silhouette(right - left + 1, bottom - top + 1, area)


def silhouettes(converted: Dict[str, Converted], style: styles.Style,
                figure: Optional[str] = None) -> List[meshes.Silhouette]:
    """Every archetype's silhouette, in `characters.ARCHETYPE_ORDER`.

    Measured on faction colour zero, which is the colour the scratch program
    measures and the one every faction recolours inside: the drawing routine is
    the same for all six and only the ramp differs.

    ``figure`` names the body to measure, and defaults to the first. It matters
    because a mesh is held to *its own* sprite: the second figure is a different
    line from shoulder to hem, so holding its solid to the first figure's
    silhouette would be checking it against a body it is not drawn as. Naming
    none measures the first figure, whose files carry no suffix and are the ones
    this function has always read.
    """
    style_suffix = styles.asset_suffix(style)
    shape = (figures.DEFAULT_FIGURE if figure is None
             else figures.FIGURES_BY_NAME[figure])
    suffix = f"{style_suffix}{shape.suffix}"
    colour = characters.FACTION_COLOURS[0].name
    return [
        silhouette_of(converted[f"characters/{archetype}_{colour}{suffix}.png"])
        for archetype in characters.ARCHETYPE_ORDER
    ]


# ---------------------------------------------------------------------------
# The two ramps a mesh face names, resolved
#
# A mesh carries no colour: a face names a ramp and a rung, and the colour is
# resolved where the figure is drawn from the CLUT the faction is already
# wearing. The rule that splits a CLUT into the two ramps needs no table and
# no naming convention. An entry is *faction-bearing* exactly when its colour
# word is absent from at least one of the other five factions' CLUTs for the
# same archetype, and *neutral* when all six carry it.
#
# It lives here for the same reason `silhouette_of` does: it is a question
# about *this machine's* colour word, and `clut_word` above is the one place
# that word is defined. `platform/playstation/scratch/scratch3d_exe.cpp` runs
# exactly this arithmetic at start-up on the arrays this module emitted, so a
# colour resolved here and a colour resolved there are the same colour by
# construction rather than by agreement.
# ---------------------------------------------------------------------------

#: The halfword a rung falls back to when a ramp has no entries at all. It is
#: the machine's own fallback, and it is opaque black in this layout with the
#: semi-transparency bit set, so it can never be mistaken for the zero word the
#: GPU skips.
EMPTY_RUNG_WORD = 0x8000


def luminance_of(word: int) -> int:
    """A CLUT word's luminance in the master palette's own five-bit space.

    Integer, weights 2:5:1. Only the *order* it induces is used, so the weights
    need to be sensible rather than colorimetric, and they are the machine's,
    which is what matters.
    """
    return (word & 0x1F) * 2 + ((word >> 5) & 0x1F) * 5 + ((word >> 10) & 0x1F)


def _rung_of(entries: Sequence[int], rung: int) -> int:
    """The ``rung``-th sample of a luminance-sorted ramp.

    The machine's sampling, integer for integer: the ends of the ramp are its
    darkest and lightest entries and the interior rungs divide it evenly.
    """
    count = len(entries)
    if count <= 0:
        return EMPTY_RUNG_WORD
    if count == 1:
        return entries[0]
    return entries[rung * (count - 1) // (meshes.RUNG_COUNT - 1)]


def mesh_ramp_words(converted: Dict[str, Converted], style: styles.Style,
                    archetype: str, faction: int) -> List[List[int]]:
    """One archetype's two ramps, four rungs each, as CLUT halfwords.

    Indexed ``[ramp][rung]`` with :data:`meshes.RAMP_NEUTRAL` and
    :data:`meshes.RAMP_FACTION` naming the rows, and resolved for the faction
    colour at index ``faction``.
    """
    suffix = styles.asset_suffix(style)
    cluts = [
        clut_entries(converted[f"characters/{archetype}_{colour.name}{suffix}.png"])
        for colour in characters.FACTION_COLOURS
    ]
    neutral: List[int] = []
    bearing: List[int] = []
    for word in cluts[faction]:
        # A CLUT word of zero is the hole the GPU skips; it is not a colour and
        # a face must never be given it.
        if word == 0:
            continue
        if all(word in other for other in cluts):
            neutral.append(word)
        else:
            bearing.append(word)
    neutral.sort(key=luminance_of)
    bearing.sort(key=luminance_of)
    ramps = [[0] * meshes.RUNG_COUNT for _ in range(meshes.RAMP_COUNT)]
    for rung in range(meshes.RUNG_COUNT):
        ramps[meshes.RAMP_NEUTRAL][rung] = _rung_of(neutral, rung)
        ramps[meshes.RAMP_FACTION][rung] = _rung_of(bearing, rung)
    return ramps


def clut_channels(word: int) -> Tuple[int, int, int]:
    """A CLUT halfword's three five-bit channels, red first.

    The inverse of :func:`clut_word`'s packing, to five bits rather than to
    eight: what the hardware stores is five bits a channel, and widening them
    back to eight is a *display* decision that belongs to whoever is drawing.
    """
    return (word & 0x1F, (word >> 5) & 0x1F, (word >> 10) & 0x1F)


def _identifier(*parts: str) -> str:
    return "grandleon_playstation_" + "_".join(parts)


def _halfword_array(name: str, payload: Sequence[int]) -> str:
    return (f"inline constexpr unsigned short {name}[{len(payload)}] = {{"
            + ",".join(str(value) for value in payload) + "};")


def _names(values: Sequence[str]) -> str:
    return ",".join('"%s"' % value for value in values)


def emit(converted: Dict[str, Converted], profile: profiles.Profile) -> str:
    """The header text, from the ``n64_ci4`` profile's converted assets."""
    size = profile.tile_size
    halfwords_per_row = size // TEXELS_PER_HALFWORD

    theme_names = [theme.name for theme in themes.THEMES]
    kind_names = list(terrain.KEYWORD_ORDER)
    style_names = [style.name for style in styles.STYLES]
    archetype_names = list(characters.ARCHETYPE_ORDER)
    faction_names = [colour.name for colour in characters.FACTION_COLOURS]

    lines: List[str] = [
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/playstation_header.py.",
        "// Do not edit.",
        "//",
        f"// Texel data is already in VRAM form: {size}x{size} cells at 4bpp,"
        " row major,",
        "// four texels per halfword with the leftmost in the LOW nibble --"
        " which is the",
        "// one thing this format reverses against the 4bpp PNGs and the"
        " Nintendo 64's",
        "// CI4 it is repacked from. CLUTs are PlayStation colour words,"
        " SBBBBBGG GGGRRRRR,",
        "// and the semi-transparency flag is clear throughout, so the master"
        " palette's",
        "// transparent entry -- and only it -- becomes the all-zero halfword"
        " the GPU skips.",
        "//",
        "// No colour is re-quantised. Every entry is a master palette colour"
        " rounded to",
        "// five bits per channel, and that rounding is injective over the"
        " whole palette.",
        "#pragma once",
        f"inline constexpr int {_identifier('cell_size')} = {size};",
        "inline constexpr int " + _identifier("texels_per_halfword")
        + f" = {TEXELS_PER_HALFWORD};",
        "inline constexpr int " + _identifier("halfwords_per_row")
        + f" = {halfwords_per_row};",
        "inline constexpr int " + _identifier("halfwords_per_cell")
        + f" = {halfwords_per_row * size};",
        f"inline constexpr int {_identifier('clut_size')} = {CLUT_SIZE};",
    ]

    # --- terrain ----------------------------------------------------------
    #
    # A base sheet holds the four interior variants side by side and one CLUT
    # serves all of them, because the profile chose that palette over the whole
    # sheet. The 47-variant blob sheets are not carried, for the reason
    # `platform/view/README.md` gives and every other console client follows.
    variants = terrain.BASE_VARIANTS
    terrain_symbols: List[List[List[str]]] = []
    terrain_cluts: List[List[str]] = []
    for theme in themes.THEMES:
        suffix = themes.asset_suffix(theme)
        per_kind: List[List[str]] = []
        per_kind_clut: List[str] = []
        for kind in kind_names:
            sheet = converted[f"terrain/{kind}_base{suffix}.png"]
            clut = _identifier("terrain", theme.name, kind, "clut")
            lines.append(_halfword_array(clut, clut_entries(sheet)))
            per_kind_clut.append(clut)
            row: List[str] = []
            for variant in range(variants):
                symbol = _identifier("terrain", theme.name, kind, str(variant))
                lines.append(_halfword_array(
                    symbol, cell_halfwords(sheet, variant * size, 0, size)))
                row.append(symbol)
            per_kind.append(row)
        terrain_symbols.append(per_kind)
        terrain_cluts.append(per_kind_clut)

    lines += [
        "inline constexpr int " + _identifier("theme_count")
        + f" = {len(theme_names)};",
        "inline constexpr const char* " + _identifier("theme_names")
        + "[] = {" + _names(theme_names) + "};",
        "inline constexpr int " + _identifier("terrain_kind_count")
        + f" = {len(kind_names)};",
        "// Terrain kinds in the same order as grandleon_terrain_kind_names in"
        " themes.h.",
        "inline constexpr const char* " + _identifier("terrain_kind_names")
        + "[] = {" + _names(kind_names) + "};",
        "inline constexpr int " + _identifier("terrain_variant_count")
        + f" = {variants};",
        "// One CLUT per sheet: the four interior variants of a kind share the"
        " palette",
        "// the profile chose over all of them.",
        "inline constexpr const unsigned short* " + _identifier("terrain_clut")
        + f"[{len(theme_names)}][{len(kind_names)}] = {{"
        + ",".join("{%s}" % ",".join(row) for row in terrain_cluts) + "};",
        "inline constexpr const unsigned short* " + _identifier("terrain")
        + f"[{len(theme_names)}][{len(kind_names)}][{variants}] = {{"
        + ",".join(
            "{%s}" % ",".join("{%s}" % ",".join(row) for row in per_kind)
            for per_kind in terrain_symbols
        ) + "};",
    ]

    # --- characters -------------------------------------------------------
    #
    # Not here. Every style's figures, CLUTs and lookup tables are in
    # `emit_characters` below, one header per style, and an executable includes
    # exactly one of them. What stays here is the menu, counts and names,
    # which points at no texel data.
    shadow = converted["characters/shadow.png"]
    lines.append(_halfword_array(
        _identifier("shadow"), cell_halfwords(shadow, 0, 0, size)))
    lines.append(_halfword_array(
        _identifier("shadow_clut"), clut_entries(shadow)))

    lines += [
        "inline constexpr int " + _identifier("style_count")
        + f" = {len(style_names)};",
        "inline constexpr const char* " + _identifier("style_names")
        + "[] = {" + _names(style_names) + "};",
        "inline constexpr int " + _identifier("archetype_count")
        + f" = {len(archetype_names)};",
        "inline constexpr const char* " + _identifier("archetype_names")
        + "[] = {" + _names(archetype_names) + "};",
        "inline constexpr int " + _identifier("faction_count")
        + f" = {len(faction_names)};",
        "inline constexpr const char* " + _identifier("faction_names")
        + "[] = {" + _names(faction_names) + "};",
    ]
    return "\n".join(lines) + "\n"


def characters_header_name(style: styles.Style) -> str:
    """The file one style's character art is emitted to.

    Named once here, because `platform/playstation/CMakeLists.txt` resolves its
    project's ``characterStyleId`` and hands this name to the compiler.
    """
    return f"playstation_characters_{style.name}.h"


def emit_characters(style: styles.Style,
                    converted: Dict[str, Converted],
                    profile: profiles.Profile) -> str:
    """One style's figures, as the only style an executable including it has.

    A ``[style][archetype][colour]`` pointer table is one data section naming
    every array it points at, so ``--gc-sections`` kept all four styles alive
    however the index was written. Measured, dropping the three unused rosters
    took both play executables from 323,584 bytes to 245,760. `play_exe.cpp`
    even named its style as a ``constexpr`` and still paid for all four, which
    is the proof the cost was never the index.

    Every style's header declares the same symbols, keyed by archetype and
    faction colour with the style dropped, so a build chooses by including one
    file.
    """
    size = profile.tile_size
    suffix = styles.asset_suffix(style)
    archetype_names = list(characters.ARCHETYPE_ORDER)
    faction_names = [colour.name for colour in characters.FACTION_COLOURS]
    style_index = [entry.name for entry in styles.STYLES].index(style.name)

    lines: List[str] = [
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/playstation_header.py.",
        "// Do not edit.",
        "//",
        f"// The character roster of the '{style.name}' style, in the texel"
        " format",
        "// playstation.h documents. Exactly one of these headers is included"
        " by a build,",
        "// chosen from the project's characterStyleId, and every one of them"
        " declares the",
        "// same symbols.",
        "#pragma once",
        "// Which entry of grandleon_playstation_style_names this file draws.",
        "inline constexpr int " + _identifier("character_style")
        + f" = {style_index};",
        "inline constexpr const char* " + _identifier("character_style_name")
        + f' = "{style.name}";',
    ]

    character_symbols: List[List[str]] = []
    character_cluts: List[List[str]] = []
    for archetype in archetype_names:
        row: List[str] = []
        clut_row: List[str] = []
        for colour in faction_names:
            asset = converted[f"characters/{archetype}_{colour}{suffix}.png"]
            symbol = _identifier("character", archetype, colour)
            clut = symbol + "_clut"
            lines.append(_halfword_array(
                symbol, cell_halfwords(asset, 0, 0, size)))
            lines.append(_halfword_array(clut, clut_entries(asset)))
            row.append(symbol)
            clut_row.append(clut)
        character_symbols.append(row)
        character_cluts.append(clut_row)

    lines += [
        "// One CLUT per figure. The hardware names a CLUT per draw call,"
        " so there is",
        "// no bank to share and no on-screen palette limit to respect.",
        "inline constexpr const unsigned short* " + _identifier("character_clut")
        + f"[{len(archetype_names)}][{len(faction_names)}] = {{"
        + ",".join("{%s}" % ",".join(row) for row in character_cluts) + "};",
        "inline constexpr const unsigned short* " + _identifier("characters")
        + f"[{len(archetype_names)}][{len(faction_names)}] = {{"
        + ",".join("{%s}" % ",".join(row) for row in character_symbols) + "};",
    ]
    return "\n".join(lines) + "\n"


def meshes_header_name(style: styles.Style,
                       figure: Optional[str] = None) -> str:
    """The file one style's character *meshes* are emitted to.

    Named once here for the reason :func:`characters_header_name` is: a build
    resolves its project's ``characterStyleId`` and hands this name to the
    compiler. The two follow the same convention on purpose: a mesh is another
    drawing of the same archetype, so it is chosen by the same style and by the
    same mechanism, and a build that included one style's sprites and another's
    meshes would be a build nothing here could describe.
    """
    if figure is None:
        return f"playstation_meshes_{style.name}.h"
    shape = figures.FIGURES_BY_NAME[figure]
    return f"playstation_meshes_{style.name}_models{shape.suffix}.h"


def _short_array(name: str, payload: Sequence[int]) -> str:
    return (f"inline constexpr short {name}[{len(payload)}] = {{"
            + ",".join(str(value) for value in payload) + "};")


def _int_array(name: str, payload: Sequence[int]) -> str:
    return (f"inline constexpr int {name}[{len(payload)}] = {{"
            + ",".join(str(value) for value in payload) + "};")


def emit_meshes(style: styles.Style, measured: Sequence[meshes.Silhouette],
                figure: Optional[str] = None) -> str:
    """One style's character meshes, and the silhouettes they are held to.

    Two kinds of number, and the difference is the whole point of the file:

    * the **parts** are art, authored in :mod:`.meshes` and emitted verbatim:
      integer corners and integer ramp and rung indices, no colour anywhere, so
      the six faction recolourings stay free and a generated header of integers
      is byte-reproducible by the same argument every other one here is;
    * the **silhouettes** are *measured*, off the same converted assets
      `emit_characters` repacks into VRAM texels, so redrawing a sprite moves
      its mesh's target with it and nothing has to be found and edited.

    Every style declares the same symbols, exactly as the character headers do,
    so a style with no mesh commission emits a header whose tables are empty and
    whose archetype rows are null rather than emitting no header. A build that
    included one would otherwise have to know which styles were commissioned.
    """
    archetype_names = list(characters.ARCHETYPE_ORDER)
    style_index = [entry.name for entry in styles.STYLES].index(style.name)
    commissioned = meshes.has_meshes(style)

    lines: List[str] = [
        "// Generated by tools/placeholder_art/generate.py from"
        " placeholder_art/playstation_header.py.",
        "// Do not edit.",
        "//",
        f"// The character meshes of the '{style.name}' style: each archetype"
        " as a short list of",
        "// convex axis-aligned boxes, in the figure's own space -- x right, y"
        " up, z away,",
        "// origin at the centre of the feet -- built"
        f" {meshes.MESH_WORLD_HEIGHT} world units tall, which is",
        f"// unit_world / cos({meshes.PITCH_DEGREES}) and not unit_world,"
        " because a world-vertical extent is",
        "// drawn focal*H*cos(pitch)/depth pixels tall.",
        "//",
        "// A part is eight values: x0,x1,y0,y1,z0,z1,ramp,rung. There is no"
        " colour here and",
        "// there is nowhere for one to be -- a face names a ramp and a rung"
        " and the drawing",
        "// resolves it from the faction's own CLUT, which is what keeps all"
        " six faction",
        "// recolourings free. Parts are authored far-to-near at the shipped"
        " pitch, which is",
        "// what buys the scene freedom from a depth buffer.",
        "//",
        "// The silhouette rows are measured, not authored: they are the opaque"
        " box of each",
        "// archetype's own sprite, in texels of its 32x32 cell, read from the"
        " very arrays",
        "// playstation_characters_<style>.h sends to VRAM.",
        "#pragma once",
        "// Which entry of grandleon_playstation_style_names these meshes"
        " belong to.",
        "inline constexpr int " + _identifier("mesh_style")
        + f" = {style_index};",
        "inline constexpr const char* " + _identifier("mesh_style_name")
        + f' = "{style.name}";',
        "// Whether this style has a mesh commission at all. A style without"
        " one is still a",
        "// complete style: a mesh is an additional drawing of an archetype,"
        " not a cell of",
        "// the animation sequence every style is required to ship.",
        "inline constexpr bool " + _identifier("mesh_commissioned")
        + f" = {'true' if commissioned else 'false'};",
        "inline constexpr int " + _identifier("mesh_values_per_part")
        + f" = {meshes.VALUES_PER_PART};",
        "inline constexpr int " + _identifier("mesh_world_height")
        + f" = {meshes.MESH_WORLD_HEIGHT};",
        "inline constexpr int " + _identifier("mesh_pitch_degrees")
        + f" = {meshes.PITCH_DEGREES};",
        "inline constexpr int " + _identifier("mesh_unit_world")
        + f" = {meshes.UNIT_WORLD};",
        "inline constexpr int " + _identifier("mesh_ramp_neutral")
        + f" = {meshes.RAMP_NEUTRAL};",
        "inline constexpr int " + _identifier("mesh_ramp_faction")
        + f" = {meshes.RAMP_FACTION};",
        "inline constexpr int " + _identifier("mesh_ramp_count")
        + f" = {meshes.RAMP_COUNT};",
        "inline constexpr int " + _identifier("mesh_rung_count")
        + f" = {meshes.RUNG_COUNT};",
        "inline constexpr int " + _identifier("mesh_faces_per_part")
        + f" = {meshes.FACES_PER_PART};",
        "inline constexpr int " + _identifier("mesh_vertices_per_part")
        + f" = {meshes.VERTICES_PER_PART};",
    ]

    symbols: List[str] = []
    counts: List[int] = []
    for archetype in archetype_names:
        parts = meshes.parts_for(style, archetype, figure)
        if not parts:
            symbols.append("nullptr")
            counts.append(0)
            continue
        payload: List[int] = []
        for part in parts:
            payload.extend(part.values())
        symbol = _identifier("mesh", archetype)
        lines.append(_short_array(symbol, payload))
        symbols.append(symbol)
        counts.append(len(parts))

    lines += [
        "// One row per archetype, in the roster's order; a null row is an"
        " archetype this",
        "// style has no mesh for, and its part count is zero.",
        "inline constexpr const short* " + _identifier("mesh_parts")
        + f"[{len(archetype_names)}] = {{" + ",".join(symbols) + "};",
        _int_array(_identifier("mesh_part_count"), counts),
        "inline constexpr int " + _identifier("mesh_max_parts")
        + f" = {max(counts) if counts else 0};",
        "// The silhouette each archetype's mesh is held to: the opaque box of"
        " its own",
        "// sprite, in texels of the cell, measured on faction colour zero --"
        " every faction",
        "// recolours inside the same silhouette. Present for every archetype,"
        " including the",
        "// ones with no mesh, because the measurement is of the sprite and not"
        " of the mesh.",
        _int_array(_identifier("mesh_silhouette_width"),
                   [entry.width for entry in measured]),
        _int_array(_identifier("mesh_silhouette_height"),
                   [entry.height for entry in measured]),
        _int_array(_identifier("mesh_silhouette_area"),
                   [entry.area for entry in measured]),
    ]
    return "\n".join(lines) + "\n"
