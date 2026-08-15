# SPDX-License-Identifier: MIT
"""Seam and adjacency verification, run as part of every build.

A build fails rather than shipping art that breaks any of these.

**Seamlessness.** A base tile is meant to be repeated, so its right column must
join its left column, and its bottom row its top row, as smoothly as any
interior pair. Measured, not eyeballed: the mean luminance step across the wrap
is compared against the mean interior step. Checked at native resolution and
again in each profile's own colours, because a reduction that flattened the
texture could create a step that was not there before.

**Autotiling survives the reduction.** The load-bearing question for the
low-end profiles is whether quantising tiles *independently* gives the same
result as quantising a whole composed scene. If it does, tiles can be selected
and blitted at runtime with no seam; if it does not, the boundary between two
tiles will show. A test scene is composed per terrain, converted whole, and
compared pixel-for-pixel against the same scene assembled out of the profile's
own converted sheet.

That check is only meaningful where a profile's mapping can depend on pixel
position, which means downscaling and ordered dithering. A pure palette remap
(the CI4 subset) is position-independent by construction and is reported as
skipped rather than silently passed.

**Mask table sanity.** The 47-variant reduction and the 256-entry lookup are
checked against the rules in :mod:`.autotile` directly.

**The glTF export round-trips.** The exported models are read back off disk,
part by part, and the eight integers of every part are reconstructed from the
geometry alone and required to be identical to what :mod:`.meshes` holds. This
is the check the Blender route rests on: an export that could not be read back
to the authored table would mean the format cannot carry the mesh rules. See
:func:`check_gltf_round_trip`.

**PlayStation loadability.** The claim that the ``n64_ci4`` profile is already
a PlayStation texture page plus CLUT rests on the master palette surviving
15-bit colour without a single merge. That is asserted rather than assumed, and
so is the packing the console reads. See :func:`check_playstation`.

**Legibility, as separation.** The pass that has forced a redraw in every style
commission reduces each sprite to a 16x16 four-shade silhouette and measures
the closest pair. It lived in a paragraph of
``tools/placeholder_art/README.md`` and in nobody's code until figures arrived.
It is now :func:`check_figure_separation` and it is the one thing that had to be
re-specified before a second figure could be drawn at all. See that function for
the contradiction it resolves.
"""

from __future__ import annotations

import itertools
import json
import pathlib
import struct
from typing import Callable, Dict, List, Mapping, Optional, Sequence, Tuple

from . import (autotile, characters, figures, frames, gltf,
               meshes, palette, playstation_header, profiles, raster, scene,
               shimmer, styles, terrain, themes)
from .raster import Canvas
from .rng import Rng, seed_of

#: How much worse the wrap-around step may be than the interior average before
#: a tile is treated as non-periodic. A tile is a finite sample of a periodic
#: field, not a constant, so some slack is required.
SEAM_TOLERANCE = 1.75

#: Side length of the per-terrain scene used for the adjacency check.
TEST_GRID_SIZE = 6


class VerificationError(AssertionError):
    """Raised when generated art violates a seam or adjacency guarantee."""


# ---------------------------------------------------------------------------
# Seamlessness
# ---------------------------------------------------------------------------


def _seam_report(width: int, height: int, tone: Sequence[float], label: str) -> None:
    def at(x: int, y: int) -> float:
        return tone[y * width + x]

    def mean(values: List[float]) -> float:
        return sum(values) / len(values) if values else 0.0

    interior_x = mean([abs(at(x, y) - at(x - 1, y))
                       for y in range(height) for x in range(1, width)])
    wrap_x = mean([abs(at(0, y) - at(width - 1, y)) for y in range(height)])
    interior_y = mean([abs(at(x, y) - at(x, y - 1))
                       for y in range(1, height) for x in range(width)])
    wrap_y = mean([abs(at(x, 0) - at(x, height - 1)) for x in range(width)])

    for axis, wrap, interior in (("horizontal", wrap_x, interior_x),
                                 ("vertical", wrap_y, interior_y)):
        limit = interior * SEAM_TOLERANCE + 0.01
        if wrap > limit:
            raise VerificationError(
                f"{label}: {axis} wrap step {wrap:.4f} exceeds {limit:.4f} "
                f"(interior step {interior:.4f}); the tile is not periodic"
            )


def check_seamless(canvas: Canvas, label: str) -> None:
    """Assert a native canvas tiles without a visible join."""
    _seam_report(canvas.width, canvas.height,
                 [palette.luminance(index) for index in canvas.data], label)


def check_tiles_after_conversion(profile: profiles.Profile, canvas: Canvas,
                                 label: str) -> None:
    """Assert a converted tile still repeats seamlessly.

    Exact rather than statistical: convert the tile on its own, convert a two
    by two block of the same tile, and require the second to be the first
    repeated. A downscale window that straddled a tile edge, or a dither phase
    that restarted per tile, would break the equality immediately. The
    luminance heuristic used at native resolution is too blunt here, because a
    handful of surviving colours leave most interior steps at zero.
    """
    single = profiles.convert(canvas, profile)
    block = profiles.convert(canvas.tiled(2, 2), profile)
    for y in range(block.height):
        for x in range(block.width):
            expected = single.indices[(y % single.height) * single.width
                                      + (x % single.width)]
            if block.indices[y * block.width + x] != expected:
                raise VerificationError(
                    f"{label}: repeating the tile changes pixel ({x}, {y}); "
                    f"the {profile.name} reduction is not tile-periodic"
                )


# ---------------------------------------------------------------------------
# Autotiling
# ---------------------------------------------------------------------------


def check_mask_tables() -> None:
    """Assert the 47-variant reduction and the 256-entry lookup agree."""
    if len(autotile.BLOB_MASKS) != 47:
        raise VerificationError(
            f"expected 47 blob variants, found {len(autotile.BLOB_MASKS)}")
    for mask in range(256):
        variant = autotile.MASK_TO_VARIANT[mask]
        if autotile.BLOB_MASKS[variant] != autotile.canonicalise(mask):
            raise VerificationError(
                f"mask {mask} maps to variant {variant}, which is not its "
                f"canonical form {autotile.canonicalise(mask)}")
    for bit, (_, first, second) in autotile.DIAGONALS.items():
        for mask in range(256):
            canonical = autotile.canonicalise(mask)
            if canonical & bit and not (canonical & first and canonical & second):
                raise VerificationError(
                    f"mask {mask} kept diagonal bit {bit} without both "
                    "adjoining cardinals")


def test_grid(name: str) -> List[List[bool]]:
    """A deterministic occupancy pattern that exercises many mask variants."""
    rng = Rng(seed_of("verify-grid", name))
    grid = [[rng.chance(0.58) for _ in range(TEST_GRID_SIZE)]
            for _ in range(TEST_GRID_SIZE)]
    # Guarantee at least one fully surrounded cell and one isolated cell.
    for y in range(1, 4):
        for x in range(1, 4):
            grid[y][x] = True
    grid[TEST_GRID_SIZE - 1][TEST_GRID_SIZE - 1] = True
    grid[TEST_GRID_SIZE - 2][TEST_GRID_SIZE - 1] = False
    grid[TEST_GRID_SIZE - 1][TEST_GRID_SIZE - 2] = False
    return grid


def _mask_at(grid: List[List[bool]], x: int, y: int) -> int:
    def same(dx: int, dy: int) -> bool:
        nx, ny = x + dx, y + dy
        return (0 <= ny < len(grid) and 0 <= nx < len(grid[ny]) and grid[ny][nx])

    return autotile.mask_from(same)


def compose_test_scene(name: str) -> Canvas:
    """Assemble the test grid out of native blob tiles, as a renderer would."""
    grid = test_grid(name)
    size = terrain.TILE
    canvas = Canvas(TEST_GRID_SIZE * size, TEST_GRID_SIZE * size)
    for y in range(TEST_GRID_SIZE):
        for x in range(TEST_GRID_SIZE):
            if not grid[y][x]:
                continue
            canvas.blit(terrain.blob_tile(name, _mask_at(grid, x, y)),
                        x * size, y * size)
    return canvas


def _assembled_from_sheet(sheet: profiles.Converted, name: str, size: int,
                          columns: int, blank: int) -> List[int]:
    """Assemble the same scene from a profile's converted blob sheet."""
    grid = test_grid(name)
    width = TEST_GRID_SIZE * size
    pixels = [blank] * (width * width)
    for cell_y in range(TEST_GRID_SIZE):
        for cell_x in range(TEST_GRID_SIZE):
            if not grid[cell_y][cell_x]:
                continue
            variant = autotile.MASK_TO_VARIANT[_mask_at(grid, cell_x, cell_y)]
            left = (variant % columns) * size
            top = (variant // columns) * size
            for y in range(size):
                for x in range(size):
                    value = sheet.indices[(top + y) * sheet.width + left + x]
                    if value == blank:
                        continue
                    pixels[(cell_y * size + y) * width + cell_x * size + x] = value
    return pixels


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------


def check_native() -> None:
    """Every check that applies to the native, pre-profile art.

    Run for every theme: a theme keeps the geometry and changes the colours,
    and the seam measurement is a luminance one, so a ramp whose steps run the
    wrong way would show up here as a tile that does not join itself.
    """
    check_mask_tables()
    for theme in themes.THEMES:
        with terrain.rendering(theme):
            for name in terrain.TERRAIN_ORDER:
                for variant in range(terrain.BASE_VARIANTS):
                    check_seamless(
                        terrain.base_tile(name, variant),
                        f"native {theme.name} {name} base variant {variant}")
                check_seamless(
                    terrain.blob_tile(name, 0xFF),
                    f"native {theme.name} {name} interior blob variant")


def check_style_rosters(profile: profiles.Profile,
                        converted: Dict[str, profiles.Converted]) -> None:
    """Assert every style holds every archetype in every faction colour.

    The sprite key is style by archetype by faction colour, and it is only
    usable if it factors: a client indexes it without a per-style roster table,
    and a class name resolves to the same archetype whatever style draws it.
    A style shipped with one routine missing would make the table ragged, so
    the build fails here rather than leaving an archetype undrawn.

    The same refusal is applied to time. Every sprite carries a sequence sheet
    of exactly :data:`~.frames.FRAME_COUNT` cells, in the one order every client
    indexes by position, so a style cannot ship a shorter animation than the
    default any more than it can ship a shorter roster.

    And to the body. Every archetype carries every entry of
    :data:`~.figures.FIGURE_ORDER`, each with its own full sequence, whether the
    style's commission has drawn that figure or its stand-in transform is still
    drawing it. So a style *cannot* hold fewer, and this is where "cannot" is
    checked rather than assumed.
    """
    cells = frames.FRAME_COUNT
    for style in styles.STYLES:
        style_suffix = styles.asset_suffix(style)
        for archetype in characters.ARCHETYPE_ORDER:
            for colour in characters.FACTION_COLOURS:
                for shape in figures.FIGURE_ORDER:
                    suffix = f"{style_suffix}{shape.suffix}"
                    path = f"characters/{archetype}_{colour.name}{suffix}.png"
                    if path not in converted:
                        raise VerificationError(
                            f"{profile.name}: style {style.name} has no "
                            f"{shape.name} figure of {archetype} in "
                            f"{colour.name}; every style holds every archetype "
                            "in every faction colour and every figure"
                        )
                    strip = (f"characters/{archetype}_{colour.name}"
                             f"{suffix}_frames.png")
                    if strip not in converted:
                        raise VerificationError(
                            f"{profile.name}: style {style.name} has no "
                            f"sequence sheet for the {shape.name} figure of "
                            f"{archetype} in {colour.name}; every style ships "
                            "every frame of every figure"
                        )
                    sheet = converted[strip]
                    standing = converted[path]
                    if sheet.width != standing.width * cells:
                        raise VerificationError(
                            f"{profile.name}: {strip} is {sheet.width} wide; a "
                            f"sequence sheet is {cells} cells of "
                            f"{standing.width}, in the order "
                            f"{', '.join(frames.FRAME_NAMES)}"
                        )
                    if sheet.height != standing.height:
                        raise VerificationError(
                            f"{profile.name}: {strip} is {sheet.height} tall; a "
                            f"sequence sheet is one row of the standing "
                            f"sprite's own {standing.height}"
                        )


# ---------------------------------------------------------------------------
# Legibility, as separation
# ---------------------------------------------------------------------------

#: How many of the native silhouette's pixels a second figure must move before
#: it counts as a second figure at all. Measured with headroom: the shipped
#: pinch moves between 14 and 37 of a roughly 1,000-pixel cell across all
#: fifty-six archetype-style pairs, so a floor of eight refuses a transform that
#: does nothing without refusing one that does half of what this one does.
#:
#: It is asserted on the **native** silhouette and not on the reduction, and the
#: split is the point. "Is there a difference at all" is a question about the
#: size the art is drawn and reviewed at; "can two of these be confused when they
#: are small" is a question about the reduction. Asserting both at 16x16 would
#: have meant refusing art that is plainly different on every screen that ships,
#: because the halving erases a one-column move about half the time.
FIGURE_SILHOUETTE_FLOOR = 8


def check_kit_shared() -> Dict[str, object]:
    """Assert a role's two figures carry one kit, and say which roles are drawn.

    The guarantee a second *drawing* had to buy back. While a figure was a
    transform it was free, because one routine drew the shield and the transform
    moved those pixels blind. The moment a second figure became a second routine
    it became something two routines could lose. A knight whose second figure
    carries a slightly different shield is two units, not one unit drawn twice.

    :func:`~.characters.kit` and :func:`~.characters.tracing_kit` make it
    measurable rather than asserted: every call to a role's kit is recorded with
    its arguments while each figure is drawn, and the two recordings must be
    equal. Construction already stops most of it, since a kit method takes no
    figure and cannot know of one. What construction cannot stop is a routine
    calling the right helper with a number changed by one, and this does.

    One faction colour, because what is compared is the *call*, and the colour
    is one of its arguments rather than a separate axis of it.

    Also refuses a commissioned role with no kit at all. Every role in this
    library is known by something it carries or wears; a ``draw_second`` that
    records no kit call is one that copied the equipment rather than shared it,
    and that is the failure this whole mechanism exists to prevent.
    """
    check_kit_shared_refuses()
    return {
        "drawn_second_figures": {
            style.name: _kit_of_one_roster(style.name, style.archetypes)
            for style in styles.STYLES
        }
    }


def _kit_of_one_roster(where: str,
                       roster: Mapping[str, characters.Archetype]
                       ) -> List[str]:
    """One roster's kit comparison; the names of the roles drawn twice."""
    colour = characters.FACTION_COLOURS[0]
    first = figures.DEFAULT_FIGURE.name
    commissioned: List[str] = []
    for name, archetype in sorted(roster.items()):
        traces: Dict[str, Tuple[object, ...]] = {}
        for shape in figures.FIGURE_ORDER:
            with characters.tracing_kit() as trace:
                characters.body_of(archetype, colour, shape)
            traces[shape.name] = tuple(trace)
        if getattr(archetype, figures.SECOND_ROUTINE, None) is not None:
            commissioned.append(name)
            if not traces[first]:
                raise VerificationError(
                    f"{where}: {name} is drawn twice but declares no kit; a "
                    f"second figure shares its role's equipment by calling it, "
                    f"and a role with none has nothing to share"
                )
        for shape in figures.FIGURE_ORDER[1:]:
            if traces[shape.name] != traces[first]:
                raise VerificationError(
                    f"{where}: the {shape.name} figure of {name} draws a "
                    f"different kit from its first figure, "
                    f"{traces[shape.name]!r} against {traces[first]!r}. A "
                    f"role's equipment is drawn once for both of its figures, "
                    f"or the two are two units rather than one unit drawn twice"
                )
    return commissioned


def check_kit_shared_refuses() -> None:
    """Provoke the kit check in both of the ways a commission can lose a kit.

    A check that has never refused is a check nobody should trust, and this one
    guards the property that a second *drawing*, unlike the transform it
    replaced, is able to break silently.

    **The copied kit.** A role whose second routine draws the equipment inline
    rather than calling the shared method records no kit call at all where its
    first figure records some. That is the failure this mechanism exists to
    prevent, and it is the one an author reaches for first.

    **The drifted kit.** A role whose second routine calls the right method with
    one number changed by one: a shield half a pixel further out. Construction
    cannot catch it, because the call is genuinely the shared one; only the
    recording can.
    """

    class Shielded(characters.Archetype):
        name = "knight"
        label = "Knight"

        @characters.kit
        def shield(self, canvas: Canvas, across: float) -> None:
            raster.disc(canvas, across, 17.0, 4.4, palette.RAMPS["steel"])

        def draw(self, canvas: Canvas, faction: characters.FactionColour) -> None:
            characters.torso(canvas, faction.colours)
            self.shield(canvas, 10.0)

    class Copied(Shielded):
        def draw_second(self, canvas: Canvas,
                        faction: characters.FactionColour) -> None:
            characters.torso(canvas, faction.colours)
            raster.disc(canvas, 10.0, 17.0, 4.4, palette.RAMPS["steel"])

    class Drifted(Shielded):
        def draw_second(self, canvas: Canvas,
                        faction: characters.FactionColour) -> None:
            characters.torso(canvas, faction.colours)
            self.shield(canvas, 10.5)

    for roster, wanted, what in (
        ({"knight": Copied()}, "different kit",
         "a second figure that drew its role's equipment itself"),
        ({"knight": Drifted()}, "different kit",
         "a second figure that called the shared kit with a number moved"),
    ):
        try:
            _kit_of_one_roster("provoked", roster)
        except VerificationError as refusal:
            if wanted in str(refusal):
                continue
            raise VerificationError(
                f"the kit check refused {what}, but for the wrong reason: "
                f"expected a refusal mentioning {wanted!r}, got {refusal}"
            ) from None
        raise VerificationError(
            f"the kit check accepted {what}; a check that has never refused "
            f"is a check nobody should trust"
        )


def silhouette(indices: Sequence[int], transparent: Sequence[int] = (0,)
               ) -> Tuple[bool, ...]:
    """A converted sprite reduced to the only thing this pass looks at.

    Opacity, and nothing else. The shades are deliberately discarded: what the
    pass measures is whether two *shapes* can be told apart, and a figure that
    differed only in tone would be a figure nobody could find on a board.
    """
    blank = set(transparent)
    return tuple(index not in blank for index in indices)


def silhouette_distance(left: Sequence[bool], right: Sequence[bool]) -> int:
    """The pixels on which exactly one of two silhouettes is opaque."""
    return sum(1 for a, b in zip(left, right) if a != b)


def _closest_cross_role(masks: Dict[Tuple[str, str], Tuple[bool, ...]]
                        ) -> Tuple[int, str, str]:
    """The nearest pair of sprites belonging to two *different* roles."""
    return min(
        (silhouette_distance(masks[left], masks[right]),
         f"{left[0]} ({left[1]})", f"{right[0]} ({right[1]})")
        for left, right in itertools.combinations(sorted(masks), 2)
        if left[0] != right[0]
    )


def check_figure_separation(
    style: styles.Style,
    reduced: Dict[Tuple[str, str], Tuple[bool, ...]],
    native: Dict[Tuple[str, str], Tuple[bool, ...]],
    colour: str = "",
    generated: Sequence[str] = (),
) -> Dict[str, object]:
    """Assert a style's figures are legible as roles and as figures.

    The contradiction this resolves
    -------------------------------
    A legibility pass that measures the **closest pair** among a style's eight
    reduced silhouettes and treats a near-collision as a redraw trigger fires
    immediately once there are sixteen, on exactly the pairs that are supposed
    to be similar, because two figures of one role must read as that role.
    "These two must be distinguishable" and "these two must read as the same
    thing" are opposite requirements for one measurement, and no amount of
    drawing skill resolves a contradiction in a gate.

    So the pass is a **separation** check, which is one ordering over the same
    data rather than two rules bolted together: **every within-role distance
    must be smaller than the smallest cross-role distance.** A role's two
    figures are always closer to each other than any two roles are to each
    other. That captures both requirements exactly, and it is strictly harder
    than a closest-pair test: eight silhouettes give 28 pairs, sixteen give
    120, of which 112 are cross-role against the 28 a closest-pair test binds.

    It needs a second bound, and pretending otherwise would be the whole trick
    missed. Separation alone is satisfied *perfectly* by a figure transform that
    does nothing: zero is smaller than everything. So the pass also asserts a
    floor, on the native silhouette, for the reason
    :data:`FIGURE_SILHOUETTE_FLOOR` states.

    Two bounds, one sentence: **the two figures of a role must be visibly
    different, and still closer to each other than any two roles are.**

    The two bounds do not have the same reach, and the difference is exact.
    **The cross-role bound applies to every sprite there is**, generated or
    provided: a unit that cannot be told from another role is unreadable however
    it got drawn, and that is the pass the seven commissions ran. **The
    within-role bound applies only where both figures came from one drawing.**
    It is a claim about the *transform*, and a role whose first figure was
    provided (:mod:`.provided`) has a second figure the generator derived from a
    drawing the author replaced. Comparing those two measures two artists rather
    than one body, and the number means nothing. ``generated`` names the
    archetypes for which the claim can be made; the rest are reported as
    unjudged rather than passed or failed.

    That is a scope, not an escape, and it closes when the figure axis becomes
    authorable: a submission cannot name a second figure
    (``provided.specifications()`` carries the key and refuses it by name), so
    the second figure of a provided role is art no project can select and
    nothing will draw. Making that key replaceable and re-widening this bound go
    together.

    ``reduced`` and ``native`` are keyed by ``(archetype, figure)``, and are one
    faction colour's, which ``colour`` names for the refusals. Returns the
    measurement, so the caller can publish it rather than re-derive it.
    """
    where = f"{style.name}/{colour}" if colour else style.name
    judged = set(generated) if generated else set(characters.ARCHETYPE_ORDER)
    # Every *pair* of a role's figures, not each figure against the first. With
    # two figures those are the same set; with three they are not, and a third
    # figure that collided with the second while differing from the first would
    # be exactly the failure this pass exists to catch.
    within: Dict[str, int] = {}
    for archetype in sorted(judged):
        for shape in figures.FIGURE_ORDER[1:]:
            moved = silhouette_distance(
                native[(archetype, figures.DEFAULT_FIGURE.name)],
                native[(archetype, shape.name)])
            if moved < FIGURE_SILHOUETTE_FLOOR:
                raise VerificationError(
                    f"{where}: the {shape.name} figure of {archetype} "
                    f"moves {moved} pixels of its native silhouette, under the "
                    f"floor of {FIGURE_SILHOUETTE_FLOOR}; a figure nobody can "
                    f"see is not a second figure"
                )
        within[archetype] = max(
            silhouette_distance(reduced[(archetype, left.name)],
                                reduced[(archetype, right.name)])
            for left, right in itertools.combinations(figures.FIGURE_ORDER, 2)
        )

    # The cross-role bound is measured over *every* sprite, judged or not: a
    # provided figure still has to be tellable from the other seven roles, which
    # is the pass every style commission ran before figures existed.
    closest, left, right = _closest_cross_role(reduced)
    widest_role, widest = max(within.items(),
                              key=lambda item: (item[1], item[0]),
                              default=("", 0))
    if widest >= closest:
        raise VerificationError(
            f"{where}: the figures of {widest_role} differ by {widest} of "
            f"256 reduced pixels while {left} and {right}, two different "
            f"roles, differ by only {closest}. A role's two figures must stay "
            f"closer to each other than any two roles are, or the second "
            f"figure has stopped reading as the role"
        )
    return {
        "faction_colour": colour,
        "closest_cross_role": closest,
        "closest_cross_role_pair": [left, right],
        "widest_within_role": widest,
        "widest_within_role_archetype": widest_role,
        "within_role": dict(sorted(within.items())),
        "unjudged": sorted(set(characters.ARCHETYPE_ORDER) - judged),
    }


def _occupied(canvas: Canvas) -> Tuple[int, int, int, int]:
    """The box a drawn sprite actually fills: left, top, right, bottom."""
    columns = [i % canvas.width for i, index in enumerate(canvas.data)
               if index != palette.TRANSPARENT]
    rows = [i // canvas.width for i, index in enumerate(canvas.data)
            if index != palette.TRANSPARENT]
    return (min(columns), min(rows), max(columns), max(rows))


def _materials(canvas: Canvas) -> Tuple[str, ...]:
    """The shading ramps a sprite names, which is what a cell's sixteen colours
    are budgeted in rather than colours."""
    return tuple(sorted({palette.RAMP_OF_INDEX[index][0]
                         for index in canvas.data
                         if index != palette.TRANSPARENT}))


def _figure_room_refusal(where: str, first: Canvas,
                         second: Canvas) -> Optional[str]:
    """Why ``second`` is not a redistribution of ``first``, or ``None``.

    Both bounds :func:`check_figure_room` states, on one pair, returned rather
    than raised so that :func:`check_figure_room_refuses` can provoke the same
    arithmetic the library is judged by instead of a copy of it.
    """
    room = _occupied(first)
    box = _occupied(second)
    if (box[0] < room[0] or box[1] < room[1]
            or box[2] > room[2] or box[3] > room[3]):
        return (f"{where} fills {box} where its first figure fills {room}; a "
                f"second figure redistributes the pixels its role already "
                f"spends and may not grow the box they sit in, or the sequence "
                f"has nowhere to pose it")
    extra = sorted(set(_materials(second)) - set(_materials(first)))
    if extra:
        return (f"{where} names {extra}, which its first figure does not; a "
                f"second figure may not spend a material its first did not, "
                f"because the two share one sixteen-colour cell budget")
    return None


def check_figure_room(native: Dict[str, Canvas],
                      provided_paths: Sequence[str] = (),
                      ) -> Dict[str, List[str]]:
    """Assert a second figure spends its role's cell rather than more of it.

    Two bounds, and both are the second-figure vocabulary stated as a refusal
    rather than as advice.

    **The room.** A second figure's occupied box must sit inside its first
    figure's, on the **standing** cell, which is the one every sequence cell is
    displaced from. The margin rule already says the standing art fills its
    cell, so a second figure has no border to grow into; that is the reason a
    second figure is a **redistribution**, and a role that wants a wider hem has
    to find the columns at its waist.

    It is deliberately the standing cell and not every cell, and the difference
    was measured rather than assumed. ``walk_contact`` steps everything at or
    below row 24 outward by one column, so a second figure with a wider hem
    *there* ends that cell one column further out than the same pose carries
    its first figure, as `medieval`'s mage, healer and commander all do. That
    is not a margin failure and demanding otherwise would cost real drawing:
    the widest such case sits at **column 4 of 32**, and the margin rule's own
    arithmetic only forbids columns 0 and 31 below row 24. What guarantees the
    cell itself is :func:`~.frames.displace`, which raises rather than dropping
    a pixel, on every figure's every cell in every build.

    **The materials.** A second figure may name no shading ramp its first figure
    does not. A sprite is budgeted at about five materials because the `n64_ci4`
    profile caps a cell at sixteen colours, and a second figure that added one
    would spend a budget its first figure had already spent. It is also what
    makes "zero appended palette entries" hold for a *drawing* rather than only
    for a transform: hair goes in a ramp the sprite already spends, which is the
    same trick `sengoku` used for its lacquer.

    **Both bounds are within-role, so both have
    :func:`check_figure_separation`'s reach and not a wider one.** Each compares
    a role's second figure against its first and means something only where one
    drawing made both. ``provided_paths`` names the assets a project stood in
    for; a role with one of them in it is reported as unjudged rather than
    passed or failed, per style, and the caller publishes that list.

    Measured on the repository's own worked example, which is what made the
    scope necessary rather than tidy: standing
    ``art/examples/lantern-keeper`` into ``pirates``/`mage`/`blue` leaves the
    second figure exactly where the generator drew it, at (7, 2, 24, 31), and
    replaces the box it is measured against with the submission's (8, 3, 25,
    29). Nothing grew. A smaller first figure arrived from another hand, and the
    materials half says the same thing twice over: the submission spends
    `amber` and `bone` where the generated figure spent `dirt`, `gold`,
    `leather` and `sand`, so four ramps read as *added* by a second figure that
    was never redrawn. Neither number is about one hand's second drawing of a
    role, which is the only thing these two bounds can speak about.

    It is a scope and not an escape, and it closes on exactly the condition
    :func:`check_figure_separation` names: a submission cannot name a second
    figure (``provided.specifications()`` carries the key and refuses it by
    name), so the second figure of a provided role is art no project can
    select. A wave that makes the figure axis authorable must make that key
    replaceable or re-widen both bounds.
    """
    stood_in = set(provided_paths)
    unjudged: Dict[str, List[str]] = {}
    for style in styles.STYLES:
        suffix = styles.asset_suffix(style)
        skipped: List[str] = []
        for archetype in characters.ARCHETYPE_ORDER:
            for colour in characters.FACTION_COLOURS:
                def path(shape: figures.Figure) -> str:
                    return (f"characters/{archetype}_{colour.name}"
                            f"{suffix}{shape.suffix}.png")

                if any(path(shape) in stood_in
                       for shape in figures.FIGURE_ORDER):
                    skipped.append(f"{archetype}/{colour.name}")
                    continue
                first = native[path(figures.DEFAULT_FIGURE)]
                for shape in figures.FIGURE_ORDER[1:]:
                    refusal = _figure_room_refusal(
                        f"{style.name}/{archetype}/{colour.name}: the "
                        f"{shape.name} figure", first, native[path(shape)])
                    if refusal:
                        raise VerificationError(refusal)
        unjudged[style.name] = skipped
    return unjudged


def _any_drawn_index(canvas: Canvas) -> int:
    """Any opaque index the canvas already draws.

    The room provocation has to move the box without moving the materials, or
    the two provocations would not be separable.
    """
    for index in canvas.data:
        if index != palette.TRANSPARENT:
            return index
    raise VerificationError("a drawn sprite with no opaque pixel")


def _room_refuses(where: str, first: Canvas, second: Canvas, wanted: str,
                  what: str) -> None:
    refusal = _figure_room_refusal(where, first, second)
    if refusal is None:
        raise VerificationError(
            f"the room bound accepted {what}; a bound that has never refused "
            f"in that direction is a bound nobody should trust")
    if wanted not in refusal:
        raise VerificationError(
            f"the room bound refused {what}, but for the wrong reason: "
            f"expected a refusal mentioning {wanted!r}, got {refusal}")


def check_figure_room_refuses() -> None:
    """Provoke both of :func:`check_figure_room`'s bounds, in every build.

    A within-role bound scoped to the roles one hand drew cannot be seen to fail
    on a library one hand drew throughout, and a bound nothing has ever seen
    fail is worth nothing. So both halves are provoked here on a real role,
    through :func:`_figure_room_refusal`, which is the arithmetic the library is
    judged by rather than a copy of it.

    The two provocations are the two mistakes a second figure can make, each
    spelled as the smallest thing that is one: **one pixel outside the box**,
    which is a hem
    let out that was not taken in at the waist, and **one pixel in a ramp the
    role does not spend**, which is hair drawn in a new material. A pixel rather
    than a transform because both bounds measure a canvas and not a
    displacement: the box is opacity and the materials are the ramps the indices
    name, so the smallest counterexample is exact and stays exact when the
    drawing under it is revised.
    """
    style = styles.STYLES_BY_NAME[PROVOCATION_STYLE]
    colour = characters.FACTION_COLOURS[0].name
    for archetype in characters.ARCHETYPE_ORDER:
        first = characters.sprite(archetype, colour, style.archetypes, None,
                                  None)
        left, top, _, _ = _occupied(first)
        spare = sorted(set(palette.RAMPS) - set(_materials(first)) - {""})
        if left == 0 or not spare:
            continue
        where = f"{PROVOCATION_STYLE}/{archetype}/{colour}: the second figure"

        wider = first.copy()
        wider.data[top * first.width + left - 1] = _any_drawn_index(first)
        _room_refuses(where, first, wider, "may not grow the box",
                      "a second figure with one pixel outside its first's box")

        richer = first.copy()
        richer.data[top * first.width + left] = palette.RAMPS[spare[0]][0]
        _room_refuses(where, first, richer, "may not spend a material",
                      "a second figure drawn in a ramp its first never spends")
        return
    raise VerificationError(
        f"no {PROVOCATION_STYLE} role leaves a column outside its own box and a "
        f"ramp it does not spend, so the room bound cannot be provoked on a "
        f"real drawing and nothing here is measuring it")


def legibility_masks(style: styles.Style,
                     converted: Dict[str, profiles.Converted],
                     colour: str) -> Dict[Tuple[str, str], Tuple[bool, ...]]:
    """A style's reduced silhouettes, out of the art the build actually emitted.

    One faction colour, and :func:`check_legibility` calls this once for each of
    the six rather than picking one. Measured over the *generated* library all
    six give the same answer to the pixel, because the disc, the body and the
    ink outline are drawn in the same places whatever ramp fills them. But that
    is a property of the generator and not of the library, because a provided
    sprite (:mod:`.provided`) stands in for exactly one archetype, colour and
    style.
    Sampling one colour would leave the other five unmeasured the moment an
    author submitted anything, which is the one case where the pass most needs to
    look.
    """
    style_suffix = styles.asset_suffix(style)
    masks: Dict[Tuple[str, str], Tuple[bool, ...]] = {}
    for archetype in characters.ARCHETYPE_ORDER:
        for shape in figures.FIGURE_ORDER:
            path = (f"characters/{archetype}_{colour}"
                    f"{style_suffix}{shape.suffix}.png")
            asset = converted[path]
            masks[(archetype, shape.name)] = silhouette(
                asset.indices, asset.transparent)
    return masks


def check_legibility(native: Dict[str, Canvas],
                     converted: Dict[str, profiles.Converted],
                     provided_paths: Sequence[str] = (),
                     ) -> Dict[str, object]:
    """Run the separation check over every style and every faction colour.

    ``native`` is the pre-profile canvases by path and ``converted`` the
    :data:`.profiles.LEGIBILITY` reduction of the same paths, so the pass
    measures the art the build actually drew rather than a re-render of it.
    That reduction is passed in rather than taken here because the comparability
    of seven styles' accumulated floors rests on it never having changed: it is
    one reduction, defined in one place, and it ships nothing.

    **Every colour, not one.** The first version of this sampled ``blue`` and
    asserted separately that a reduced silhouette does not depend on its faction,
    which is true of the generated library, where all six were measured and
    agree to the pixel. It is false the moment a project provides art, because
    a submission stands in for exactly one archetype, colour and style. The
    provided-art suite found it by standing a sheet into one colour of one role
    and watching the assertion fire on the five it had not replaced. Measuring
    all six costs nothing worth counting (the reductions are already in hand)
    and removes both the shortcut and the claim that propped it up.

    The reported numbers are the binding ones: the smallest cross-role distance
    any colour produced, and the largest within-role distance any colour
    produced.

    ``provided_paths`` names the assets a project stood in for. Those roles keep
    the cross-role bound and lose the within-role ones, for the reason
    :func:`check_figure_separation` gives. The room bound of
    :func:`check_figure_room` is within-role too, so it takes the same scope and
    publishes the same list of roles it declined to judge.
    """
    check_legibility_refuses()
    check_figure_room_refuses()
    room_unjudged = check_figure_room(native, provided_paths)
    stood_in = set(provided_paths)
    report: Dict[str, object] = {}
    for style in styles.STYLES:
        style_suffix = styles.asset_suffix(style)
        rows: Dict[str, Dict[str, object]] = {}
        for colour in characters.FACTION_COLOURS:
            paths = {
                (archetype, shape.name):
                    f"characters/{archetype}_{colour.name}"
                    f"{style_suffix}{shape.suffix}.png"
                for archetype in characters.ARCHETYPE_ORDER
                for shape in figures.FIGURE_ORDER
            }
            shapes = {
                key: silhouette(native[path].data, (palette.TRANSPARENT,))
                for key, path in paths.items()
            }
            wholly_generated = [
                archetype for archetype in characters.ARCHETYPE_ORDER
                if not any(paths[(archetype, shape.name)] in stood_in
                           for shape in figures.FIGURE_ORDER)
            ]
            rows[colour.name] = check_figure_separation(
                style, legibility_masks(style, converted, colour.name), shapes,
                colour.name, wholly_generated)
        binding = min(rows.values(),
                      key=lambda row: int(row["closest_cross_role"]))
        widest = max(rows.values(),
                     key=lambda row: int(row["widest_within_role"]))
        report[style.name] = {
            "closest_cross_role": binding["closest_cross_role"],
            "closest_cross_role_pair": binding["closest_cross_role_pair"],
            "closest_cross_role_faction": binding["faction_colour"],
            "widest_within_role": widest["widest_within_role"],
            "widest_within_role_archetype": widest["widest_within_role_archetype"],
            "widest_within_role_faction": widest["faction_colour"],
            "room_unjudged": room_unjudged[style.name],
            "by_faction_colour": rows,
        }
    return report


#: The style the refusals are provoked on: the one with the least room. Its
#: closest cross-role pair is 12 of 256, the tightest floor in the library, so a
#: transform that ``medieval`` tolerates is one every other style tolerates by a
#: wider margin, and a bound demonstrated where it binds is a bound
#: demonstrated.
PROVOCATION_STYLE = "medieval"


def _provoked_masks(style: styles.Style,
                    body: Callable[[Canvas], Canvas],
                    ) -> Tuple[Dict[Tuple[str, str], Tuple[bool, ...]],
                               Dict[Tuple[str, str], Tuple[bool, ...]]]:
    """A style's eight roles drawn twice: as drawn, and under ``body``.

    The provoked second figure is spelled as a figure whose *routine* is one no
    archetype holds, so every role falls through to its stand-in and the stand-in
    is ``body``. That keeps the provocation on the same path a real figure takes
    rather than on a private one beside it.
    """
    profile = profiles.LEGIBILITY
    colour = characters.FACTION_COLOURS[0].name
    reduced: Dict[Tuple[str, str], Tuple[bool, ...]] = {}
    native: Dict[Tuple[str, str], Tuple[bool, ...]] = {}
    provoked = figures.Figure("provoked", "Provoked", "", "draw_provoked", body)
    for archetype in characters.ARCHETYPE_ORDER:
        for name, shape in ((figures.DEFAULT_FIGURE.name, None),
                            (figures.FIGURE_ORDER[1].name, provoked)):
            canvas = characters.sprite(archetype, colour, style.archetypes,
                                       None, shape)
            native[(archetype, name)] = silhouette(canvas.data,
                                                   (palette.TRANSPARENT,))
            converted = profiles.convert(canvas, profile, is_sprite=True)
            reduced[(archetype, name)] = silhouette(converted.indices,
                                                    converted.transparent)
    return reduced, native


def _refuses(style: styles.Style, body: Callable[[Canvas], Canvas],
             wanted: str, what: str) -> None:
    reduced, native = _provoked_masks(style, body)
    try:
        check_figure_separation(style, reduced, native)
    except VerificationError as refusal:
        if wanted in str(refusal):
            return
        raise VerificationError(
            f"the legibility pass refused {what}, but for the wrong reason: "
            f"expected a refusal mentioning {wanted!r}, got {refusal}"
        ) from None
    raise VerificationError(
        f"the legibility pass accepted {what}; a pass that has never refused "
        f"in that direction is a pass nobody should trust"
    )


def check_legibility_refuses() -> None:
    """Provoke the pass in both directions, because one bound is not the gate.

    The separation check and the visibility floor fail on opposite mistakes, and
    a suite that only ever saw the shipped figures would never have seen either
    fail. So both are provoked here, on :data:`PROVOCATION_STYLE`, out of the
    same drawing routines the library ships. Nothing is mocked, and what is
    measured is a real roster under a real transform.

    **Too weak to see.** A pinch confined to a single row moves four or five
    pixels of a native silhouette. It satisfies the separation ordering
    perfectly, because a difference of nearly nothing is smaller than every
    cross-role distance, and that is exactly why the ordering alone is not the
    gate.

    **Strong enough to break the role.** A three-column pinch over the whole
    body pulls a knight far enough that its two figures are further apart than
    two different roles are. Nothing here is a *bad drawing*; it is a good
    drawing of the wrong thing, which is the failure a second figure actually
    risks.
    """
    style = styles.STYLES_BY_NAME[PROVOCATION_STYLE]

    def one_row(canvas: Canvas) -> Canvas:
        return frames.displace(canvas, lambda x, y: (
            (-1 if x > figures.CENTRE_X else 1, 0)
            if y == figures.HIP_Y - 1 and abs(x - figures.CENTRE_X) >= 3
            else (0, 0)))

    def whole_body(canvas: Canvas) -> Canvas:
        return frames.displace(canvas, lambda x, y: (
            (-3 if x > figures.CENTRE_X else 3, 0)
            if abs(x - figures.CENTRE_X) >= 3 else (0, 0)))

    _refuses(style, one_row, "under the floor",
             "a figure that moves a handful of pixels on one row")
    _refuses(style, whole_body, "stopped reading as the role",
             "a figure pinched three columns over its whole body")


def check_shimmer(profile: profiles.Profile,
                  converted: Dict[str, profiles.Converted]) -> None:
    """Assert every theme's water can actually be cycled in this profile.

    The shimmer is a rotation of four palette entries, and four is the number
    every client agreed on (:mod:`.shimmer`). A theme whose water reached only
    three distinct entries in some profile would have to shimmer at a different
    period there, which is a per-theme, per-profile table nobody should have to
    hold, so this fails the build instead.

    It also asserts the rotation is *closed*: the entries it moves are entries
    the water sheet itself draws, so a rotation can never change a pixel that is
    not water.
    """
    if not profile.emits_palette:
        return
    for theme in themes.THEMES:
        suffix = themes.asset_suffix(theme)
        path = f"terrain/{shimmer.CYCLED_RAMP}_base{suffix}.png"
        sheet = converted.get(path)
        if sheet is None:
            raise VerificationError(
                f"{profile.name}: no {path}; the water shimmer has nothing to "
                "rotate")
        if profile.palette_mode != "subset":
            continue
        drawn = {int(index) for index in sheet.indices}
        window = shimmer.subset_window(theme, sheet.colours)
        for slot in window:
            if slot not in drawn:
                raise VerificationError(
                    f"{profile.name} {theme.name}: the shimmer rotates palette "
                    f"slot {slot}, which {path} never draws; a rotation must "
                    "only move colours the sheet it belongs to spends")
        if len(set(window)) != shimmer.CYCLE_ENTRIES:
            raise VerificationError(
                f"{profile.name} {theme.name}: the shimmer window "
                f"{window} repeats a slot; a rotation over a repeated entry "
                "writes one colour twice and drops another")


def check_playstation(profile: profiles.Profile,
                      converted: Dict[str, profiles.Converted]) -> None:
    """Assert this profile's output is loadable on a PlayStation as it stands.

    The PlayStation has no profile of its own, and that is a claim rather than
    an omission: ``playstation_header`` repacks *this* profile's assets and
    changes no colour. These are the four things that have to be true for that
    to be honest, and none of them was being checked before, because the subset
    path returns early from every other check here.

    * **The master palette survives 15-bit colour.** All
      :data:`~.palette.PALETTE_SIZE` entries must map to that many *distinct*
      five-bits-per-channel triples. This is the whole saving over nine-bit
      colour, where the same palette collapses from 123 opaque entries to 79
      and every asset needs re-quantising to survive it. If it ever stops
      holding, the PlayStation needs a profile too.
    * **Only the transparent entry becomes the transparent halfword.** The
      hardware skips a texel whose CLUT word is all zero, so an opaque colour
      that rounded to black would be drawn as a hole rather than as a dark
      pixel. It cannot happen while the map above is injective and the master
      palette's black is its transparent entry, and that is asserted rather
      than reasoned.
    * **No asset spends more than a CLUT.** The hardware reads sixteen
      contiguous halfwords whatever the art uses.
    * **Where an asset has transparency at all, it is slot 0.** Opaque ground
      legitimately has none, and all forty ``terrain-base`` sheets are in that
      case. But an asset that puts its hole anywhere but slot 0 would disagree
      with every client's convention.

    Plus the packing round trip: the header is the only form of this art the
    console can read, so a swapped nibble is load bearing and nothing else
    would catch it.
    """
    quantised = {playstation_header.clut_word(colour) for colour in palette.RGB}
    if len(quantised) != len(palette.RGB):
        raise VerificationError(
            f"{profile.name}: the {len(palette.RGB)}-entry master palette "
            f"collapses to {len(quantised)} colours at 15 bits, so the "
            "PlayStation needs a profile of its own rather than this one")
    for index, colour in enumerate(palette.RGB):
        if index == palette.TRANSPARENT:
            continue
        if playstation_header.clut_word(colour) == 0:
            raise VerificationError(
                f"{profile.name}: master palette entry {index} is {colour}, "
                "which becomes the CLUT word the GPU skips; it would be drawn "
                "as a hole rather than as a dark pixel")

    for path, asset in sorted(converted.items()):
        if len(asset.colours) > playstation_header.CLUT_SIZE:
            raise VerificationError(
                f"{profile.name} {path}: {len(asset.colours)} palette entries "
                f"exceeds the {playstation_header.CLUT_SIZE}-entry CLUT")
        if asset.transparent not in ((), (0,)):
            raise VerificationError(
                f"{profile.name} {path}: the transparent slot is "
                f"{asset.transparent} rather than 0")

    size = profile.tile_size
    per_halfword = playstation_header.TEXELS_PER_HALFWORD
    for path in ("characters/shadow.png",
                 f"characters/{characters.ARCHETYPE_ORDER[0]}_"
                 f"{characters.FACTION_COLOURS[0].name}.png",
                 f"terrain/{terrain.TERRAIN_ORDER[0]}_base.png"):
        asset = converted[path]
        payload = playstation_header.cell_halfwords(asset, 0, 0, size)
        per_row = size // per_halfword
        if len(payload) != per_row * size:
            raise VerificationError(
                f"{profile.name} {path}: {len(payload)} halfwords for a "
                f"{size}x{size} cell")
        for y in range(size):
            for x in range(size):
                word = payload[y * per_row + x // per_halfword]
                nibble = (word >> (4 * (x % per_halfword))) & 0xF
                if asset.indices[y * asset.width + x] != nibble:
                    raise VerificationError(
                        f"{profile.name} {path}: texel data does not unpack to "
                        f"pixel ({x}, {y}); the leftmost texel of a halfword "
                        "belongs in its low nibble")


# ---------------------------------------------------------------------------
# The glTF round trip
#
# This is the load-bearing check on the Blender route, and it is deliberately
# written as a *reader* rather than as a comparison of
# the exporter against itself. It opens the bytes on disk, walks them the way a
# converter that had never seen this repository would have to, and reconstructs
# the eight integers of every part. If those integers are not identical to what
# `meshes` holds, then glTF cannot carry this contract, which is a finding
# worth failing a build over.
#
# It is the discipline the console headers' own packing round trip applies,
# one format along: unpacking one artefact back to what it claims to encode
# costs nothing and catches a swapped axis, a lost part order or a smoothed
# normal immediately.
# ---------------------------------------------------------------------------

#: How many parts the reader has to see before it will believe it checked
#: anything. A reader that silently found no files would pass every assertion
#: below by examining nothing.
MINIMUM_ROUND_TRIPPED_PARTS = 1


def _gltf_floats(buffer: bytes, views: Sequence[Dict[str, object]],
                 accessor: Dict[str, object], where: str) -> List[float]:
    """One accessor's payload as floats, read the way any reader must."""
    if accessor.get("componentType") != gltf.COMPONENT_FLOAT:
        raise VerificationError(
            f"{where}: accessor component type {accessor.get('componentType')} "
            "is not FLOAT, so a reader cannot take these as positions")
    if accessor.get("type") != "VEC3":
        raise VerificationError(f"{where}: accessor type is not VEC3")
    view = views[int(accessor["bufferView"])]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    count = int(accessor["count"])
    # Honoured rather than assumed away: glTF requires `byteStride` the moment
    # two accessors share a view, which every one of these does, so a reader
    # that walked the bytes contiguously would be reading a file this one does
    # not claim to have written.
    stride = int(view.get("byteStride", gltf.VECTOR_BYTES))
    if stride < gltf.VECTOR_BYTES:
        raise VerificationError(
            f"{where}: a stride of {stride} cannot hold three floats")
    end = start + (count - 1) * stride + gltf.VECTOR_BYTES
    if end > int(view.get("byteOffset", 0)) + int(view["byteLength"]):
        raise VerificationError(
            f"{where}: the accessor runs past the end of its buffer view")
    values: List[float] = []
    for element in range(count):
        values.extend(struct.unpack_from("<3f", buffer, start + element * stride))
    return values


def _gltf_indices(buffer: bytes, views: Sequence[Dict[str, object]],
                  accessor: Dict[str, object], where: str) -> List[int]:
    """One index accessor's payload."""
    if accessor.get("componentType") != gltf.COMPONENT_UNSIGNED_SHORT:
        raise VerificationError(
            f"{where}: index component type is not UNSIGNED_SHORT")
    if accessor.get("type") != "SCALAR":
        raise VerificationError(f"{where}: index accessor type is not SCALAR")
    view = views[int(accessor["bufferView"])]
    start = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    count = int(accessor["count"])
    return list(struct.unpack_from(f"<{count}H", buffer, start))


def _exact_integer(value: float, where: str) -> int:
    """A coordinate that must have survived the file as an integer."""
    whole = int(value)
    if float(whole) != value:
        raise VerificationError(
            f"{where}: coordinate {value!r} is not an integer, so the authored "
            "table cannot be reconstructed from this file")
    # The claim the root node's 1/64 scale rests on: an authored integer scaled
    # into Blender units and back is the same integer, with no rounding step
    # anywhere. 64 is a power of two, so this is exact or the scale is wrong.
    if (value * gltf.ROOT_SCALE) * meshes.UNIT_WORLD != value:
        raise VerificationError(
            f"{where}: coordinate {value!r} does not survive the root node's "
            "scale exactly, so the model's units are lossy")
    return whole


def _round_trip_part(document: Dict[str, object], buffer: bytes,
                     index: int, where: str) -> meshes.Part:
    """Reconstruct one part from the file, as a converter would have to.

    Nothing in here reads the part table it will be compared against: the
    corners come out of the buffer, the ramp and rung out of the material, and
    the name and order out of the node. That is what makes the comparison worth
    making.
    """
    nodes = document["nodes"]
    node = nodes[index + 1]
    label = node.get("name", "")
    prefix = f"{index:02d}_"
    if not str(label).startswith(prefix):
        raise VerificationError(
            f"{where}: node {label!r} does not carry authored position "
            f"{index}, so the far-to-near order is not recoverable from names")

    mesh = document["meshes"][int(node["mesh"])]
    if mesh.get("name") != label:
        raise VerificationError(
            f"{where}: node {label!r} and its mesh {mesh.get('name')!r} "
            "disagree about which part this is")
    primitives = mesh["primitives"]
    if len(primitives) != 1:
        raise VerificationError(
            f"{where}: a part is one box and must be one primitive")
    primitive = primitives[0]
    if primitive.get("mode", gltf.MODE_TRIANGLES) != gltf.MODE_TRIANGLES:
        raise VerificationError(f"{where}: a part is not drawn as triangles")

    # The ramp and the rung, by both routes the export carries them, checked
    # against each other. "A face names a ramp and a rung, never a colour" is
    # the one rule glTF has no native place for, so a convention that held in
    # only one of its two places would be a
    # convention half-lost.
    material = document["materials"][int(primitive["material"])]
    try:
        named_ramp, named_rung = gltf.parse_material_name(
            str(material.get("name", "")))
    except ValueError as error:
        raise VerificationError(f"{where}: {error}") from error
    extras = material.get("extras") or {}
    if (extras.get("ramp"), extras.get("rung")) != (named_ramp, named_rung):
        raise VerificationError(
            f"{where}: material {material.get('name')!r} names ramp "
            f"{named_ramp} rung {named_rung} and its extras say "
            f"{extras.get('ramp')} / {extras.get('rung')}")
    if extras.get("rampName") != gltf.RAMP_NAMES.get(named_ramp):
        raise VerificationError(
            f"{where}: material {material.get('name')!r} spells its ramp two "
            "different ways")
    node_extras = node.get("extras") or {}
    if (node_extras.get("ramp"), node_extras.get("rung")) != (named_ramp,
                                                              named_rung):
        raise VerificationError(
            f"{where}: the node's extras and its material disagree about the "
            "ramp and rung")
    if node_extras.get("part") != index:
        raise VerificationError(
            f"{where}: the node's extras place it at position "
            f"{node_extras.get('part')}, not {index}")

    accessors = document["accessors"]
    views = document["bufferViews"]
    attributes = primitive["attributes"]
    if set(attributes) != {"POSITION", "NORMAL"}:
        raise VerificationError(
            f"{where}: a flat-shaded box carries positions and normals and "
            f"nothing else; this one carries {sorted(attributes)}")
    position_accessor = accessors[int(attributes["POSITION"])]
    positions = _gltf_floats(buffer, views, position_accessor, where)
    normals = _gltf_floats(buffer, views, accessors[int(attributes["NORMAL"])],
                           where)
    indices = _gltf_indices(buffer, views, accessors[int(primitive["indices"])],
                            where)
    if len(positions) != gltf.VERTICES_PER_BOX * 3:
        raise VerificationError(
            f"{where}: {len(positions) // 3} corners where a box with no "
            f"shared vertex has {gltf.VERTICES_PER_BOX}")
    if len(normals) != len(positions):
        raise VerificationError(f"{where}: a normal per corner, or flat "
                                "shading is not what this file describes")
    if len(indices) != gltf.INDICES_PER_BOX:
        raise VerificationError(
            f"{where}: {len(indices)} indices where a box is "
            f"{gltf.INDICES_PER_BOX}")

    corners = [tuple(_exact_integer(value, where)
                     for value in positions[at:at + 3])
               for at in range(0, len(positions), 3)]
    facing = [tuple(normals[at:at + 3]) for at in range(0, len(normals), 3)]

    # Flat shading, checked rather than assumed: the twenty-four normals are
    # the six axis directions, four corners each, and no corner is shared
    # between two faces, which is what makes smoothing impossible rather than
    # merely switched off.
    axis_normals = [(1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, 1.0, 0.0),
                    (0.0, -1.0, 0.0), (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)]
    for outward in facing:
        if outward not in axis_normals:
            raise VerificationError(
                f"{where}: normal {outward} is not an axis direction, so this "
                "is not the flat-shaded box the renderer draws")
    for outward in axis_normals:
        if facing.count(outward) != 4:
            raise VerificationError(
                f"{where}: {facing.count(outward)} corners face {outward} "
                "where a box's face has four; a vertex has been shared and a "
                "viewer would smooth this solid")

    low = tuple(min(corner[axis] for corner in corners) for axis in range(3))
    high = tuple(max(corner[axis] for corner in corners) for axis in range(3))
    for corner, outward in zip(corners, facing):
        axis = [abs(value) for value in outward].index(1.0)
        want = high[axis] if outward[axis] > 0 else low[axis]
        if corner[axis] != want:
            raise VerificationError(
                f"{where}: a corner facing {outward} is not on that face of "
                "the box, so the part is not the box it claims to be")
    if position_accessor.get("min") != [float(value) for value in low] or \
            position_accessor.get("max") != [float(value) for value in high]:
        raise VerificationError(
            f"{where}: the POSITION accessor's declared bounds are not the "
            "box its own corners describe")

    # Every triangle is wound so its face points outward. The renderer skips
    # wrongly-wound faces and that is exact only for a convex solid, so a file
    # that lost the winding would draw a different object.
    for at in range(0, len(indices), 3):
        one, two, three = (corners[indices[at + step]] for step in range(3))
        first = tuple(two[axis] - one[axis] for axis in range(3))
        second = tuple(three[axis] - one[axis] for axis in range(3))
        cross = (first[1] * second[2] - first[2] * second[1],
                 first[2] * second[0] - first[0] * second[2],
                 first[0] * second[1] - first[1] * second[0])
        outward = facing[indices[at]]
        if sum(cross[axis] * outward[axis] for axis in range(3)) <= 0:
            raise VerificationError(
                f"{where}: a triangle is wound against the face it belongs "
                "to, so the outside of the solid would be culled")

    # The axis mapping, inverted: x and y pass through and z was negated, so
    # the far corner in glTF is the near corner in the figure's space.
    return meshes.Part(low[0], high[0], low[1], high[1], -high[2], -low[2],
                       named_ramp, named_rung, str(node_extras.get("partName")))


def check_gltf_round_trip(output: pathlib.Path, style: styles.Style) -> None:
    """Read every exported model back and require the part tables to match.

    The strongest check this repository can make on the Blender route, and
    the reason the export was worth building: a converter written against these
    files, by somebody who never reads :mod:`.meshes`, has to arrive at the
    integers :mod:`.meshes` holds. Every one of them, in the authored order,
    with the ramp and the rung intact.
    """
    directory = output / gltf.relative_directory(style.name)
    checked = 0
    for archetype in characters.ARCHETYPE_ORDER:
        parts = meshes.parts_for(style, archetype)
        if not parts:
            continue
        stem = gltf.model_name(archetype)
        where = f"{style.name}/{archetype}.gltf"
        text = (directory / f"{stem}.gltf").read_text(encoding="utf-8")
        buffer = (directory / f"{stem}.bin").read_bytes()
        document = json.loads(text)

        asset = document.get("asset") or {}
        if asset.get("version") != gltf.VERSION:
            raise VerificationError(
                f"{where}: asset version {asset.get('version')!r} is not "
                f"{gltf.VERSION}")
        buffers = document.get("buffers") or []
        if len(buffers) != 1 or buffers[0].get("uri") != f"{stem}.bin":
            raise VerificationError(
                f"{where}: the document does not reference {stem}.bin, and a "
                "reader has nothing else to open")
        if buffers[0].get("byteLength") != len(buffer):
            raise VerificationError(
                f"{where}: the document declares a {buffers[0].get('byteLength')}"
                f"-byte buffer and {stem}.bin is {len(buffer)}")
        scene = document["scenes"][int(document["scene"])]
        if scene.get("nodes") != [0]:
            raise VerificationError(
                f"{where}: the scene does not root at the figure's own node")
        root = document["nodes"][0]
        if root.get("name") != archetype:
            raise VerificationError(
                f"{where}: the root node is named {root.get('name')!r}")
        if root.get("scale") != [gltf.ROOT_SCALE] * 3:
            raise VerificationError(
                f"{where}: the root node's scale is {root.get('scale')}, not "
                f"the uniform {gltf.ROOT_SCALE} that puts a board tile on a "
                "Blender unit")
        if root.get("children") != list(range(1, len(parts) + 1)):
            raise VerificationError(
                f"{where}: the root's children are not the parts in authored "
                "order, so the far-to-near order is lost")
        if len(document["nodes"]) != len(parts) + 1:
            raise VerificationError(
                f"{where}: {len(document['nodes']) - 1} part nodes where the "
                f"figure has {len(parts)}")

        rebuilt = [_round_trip_part(document, buffer, index,
                                    f"{where} part {index}")
                   for index in range(len(parts))]
        for index, (part, original) in enumerate(zip(rebuilt, parts)):
            if part.values() != original.values():
                raise VerificationError(
                    f"{where} part {index} ('{original.name}'): reads back as "
                    f"{part.values()} where the authored table holds "
                    f"{original.values()}")
            if part.name != original.name:
                raise VerificationError(
                    f"{where} part {index}: reads back named {part.name!r} "
                    f"rather than {original.name!r}")

        # Rule 4 recomputed from the file alone. The export could carry every
        # integer faithfully and still have shuffled the parts; this is what
        # says it did not.
        for before, after in zip(rebuilt, rebuilt[1:]):
            if meshes.depth_key(before) < meshes.depth_key(after):
                raise VerificationError(
                    f"{where}: read back, part '{after.name}' is further from "
                    f"the eye than '{before.name}' before it, so the exported "
                    "order is not the authored far-to-near one")
        triangles = len(rebuilt) * meshes.TRIANGLES_PER_PART
        if not (meshes.TRIANGLE_BAND[0] <= triangles <= meshes.TRIANGLE_BAND[1]):
            raise VerificationError(
                f"{where}: reads back as {triangles} triangles, outside the "
                f"{meshes.TRIANGLE_BAND[0]}-{meshes.TRIANGLE_BAND[1]} band")

        # A baked colour is a courtesy, but a wrong one is a lie: every
        # material's colour has to be a level the palette can hold.
        for material in document["materials"]:
            factor = material["pbrMetallicRoughness"]["baseColorFactor"]
            if len(factor) != 4 or factor[3] != 1.0:
                raise VerificationError(
                    f"{where}: material {material.get('name')!r} is not opaque")
            for channel in factor[:3]:
                if channel not in gltf.SRGB_LINEAR:
                    raise VerificationError(
                        f"{where}: material {material.get('name')!r} bakes "
                        f"{channel}, which no five-bit palette entry resolves to")
        checked += len(rebuilt)

    if checked < MINIMUM_ROUND_TRIPPED_PARTS:
        raise VerificationError(
            f"{style.name}: the glTF round trip examined no parts at all, so "
            "it proved nothing")


def check_profile(profile: profiles.Profile,
                  converted: Dict[str, profiles.Converted]) -> List[str]:
    """Checks for one profile's converted output. Returns skip reasons."""
    skipped: List[str] = []
    size = profile.tile_size
    columns = 8

    check_style_rosters(profile, converted)
    check_shimmer(profile, converted)
    if profile.name == playstation_header.SOURCE_PROFILE:
        check_playstation(profile, converted)

    for theme in themes.THEMES:
        with terrain.rendering(theme):
            for name in terrain.TERRAIN_ORDER:
                for variant in range(terrain.BASE_VARIANTS):
                    check_tiles_after_conversion(
                        profile, terrain.base_tile(name, variant),
                        f"{profile.name} {theme.name} {name} base variant "
                        f"{variant}")

    if profile.palette_mode == "subset":
        # A subset remap is a per-pixel palette lookup with no dependence on
        # position, so it cannot move an edge or interrupt a dither pattern.
        # There is nothing for the scene comparison to find.
        skipped.append(
            f"{profile.name}: scene comparison not applicable "
            "(position-independent palette remap)")
        return skipped

    for theme in themes.THEMES:
        suffix = themes.asset_suffix(theme)
        with terrain.rendering(theme):
            for name in terrain.TERRAIN_ORDER:
                scene_canvas = compose_test_scene(name)
                whole = profiles.convert(scene_canvas, profile)
                sheet = converted[f"terrain/{name}_blob{suffix}.png"]
                blank = sheet.transparent[0] if sheet.transparent else -1
                if blank < 0:
                    skipped.append(f"{profile.name} {theme.name} {name}: sheet "
                                   "has no transparent slot")
                    continue
                pieces = _assembled_from_sheet(sheet, name, size, columns, blank)
                for index, (expected, actual) in enumerate(
                        zip(whole.indices, pieces)):
                    if expected != actual:
                        x, y = index % whole.width, index // whole.width
                        raise VerificationError(
                            f"{profile.name} {theme.name} {name}: tile-by-tile "
                            f"assembly differs from whole-scene conversion at "
                            f"({x}, {y}); the reduction is not tile-local and "
                            "would show a seam"
                        )
    return skipped
