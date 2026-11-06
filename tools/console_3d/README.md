# Console 3D model pipeline

This directory turns Grandleon's current character direction into readable,
bounded 3D figures for Nintendo 64 and PlayStation. Both consoles use the same
generated model library in the playable Sevenfold campaign and in a separate
complete-roster viewer.

The curated Godot characters are raster animation, not hidden source meshes.
The console models are therefore deterministic low-poly interpretations. They
preserve the large shapes that communicate each role—shield, bow, staff, rods,
crook, banner, paired blades, or quadruped body—then vary headgear, armour,
palette, and outline for each setting.

## Coverage

The generated roster covers:

- seven styles: medieval, sci-fi, mythical, nature, Sengoku, Undead, and
  Pirates;
- all eight archetypes in every style;
- male and female bodies for every archetype;
- runtime red and blue team materials; and
- low and high detail levels for all 112 combinations.

`generated/sevenfold_roster_proxies.json` records every vertex and triangle
count, the generator hash, and the hashes of both generated headers. The
release checks refuse stale generated output.

## How the two detail levels are used

Low detail is for the tactical board. High detail is reserved for views with a
few characters, especially the temporary terrain-aware attack view. The
simulation still owns movement, hits, misses, damage, counters, and campaign
state; the 3D renderer changes only the picture.

Nintendo 64 has enough margin for eight low-detail figures in the roster view
and two high-detail figures in the close view. PlayStation keeps board groups
more conservative and also limits the high-detail close view to two figures.
The sprite campaigns remain available on both systems as a visual comparison
while the 3D presentation continues improving.

## Current release checks

The Nintendo 64 viewer shows all eight roles per page, switches between low and
high detail, exercises both bodies and team colours, and includes seven terrain
close views plus an animated hit frame. Its ares gate passes `38/38`.

The PlayStation viewer uses four-role pages and auto-framed two-character close
views. Its guest gate passes `352/352`, including safe-picture checks for all
56 style/role pairs; the independent GPU observer passes `18/18`.

Current emulator measurements are:

| Platform and view | Measured result |
|---|---:|
| N64, eight low-detail models | 62–87 fps |
| N64, eight high-detail comparison models | 39–51 fps |
| N64, two high-detail close-view models | 132–163 fps |
| PS1, four low-detail board models | about 52 fps |
| PS1, four high-detail comparison models | about 32 fps |
| PS1, two high-detail close-view models | about 51 fps |

The four-model PlayStation high-detail page is a static comparison, not the
campaign board. Animated PS1 3D scenes target a locked 30 fps, synchronised to
every second NTSC retrace, and use a 33 ms regression ceiling. The throughput
figures above are pinned-emulator measurements rather than display cadence or
promises about original hardware.

## Regenerate the roster

After changing the construction rules, regenerate both detail levels and their
provenance report:

```sh
python3 tools/console_3d/generate_sevenfold_roster.py
```

Review the generated report and native-size captures before accepting the
change. Keep comparisons at 320×240; nearest-neighbour enlargement is fine,
but cropping away the board defeats a board-scale readability check.

The focused commander/rogue source reduction remains available when changing
the reduction method itself:

```sh
blender --background --python tools/console_3d/generate_v2_proxies.py -- \
  platform/godot/tools/export_relay_commander.py \
  platform/godot/tools/export_relay_rogue.py \
  tools/console_3d/generated/character_proxies_v2.h \
  tools/console_3d/evidence/v2/source
```

`generated/character_proxies_v2.json` records the exact source and helper
hashes for that focused comparison.

## Build and check

Build the playable N64 sprite campaign, playable N64 3D campaign, and N64
viewer together:

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

Run the N64 and PlayStation viewer gates:

```sh
cmake --build build --target grandleon_n64_sevenfold_3d_check
cmake --build build --target grandleon_playstation_sevenfold_3d_check
```

Build the playable PlayStation 3D disc:

```sh
cmake --build build --target grandleon_playstation_sevenfold_3d_disc
```

The N64 viewer source is
`platform/nintendo64/prototype/sevenfold_roster_rom.cpp`; the playable renderer
is `platform/nintendo64/src/campaign_3d_renderer.cpp`. The PlayStation viewer is
`platform/playstation/prototype/sevenfold_roster_exe.cpp`; its playable path is
selected in `platform/playstation/src/turn_exe.cpp`.

Native captures and the original measured comparisons remain under
`evidence/v2/`. They are supporting evidence; the complete Sevenfold gates
above are the release verdicts.
