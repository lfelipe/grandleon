# Sevenfold Gate

Sevenfold Gate is the compact console showcase campaign. Its seven stages use
the medieval, sci-fi, mythical, nature, Sengoku, Undead and Pirates styles in
order. Across the campaign it fields every archetype, mixes male and female
characters on both sides, and uses every terrain kind.

The maps are deliberately different rather than palette swaps. They include a
crossroads, staggered ruins, ridge switchbacks, marsh islands, bamboo
courtyards, a broken grave road and a coastal landing. The generator verifies
that every starting tile remains connected despite water and ridges.

The same campaign supports one or two local players. In solo mode Player 1
controls all four Gatewardens. In co-op, the Gatewardens are split evenly
between Players 1 and 2 and one additional opponent enters the board.

## Build the Nintendo 64 campaign

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

The playable ROM is `build-n64-sevenfold-roms/sevenfold-n64.z64`. It asks for
one or two players on its main menu. Review builds also show live FPS and add
**Go to another Stage** to the pause menu, so every style and map can be reached
without recording victories for skipped stages.

The same folder contains `sevenfold-n64-renderer-check.z64`. That is an
automated visual diagnostic, not another campaign renderer or game mode.

## Regenerate the campaign

`generate_project.py` keeps the 56-unit source project readable and
repeatable. Run it after changing style, archetype, placement or terrain tables,
then compile `source/project.json` normally.

Renderer build commands, native-frame checks and current performance budgets
are documented in [Console renderer](../../docs/CONSOLE_RENDERER.md).
