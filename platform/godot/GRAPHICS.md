# Godot presentation

Grandleon keeps presentation separate from game rules. The supported Godot
presentation may change its camera, lighting, animation sheets, terrain scenes,
and effects without changing a campaign or its simulation.

## Supported presentation

**Voxel + Sprites** combines authored 3D terrain scenes with high-resolution
animated character sheets. It is the only presentation offered in player
settings and the one shown throughout the human-facing documentation.

Pixel 2D, 3D Diorama, and Full Voxel remain compiled as internal diagnostic
renderers. Focused smoke tests use them to exercise fallback assets and the 3D
model pipeline, but they are not release options or advertised art directions.

## Current art coverage

The supported presentation binds authored scenes for all thirteen terrain kinds:
grass, farmland, sand, snow, swamp, hills, bamboo, forest, mountain, water,
road, paved, and ruins. Road selects a bridge scene when it crosses water.

Its high-resolution sci-fi and mythical rosters cover all eight archetypes:
knight, archer, mage, stormcaller, healer, commander, rogue, and beast. Each
direction has separate male and female designs. Every source sheet has a
team-colour mask, so the same character can use blue, red, green, violet,
amber, or bone while its face, hair, equipment, and effects stay unchanged.
Every sheet provides idle, movement, attack, ability, and hurt/defeat poses.
The complete animated rosters and colour comparisons are in
[the artwork guide](../../docs/ARTWORK.md).

## Replacement contract

Profile data lives in
[`project/content/graphics_profiles.json`](project/content/graphics_profiles.json).
It can bind:

- terrain names to `.tscn` or imported scenes;
- character identities to sprite sheets, optional team-colour masks, and named
  frame sequences;
- character archetypes to imported scenes and semantic animation clips; and
- camera, lighting, palette, scale, pivot, and effect values.

Missing or invalid bindings fall back one asset at a time. Portable source does
not contain Godot paths, and simulation code does not know which profile is
active.

## Layered terrain backdrops

[`combat_backdrops/manifest.json`](project/art/combat_backdrops/manifest.json)
is the shared pool for close combat and conversations. The backdrop resolver
accepts either a battle exchange key and focus cells or a dialogue identity,
authored backdrop, and optional live-board context. It returns one frozen pack,
variant, terrain fingerprint, and ordered list of layers. Both uses therefore
apply the same compatibility and FNV selection rule rather than maintaining
similar-looking selectors.

An in-battle conversation anchors its terrain sample on the first cast
character currently on the board. A story scene between maps uses its authored
backdrop instead. The first production pool supports **Deep Wood** and
forest-dominant map neighbourhoods; unsupported terrain or authored backdrops
retain the existing colour wash. The dialogue panel remains inside a dedicated
dark safe region, independent of how detailed later backdrop pools become.

The editor currently offers the included world themes and character styles.
External art-pack import is not part of the current editor; it may be revisited
after the built-in art directions and preview workflow are complete.

## Capture and validation

Exercise every internal renderer against the same campaign state:

```sh
platform/godot/scripts/capture-graphics-prototypes.sh
```

Rebuild the supported presentation image and gameplay reel used by the README:

```sh
platform/godot/scripts/capture-readme-media.sh
```

Validate the manifest, terrain scenes, sprite dimensions, animation frames,
imported models, and facing behavior:

```sh
platform/godot/scripts/smoke-art-library.sh build-godot
platform/godot/scripts/smoke-animated-characters.sh build-godot
platform/godot/scripts/smoke-conversation-backdrops.sh build-godot
```

The project roadmap in the root [README](../../README.md#roadmap) places broader
Godot art coverage and playable console 3D campaigns in 0.4, with console
graphics improving continuously across later releases.
