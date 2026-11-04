# Artwork and animation roster

Grandleon has artwork for all eight character archetypes and all thirteen
terrain kinds. The supported Godot presentation, **Voxel + Sprites**, combines
authored 3D terrain with animated high-resolution characters.

This page separates two useful meanings of “complete”:

- The supported Godot presentation has one high-resolution animated sci-fi
  character for each archetype, plus male and female mythical characters for
  each archetype. The mythical set supports all six faction colours. Godot also
  has one authored scene for each of the thirteen terrains.
- The portable generated library covers every combination of eight archetypes,
  seven styles, six faction colours, and two figures. These are the assets shared
  with the constrained renderers and used as a safe fallback in Godot.

The sci-fi direction was the first complete high-resolution set. Release 0.4 is
expanding the same Godot art contract across other styles and themes. The first
complete expansion is the mythical set:

| Portable identity | Animated preview |
|---|---|
| Knight · male (`first`) · blue | ![The blue male mythical knight idles, walks, attacks, raises a ward, and reacts to a hit](screenshots/art-roster/mythical-knight.gif) |
| Knight · female (`second`) · blue | ![The blue female mythical knight idles, walks, attacks, raises a ward, and reacts to a hit](screenshots/art-roster/mythical-knight-female.gif) |
| Archer · male (`first`) · violet | ![The violet male mythical archer idles, walks, fires an arrow, uses an enchanted shot, and reacts to a hit](screenshots/art-roster/mythical-archer-male.gif) |
| Archer · female (`second`) · violet | ![The violet female mythical archer idles, walks, fires an arrow, uses an enchanted shot, and reacts to a hit](screenshots/art-roster/mythical-archer.gif) |
| Mage · male (`first`) · green | ![The green male mythical mage idles, walks, casts, expands an arcane ring, and reacts to a hit](screenshots/art-roster/mythical-mage-male.gif) |
| Mage · female (`second`) · green | ![The green female mythical mage idles, walks, casts, opens an arcane crescent, and reacts to a hit](screenshots/art-roster/mythical-mage-female.gif) |
| Healer · male (`first`) · amber | ![The amber male mythical healer idles, walks, attacks, blesses, and reacts to a hit](screenshots/art-roster/mythical-healer-male.gif) |
| Healer · female (`second`) · amber | ![The amber female mythical healer idles, walks, attacks, blesses, and reacts to a hit](screenshots/art-roster/mythical-healer-female.gif) |
| Commander · male (`first`) · blue | ![The blue male mythical commander idles, walks, attacks, commands, and reacts to a hit](screenshots/art-roster/mythical-commander-male.gif) |
| Commander · female (`second`) · blue | ![The blue female mythical commander idles, walks, attacks, commands, and reacts to a hit](screenshots/art-roster/mythical-commander-female.gif) |
| Stormcaller · male (`first`) · red | ![The red male mythical stormcaller idles, walks, attacks, calls a storm, and reacts to a hit](screenshots/art-roster/mythical-stormcaller-male.gif) |
| Stormcaller · female (`second`) · red | ![The red female mythical stormcaller idles, walks, attacks, calls a storm, and reacts to a hit](screenshots/art-roster/mythical-stormcaller-female.gif) |
| Rogue · male (`first`) · violet | ![The violet male mythical rogue idles, walks, attacks, shadow-steps, and reacts to a hit](screenshots/art-roster/mythical-rogue-male.gif) |
| Rogue · female (`second`) · violet | ![The violet female mythical rogue idles, walks, attacks, mirror-steps, and reacts to a hit](screenshots/art-roster/mythical-rogue-female.gif) |
| Beast · male (`first`) · bone | ![The bone-armoured male mythical beast idles, walks, attacks, projects a primal ward, and reacts to a hit](screenshots/art-roster/mythical-beast-male.gif) |
| Beast · female (`second`) · bone | ![The bone-armoured female mythical beast idles, walks, attacks, projects a primal ward, and reacts to a hit](screenshots/art-roster/mythical-beast-female.gif) |

These eight finished body pairs cover every mythical archetype. The names
describe their portable art identity, not a campaign. Other visual themes still
use their portable generated fallback until their curated Godot sheets are
painted.
Identities without a curated high-resolution sheet continue to use the portable
generated library, so every valid project remains playable while the new sets
are built out.

## Bodies and team colours

Every character archetype has two body variants. The project file stores them
as `first` and `second` for compatibility; the creator presents those choices
to people as **Male** and **Female**. The portable generated library already
draws both bodies for every archetype, style, and faction colour.

The high-resolution roster uses one male and one female source sheet per
archetype and style. These are independent character designs, not the same
costume placed on two bodies: faces, hair, stature, clothing construction,
equipment styling, and silhouettes may all differ. What stays consistent is
the archetype, the eight-action sheet contract, equipment handedness where the
renderer depends on it, and a large readable team-colour region.

Independent does not mean unrelated. Every sheet follows the same compact
tactical art direction: roughly three-and-a-half heads tall, slightly oversized
heads and hands, strong dark contours, simplified painterly materials, a shared
three-quarter camera, and consistent board-scale framing. Body type can vary,
but a pair should still look as though it was made by the same art team for the
same game.

It does not require six separately painted copies of each body. Every mythical
source sheet has a team-colour mask. Godot recolours only the marked cloth and
armour into blue, red, green, violet, amber, or bone, then caches the finished
texture for the battlefield, portraits, and creator previews. Faces, hair,
weapons, and spell effects remain unchanged. If a mask is missing or malformed,
Godot keeps the original source colour instead of failing the character.

The generated comparison gallery shows every mythical body in every faction
colour at portrait and board scale:

- [Browse all 96 mythical body-and-colour combinations](screenshots/art-roster/team-colours/index.html).
- [Inspect the mask selection on idle poses](screenshots/art-roster/team-colours/mythical-team-masks-idle.png).
- [Inspect the mask selection across every action](screenshots/art-roster/team-colours/mythical-team-masks-actions.png).

## Animated sci-fi Godot roster

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

Run these after changing a mythical sheet, mask, or faction palette to rebuild
the masks and the six-colour review gallery:

```sh
tools/placeholder_art/.venv/bin/python \
  platform/godot/scripts/generate-mythical-team-masks.py
platform/godot/scripts/generate-team-colour-showcase.sh
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
portable project remains visible on every renderer. The curated Godot rosters
are the high-resolution presentation, not a promise of a separately painted
sheet for every colour and body combination.

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
