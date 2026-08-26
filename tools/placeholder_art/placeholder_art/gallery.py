# SPDX-License-Identifier: MIT
"""Contact sheets and the generated ``GALLERY.md`` and ``ROSTER.md``.

Everything here is output, never input: both pages are rewritten on every run
and must not be edited by hand.

The two pages cut the same library along different axes and that is why there
are two. ``GALLERY.md`` is organised by **asset kind** and answers *what does
the library contain*: every terrain in every profile, every transition, every
faction colour, every sequence. ``ROSTER.md`` is organised by **unit** and
answers *how good is it, and what is missing*: one row per archetype per style,
its sprite, its motion and its recolourings side by side. Judging one
unit on the first page means scrolling between three of its sections; judging
the whole library on the second is one scroll down one table.

Two constraints shape the design.

*GitHub renders Markdown images at their natural size with smoothing.* A 32x32
tile embedded directly would be a thumbnail, and a scaled one would be blurred
mush. So the gallery embeds purpose-built **contact sheets**: nearest-neighbour
upscales, composed and labelled here, at a size that reads in a browser.

*The point of the gallery is comparison.* Every panel puts every output profile
next to the others at a matched display size, so the cost of each target's
constraints is visible rather than described.
"""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

from . import (autotile, backdrops, characters, figures, frames,
               pixelfont, playstation_header, pngio, profiles,
               styles, terrain, themes)
from .palette import PALETTE_SIZE, RAMPS, RGB, TRANSPARENT
from .raster import Canvas

Rgba = Tuple[int, int, int, int]

#: Every tile is shown at this many screen pixels, whatever its native size.
TILE_DISPLAY = 64
#: Every sprite is shown at this many screen pixels.
SPRITE_DISPLAY = 128

#: The roster page's three sizes for the 2D half of a row, and the argument for
#: each. The standing sprite is the row's identity and is shown at the size the
#: rest of this gallery shows a sprite. The four animation cells are shown at
#: half of it: a reviewer is asking "does it move and does it stay the same
#: figure while it does", which reads at 64, and four cells at 128 would push
#: the 3D half of the row off the side of the page, the one thing the layout
#: may not do. The faction strip is at **native** size, because it is asked to
#: prove only that the recolouring holds, and the whole menu at any larger size
#: is already a section of ``GALLERY.md``.
UNIT_STAND_DISPLAY = SPRITE_DISPLAY
UNIT_CELL_DISPLAY = SPRITE_DISPLAY // 2
UNIT_FACTION_DISPLAY = characters.SPRITE

#: The faction colour a row's large art is drawn in: the sprite, the cells and
#: the recolourings alike. It is the colour every other measurement in this
#: pipeline is taken on, and naming it once here is what keeps a row's parts
#: from being drawn in two different factions.
UNIT_FACTION = 0

BACKGROUND: Rgba = (0x1B, 0x1F, 0x26, 255)
PANEL: Rgba = (0x2A, 0x30, 0x3A, 255)
TEXT: Rgba = (0xF2, 0xF5, 0xF7, 255)
DIM: Rgba = (0x8A, 0x95, 0xA2, 255)

#: The terrain patch used to show a transition, as an occupancy pattern for the
#: overlay terrain over a bed of the underlying one.
TRANSITION_PATCH: Tuple[str, ...] = (
    "..##",
    ".###",
    "..##",
)


class Sheet:
    """A small RGBA image builder used only for gallery panels.

    It duck-types :meth:`Canvas.put`, so :mod:`.pixelfont` draws into it
    unchanged; the "palette index" it passes through is an RGBA tuple here.
    """

    def __init__(self, width: int, height: int, background: Rgba = BACKGROUND) -> None:
        self.width = width
        self.height = height
        self.pixels: List[Rgba] = [background] * (width * height)

    def put(self, x: int, y: int, value: Rgba) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            self.pixels[y * self.width + x] = value

    def rect(self, x: int, y: int, width: int, height: int, value: Rgba) -> None:
        for row in range(y, y + height):
            for column in range(x, x + width):
                self.put(column, row, value)

    def text(self, message: str, x: int, y: int, colour: Rgba = TEXT,
             scale: int = 2) -> None:
        pixelfont.draw(self, message, x, y, colour, scale, shadow=(0, 0, 0, 255))

    def draw_region(self, source: profiles.Converted, left: int, top: int,
                    width: int, height: int, x: int, y: int, scale: int,
                    transparent: Sequence[int] = ()) -> None:
        """Nearest-neighbour blit of part of a converted image."""
        blanks = set(transparent)
        for row in range(height):
            for column in range(width):
                slot = source.indices[(top + row) * source.width + left + column]
                if slot in blanks:
                    continue
                red, green, blue = source.colours[slot]
                for dy in range(scale):
                    for dx in range(scale):
                        self.put(x + column * scale + dx, y + row * scale + dy,
                                 (red, green, blue, 255))

    def draw_canvas(self, canvas: Canvas, x: int, y: int, scale: int) -> None:
        """Nearest-neighbour blit of a native palette-index canvas."""
        for row in range(canvas.height):
            for column in range(canvas.width):
                index = canvas.data[row * canvas.width + column]
                if index == TRANSPARENT:
                    continue
                red, green, blue = RGB[index]
                for dy in range(scale):
                    for dx in range(scale):
                        self.put(x + column * scale + dx, y + row * scale + dy,
                                 (red, green, blue, 255))

    def encode(self) -> bytes:
        return pngio.encode_rgba(self.width, self.height, self.pixels)


def _zoom(profile: profiles.Profile, display: int) -> int:
    return max(1, display // profile.tile_size)


def _tile_origin(index: int, columns: int, size: int) -> Tuple[int, int]:
    return ((index % columns) * size, (index // columns) * size)


# ---------------------------------------------------------------------------
# Panels
# ---------------------------------------------------------------------------


def _palette_panel() -> Sheet:
    swatch = 26
    label_width = 90
    rows = list(RAMPS.items())
    widest = max(len(entries) for _, entries in rows)
    sheet = Sheet(label_width + widest * (swatch + 2) + 12,
                  len(rows) * (swatch + 6) + 30)
    sheet.text(f"MASTER PALETTE  {PALETTE_SIZE} ENTRIES  INDEX 0 TRANSPARENT",
               10, 9, TEXT, 2)
    for row, (name, entries) in enumerate(rows):
        top = 28 + row * (swatch + 6)
        sheet.text(name, 10, top + swatch // 2 - 5, DIM, 2)
        for column, index in enumerate(entries):
            left = label_width + column * (swatch + 2)
            red, green, blue = RGB[index]
            sheet.rect(left, top, swatch, swatch, (red, green, blue, 255))
    return sheet


def _terrain_panel(name: str,
                   converted: Dict[str, Dict[str, profiles.Converted]]) -> Sheet:
    proof, variants = 3, terrain.BASE_VARIANTS
    row_height = TILE_DISPLAY * proof + 22
    label_width = 116
    width = label_width + TILE_DISPLAY * proof + 24 + TILE_DISPLAY * variants + 20
    sheet = Sheet(width, len(profiles.PROFILES) * row_height + 34)
    sheet.text(f"{name}  -  LEFT 3X3 REPEAT OF VARIANT 0  -  RIGHT ALL VARIANTS",
               10, 10, TEXT, 2)
    for row, profile in enumerate(profiles.PROFILES):
        top = 30 + row * row_height
        zoom = _zoom(profile, TILE_DISPLAY)
        size = profile.tile_size
        sheet.rect(0, top - 4, width, row_height, PANEL if row % 2 else BACKGROUND)
        sheet.text(profile.name, 10, top + 10, TEXT, 2)
        sheet.text(f"{size}PX {profile.bit_depth}BPP", 10, top + 28, DIM, 1)
        base = converted[profile.name][f"terrain/{name}_base.png"]
        for repeat_y in range(proof):
            for repeat_x in range(proof):
                sheet.draw_region(base, 0, 0, size, size,
                                  label_width + repeat_x * TILE_DISPLAY,
                                  top + repeat_y * TILE_DISPLAY, zoom)
        left = label_width + TILE_DISPLAY * proof + 24
        for variant in range(variants):
            sheet.draw_region(base, variant * size, 0, size, size,
                              left + variant * TILE_DISPLAY, top, zoom)
            sheet.text(f"V{variant}", left + variant * TILE_DISPLAY + 4,
                       top + TILE_DISPLAY + 4, DIM, 1)
    return sheet


def _theme_panel(theme: themes.Theme,
                 converted: Dict[str, profiles.Converted]) -> Sheet:
    """One theme's whole ground vocabulary in a row, at the modern profile.

    Read down the page, the theme panels are the comparison that matters: the
    same tiles, the same variants, the same rims, in another climate.
    """
    profile = profiles.PROFILES_BY_NAME["modern"]
    size = profile.tile_size
    zoom = _zoom(profile, TILE_DISPLAY)
    suffix = themes.asset_suffix(theme)
    cell = TILE_DISPLAY + 16
    sheet = Sheet(len(terrain.TERRAIN_ORDER) * cell + 12, cell + 44)
    sheet.text(f"{theme.name}  -  BASE VARIANT 0 OF EVERY TERRAIN", 8, 10,
               TEXT, 2)
    for index, name in enumerate(terrain.TERRAIN_ORDER):
        source = converted[f"terrain/{name}_base{suffix}.png"]
        x = 8 + index * cell
        sheet.rect(x - 2, 28, TILE_DISPLAY + 4, TILE_DISPLAY + 4, PANEL)
        sheet.draw_region(source, 0, 0, size, size, x, 30, zoom)
        sheet.text(name, x, 30 + TILE_DISPLAY + 5, DIM, 1)
    return sheet


def _blob_panel(name: str, converted: Dict[str, profiles.Converted]) -> Sheet:
    profile = profiles.PROFILES_BY_NAME["modern"]
    size = profile.tile_size
    zoom = _zoom(profile, TILE_DISPLAY)
    columns = 8
    rows = (len(autotile.BLOB_MASKS) + columns - 1) // columns
    cell = TILE_DISPLAY + 14
    sheet = Sheet(columns * cell + 12, rows * cell + 34)
    sheet.text(f"{name}  47-VARIANT BLOB SET  (MASK VALUE BELOW EACH TILE)",
               8, 10, TEXT, 2)
    source = converted[f"terrain/{name}_blob.png"]
    for index, mask in enumerate(autotile.BLOB_MASKS):
        left, top = _tile_origin(index, columns, size)
        x = 8 + (index % columns) * cell
        y = 30 + (index // columns) * cell
        sheet.rect(x - 2, y - 2, TILE_DISPLAY + 4, TILE_DISPLAY + 4, PANEL)
        sheet.draw_region(source, left, top, size, size, x, y, zoom,
                          transparent=source.transparent)
        sheet.text(str(mask), x + 2, y + TILE_DISPLAY + 3, DIM, 1)
    return sheet


def _transition_patch(over: str, under: str) -> Canvas:
    """Compose the overlay terrain over a bed of the underlying terrain."""
    size = terrain.TILE
    height = len(TRANSITION_PATCH)
    width = len(TRANSITION_PATCH[0])
    canvas = Canvas(width * size, height * size)
    for y in range(height):
        for x in range(width):
            canvas.blit_opaque(terrain.base_tile(under, (x + y) % terrain.BASE_VARIANTS),
                               x * size, y * size)
    for y in range(height):
        for x in range(width):
            if TRANSITION_PATCH[y][x] != "#":
                continue
            mask = autotile.mask_from(
                lambda dx, dy: (
                    0 <= y + dy < height and 0 <= x + dx < width
                    and TRANSITION_PATCH[y + dy][x + dx] == "#"
                )
            )
            canvas.blit(terrain.blob_tile(over, mask, under), x * size, y * size)
    return canvas


def _transition_panel(over: str, under: str) -> Sheet:
    native = _transition_patch(over, under)
    cells_x = len(TRANSITION_PATCH[0])
    cells_y = len(TRANSITION_PATCH)
    column = cells_x * TILE_DISPLAY + 12
    sheet = Sheet(column * len(profiles.PROFILES) + 8,
                  cells_y * TILE_DISPLAY + 52)
    sheet.text(f"{over} OVER {under}", 8, 10, TEXT, 2)
    for index, profile in enumerate(profiles.PROFILES):
        converted = profiles.convert(native, profile)
        zoom = _zoom(profile, TILE_DISPLAY)
        x = 8 + index * column
        sheet.text(profile.name, x, 28, DIM, 1)
        sheet.draw_region(converted, 0, 0, converted.width, converted.height,
                          x, 40, zoom, transparent=converted.transparent)
    return sheet


def _map_panel(profile: profiles.Profile, converted: profiles.Converted) -> Sheet:
    zoom = _zoom(profile, terrain.TILE)
    sheet = Sheet(converted.width * zoom + 16, converted.height * zoom + 40)
    sheet.text(f"SAMPLE MAP  -  {profile.name}", 8, 10, TEXT, 2)
    sheet.draw_region(converted, 0, 0, converted.width, converted.height, 8, 28,
                      zoom, transparent=converted.transparent)
    return sheet


def _character_panel(archetype: str,
                     converted: Dict[str, Dict[str, profiles.Converted]]) -> Sheet:
    column = SPRITE_DISPLAY + 14
    row = SPRITE_DISPLAY + 26
    sheet = Sheet(column * len(profiles.PROFILES) + 10,
                  row * len(characters.FACTION_COLOURS) + 32)
    sheet.text(f"{archetype}  -  EVERY FACTION COLOUR IN EVERY PROFILE",
               8, 10, TEXT, 2)
    for colour_index, colour in enumerate(characters.FACTION_COLOURS):
        for profile_index, profile in enumerate(profiles.PROFILES):
            source = converted[profile.name][
                f"characters/{archetype}_{colour.name}.png"]
            zoom = _zoom(profile, SPRITE_DISPLAY)
            x = 8 + profile_index * column
            y = 30 + colour_index * row
            sheet.rect(x - 2, y - 2, SPRITE_DISPLAY + 4, SPRITE_DISPLAY + 4, PANEL)
            sheet.draw_region(source, 0, 0, source.width, source.height, x, y,
                              zoom, transparent=source.transparent)
            sheet.text(f"{profile.name} {colour.name}", x,
                       y + SPRITE_DISPLAY + 4, DIM, 1)
    return sheet


def _figures_panel(style: styles.Style,
                   converted: Dict[str, Dict[str, profiles.Converted]]
                   ) -> Sheet:
    """One style's whole roster, drawn as every figure, at both sizes that matter.

    The page that answers the only two questions a second figure raises, and it
    puts them side by side on purpose. Left of the divider is the size a board
    draws a unit at; right of it is the halved four-tone reduction the
    legibility pass measures on (``profiles.LEGIBILITY``). Read across a row and
    the questions are: **do these two read as the same role**, and **do they
    read as two people**. The second half of the row is where the honest answer
    to the second question is.
    """
    shown = (profiles.PROFILES_BY_NAME["modern"], profiles.LEGIBILITY)
    cells = len(shown) * figures.FIGURE_COUNT
    column = SPRITE_DISPLAY + 12
    row = SPRITE_DISPLAY + 26
    sheet = Sheet(column * cells + 24,
                  row * len(characters.ARCHETYPE_ORDER) + 44)
    sheet.text(f"{style.name}  -  EVERY FIGURE, AT THE BOARD AND AT THE "
               "REDUCTION", 8, 10, TEXT, 2)
    style_suffix = styles.asset_suffix(style)
    places = [(profile, shape) for profile in shown
              for shape in figures.FIGURE_ORDER]
    for index, (profile, shape) in enumerate(places):
        sheet.text(f"{shape.name}/{profile.name}".upper(),
                   8 + index * column + (8 if index >= figures.FIGURE_COUNT
                                         else 0), 26, DIM, 1)
    for archetype_index, archetype in enumerate(characters.ARCHETYPE_ORDER):
        y = 40 + archetype_index * row
        for index, (profile, shape) in enumerate(places):
            source = converted[profile.name][
                f"characters/{archetype}_blue{style_suffix}{shape.suffix}.png"]
            # A gap rather than a rule between the two halves: the reduction is
            # a different question, not a different colour of the same one.
            x = 8 + index * column + (8 if index >= figures.FIGURE_COUNT else 0)
            sheet.rect(x - 2, y - 2, SPRITE_DISPLAY + 4, SPRITE_DISPLAY + 4,
                       PANEL)
            sheet.draw_region(source, 0, 0, source.width, source.height, x, y,
                              _zoom(profile, SPRITE_DISPLAY),
                              transparent=source.transparent)
        sheet.text(archetype, 8, y + SPRITE_DISPLAY + 4, DIM, 1)
    return sheet


def _frames_panel(style: styles.Style, profile: profiles.Profile,
                  converted: Dict[str, profiles.Converted]) -> Sheet:
    """One style's whole sequence: every archetype, standing then in motion.

    Laid out as the animation runs rather than as the files sit: the standing
    cell first, because it is frame 0 of every sequence and the cell a walk
    begins and ends on, then the sheet's own cells in sheet order. This is the
    page a reviewer reads to answer "does it move", and the page an external
    artist reads to see what a conforming sheet looks like.
    """
    suffix = styles.asset_suffix(style)
    cells = frames.FRAME_COUNT + 1
    column = SPRITE_DISPLAY + 10
    row = SPRITE_DISPLAY + 26
    sheet = Sheet(column * cells + 16,
                  row * len(characters.ARCHETYPE_ORDER) + 44)
    sheet.text(f"{style.name}  -  THE SEQUENCE, IN {profile.name}", 8, 10,
               TEXT, 2)
    labels = ["stand"] + list(frames.FRAME_NAMES)
    for index, label in enumerate(labels):
        sheet.text(label.upper(), 8 + index * column, 26, DIM, 1)
    zoom = _zoom(profile, SPRITE_DISPLAY)
    for archetype_index, archetype in enumerate(characters.ARCHETYPE_ORDER):
        standing = converted[f"characters/{archetype}_blue{suffix}.png"]
        strip = converted[f"characters/{archetype}_blue{suffix}_frames.png"]
        y = 40 + archetype_index * row
        for index in range(cells):
            source = standing if index == 0 else strip
            left = 0 if index == 0 else (index - 1) * standing.width
            x = 8 + index * column
            sheet.rect(x - 2, y - 2, SPRITE_DISPLAY + 4, SPRITE_DISPLAY + 4,
                       PANEL)
            sheet.draw_region(source, left, 0, standing.width, standing.height,
                              x, y, zoom, transparent=source.transparent)
        sheet.text(archetype, 8, y + SPRITE_DISPLAY + 4, DIM, 1)
    return sheet


# ---------------------------------------------------------------------------
# The roster page's two panels
#
# One row of `ROSTER.md` is two images and the layout inside each of them is
# decided here rather than by the Markdown table, for one reason: a table sizes
# itself from its content, so leaving four cells and a faction strip loose in
# their own columns would make a row wider than the page and push the 3D half
# out of sight. A row whose halves cannot be seen together is not a comparison.
# ---------------------------------------------------------------------------


def _unit_panel(style: styles.Style, archetype: str,
                converted: Dict[str, profiles.Converted]) -> Sheet:
    """One unit's whole 2D drawing: standing, in motion, and recoloured.

    Drawn at the reference profile, which is the one the art is authored at;
    what each of the other profiles costs is what ``GALLERY.md``'s own
    character section is for.
    """
    profile = profiles.PROFILES[0]
    suffix = styles.asset_suffix(style)
    colour = characters.FACTION_COLOURS[UNIT_FACTION]
    stand_zoom = _zoom(profile, UNIT_STAND_DISPLAY)
    cell_zoom = _zoom(profile, UNIT_CELL_DISPLAY)
    faction_zoom = _zoom(profile, UNIT_FACTION_DISPLAY)

    cells_left = 8 + UNIT_STAND_DISPLAY + 12
    cell_pitch = UNIT_CELL_DISPLAY + 4
    width = cells_left + frames.FRAME_COUNT * cell_pitch + 8
    faction_pitch = UNIT_FACTION_DISPLAY + 4
    height = 20 + UNIT_STAND_DISPLAY + 20 + 12 + UNIT_FACTION_DISPLAY + 14

    sheet = Sheet(width, height)
    standing = converted[f"characters/{archetype}_{colour.name}{suffix}.png"]
    strip = converted[f"characters/{archetype}_{colour.name}{suffix}_frames.png"]

    sheet.text("STAND", 8, 10, DIM, 1)
    sheet.rect(6, 18, UNIT_STAND_DISPLAY + 4, UNIT_STAND_DISPLAY + 4, PANEL)
    sheet.draw_region(standing, 0, 0, standing.width, standing.height, 8, 20,
                      stand_zoom, transparent=standing.transparent)
    for index, frame in enumerate(frames.FRAME_ORDER):
        x = cells_left + index * cell_pitch
        sheet.text(_cell_label(frame), x, 10, DIM, 1)
        sheet.rect(x - 2, 18, UNIT_CELL_DISPLAY + 4, UNIT_CELL_DISPLAY + 4, PANEL)
        sheet.draw_region(strip, index * standing.width, 0, standing.width,
                          standing.height, x, 20, cell_zoom,
                          transparent=strip.transparent)

    strip_top = 20 + UNIT_STAND_DISPLAY + 20
    sheet.text("SIX FACTION COLOURS", 8, strip_top - 10, DIM, 1)
    for index, faction in enumerate(characters.FACTION_COLOURS):
        source = converted[f"characters/{archetype}_{faction.name}{suffix}.png"]
        x = 8 + index * faction_pitch
        sheet.rect(x - 2, strip_top - 2, UNIT_FACTION_DISPLAY + 4,
                   UNIT_FACTION_DISPLAY + 4, PANEL)
        sheet.draw_region(source, 0, 0, source.width, source.height, x,
                          strip_top, faction_zoom,
                          transparent=source.transparent)
        sheet.text(faction.name, x, strip_top + UNIT_FACTION_DISPLAY + 4, DIM, 1)
    return sheet


def _cell_label(frame: frames.Frame) -> str:
    """An animation cell's name, short enough to sit over a 64-pixel cell.

    The sequence's own labels carry the animation they belong to, as in "Walk:
    contact". Here the four cells are in sequence order with nothing else
    between them, so the animation is visible from the arrangement and the word
    that would not fit is the word that is not needed.
    """
    return frame.label.split(":")[-1].strip().upper()



# ---------------------------------------------------------------------------
# Markdown
# ---------------------------------------------------------------------------


def _backdrop_panel() -> Sheet:
    """Every backdrop, at the proportions a client draws it in.

    Drawn here rather than emitted as an asset because a backdrop *is* this
    picture: a run of flat bands and their row counts. Nothing under
    ``assets/`` holds one, and nothing needs to. See
    `placeholder_art/backdrops.py` for why a backdrop is a table rather than
    an image.
    """
    column, gap, height, label = 200, 24, 280, 34
    top_of_bands = 40 + label
    menu = backdrops.BACKDROPS
    sheet = Sheet(len(menu) * (column + gap) + gap, top_of_bands + height + 20)
    sheet.text("SCENE BACKDROPS  BANDS, NOT PIXELS", 12, 10, TEXT, 2)
    words = backdrops.TEXT_COLOUR + (255,)
    for index, backdrop in enumerate(menu):
        left = gap + index * (column + gap)
        sheet.text(backdrop.name.upper().replace("_", " "), left, 40, DIM, 2)
        for band, (top, rows, entry) in enumerate(backdrops.band_rows(backdrop)):
            red, green, blue = RGB[entry]
            first = top_of_bands + round(
                top * height / backdrops.BACKDROP_ROWS)
            last = top_of_bands + round(
                (top + rows) * height / backdrops.BACKDROP_ROWS)
            sheet.rect(left, first, column, last - first,
                       (red, green, blue, 255))
        # The words a scene puts on it, in the colour every band was measured
        # against. A band that swallowed them would be visible here.
        sheet.text("THE GATES", left + 12, top_of_bands + 26, words, 2)
        sheet.text("OPEN", left + 12, top_of_bands + 46, words, 2)
    return sheet


def _markdown(pages: Dict[str, str]) -> str:
    lines: List[str] = [
        "# Placeholder art gallery",
        "",
        "<!-- Generated by tools/placeholder_art/generate.py. Do not edit. -->",
        "",
        "Every image on this page is produced by the generator in this folder "
        "from the definitions in `placeholder_art/terrain.py` and",
        "`placeholder_art/characters.py`. Tiles and sprites are shown "
        "nearest-neighbour upscaled so the pixels stay crisp; the assets "
        "themselves",
        "live under `assets/<profile>/` at their native size.",
        "",
        "## Logo",
        "",
        "Drawn by `placeholder_art/logo.py` from the master palette and the "
        "gallery's own pixel font; there is no source image.",
        "",
        "![logo](gallery/logo.png)",
        "",
        "## Output profiles",
        "",
        "| Profile | Tile | Sprite | Encoding | Colours | Notes |",
        "| --- | --- | --- | --- | --- | --- |",
    ]
    for profile in profiles.PROFILES:
        encoding = ("RGBA8888" if profile.encoding == "rgba"
                    else f"indexed {profile.bit_depth}bpp")
        lines.append(
            f"| `{profile.name}` | {profile.tile_size}x{profile.tile_size} | "
            f"{profile.sprite_size}x{profile.sprite_size} | {encoding} | "
            f"{profile.max_colours} | {profile.notes} |"
        )
    lines += [
        "",
        "## Palette",
        "",
        f"![master palette]({pages['palette']})",
        "",
        "## Sample map",
        "",
        "One map, composited the way a renderer would: a neighbour mask per "
        "cell, a variant looked up from it, and a transition rim chosen from "
        "the",
        "terrain underneath. Units are placed so the sprites can be judged at "
        "true scale against the ground they stand on.",
        "",
    ]
    for profile in profiles.PROFILES:
        lines += [f"**{profile.label}**", "",
                  f"![sample map {profile.name}]({pages['map_' + profile.name]})",
                  ""]

    lines += [
        "## Themes",
        "",
        "A theme is a palette substitution and nothing else: the same height "
        "fields, the same props, the same rims, in another climate. A project "
        "picks",
        "one, and every terrain it uses is drawn in it. The first theme "
        "substitutes nothing, so a project that names no theme is unchanged.",
        "",
        "| Theme | What it is |",
        "| --- | --- |",
    ]
    for theme in themes.THEMES:
        lines.append(f"| `{theme.name}` | {theme.summary} |")
    lines.append("")
    for theme in themes.THEMES:
        lines += [f"### {theme.label}", "",
                  f"![{theme.name} terrain]({pages['theme_' + theme.name]})", ""]

    lines += [
        "## Terrain",
        "",
        "For each terrain: the base tile repeated three by three (any visible "
        "grid here would be a bug), all four interior variants, and the full",
        "47-variant blob set with its mask value under each tile.",
        "",
    ]
    for name in terrain.TERRAIN_ORDER:
        lines += [
            f"### {name}",
            "",
            f"![{name} across profiles]({pages['terrain_' + name]})",
            "",
            f"![{name} blob set]({pages['blob_' + name]})",
            "",
        ]

    lines += [
        "## Transitions",
        "",
        "Where two terrains meet, the rim is styled for the pair rather than "
        "being one generic border: water finishes as foam over sand and as a "
        "dark",
        "reedy bank over grass, and a road crossing water becomes a timber "
        "bridge.",
        "",
    ]
    for over, under in terrain.TRANSITION_PAIRS:
        key = f"transition_{over}_over_{under}"
        lines += [f"### {over} over {under}", "",
                  f"![{over} over {under}]({pages[key]})", ""]

    lines += [
        "## Characters",
        "",
        "The whole faction colour menu in every profile: a game's factions "
        "choose from these, they are not a roster of anyone's factions.",
        "",
    ]
    for archetype in characters.ARCHETYPE_ORDER:
        lines += [f"### {characters.ARCHETYPES[archetype].label}", "",
                  f"![{archetype}]({pages['character_' + archetype]})", ""]
    lines += [
        "## Animation frames",
        "",
        "Every style ships the same sequence for every archetype, and the "
        "registry refuses one that does not. The standing sprite is frame 0 "
        "and keeps its own filename; the cells beside it ship as one strip, "
        "`<archetype>_<colour>_frames.png`, in the order shown. A frame is a "
        "*pose* applied to the body the archetype's own routine drew, never a "
        "second drawing. See `placeholder_art/frames.py`, which holds both "
        "the reason and the rules a supplied strip is held to.",
        "",
    ]
    for style in styles.STYLES:
        lines += [f"### {style.label}", "",
                  f"![{style.name} frames]({pages['frames_' + style.name]})",
                  ""]
    lines += [
        "## Figures",
        "",
        "A second body for every role: **the same kit over a different "
        "person**, drawn by a routine of its own where a style's commission "
        "has reached it and by a stand-in transform of the first figure's "
        "pixels where it has not. See `placeholder_art/figures.py`. The "
        "first figure keeps its own filename; the second carries `_second`.",
        "",
        "`medieval` is drawn. The other six carry the stand-in, and what the "
        "reduction shows honestly is the difference: a drawing puts hair, a "
        "veil, a mane or a waist into the outline, and a transform can only "
        "narrow what was already there. The vocabulary a commission follows "
        "is hair first, then the line (narrower shoulders and a cut-in "
        "waist, the hip left alone), then the hem, each only where the kit "
        "leaves room; `placeholder_art/figures.py` carries it.",
        "",
        "Each row is one role at the size a board draws it and again at the "
        "halved four-tone reduction the legibility pass measures on. Read "
        "across: the two figures must stay the same role, and the pass "
        "asserts exactly that: every within-role distance smaller than the "
        "smallest cross-role one.",
        "",
    ]
    for style in styles.STYLES:
        lines += [f"### {style.label}", "",
                  f"![{style.name} figures]({pages['figures_' + style.name]})",
                  ""]
    lines += [
        "## Scene backdrops",
        "",
        "What a conversation between maps is drawn against. A backdrop is a "
        "run of flat horizontal bands in master palette entries and nothing "
        "else. No pixels, so it costs no tile, no TMEM and no VRAM on any "
        "client. The rows are counted in twenty-eighths, the coarsest grid "
        "any client lays a scene out on. Every band is held to "
        f"{backdrops.MIN_CONTRAST}:1 against the words a scene puts on it, "
        "which is why this is a library of night and interior backdrops: a "
        "dawn sky cannot carry cream letters. See "
        "`placeholder_art/backdrops.py`.",
        "",
        f"![scene backdrops]({pages['backdrops']})",
        "",
    ]
    return "\n".join(lines) + "\n"


def _roster_markdown(units: Dict[str, str]) -> str:
    """``ROSTER.md``: one row per unit, every archetype in every style.

    The counts are measured off the registry rather than stated, so a style
    commissioned later moves the numbers with no edit here.
    """
    total = len(styles.STYLES) * len(characters.ARCHETYPE_ORDER)
    lines: List[str] = [
        "# Unit roster review",
        "",
        "<!-- Generated by tools/placeholder_art/generate.py. Do not edit. -->",
        "",
        "One row per unit: every archetype in every style, at one size. "
        "`GALLERY.md` cuts the same",
        "library by asset kind, and is the page to read to see what a profile "
        "costs or what a transition looks like. This one exists to be judged:",
        # Counted rather than spelled, for the reason the docstring gives: a
        # style commissioned later moves this sentence with no edit here. It
        # said "four styles is thirty-two units" for three styles longer than
        # that was true.
        f"{len(characters.ARCHETYPE_ORDER)} archetypes times "
        f"{len(styles.STYLES)} styles is {total} units, and this is "
        "all of them, in one table, at one size.",
        "",
        "## Coverage",
        "",
        f"- **2D: {total} of {total}.** Every archetype is drawn in every "
        "style, standing and in motion, in all six faction colours. The "
        "registry refuses a style that is missing one.",
        "",
        "| Style | Units |",
        "| --- | --- |",
    ]
    for style in styles.STYLES:
        lines.append(
            f"| {style.label} | {len(characters.ARCHETYPE_ORDER)} of "
            f"{len(characters.ARCHETYPE_ORDER)} |")

    lines += [
        "",
        "## How to read a row",
        "",
        "The 2D half carries three things, and each is there for a different "
        "question. The **standing sprite**, largest, is the unit's identity and "
        "is",
        "frame 0 of every sequence it has. The **four animation cells** beside "
        "it, at half that size, are what make it a unit rather than a "
        "portrait: a",
        "pose is applied to the body the archetype's own routine drew, so the "
        "question they answer is whether it stays the same figure while it "
        "moves.",
        "The **six faction colours**, at native size, are asked to prove only "
        "that the recolouring holds. The whole menu at every profile is a "
        "section of",
        "`GALLERY.md`, and repeating it here at any larger size would have "
        "pushed the row off the side of the page.",
        "",
        "All of it is drawn at the reference profile in "
        f"`{characters.FACTION_COLOURS[UNIT_FACTION].name}`, which is the "
        "faction every other measurement in this pipeline is taken on.",
        "",
        "## The roster",
        "",
        "| Style | Unit | Standing, in motion, recoloured |",
        "| --- | --- | --- |",
    ]
    for style in styles.STYLES:
        for archetype in characters.ARCHETYPE_ORDER:
            key = f"{style.name}_{archetype}"
            label = characters.ARCHETYPES[archetype].label
            lines.append(
                f"| {style.label} | **{label}** | "
                f"![{key}]({units[key]}) |")
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def write(output: Path, assets: Sequence[object],
          converted: Dict[str, Dict[str, profiles.Converted]]) -> int:
    """Write every contact sheet and both pages. Returns the file count."""
    directory = output / "gallery"
    pages: Dict[str, str] = {}
    written = 0

    def emit(key: str, filename: str, sheet: Sheet) -> None:
        nonlocal written
        pngio.write(directory / filename, sheet.encode())
        pages[key] = f"gallery/{filename}"
        written += 1

    emit("palette", "palette.png", _palette_panel())
    emit("backdrops", "backdrops.png", _backdrop_panel())
    for profile in profiles.PROFILES:
        emit(f"map_{profile.name}", f"sample_map_{profile.name}.png",
             _map_panel(profile, converted[profile.name]["sample_map.png"]))
    for theme in themes.THEMES:
        emit(f"theme_{theme.name}", f"theme_{theme.name}.png",
             _theme_panel(theme, converted["modern"]))
    for name in terrain.TERRAIN_ORDER:
        emit(f"terrain_{name}", f"terrain_{name}.png",
             _terrain_panel(name, converted))
        emit(f"blob_{name}", f"blob_{name}.png",
             _blob_panel(name, converted["modern"]))
    for over, under in terrain.TRANSITION_PAIRS:
        emit(f"transition_{over}_over_{under}",
             f"transition_{over}_over_{under}.png",
             _transition_panel(over, under))
    for archetype in characters.ARCHETYPE_ORDER:
        emit(f"character_{archetype}", f"character_{archetype}.png",
             _character_panel(archetype, converted))
    for style in styles.STYLES:
        emit(f"frames_{style.name}", f"frames_{style.name}.png",
             _frames_panel(style, profiles.PROFILES[0], converted["modern"]))
    for style in styles.STYLES:
        emit(f"figures_{style.name}", f"figures_{style.name}.png",
             _figures_panel(style, converted))

    (output / "GALLERY.md").write_text(_markdown(pages), encoding="utf-8")
    return written + 1 + _write_roster(output, converted)


def _write_roster(output: Path,
                  converted: Dict[str, Dict[str, profiles.Converted]]) -> int:
    """Write ``ROSTER.md`` and its panels. Returns the file count."""
    directory = output / "gallery"
    reference = converted[profiles.PROFILES[0].name]
    units: Dict[str, str] = {}
    written = 0
    for style in styles.STYLES:
        for archetype in characters.ARCHETYPE_ORDER:
            key = f"{style.name}_{archetype}"
            filename = f"unit_{key}.png"
            pngio.write(directory / filename,
                        _unit_panel(style, archetype, reference).encode())
            units[key] = f"gallery/{filename}"
            written += 1
    (output / "ROSTER.md").write_text(_roster_markdown(units),
                                      encoding="utf-8")
    return written + 1
