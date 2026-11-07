# Cross-renderer showcase capture

This repeatable scene compares Godot, Nintendo 64, and PlayStation presentation
without pretending it is a campaign battle. It is a renderer showcase: board,
scenery, combatants, camera, health changes, and timing are fixed while each
platform implements the same composition within its limits.

The machine-readable source of truth is
[`tools/renderer_comparison/scene.json`](../../tools/renderer_comparison/scene.json).
The recorder reads it directly.

## The complete scene

The playable board is 6×4. `F` is forest, `G` is grass, and `P` is paved
stone.

```text
F F G P P F
F G G P P F
F G G P G F
F F G P F F
```

That centre is not the whole picture. Three explicit rings extend its edge
terrain, producing a 12×10 visual footprint. Forest cells use two-tree
clusters with a taller tree and a sapling; connected clusters vary height
deterministically. Grass keeps moss, tufts, and field stones. Paved cells keep
four slabs, dark seams, and cyan inlays. The contract contains the full 12×10
rows and decoration recipe so a console does not reduce the scene to a tiny
6×4 grid floating in a diagnostic frame.

The tactical camera is orthographic at `(7.7, 10.8, 8.8)`, looking at
`(0, 0.15, 0)`, with vertical size `8.4`. At a native 320×240 reference,
the scene may use the entire frame except for a four-pixel safe margin. There
is no reserved title or footer band.

| Role | Unit | Cell | Before | After |
|---|---|---:|---:|---:|
| Selected attacker | Auren, blue medieval male knight | `(1, 2)` | 12/12 HP | 8/12 HP |
| Attack target | Ilyra, red medieval female knight | `(4, 1)` | 11/11 HP | 8/11 HP |

Auren attacks for 3. Ilyra counters for 4. Auren stays on the left and Ilyra
on the right for the complete exchange; the counter travels right-to-left.

## Capture HUD

The showcase is for looking at the game, not diagnostics. The capture HUD may
show only:

- compact health bars above units;
- selected-cell and attack-target markers; and
- the transient damage number beside the unit that was hit.

It must not reserve screen space for titles, instructions, performance
counters, platform labels, or diagnostic text. Subtle letterboxing and a
vignette remain part of the cinematic composition, not the HUD. Normal Godot
gameplay still shows its controls; only deterministic capture mode applies
this reduced HUD.

## Terrain-derived close backdrop

The close view uses source plates rendered from the existing Godot
`relay_forest.tscn`, `relay_grass.tscn`, and `relay_paved.tscn` scenes.
No external artwork is involved.

The descriptor records forest as dominant, grass as secondary, a north–south
paved transition, the combat-cell direction, flat elevation, and forest
cluster cues. FNV-1a over
`medieval_forest_renderer_showcase_v2|1,2|4,1` produces seed
`2031659219`, selecting variant 2 of 3. The choice and layer order stay
frozen through attack and counter.

Each 1280×720 sRGB variant has four modular layers:

1. opaque panorama;
2. grass/paved transition;
3. forest midground;
4. foreground props.

The reproducible source render uses a 42-degree perspective camera at
`(0, 2.55, 6.2)`, looking at `(0, 0.34, -1.35)`, with the lighting values
recorded beside it in `scene.json`. Transition, midground, and prop plates
retain alpha; only the panorama is opaque.

Godot then draws a small aligned grass/paved floor strip, shadows, units,
effects, and damage live. Props never cover units. The generic
[`manifest.json`](project/art/combat_backdrops/manifest.json) lists all
13 terrain kinds and deterministic selection fields. A road beside water uses
the bridge scene as a conditional transition.

Regenerate the proof variants from the repository root:

```sh
platform/godot/scripts/generate-combat-backdrops.sh
```

Nintendo 64 and PlayStation textures must derive from these same PNG layers,
not separate artwork. The contract permits CI8/RGBA16 tiling on Nintendo 64
and CLUT/RGB16 TIM conversion on PlayStation while preserving variant and
depth order.

## Timing

The fixed 30 fps movie lasts 8.15 seconds by contract (8.1667 seconds, or 245
encoded frames, in the Godot recorder).

| Time | Landmark |
|---:|---|
| `0.00–1.15` | Full tactical scene |
| `1.15–2.25` | Live board camera flies toward the pair |
| `2.25–4.30` | Left-to-right attack and `−3` |
| `4.30–6.35` | Right-to-left counter and `−4` |
| `6.35–7.30` | Close view dissolves and camera reverses |
| `7.30–8.15` | Restored settled board |

The review frames are `board.png` at 0.50, `focus.png` at 1.70,
`attack_impact.png` at 3.45, `counter_impact.png` at 5.50, and
`restored.png` at 7.55 seconds.

## Record it

```sh
platform/godot/scripts/record-cross-renderer-comparison.sh
```

The defaults are:

- `out/renderer-comparison/godot.mp4`, H.264 at 1280×720 and 30 fps; and
- `out/renderer-comparison/godot-frames/*.png`.

The adapter validates schema, surroundings, camera, HUD exclusions, backdrop
seed, chosen layers, and stable unit slots. The wrapper verifies codec, size,
frame rate, and duration. MP4 bytes may differ between FFmpeg versions, so
compare named frames and motion rather than an MP4 hash.
