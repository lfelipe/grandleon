# Godot graphics profiles

Grandleon keeps presentation separate from game rules. A graphics profile may
change the renderer, camera, lighting, animation sheets, terrain scenes, and
effects without changing a campaign or its simulation.

## Included profiles

| Profile | Terrain | Characters | Purpose |
|---|---|---|---|
| **Voxel + Sprites** | Authored 3D scenes | High-resolution animated sprites | Recommended PC presentation |
| **Full Voxel** | Authored 3D scenes | Imported or procedural 3D models | 3D character pipeline and fallback |
| **3D Diorama + Sprites** | Procedural 3D board | Portable animated sprites | Simple depth with the shared art library |
| **Pixel 2D** | Portable terrain atlas | Portable animated sprites | Smallest and closest to the console art lineage |

The player can switch profiles from Settings without restarting the campaign.
The board, cursors, selected characters, legal actions, and authoritative
snapshot remain the same.

## Current art coverage

The recommended profile binds authored scenes for all thirteen terrain kinds:
grass, farmland, sand, snow, swamp, hills, bamboo, forest, mountain, water,
road, paved, and ruins. Road selects a bridge scene when it crosses water.

Its high-resolution sci-fi roster covers all eight archetypes: knight, archer,
mage, stormcaller, healer, commander, rogue, and beast. Each sheet provides
idle, movement, attack, ability, and hurt/defeat poses. The complete animated
roster is visible in [the artwork guide](../../docs/ARTWORK.md).

Full Voxel currently includes imported animated models for commander, rogue,
stormcaller, and beast. Other archetypes use the procedural 3D fallback. This
is an honest fallback path, not a claim that the authored 3D roster is complete.

## Replacement contract

Profile data lives in
[`project/content/graphics_profiles.json`](project/content/graphics_profiles.json).
It can bind:

- terrain names to `.tscn` or imported scenes;
- character identities to sprite sheets and named frame sequences;
- character archetypes to imported scenes and semantic animation clips; and
- camera, lighting, palette, scale, pivot, and effect values.

Missing or invalid bindings fall back one asset at a time. Portable source does
not contain Godot paths, and simulation code does not know which profile is
active.

The editor currently offers the included world themes and character styles.
External art-pack import is not part of the current editor; it may be revisited
after the built-in art directions and preview workflow are complete.

## Capture and validation

Capture all four profiles from the same campaign state:

```sh
platform/godot/scripts/capture-graphics-prototypes.sh
```

Record the automated Godot gameplay reel:

```sh
platform/godot/scripts/record-demo-reel.sh
```

Validate the manifest, terrain scenes, sprite dimensions, animation frames,
imported models, and facing behavior:

```sh
platform/godot/scripts/smoke-art-library.sh build-godot
platform/godot/scripts/smoke-animated-characters.sh build-godot
```

The project roadmap in the root [README](../../README.md#roadmap) places broader
Godot art coverage and further console 3D experiments in 0.4.
