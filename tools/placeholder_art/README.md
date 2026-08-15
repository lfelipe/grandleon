# Placeholder art generator

**Both the generator and everything it generates are committed on purpose**: a
drift check run on every build regenerates the whole tree into a temporary
directory and fails if a single byte differs, so the pixels a client links are
exactly the pixels this Python produces, and nobody has to install Python and
Pillow to build a ROM.

It produces reproducible placeholder art: a seamless, autotiled terrain
tileset and unit sprites, generated from checked-in code rather than drawn by
hand. A project names a terrain theme, a character style and a faction colour,
and the compiler resolves those to indices into what is generated here.
Nothing outside this folder is touched.

[`GALLERY.md`](GALLERY.md) shows the generated art cut by asset kind;
[`ROSTER.md`](ROSTER.md) is the same library cut by unit: sprite, motion,
recolourings and mesh side by side.

## Contents

- [Running it](#running-it)
- [What is generated](#what-is-generated)
- [The autotiling convention](#the-autotiling-convention)
- [Output profiles](#output-profiles)
- [Adding a terrain type](#adding-a-terrain-type)
- [Adding a character archetype](#adding-a-character-archetype)
- [The faction colour menu](#the-faction-colour-menu)
- [Biome and season themes](#biome-and-season-themes)
- [The character style menu](#the-character-style-menu)
- [The terrain registry](#the-terrain-registry)
- [The scene backdrop menu](#the-scene-backdrop-menu)
- [The palette](#the-palette)
- [Module map](#module-map)
- [Known limitations](#known-limitations)

## Running it

Python 3.9+ and Pillow, in a project-local gitignored venv:

```sh
python3 -m venv tools/placeholder_art/.venv
tools/placeholder_art/.venv/bin/pip install -r tools/placeholder_art/requirements.txt

# Regenerate assets/, gallery/, GALLERY.md and ROSTER.md in place.
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py

# Verify the committed output is exactly what the code produces.
tools/placeholder_art/.venv/bin/python tools/placeholder_art/generate.py --check
```

`--check` regenerates into a temporary directory and fails listing every file
that is missing, extra, or different. A full run takes a couple of minutes,
writes several thousand files, and prints the count. `--out DIR` writes
elsewhere while iterating.

Output is byte-identical across runs: all randomness comes from `rng.seed_of`
(a BLAKE2b digest of a fixed string, never the clock or dict order); PNGs are
written by `pngio` with only `IHDR`/`PLTE`/`tRNS`/`IDAT`/`IEND`, filter 0 and
fixed deflate parameters; JSON is dumped with sorted keys; Pillow is pinned.
The one environmental dependency is the zlib build behind Python's `zlib`
module. Within one environment, and therefore within CI and `--check`, output
is stable.

## What is generated

```
assets/<profile>/terrain/<name>_base.png          4 seamless interior variants
assets/<profile>/terrain/<name>_blob.png          47 autotile variants
assets/<profile>/terrain/<name>_over_<under>.png  47 transition variants
assets/<profile>/characters/<archetype>_<colour>.png
assets/<profile>/sample_map.png                   composed proof-of-adjacency
assets/<profile>/palette.png                      shared-palette profiles only
assets/<profile>/palettes.json                    palette(s) for the profile
assets/<profile>/manifest.json                    sizes, layouts, mask tables
assets/palette_usage.json                         what each drawing costs
assets/gltf/<style>/<archetype>.gltf              one mesh as a Blender-openable model
assets/gltf/<style>/<archetype>.bin               its vertex buffer
gallery/*.png, GALLERY.md, ROSTER.md              generated, do not hand-edit
```

`manifest.json` is the file a renderer should read: the tile grid of every
sheet, the master palette, and the complete 256-entry autotile lookup table.

`palette_usage.json` is the file a budget should read: which master palette
entries each character sprite and each terrain base sheet spends, keyed the
way a client selects them, measured off the native canvases before any
profile converts them. The figure is in the key because a role's second
figure is its own drawing, not a transform of the first.
`editor/src/domain/target-budget.ts` adds these rows up to tell an author
what their game would cost a Nintendo 64, reading this file through
`editor/scripts/generate-board-assets.py` rather than carrying a table that
could drift from the pixels.

## The autotiling convention

**47-tile blob set, driven by an eight-bit neighbour mask.** (A 16-variant
four-bit edge set cannot express inner corners, which is exactly where a
naive tileset shows a broken seam.)

```
128   1   2        NW  N  NE
 64   *   4        W   *  E
 32  16   8        SW  S  SE
```

A bit is set when that neighbour holds the same terrain as the centre.
Diagonal bits matter only when both adjoining cardinal bits are set; clearing
the meaningless bits collapses 256 raw masks onto 47 tiles. To pick a tile:
build the mask, index `manifest.autotile.mask_to_variant`, read that variant
from the sheet left-to-right, eight per row. Mask 255 is the interior
variant, and any of the four base variants may substitute for it.

Seams hold by construction: the 3x3 occupancy around a cell is interpolated
into a single globally defined scalar field, and a tile covers every pixel
where `field + jitter > threshold`, so two variants that can legally sit next
to each other agree along their shared edge whichever of the 47 they are. The
jitter is periodic over one tile. Base tiles are seamless because every
texture is built from noise periodic over the tile, and props draw with
wrapping enabled. `verify.py` fails the build if a base tile's wrap-around
luminance step exceeds its interior step, if the mask tables disagree with
the rules above, or if quantising tiles independently differs from quantising
a composed scene.

## Output profiles

One source definition per terrain and archetype; several target machines.
Nothing in `terrain.py` or `characters.py` branches on a target.

| Profile | Tile | Sprite | Encoding | Palette |
| --- | --- | --- | --- | --- |
| `modern` | 32x32 | 32x32 | RGBA8888 | full 124-entry master palette |
| `n64_ci8` | 32x32 | 32x32 | indexed 8bpp | shared 124-entry master palette |
| `n64_ci4` | 32x32 | 32x32 | indexed 4bpp | per-asset 16-colour subset |

A fourth reduction exists and ships nothing. `profiles.LEGIBILITY` is 16x16
and four neutral tones: the smallest a sprite in this library is ever asked
to read at, and therefore where "can two of these be told apart?" has a hard
answer. `verify.check_legibility` measures on it every build and the gallery's
figure pages show what it saw. It is deliberately outside `PROFILES`, because
everything that writes files is driven off that registry and a measuring
instrument is not an output target.

Every reduction is a deliberate, checked-in step:

- **CI4 subsetting** keeps the 16 most-used master colours per asset, ties
  broken by palette index, mapping the rest to their nearest survivor. That is
  the per-texture TLUT bank model the Nintendo 64 uses.
- **Ordered dithering** uses the renderers' own 8x8 Bayer matrix, indexed by
  destination coordinates; tile dimensions are multiples of eight, so the
  pattern is continuous across tiles.
- **Downscaling** is a box filter at an exact integer ratio with
  premultiplied alpha, so sprites do not grow a dark fringe.
- **Tone assignment is a table, not a luminance measurement.** Grass and
  water have nearly identical luminance, so a measured mapping would merge a
  river into its field. The table in `profiles.py` spreads terrain across the
  four tones in a chosen order.

**Adding a profile**: append a `Profile` to `PROFILES` in `profiles.py`; if
it needs a colour mapping the existing `palette_mode` values (`master`,
`subset`, `tones`) do not cover, add a mode in `convert`. Sheets,
manifests, verification and the gallery are driven off the registry. A
profile's tile size must divide 32 and be a multiple of 8 (so: 8, 16, 32),
and the choice is not free: the box filter resolves each downscaled pixel to
the nearest colour in the whole master palette, so halving the resolution
*adds* colours.

### The PlayStation ships as a header, not as PNGs

The Nintendo 64 build runs `mksprite` over the generated PNGs at configure
time. The PlayStation toolchain has no PNG decoder, so
`assets/playstation.h` carries the art already in the form VRAM loads: 4bpp
cells, left pixel of a pair in the **low** nibble, and a sixteen-entry CLUT
per cell as little-endian `SBBBBBGG GGGRRRRR`. It is a repack of `n64_ci4`,
emitted from the same converted assets, so the two cannot drift;
`verify.check_playstation` runs on every build. It carries the four interior
variants of every terrain kind in every theme (not the blob sheets, which is
the same reduction `platform/view/README.md` records), the drop shadow, and
the style menu as names. Each cell is its own array so an executable that names
a handful links only those under `--gc-sections`.

Character art is not in it: each style's sprites go to
`assets/playstation_characters_<style>.h`, and a build includes exactly the
one its project's `characterStyleId` names. Every such header declares the
same symbols, keyed by archetype and faction colour, which is what makes the
choice an include rather than an index. A `[style][archetype][colour]` pointer
table would be one data section naming every style's arrays, and no call-site
care would let the linker drop the unused ones.

### The meshes also come out as glTF

`meshes/` is what a character mesh *is*; the consoles read the integer
tables in `assets/playstation_meshes_<style>.h`. Beside them, `gltf.py`
writes every commissioned figure as glTF 2.0 so a mesh opens in Blender. No
console links the glTF; three readers, deliberately separate:
`verify.check_gltf_round_trip` reconstructs every part's eight integers from
the geometry alone and fails the build if one differs from the table;
`preview3d.load` draws the roster's picture; `gltf.read_model` reads a file
somebody else wrote. A provided mesh is this format, at
`art/provided/gltf/<style>/<archetype>.gltf`, held to every rule
`meshes/rules.py` states for a commissioned figure and refused by name with
its measurement (`check_provided.py`). A submission is just a file at
exactly the path of the asset it stands in for: there is no upload surface
and no registration step, `generate.py --validate-provided` answers yes or no
with the measurement that failed, and not one byte of a submitted file
reaches a generated artefact. A sheet is decoded to palette indices and a
model to a part list of integers, then both are re-emitted by this
repository's own writer.

Conventions, all decided in `gltf.py`: z is negated (figure space is
left-handed, glTF is not) and nothing else; the root node carries a uniform
1/64 so one board tile is one Blender unit; no vertex is shared between
faces, so a viewer cannot smooth what the renderer flat-shades; a mesh
carries no colour (each material's name and `extras` say which ramp and
rung, with `baseColorFactor` a courtesy); parts are authored far-to-near and
the export keeps that order.

## Adding a terrain type

1. Subclass `Terrain` in `terrain.py`: `name`, `layer` (compositing order),
   `rim`.
2. Implement `paint_ground`: build a periodic height field from `noise` and
   hand it to `texture.shade_field`. Describe the surface as a height field,
   not a colour pattern. The shared lighting pass keeps the light direction
   consistent. Non-periodic noise fails the seam check.
3. Optionally implement `paint_props`. Props draw with wrapping enabled; do
   not disable it.
4. Add `rim_over` entries for neighbours where a transition matters. Only a
   terrain of a lower layer is ever the one underneath.
5. Append the class to `TERRAIN_CLASSES` and its name to the end of
   `KEYWORD_ORDER`, with exactly two keywords. See that constant's note.
6. If needed, add the ramp to `_LEGIBILITY_RAMP_TONES` in `profiles.py`.
   There is no free tone left, so a new terrain must be separable by pattern.
7. Give it a letter in `scene.py`'s `LEGEND` and paint it into `SAMPLE_MAP`.

An added kind also needs, outside this library: `terrain_kind_count` and a
keyword row in `tools/game_content`, a sheet symbol in
`platform/nintendo64/src/play_rom.cpp`, a brush in the editor's
`MapEditor.vue`, and a regenerated `editor/src/generated/board-art.ts` from
`editor/scripts/generate-board-assets.py`. That is a different generator from
this one.

## Adding a character archetype

1. Subclass `Archetype` in `characters.py`: `name` and `label`.
2. Implement `draw` with the shared body helpers (`legs`, `torso`, `head`,
   `arm`, `cloak`), then the gear that makes the silhouette distinctive.
   Silhouette beats detail: a shape that survives four shades at half size is
   worth more than a face.
3. Put the faction ramp on a large contiguous area. Colour on a belt is
   invisible, colour on a tabard is not.
4. Append the class to `ARCHETYPE_CLASSES`.

## The faction colour menu

Sprites are generated in every colour a faction can wear, and the filename
says which: `knight_blue.png`, never `knight_dawn_guard.png`. The library
holds colours; a game's factions choose among them through `faction.color`.
`FACTION_COLOURS` in `characters.py` is the menu, in the order the schema
enumerates and every consumer indexes: **blue, red, green, violet, amber,
bone**. A faction that chooses none takes the colour at its own position in
the project's faction list.

Adding a colour: a `FactionColour` entry plus a four-step ramp in
`palette.py` appended after the existing ramps, a tone row in
`_LEGIBILITY_RAMP_TONES`, then the schema enum,
`editor/src/domain/board-art.ts`, and `faction_colour_index` in
`tools/game_content/src/compiler.cpp`.

## Biome and season themes

A **theme** is a substitution of palette ramps and nothing else, legal only
between ramps of the same length. That is what makes a themed tile the
untinted tile recoloured rather than a second drawing. `THEMES` in
`themes.py` is the menu, in schema order: **temperate, autumn, winter,
ashland**. The first substitutes nothing, so a project naming no theme pays
nothing. The default theme's files carry no suffix; every other theme
suffixes its name (`grass_blob_winter.png`), so adding a theme adds files
and moves none.

Adding a theme: a `Theme` entry plus its ramps in `palette.py` (appended),
then the schema enum and `theme_index` in the compiler. The legibility
reduction needs no work: a substituted ramp inherits the tone assignment of
the ramp it replaces.

## The character style menu

A **style** is a set of drawing routines, one per archetype. A theme
recolours art, a style *is* the drawing. A gunslinger is not an archer
recoloured, so a style costs one routine per archetype, sequence cells per
archetype and colour, and a legibility pass. That is a costed art
commission, not a configuration change.

A style is a **setting's dress, and presentation only**. `characterStyleId` is
a choice field on the project and on a unit type, never read by a rule and
never in a canonical hash, so a medieval campaign can raise an undead enemy
and a project restyled from medieval to sci-fi plays a bit-identical battle.
The archetype vocabulary underneath is genre-neutral; a setting supplies art
and names for it:

| Archetype | What the rules do | `medieval` | `scifi` | `pirates` | `undead` |
|---|---|---|---|---|---|
| knight | tough, band 1, high defence | knight | Trooper | Boarder | Barrow knight |
| archer | band 2–3, cannot strike adjacent | archer | Sniper | Musketeer | Bonepicker |
| mage | ignores armour, short band | mage | Psion | Hexer | Wraith |
| stormcaller | area effect | stormcaller | Drone swarm | Gunner | Bellringer |
| healer | restores, acts after striking | healer | Medic | Surgeon | Mourner |
| commander | leader, worth protecting | commander | Captain | Captain | Barrow lord |
| rogue | fast, acts after striking | rogue | Infiltrator | Cutpurse | Grave-thief |
| beast | non-humanoid | wolf | Xenoform | Parrot | Bone hound |

`mythical`, `nature` and `sengoku` dress the same eight, under the `label` on
each class in the generator module beside them.

`STYLES` in `styles.py` is the menu, in schema order, opening on
**medieval**: `scifi`, `mythical`, `nature`, `sengoku`, `undead`, `pirates`
follow, seven in all. `undead` is drawn as the force a player fights, and
still offered in all six faction colours like every other style; nothing
outside these files knows it is meant to be the enemy. `medieval`'s files
carry no suffix, every other style suffixes its name, and **a style is
appended, never inserted**. The menu index is the number the schema enum, the
editor and both consoles agree on, so inserting renumbers all of them at once.

Every style holds every archetype, in every colour, in every profile. That
is what keeps the sprite key (style, archetype, colour) factoring, so a
client indexes archetype by faction without a per-style roster. `styles.py`
refuses at import a style that leaves an archetype undrawn or invents one,
and `verify.check_style_rosters` fails the build per profile if a converted
sprite is missing. The roster is closed at eight archetypes (knight, archer,
mage, stormcaller, healer, commander, rogue, beast) and a ninth
would cost one draw routine in every style, forever, so a style may not add,
remove or rename one. Every manifest character entry carries its
`style`, and each manifest gains a `character_styles` block: default,
roster, and menu with labels and suffixes.

### The screen a candidate style has to pass

Finding a failure before eight routines are written is the point. Three of the
five gates are already enforced by something that fails: a style must dress all
eight roles (`styles.py` refuses at import), its eight silhouettes must
separate (`verify.check_legibility`, on every build), and its look must fit
about five ramps (the sixteen-colour `n64_ci4` cap below; drafts naming seven
materials lost 2.3% and 1.8% of their opaque texels to the remap, and merging
took them to 0.3% and 0.6%).

Two gates are not enforced by anything and have to be applied by a person.

**Is it far enough from the styles already drawn?** Measured by hand over forty
silhouettes; two differing in one pixel of 256 are the same picture.

| closest cross-style pairs, of 256 | |
|---|---:|
| `medieval`/rogue vs `scifi`/rogue | **1** |
| `medieval`/commander vs `scifi`/commander | **4** |
| `medieval`/rogue vs `mythical`/rogue | 7 |
| `medieval`/knight vs `scifi`/healer | 10 |
| `medieval`/commander vs `nature`/commander | 11 |
| the closest pair involving `sengoku` | **16** |

The machinery for it exists (`verify.silhouette_distance`,
`verify.legibility_masks`) and is **deliberately not wired into the build**,
because its first run would fail on art already in the library: that is a
redraw, not a check. **This gate is run by hand, and nothing catches it if
somebody forgets.**

**Does its name say which setting it draws?** A `characterStyleId` names a
world (`sengoku`, `pirates`, `undead`), never a register, a mood or a tone,
and never one of the eight roles. The menu is one dimension, so a name
describing how a style feels leaves the next candidate nothing to be called;
and the identifier reaches authored projects, so renaming one after a project
has written it is a migration rather than an edit.

### Adding a style

1. One `Archetype` subclass per name in `ARCHETYPE_ORDER`: eight drawings,
   not seven. A style leaving one undrawn is refused at import.
2. Append a `Style` to `STYLES`; never insert. The menu index is the number
   the schema, the editor and the consoles agree on.
3. Add the name to the `characterStyleId` enum in
   `schemas/source/v1/project.schema.json` and to `character_style_index` in
   `tools/game_content/src/compiler.cpp`.
4. Regenerate. Sprites, manifests, `assets/styles.h`, the editor's board art
   and contracts are all driven off the registry.
5. Add the setting to `CATALOGUE_SETTINGS` in
   `editor/src/domain/character-recipe.ts`. The art gives an author a
   picture, the catalogue gives them a unit, and a test asserts the two
   menus are the same menu.

A **mesh commission is optional and separate**; a style is complete without
one. To add one: write `meshes/<style>.py` declaring `STYLE` and a `MESHES`
mapping, register it in `meshes/__init__.py`. Everything reaches a figure
through `meshes.parts_for(style, archetype)` and already answers for a style
that has none. Read `meshes/rules.py` before authoring; it holds the rules
and the measurement behind each. In one line: a figure is a short list of
convex axis-aligned boxes, built at `MESH_WORLD_HEIGHT` with its feet at
y = 0, authored far-to-near, every face naming a ramp and a rung and never a
colour, 150 to 300 triangles in all, and held on both screen axes to its own
style's sprite's opaque box within `rules.WIDTH_TOLERANCE`, 8 world units.
The rules are necessary rather than sufficient.

Brief a sprite commission at **sixteen colours per sprite** (the `n64_ci4`
cap, which binds in practice) and therefore at **about five materials**: a
shading ramp costs three to five entries once it covers more than a few
pixels, and a style drawn with seven or eight materials loses measurably
more texels to the subset's silent nearest-neighbour remap. Decide the
material list before drawing and write it in the module's docstring; decide
which ramp the style will *not* name. A refusal costs nothing and survives
every reduction. Review a new style in `n64_ci4` and at the legibility
reduction before `modern`. Those two can lose a silhouette where `modern`
never will. An external artist is
briefed on the sheet as well: `characters/<archetype>_<colour>.png` is one
32x32 standing cell and `..._frames.png` is one row of four, in the fixed
order `walk_contact`, `walk_pass`, `lunge`, `cast`, indexed by position and
never by name. The standing art fills the cell, so nothing may sit on column
0 or column 31 below row 24. `walk_contact` displaces everything at or
below that row outward by one column, and `frames.displace` refuses with
the offending pixel's coordinates rather than clipping (`cell-margin`).

**A style may not grow the palette.** Two mechanisms, not a preference: the
`n64_ci8` profile writes the whole master palette into every asset's `PLTE`,
so one appended entry rewrites all 1,551 checked-in `n64_ci8` PNGs; and the
legibility reduction resolves each downscaled pixel against the whole
palette, so an appended colour can move a measurement nobody touched. A style
that cannot be drawn from the existing ramps is a palette change in its own
right: regenerate every profile and re-measure.

**The legibility pass is part of the commission.** `verify.check_legibility`
runs on every build, over every style, and refuses in both directions: a
figure too weak to see and a figure that stopped reading as its role. The
separation property is an ordering: every within-role distance is strictly
smaller than the smallest cross-role distance, with a floor of 8 on the
within-role difference, measured on the native silhouette. A style's second
figures owe three more refusals, also run on every build: the same kit calls
for both figures (`verify.check_kit_shared`), the second figure's box inside
the first's, and no shading ramp the first does not name
(`verify.check_figure_room`).

## The terrain registry

A terrain is selected by name, and the generator owns the table: each recipe
declares the keywords an authored name may contain, its flat colour, and its
mark. `KEYWORD_ORDER` in `terrain.py` settles ambiguity: the first terrain
with a keyword in the lowered name wins, so "mountain road" is a road. Grass
is last because it is also open ground's name.

That registry, the theme menu and each terrain's flat colour per theme are
emitted three ways from one definition: every `manifest.json`,
`editor/src/generated/board-art.ts` (via
`editor/scripts/generate-board-assets.py`), and `assets/themes.h` for native
clients. All three carry names, colours, keywords and substitutions, and no
pixels. The style menu travels the same three ways; its header is
`assets/styles.h`, names only, because a console binds its style when the ROM
is built.

## The scene backdrop menu

What a conversation between maps is drawn against. It travels the same three
ways (manifest, editor board art, `assets/backdrops.h`) and emits no asset
files at all, because a backdrop is a table: an ordered run of flat
horizontal bands, each a number of rows in one master palette entry. The
shape is forced by console budgets: a full-screen image is 38,400 bytes of
CI4 against the 2,048 a Nintendo 64 texture may hold in TMEM; bands cost one
rectangle each.

**Append** to `BACKDROPS` in `backdrops.py`; menu order is index order
everywhere. Then add the name to the `backgroundId` enum in
`schemas/source/v1/dialogue.schema.json`, the compiler menu, and the option
labels in `editor/src/domain/source-form-model.ts`. Four rules are asserted
at import: bands sum to exactly `BACKDROP_ROWS` (twenty-eight); at most six
bands, none narrower than two rows; no band names the transparent entry; and
every band reaches 4.5:1 against the colour a scene's words are drawn in. A
dawn sky cannot carry cream letters, so the library does not contain one.
Spend only ramps the palette already holds.

## The palette

**124 entries: index 0 transparent, 123 opaque**, organised into named ramps
running dark to light. Renderers never name a colour; they pick a position
along a ramp from a lighting term. The size makes a Nintendo 64 CI8 texture
trivial and a CI4 subset obvious, and is asserted at import.

The terrain base colours match `editor/src/domain/terrain-presentation.ts`,
and the first two faction ramps carry the tactical board's side colours
(`#2375a9`, `#b3483f`) as their third step, so a sprite and its board
highlight agree.

Ramp order is palette index order, so **new ramps are appended, never
inserted**. Appending leaves every existing indexed asset untouched. The one
thing an append still moves is the legibility reduction, whose box filter
resolves against the whole palette; that tone shift is the price of a growing
palette, and it lands on a measurement rather than on a shipped file.

## Module map

| Module | Responsibility |
| --- | --- |
| `palette.py` | the fixed 124-entry palette and its ramps |
| `themes.py` | the biome and season menu, as ramp substitutions |
| `rng.py` | deterministic seeds and random numbers |
| `noise.py` | periodic value noise |
| `texture.py` | height-field lighting |
| `raster.py` | palette-index canvas and lit drawing primitives |
| `autotile.py` | the blob mask convention and its coverage field |
| `terrain.py` | one class per terrain type |
| `characters.py` | one class per archetype, plus the factions |
| `styles.py` | the character style menu |
| `figures.py` | the figure menu |
| `meshes/rules.py` | what a character mesh is, and every rule one is held to |
| `meshes/<style>.py` | one style's mesh commission |
| `backdrops.py` | the scene backdrop menu |
| `scene.py` | the composed sample map |
| `profiles.py` | output targets and their quantisation rules |
| `playstation_header.py` | `n64_ci4` repacked as PlayStation VRAM data |
| `pngio.py` | the deterministic PNG encoder |
| `pixelfont.py` | a 3x5 bitmap font for gallery labels |
| `verify.py` | seam, adjacency and legibility assertions, every build |
| `gallery.py` | contact sheets, `GALLERY.md` and `ROSTER.md` |
| `preview3d.py` | draws an exported mesh at the shipped camera |
| `build.py` | orchestration |

## Known limitations

- **Humanoid sprites are marginal at the legibility reduction.** A 32x32
  figure box-filtered to 16x16 loses its gear; the silhouette-restoration
  pass keeps a hard border, but a knight and a rogue are hard to tell apart.
  Reading properly at that size needs sprites authored natively at 16x16.
  The `commander` and `beast` survive best; `knight`, `rogue` and `archer`
  worst.
- **Four tones cannot separate thirteen terrains.** Forest and mountain share
  the darkest tone; `farmland`, `bamboo` and `paved` are told apart from
  their tone-mates by repeating geometry (stripes, verticals, a grid), which
  is the rule for anything added.
- **Four tones cannot separate six faction colours.** The extremes are spent
  on blue and red; green against violet reads as neighbouring mid-tones at
  the reduction. Every shipped profile keeps all six apart.
- **The composed sample map is a single bank on the low-colour profiles.** One
  PNG, one palette. Judge the individual sheets, not the composed proof.
- Terrain variety is four interior variants per type, so a very large field
  of one terrain shows repetition.
