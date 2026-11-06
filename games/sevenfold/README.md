# Sevenfold Gate

Sevenfold Gate is a short, deliberately plain showcase campaign. Its seven
Stages use the medieval, sci-fi, mythical, nature, sengoku, undead and pirate
character styles in order. Every Stage fields all eight archetypes, split four
against four, with two male and two female drawings on each side.

The maps are not palette swaps of one layout. They cover all thirteen terrain
kinds across a crossroads, staggered ruins, ridge switchbacks, marsh islands,
bamboo courtyards, a broken grave road and a coastal landing. The generator
checks that every starting tile remains connected despite the water and ridges.

The same campaign supports one or two local players. In one-player mode all
four Gatewardens belong to Player 1. In two-player mode the four are divided
evenly between Players 1 and 2, and one additional opponent enters the board.

## Nintendo 64 campaign ROMs

Build the sprite campaign, playable 3D campaign, and complete-roster 3D viewer
together:

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

`build-n64-sevenfold-roms/` receives `sevenfold-n64-sprite.z64`,
`sevenfold-n64-3d.z64`, and `sevenfold-n64-3d-viewer.z64`. The first two run the
same persistent campaign with different renderers. Their title flow asks for
one or two players. During a battle, press Start and choose **Go to another
Stage** to jump directly to any style. A jump records no victories for skipped
Stages; that is intentional for this showcase. Both debug campaign builds also
show live FPS in the lower-right corner.

`generate_project.py` keeps the regular 56-unit project readable and
repeatable. Run it after changing the style, archetype, placement or terrain
tables, then compile `source/project.json` normally.

## Nintendo 64 3D roster viewer

The companion 3D ROM makes every planned N64 model easy to review:

```sh
platform/nintendo64/scripts/build-sevenfold-3d.sh
platform/nintendo64/scripts/check-sevenfold-3d.sh
```

It contains 112 procedural low-poly characters: seven styles, eight
archetypes, and male and female variants. Each strategic page shows all eight
archetypes at a readable board distance and alternates red and blue team
colours. Press **A** to compare low and high detail, use the d-pad to change
style or body, and press **Start** to enter the close attack view.

The close view places two high-detail characters in a scene built from the
selected map tile and its surrounding terrain. Use **A** to change the role,
**B** to preview an attack, hit, counter-attack, and damage callouts, and the
d-pad to change style or terrain. The damage is illustrative; this review ROM
does not run campaign combat. The playable `sevenfold-n64-3d.z64` instead
opens its close view from real campaign combat and displays simulation-owned
damage and counters.

The automated check captures 28 strategic comparisons, seven terrain-aware
close views, and one animated hit frame. Its current ares run passes all 38
checks with depth enabled. Low-detail strategic pages measure 62–87 fps,
high-detail pages measure 39–51 fps, and close views measure 132–163 fps. These
are emulator measurements, not original-hardware timings.

The viewer is deliberately separate from the campaign so every model and
terrain can be inspected quickly. The playable sprite and 3D ROMs share these
same seven terrain layouts, authored stages, campaign state, controls, and
one/two-player rules.
