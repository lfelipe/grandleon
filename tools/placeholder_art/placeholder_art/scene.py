# SPDX-License-Identifier: MIT
"""The composed sample map.

This is the proof that the autotiling actually works. It is not decoration: it
lays every terrain type against every other one it can meet, runs a river with
a road bridging it, and composites the result exactly the way a renderer would:
by looking up a neighbour mask per cell and fetching that variant.

If the coverage field or the transition rims were wrong, this image is where it
would be obvious.
"""

from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from . import autotile, characters, terrain
from .raster import Canvas
from .rng import seed_of

#: Single letters, so the map below stays readable as a picture in source.
LEGEND: Dict[str, str] = {
    "g": "grass",
    "s": "sand",
    "n": "snow",
    "p": "swamp",
    "r": "road",
    "f": "forest",
    "m": "mountain",
    "w": "water",
    "h": "hills",
    "u": "ruins",
    "a": "farmland",
    "b": "bamboo",
    "v": "paved",
}

#: A north-south road runs down column 13 and crosses the river at row 6,
#: which is the bridge: a road cell whose neighbours are water. Mountain meets
#: snow at the top left and water meets grass along the whole river, the two
#: transitions the sample campaign leans on hardest. Hills step the high
#: ground down to the field, and a ruin sits in the open where its rim over
#: grass can be judged.
#:
#: The bottom four rows are the settled end of the map, and they are where the
#: three terrains added for the seven settings can be judged at board scale:
#: a farmed field, a paved court beside it, and a bamboo stand along the east
#: edge below the marsh. Each meets grass, which is the neighbour each was
#: given a transition rim for, and the paved court runs up to the road so the
#: two worked surfaces can be told apart where they touch.
SAMPLE_MAP: Tuple[str, ...] = (
    "mmmmmnnngggggrggffff",
    "mmmmnnnggggggrggffff",
    "mmmnnnhggggggrgggfff",
    "mmnnnhhggggggrggggff",
    "gnnhhhssssgggrgggggf",
    "gggsswwwwwssgrgggggg",
    "sswwwwwwwwwwwrwwwwww",
    "gggsswwwwwssgrgggggg",
    "ggggsssssggggrgggggg",
    "gggggggggggggrggpppp",
    "fffgguuggggggrggpppp",
    "ffffguuugggggrggpppp",
    "ffffgggggggggrgppppp",
    "fffggggggggggrgppppp",
    "ffggaaaaaaaaarggbbbb",
    "fgggaaaaaaaaargbbbbb",
    "ggaaaaaaavvvvrgbbbbb",
    "ggaaaaavvvvvvrggbbbb",
)

#: Units dropped onto the map so the sprites can be judged at true scale
#: against the terrain they will actually stand on.
SAMPLE_UNITS: Tuple[Tuple[int, int, str, str], ...] = (
    (4, 3, "commander", "blue"),
    (6, 4, "knight", "blue"),
    (3, 10, "archer", "blue"),
    (5, 11, "healer", "blue"),
    (2, 12, "mage", "blue"),
    (16, 4, "commander", "red"),
    (17, 8, "stormcaller", "red"),
    (18, 10, "rogue", "red"),
    (11, 12, "beast", "red"),
)


#: Width of every row of :data:`SAMPLE_MAP`.
MAP_WIDTH = 20


def _grid() -> List[List[str]]:
    grid = []
    for index, row in enumerate(SAMPLE_MAP):
        # Asserted, not padded: a short row silently shifts everything after
        # it and produces a road that autotiles into disconnected diamonds.
        assert len(row) == MAP_WIDTH, (
            f"sample map row {index} is {len(row)} cells, expected {MAP_WIDTH}")
        grid.append([LEGEND[character] for character in row])
    return grid


def _under(grid: List[List[str]], x: int, y: int, name: str) -> Optional[str]:
    """Which terrain this cell should transition *into*.

    The lowest-layer differing cardinal neighbour wins; ties break on name so
    the choice is stable. That is the neighbour the rim will be styled for.
    """
    candidates = []
    for dx, dy in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        nx, ny = x + dx, y + dy
        if not (0 <= ny < len(grid) and 0 <= nx < len(grid[ny])):
            continue
        neighbour = grid[ny][nx]
        if neighbour != name and terrain.TERRAINS[neighbour].layer < terrain.TERRAINS[name].layer:
            candidates.append((terrain.TERRAINS[neighbour].layer, neighbour))
    if not candidates:
        return None
    return min(candidates)[1]


def compose(with_units: bool = True) -> Canvas:
    """Render the sample map at the native tile size."""
    grid = _grid()
    height = len(grid)
    width = len(grid[0])
    tile = terrain.TILE
    canvas = Canvas(width * tile, height * tile)

    # Ground layer: grass everywhere, variant chosen from the cell position so
    # the field is not visibly one repeated tile.
    for y in range(height):
        for x in range(width):
            variant = seed_of("map-variant", x, y) % terrain.BASE_VARIANTS
            canvas.blit_opaque(terrain.base_tile("grass", variant), x * tile, y * tile)

    for name in terrain.TERRAIN_ORDER:
        if name == "grass":
            continue
        for y in range(height):
            for x in range(width):
                if grid[y][x] != name:
                    continue
                mask = autotile.mask_from(
                    lambda dx, dy: (
                        0 <= y + dy < height
                        and 0 <= x + dx < width
                        and grid[y + dy][x + dx] == name
                    )
                )
                if autotile.canonicalise(mask) == 0xFF:
                    variant = seed_of("map-variant", x, y) % terrain.BASE_VARIANTS
                    piece = terrain.base_tile(name, variant)
                else:
                    piece = terrain.blob_tile(name, mask, _under(grid, x, y, name))
                canvas.blit(piece, x * tile, y * tile)

    if with_units:
        for x, y, archetype, colour in SAMPLE_UNITS:
            canvas.blit(characters.sprite(archetype, colour), x * tile, y * tile)
    return canvas
