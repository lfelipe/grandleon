# Artwork and animation roster

Grandleon has complete artwork coverage for the 0.3 release vocabulary: all
eight character archetypes and all thirteen terrains have assets that the game
can load. The recommended Godot presentation is the **Voxel + Sprites** profile,
which combines authored 3D terrain with the animated high-resolution roster
shown below.

This page separates three useful meanings of “complete”:

- The recommended Godot profile has one high-resolution animated sci-fi
  character for each of the eight archetypes and one authored scene for each of
  the thirteen terrains.
- The portable generated library covers every combination of eight archetypes,
  seven styles, six faction colours, and two figures. These are the assets shared
  with the constrained renderers and used as a safe fallback in Godot.
- The optional **Full Voxel** profile currently has four imported, animated GLB
  characters. Its other archetypes still use the procedural 3D fallback, so it
  is not yet an eight-model authored roster.

## Animated Godot roster

These are the actual committed sheets used by the recommended profile. Each
preview runs through idle, walk, attack, ability, and hurt/defeat poses.

| Archetype | Preview | Archetype | Preview |
|---|---|---|---|
| Knight | ![The Relay knight idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/knight.gif) | Archer | ![The Relay archer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/archer.gif) |
| Mage | ![The Relay mage idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/mage.gif) | Stormcaller | ![Morrow the stormcaller idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/stormcaller.gif) |
| Healer | ![The Relay healer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/healer.gif) | Commander | ![Rook the commander idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/commander.gif) |
| Rogue | ![Kestrel the rogue idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/rogue.gif) | Beast | ![The Relay beast idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/beast.gif) |

Every source sheet is a transparent 4×2 image made of eight 443×443 cells:
idle A, idle B, walk A, walk B, attack wind-up, attack follow-through, ability,
and hurt/defeat. The graphics profile owns the timing, pivot, and directional
mirroring, so replacing a sheet does not change a campaign or its rules.

Run the following command after changing a sheet to rebuild the previews:

```sh
platform/godot/scripts/generate-art-roster.sh
```

## Terrain coverage

The authored Godot terrain pack covers:

> grass, farmland, sand, snow, swamp, hills, bamboo, forest, mountain, water,
> road, paved, and ruins

Road tiles can select the separate bridge scene when they meet water. Several
asymmetric scenes also have deterministic rotated variants, giving repeated
terrain some variety without adding randomness to portable game data. If a
replacement scene is missing or invalid, the renderer falls back on that tile
instead of making the whole art pack unusable.

The bindings live in
[`graphics_profiles.json`](../platform/godot/project/content/graphics_profiles.json),
the character sources live in
[`platform/godot/project/art/characters`](../platform/godot/project/art/characters),
and the terrain scenes live in
[`platform/godot/project/art/terrain`](../platform/godot/project/art/terrain).

## The full portable library

The portable art generator contains 672 standing character assets: eight
archetypes × seven styles × six faction colours × two figures. It also emits
their animation poses and the complete thirteen-terrain set for the modern and
console-oriented output profiles.

- [Browse all 56 archetype-and-style combinations](../tools/placeholder_art/ROSTER.md).
- [Browse terrain, transitions, animation frames, palettes, and output profiles](../tools/placeholder_art/GALLERY.md).

This larger library is deliberately lower resolution. It exists so every valid
portable project remains visible on every renderer. The eight-sheet Godot
roster is the curated high-resolution release presentation, not a promise of a
high-resolution sheet for every colour and figure combination.

## Source and licence

The four new knight, archer, mage, and healer sheets were generated for this
project in built-in image-generation mode, using the existing commander, rogue,
stormcaller, and beast sheets as visual references. The prompts requested a
transparent 4×2 production animation sheet, one consistent character, the eight
poses listed above, no text or scenery, and a readable tactical-game silhouette.
They were then cropped to exact cell boundaries without repainting the output.

Grandleon's source and project-owned artwork are offered under the repository's
[MIT License](../LICENSE). Bundled third-party tools and runtimes keep their own
licences and notices; release packaging must continue to include those notices
and any corresponding-source material they require.
