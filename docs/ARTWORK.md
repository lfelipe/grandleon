# Artwork and animation roster

Grandleon has artwork for all eight character archetypes and all thirteen
terrain kinds. The supported Godot presentation, **Voxel + Sprites**, combines
authored 3D terrain with animated high-resolution characters.

This page separates two useful meanings of “complete”:

- The supported Godot presentation has one high-resolution animated sci-fi
  character for each of the eight archetypes and one authored scene for each of
  the thirteen terrains.
- The portable generated library covers every combination of eight archetypes,
  seven styles, six faction colours, and two figures. These are the assets shared
  with the constrained renderers and used as a safe fallback in Godot.

The sci-fi direction is the first complete high-resolution set, not the only
visual direction planned. Release 0.4 is expanding the same Godot art contract
across the other styles and themes. The growing mythical set currently has:

| Portable identity | Animated preview |
|---|---|
| Knight · blue · first figure | ![The blue mythical knight idles, walks, attacks, raises a ward, and reacts to a hit](screenshots/art-roster/mythical-knight.gif) |
| Archer · violet · second figure | ![The violet mythical archer idles, walks, fires an arrow, uses an enchanted shot, and reacts to a hit](screenshots/art-roster/mythical-archer.gif) |

These are finished 0.4 characters, not a claim that the mythical roster is
complete. The names describe their portable art identity, not a campaign.
Identities without a curated high-resolution sheet continue to use the portable
generated library, so every valid project remains playable while the new sets
are built out.

## Animated Godot roster

These are the actual committed sheets used by the supported presentation. Each
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
roster is the curated high-resolution presentation, not a promise of a
high-resolution sheet for every colour and figure combination.

## Source and licence

The character sheets were generated for this project in built-in
image-generation mode. Existing sheets and the portable roster were used as
visual and design references. Each prompt requests a transparent 4×2 production
sheet, one consistent character, the eight poses listed above, no text or
scenery, and a readable tactical-game silhouette. Generated images are cropped
to exact cell boundaries without repainting the output.

Grandleon's source and project-owned artwork are offered under the repository's
[MIT License](../LICENSE). Bundled tools and runtimes retain their own licences
and notices. External art-pack import is not part of the current editor and may
be reconsidered after the built-in art directions are complete.
