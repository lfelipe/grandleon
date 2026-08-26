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

from typing import Dict, List, Sequence, Tuple

from . import characters, profiles, styles, terrain, themes
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


def _short_array(name: str, payload: Sequence[int]) -> str:
    return (f"inline constexpr short {name}[{len(payload)}] = {{"
            + ",".join(str(value) for value in payload) + "};")


def _int_array(name: str, payload: Sequence[int]) -> str:
    return (f"inline constexpr int {name}[{len(payload)}] = {{"
            + ",".join(str(value) for value in payload) + "};")


