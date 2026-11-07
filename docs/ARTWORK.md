# Artwork and animation roster

Grandleon's Godot player includes seven complete character themes: sci-fi,
mythical, medieval, nature, Sengoku, Undead, and Pirates. Each theme has male
and female designs for all eight unit roles, and each design supports blue,
red, green, violet, amber, and bone team colours.

![Examples of the seven themes and eight unit roles](screenshots/art-roster/readme-art-sampler.png)

The compact image above is only an introduction. The tables below show the
actual animated sheets used in the game. Each preview includes idle, walk,
attack, ability, and hurt or defeat poses.

For a later correction or redesign, use the
[artwork generation and tweak guide](ARTWORK_GENERATION.md). It records the
confirmed authoring model, the preserved prompts, and the rules that keep a new
sheet consistent with the rest of the game.

## Animated mythical roster

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
describe their portable art identity, not a campaign. The same coverage now
exists for all seven styles. Godot keeps an original sheet colour if a team
mask is malformed; missing character sheets are reported as content errors.

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

It does not require six separately painted copies of each body. Every curated
source sheet has a team-colour mask. Godot recolours only the marked cloth and
armour into blue, red, green, violet, amber, or bone, then caches the finished
texture for the battlefield, portraits, and creator previews. Faces, hair,
weapons, and spell effects remain unchanged. If a mask is missing or malformed,
Godot keeps the original source colour instead of failing the character.

The colour galleries show every body in all six faction colours at portrait and
board scale: [mythical](screenshots/art-roster/team-colours/index.html),
[sci-fi](screenshots/art-roster/team-colours/scifi/index.html),
[medieval](screenshots/art-roster/team-colours/medieval/index.html),
[nature](screenshots/art-roster/team-colours/nature/index.html),
[Sengoku](screenshots/art-roster/team-colours/sengoku/index.html),
[Undead](screenshots/art-roster/team-colours/undead/index.html), and
[Pirates](screenshots/art-roster/team-colours/pirates/index.html).

## Animated sci-fi Godot roster

These are the actual committed sheets used by the supported presentation. Each
preview runs through idle, walk, attack, ability, and hurt/defeat poses.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male sci-fi knight idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/knight.gif) | ![The female sci-fi knight idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-knight-female.gif) |
| Archer | ![The male sci-fi archer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/archer.gif) | ![The female sci-fi archer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-archer-female.gif) |
| Mage | ![The male sci-fi mage idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/mage.gif) | ![The female sci-fi mage idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-mage-female.gif) |
| Stormcaller | ![The male sci-fi stormcaller idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/stormcaller.gif) | ![The female sci-fi stormcaller idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-stormcaller-female.gif) |
| Healer | ![The male sci-fi healer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/healer.gif) | ![The female sci-fi healer idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-healer-female.gif) |
| Commander | ![The male sci-fi commander idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/commander.gif) | ![The female sci-fi commander idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-commander-female.gif) |
| Rogue | ![The male sci-fi rogue idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/rogue.gif) | ![The female sci-fi rogue idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-rogue-female.gif) |
| Beast | ![The male sci-fi beast idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/beast.gif) | ![The female sci-fi beast idles, walks, attacks, uses an ability, and reacts to a hit](screenshots/art-roster/scifi-beast-female.gif) |

Every source sheet is a transparent 4×2 image made of eight 443×443 cells:
idle A, idle B, walk A, walk B, attack wind-up, attack follow-through, ability,
and hurt/defeat. The graphics profile owns the timing, pivot, and directional
mirroring, so replacing a sheet does not change a campaign or its rules.

## Animated medieval Godot roster

The medieval roster uses grounded steel, leather, cloth, wood, and heraldry.
The two bodies are independently designed while sharing one company palette,
camera, scale, and material language.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male medieval knight runs through the production actions](screenshots/art-roster/medieval-knight-male.gif) | ![The female medieval knight runs through the production actions](screenshots/art-roster/medieval-knight-female.gif) |
| Archer | ![The male medieval archer runs through the production actions](screenshots/art-roster/medieval-archer-male.gif) | ![The female medieval archer runs through the production actions](screenshots/art-roster/medieval-archer-female.gif) |
| Mage | ![The male medieval mage runs through the production actions](screenshots/art-roster/medieval-mage-male.gif) | ![The female medieval mage runs through the production actions](screenshots/art-roster/medieval-mage-female.gif) |
| Stormcaller | ![The male medieval stormcaller runs through the production actions](screenshots/art-roster/medieval-stormcaller-male.gif) | ![The female medieval stormcaller runs through the production actions](screenshots/art-roster/medieval-stormcaller-female.gif) |
| Healer | ![The male medieval healer runs through the production actions](screenshots/art-roster/medieval-healer-male.gif) | ![The female medieval healer runs through the production actions](screenshots/art-roster/medieval-healer-female.gif) |
| Commander | ![The male medieval commander runs through the production actions](screenshots/art-roster/medieval-commander-male.gif) | ![The female medieval commander runs through the production actions](screenshots/art-roster/medieval-commander-female.gif) |
| Rogue | ![The male medieval rogue runs through the production actions](screenshots/art-roster/medieval-rogue-male.gif) | ![The female medieval rogue runs through the production actions](screenshots/art-roster/medieval-rogue-female.gif) |
| Beast | ![The male medieval beast runs through the production actions](screenshots/art-roster/medieval-beast-male.gif) | ![The female medieval beast runs through the production actions](screenshots/art-roster/medieval-beast-female.gif) |

## Animated nature Godot roster

Nature uses animal-folk silhouettes, bark, leaf, leather, and warm metal. Each
body pair is deliberately different while retaining one woodland faction
language and broad blue cloth regions for team recolouring.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male nature knight runs through the production actions](screenshots/art-roster/nature-knight-male.gif) | ![The female nature knight runs through the production actions](screenshots/art-roster/nature-knight-female.gif) |
| Archer | ![The male nature archer runs through the production actions](screenshots/art-roster/nature-archer-male.gif) | ![The female nature archer runs through the production actions](screenshots/art-roster/nature-archer-female.gif) |
| Mage | ![The male nature mage runs through the production actions](screenshots/art-roster/nature-mage-male.gif) | ![The female nature mage runs through the production actions](screenshots/art-roster/nature-mage-female.gif) |
| Stormcaller | ![The male nature stormcaller runs through the production actions](screenshots/art-roster/nature-stormcaller-male.gif) | ![The female nature stormcaller runs through the production actions](screenshots/art-roster/nature-stormcaller-female.gif) |
| Healer | ![The male nature healer runs through the production actions](screenshots/art-roster/nature-healer-male.gif) | ![The female nature healer runs through the production actions](screenshots/art-roster/nature-healer-female.gif) |
| Commander | ![The male nature commander runs through the production actions](screenshots/art-roster/nature-commander-male.gif) | ![The female nature commander runs through the production actions](screenshots/art-roster/nature-commander-female.gif) |
| Rogue | ![The male nature rogue runs through the production actions](screenshots/art-roster/nature-rogue-male.gif) | ![The female nature rogue runs through the production actions](screenshots/art-roster/nature-rogue-female.gif) |
| Beast | ![The male nature beast runs through the production actions](screenshots/art-roster/nature-beast-male.gif) | ![The female nature beast runs through the production actions](screenshots/art-roster/nature-beast-female.gif) |

## Animated Sengoku Godot roster

The Sengoku roster uses lacquered lamellar armour, indigo cloth, period-inspired
weapons and ritual tools, and subtle woodblock influence. Male and female
counterparts share one faction language without sharing a single costume.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male Sengoku knight runs through the production actions](screenshots/art-roster/sengoku-knight-male.gif) | ![The female Sengoku knight runs through the production actions](screenshots/art-roster/sengoku-knight-female.gif) |
| Archer | ![The male Sengoku archer runs through the production actions](screenshots/art-roster/sengoku-archer-male.gif) | ![The female Sengoku archer runs through the production actions](screenshots/art-roster/sengoku-archer-female.gif) |
| Mage | ![The male Sengoku mage runs through the production actions](screenshots/art-roster/sengoku-mage-male.gif) | ![The female Sengoku mage runs through the production actions](screenshots/art-roster/sengoku-mage-female.gif) |
| Stormcaller | ![The male Sengoku stormcaller runs through the production actions](screenshots/art-roster/sengoku-stormcaller-male.gif) | ![The female Sengoku stormcaller runs through the production actions](screenshots/art-roster/sengoku-stormcaller-female.gif) |
| Healer | ![The male Sengoku healer runs through the production actions](screenshots/art-roster/sengoku-healer-male.gif) | ![The female Sengoku healer runs through the production actions](screenshots/art-roster/sengoku-healer-female.gif) |
| Commander | ![The male Sengoku commander runs through the production actions](screenshots/art-roster/sengoku-commander-male.gif) | ![The female Sengoku commander runs through the production actions](screenshots/art-roster/sengoku-commander-female.gif) |
| Rogue | ![The male Sengoku rogue runs through the production actions](screenshots/art-roster/sengoku-rogue-male.gif) | ![The female Sengoku rogue runs through the production actions](screenshots/art-roster/sengoku-rogue-female.gif) |
| Beast | ![The male Sengoku beast runs through the production actions](screenshots/art-roster/sengoku-beast-male.gif) | ![The female Sengoku beast runs through the production actions](screenshots/art-roster/sengoku-beast-female.gif) |

## Animated Undead Godot roster

The Undead roster uses corroded dark iron, old ivory bone, worn leather,
tattered indigo heraldry, and pale green spectral effects. Male and female
counterparts share that gothic storybook language while keeping clearly
different silhouettes and equipment treatments.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male Undead knight runs through the production actions](screenshots/art-roster/undead-knight-male.gif) | ![The female Undead knight runs through the production actions](screenshots/art-roster/undead-knight-female.gif) |
| Archer | ![The male Undead archer runs through the production actions](screenshots/art-roster/undead-archer-male.gif) | ![The female Undead archer runs through the production actions](screenshots/art-roster/undead-archer-female.gif) |
| Mage | ![The male Undead mage runs through the production actions](screenshots/art-roster/undead-mage-male.gif) | ![The female Undead mage runs through the production actions](screenshots/art-roster/undead-mage-female.gif) |
| Stormcaller | ![The male Undead stormcaller runs through the production actions](screenshots/art-roster/undead-stormcaller-male.gif) | ![The female Undead stormcaller runs through the production actions](screenshots/art-roster/undead-stormcaller-female.gif) |
| Healer | ![The male Undead healer runs through the production actions](screenshots/art-roster/undead-healer-male.gif) | ![The female Undead healer runs through the production actions](screenshots/art-roster/undead-healer-female.gif) |
| Commander | ![The male Undead commander runs through the production actions](screenshots/art-roster/undead-commander-male.gif) | ![The female Undead commander runs through the production actions](screenshots/art-roster/undead-commander-female.gif) |
| Rogue | ![The male Undead rogue runs through the production actions](screenshots/art-roster/undead-rogue-male.gif) | ![The female Undead rogue runs through the production actions](screenshots/art-roster/undead-rogue-female.gif) |
| Beast | ![The male Undead beast runs through the production actions](screenshots/art-roster/undead-beast-male.gif) | ![The female Undead beast runs through the production actions](screenshots/art-roster/undead-beast-female.gif) |

## Animated Pirates Godot roster

The Pirates roster uses weathered leather and linen, tarnished brass, dark
wood, naval indigo, and pale sea-green or warm-gold effects. The age-of-sail
language is shared across the roster while each role keeps unmistakable tools,
clothing construction, and movement silhouettes.

| Archetype | Male (`first`) | Female (`second`) |
|---|---|---|
| Knight | ![The male Pirates knight runs through the production actions](screenshots/art-roster/pirates-knight-male.gif) | ![The female Pirates knight runs through the production actions](screenshots/art-roster/pirates-knight-female.gif) |
| Archer | ![The male Pirates archer runs through the production actions](screenshots/art-roster/pirates-archer-male.gif) | ![The female Pirates archer runs through the production actions](screenshots/art-roster/pirates-archer-female.gif) |
| Mage | ![The male Pirates mage runs through the production actions](screenshots/art-roster/pirates-mage-male.gif) | ![The female Pirates mage runs through the production actions](screenshots/art-roster/pirates-mage-female.gif) |
| Stormcaller | ![The male Pirates stormcaller runs through the production actions](screenshots/art-roster/pirates-stormcaller-male.gif) | ![The female Pirates stormcaller runs through the production actions](screenshots/art-roster/pirates-stormcaller-female.gif) |
| Healer | ![The male Pirates healer runs through the production actions](screenshots/art-roster/pirates-healer-male.gif) | ![The female Pirates healer runs through the production actions](screenshots/art-roster/pirates-healer-female.gif) |
| Commander | ![The male Pirates commander runs through the production actions](screenshots/art-roster/pirates-commander-male.gif) | ![The female Pirates commander runs through the production actions](screenshots/art-roster/pirates-commander-female.gif) |
| Rogue | ![The male Pirates rogue runs through the production actions](screenshots/art-roster/pirates-rogue-male.gif) | ![The female Pirates rogue runs through the production actions](screenshots/art-roster/pirates-rogue-female.gif) |
| Beast | ![The male Pirates beast runs through the production actions](screenshots/art-roster/pirates-beast-male.gif) | ![The female Pirates beast runs through the production actions](screenshots/art-roster/pirates-beast-female.gif) |

## Terrain coverage

The authored Godot terrain pack covers:

> grass, farmland, sand, snow, swamp, hills, bamboo, forest, mountain, water,
> road, paved, and ruins

Roads form bridges where they meet water. Forest, bamboo, hills, mountains,
snow, swamp, fields, sand, and ruins vary their scenery when several matching
tiles meet, while exposed edges remain smaller and readable. These changes are
visual only; walkable cells and campaign rules stay the same.

The bindings live in
[`graphics_profiles.json`](../platform/godot/project/content/graphics_profiles.json),
the character sources live in
[`platform/godot/project/art/characters`](../platform/godot/project/art/characters),
and the terrain scenes live in
[`platform/godot/project/art/terrain`](../platform/godot/project/art/terrain).

### Large map-feature prototypes

The first village and deep-forest landmarks are preserved as transparent
source renders: a 2×2 timber-and-stone house and a 2×2 ancient-tree cluster.
They are not ordinary one-cell terrain. Steel and Cedar now authors each as an
explicit four-cell blocked footprint, and Godot groups that footprint into one
procedural building or natural mass that follows the scene lighting.

[Review the source renders and their intended footprints](../platform/godot/project/art/map_features/README.md).

The transparent renders are visual references, not the current in-game
textures. Godot uses the house reference as the visual direction for a family
of four deterministic Medieval buildings: their proportions, orientation,
plaster and roof tones, chimneys, timber frames, foundations, windows, and side
structures vary while their tactical footprint stays the same. This is the
first scenario-specific family, not a universal house that should be recoloured
for every setting; Sengoku, pirate, science-fiction, and other settings need
their own landmark families.

In the Godot map editor, **House 2×2** and **Grove 2×2** place the complete
blocked footprint with one action, and **Remove** clears the whole landmark
from any of its four cells. The editor keeps the exact portable source
identities `mountain_village_house` and `mountain_ancient_grove` and refuses
partial, overlapping, out-of-bounds, occupied, or deployment-area footprints.
Nintendo 64 and PlayStation draw recognizable connected house and grove
silhouettes over the same blocked mountain cells without spending additional
texture memory. The castle-gate draft was deliberately not committed because
its alpha retained a dark halo.

## The full portable library

The portable pixel-art generator contains 672 standing character assets: eight
archetypes × seven styles × six faction colours × two figures. It also emits
their map-scale animation poses and the complete thirteen-terrain set for the
portable modern profile used by previews and shared interface art.

- [Browse all 56 archetype-and-style combinations](../tools/placeholder_art/ROSTER.md).
- [Browse terrain, transitions, animation frames, palettes, and output profiles](../tools/placeholder_art/GALLERY.md).

This larger library is deliberately lower resolution. Godot and both consoles use the curated
high-resolution sheets and team masks instead; a console build refuses a
project whose required curated binding is missing.

## Source and licence

The project owner confirmed that the character sheets were generated by Codex
(GPT-5) in built-in image-generation mode. The tool did not expose a separate
image-renderer version. Existing sheets and the portable roster were used as
visual and design references. Each prompt requests a transparent 4×2 production
sheet, one consistent character, the eight poses listed above, no text or
scenery, and a readable tactical-game silhouette. Generated images are cropped
to exact cell boundaries without repainting the output. The
[tweak guide](ARTWORK_GENERATION.md) explains what provenance was preserved and
what must be recorded for future changes.

Grandleon's source and project-owned artwork are offered under the repository's
[MIT License](../LICENSE). Bundled tools and runtimes retain their own licences
and notices. External art-pack import is not part of the current editor and may
be reconsidered after the built-in art directions are complete.
