# Shared console art

## Castle

The stone keep comes from Godot's `stone_keep.gd` and castle-cell geometry.
`stone_keep.json` and `stone_keep_masonry.png` are its portable source assets;
`stone_keep.py` builds console meshes from them. Exports check their source
hashes and refuse stale geometry. After editing the keep, regenerate them with
`platform/godot/tests/export_stone_keep.gd`, using the staged Godot project and
the Steel and Cedar source. PS1 simplifies small details and subdivides the
terrace so its floor cannot draw over the roof.

## Settlement scenery

`settlement_scenery.json` contains simpler meshes exported from Godot's
`settlement_scenery.gd`: a cart, well, garden, gate tower and wall
junctions. It also exports the orchard bell, signal beacons and broken watch
walls and branching orchard trees directly from the playable Godot board
builders. Leaves and branches use deliberately smaller meshes; their native
colours include a baked ambient lift while retaining their leaf and bark hues.
The bell and low walls remain passable:
the bell spans two adjacent cells, and low walls stay along cell edges.
Both console generators check the scenery, board and natural-scenery hashes and reject
stale geometry. Regenerate with `platform/godot/tests/export_settlement_scenery.gd`
using a real rendering server (`xvfb-run` and `--rendering-method gl_compatibility`
work locally). The headless dummy renderer discards instance transforms and is
refused by this exporter.
PS1 stores each model once and places it separately from terrain pages; its
build audit still enforces main RAM and texture limits. Console prop animation
and the busier village's frame budget remain open work.

## Shared houses and ground

Houses share Godot’s sixteen drawings: four Medieval buildings and two each
for Sengoku, pirates, sci-fi, mythical, nature and undead. The editor authors
each family explicitly; all retain the same blocked 2×2 footprint.
`scenery_style.json` in the Godot content folder defines drawing choices,
mirroring, small placement offsets and size. Nintendo 64 uses transparent
64×64 CI8 sprites, drawn in three overlapping strips so the palette and texture
fit TMEM without horizontal joins. PlayStation gives each 96×96 house its own
palette in the already reserved CLUT area; unit poses keep their shared palette.
Changing the drawing or recipe updates all three builds. The ancient grove
uses the same source drawing too, with a transparent square canvas preserving
its proportions and a seasonal foliage tint.

`natural_materials/` contains 52 tiles rendered from Godot's real ground shader
and scene lighting: thirteen terrain kinds across four seasons. The generators
check the shader, ground-script and graphics-profile hashes before using them.
After changing those sources, regenerate with
`platform/godot/tests/export_natural_materials.gd`, passing `--repo` and
`--output` after `--`. Nintendo 64 uses a compact luminance atlas tinted with
the sampled colours with a modest saturation correction. Its non-playable
surroundings share those textures and blend neighbouring colours instead of
using dark solid squares. PlayStation uses the tile images in its material
atlas; its continuous floor is independent of paged nearby scenery. Large
floors are split into strips to respect the per-mesh vertex limit.
Neither console runs Godot's shader, so this shares the artwork and palette
without implying identical lighting or geometry.

## Characters

The Nintendo 64 and PlayStation builds generate their character cutouts from
the same curated high-resolution sheets and team-colour masks as Godot. Run
`generate-character-art.py` with a project source and a platform name; the
output belongs in a build directory, not in source control.

Each base body sheet is a 4×2 animation sheet: two idle cells, two walking
cells, wind-up, follow-through, ability and hurt. It has one native
right-facing view. The renderers use that view for eastward travel and mirror
it for westward travel. This costs no additional texture memory and keeps the
current character ROM, RDRAM/VRAM and frame budgets unchanged.

All 112 curated bodies now have a separate authored 4×2 directional sheet:
genuine north idle/walk on the top row and genuine south idle/walk on the
bottom row. They span all seven styles. The matching team mask and explicit
`directionalSheet` profile binding keep the same faction-colour workflow.
Godot uses these views on vertical route legs. Nintendo 64 now stages six
64×64 CI4 sprites per project drawing: north/south idle, planted contact and
passing poses. Units retain their facing after movement and turn with the
accepted route. Steel and Cedar's full five-battle replay fits base 4 MiB
memory with these sprites resident. PlayStation uses paged north/south walking
poses. Drawings without optional compass art keep their side view.

Run the focused source audit with:

```sh
python3 platform/console_art/check-directional-art.py
```

The generator now writes those frames to `directional_art.gdb`, beside its
existing output, instead of adding them to an executable. The file is a small
checked format: a 40-byte header, one sorted 8-byte identity/offset record per
available project drawing, then four fixed 2,080-byte frames in north idle,
north walk, south idle, south walk order. Each frame is 64×64 CI4 texels plus a
16-entry BGR555 palette. The whole file has a CRC-32; readers reject unknown
geometry, oversized indexes, bad offsets, duplicates, truncation, corruption,
and a non-transparent palette index zero before returning a frame. A missing
identity returns no record, which tells the renderer to use its exact existing
side view.

Every integer is little-endian. The header stores magic `GLDIRC1\0`, version,
header size, side, frames per drawing, palette entries, index-record size,
record count, index offset, payload offset, file size and CRC-32 in that order.
An index record is four one-byte style/figure/archetype/colour IDs followed by
the absolute four-byte payload offset. The CRC field is treated as zero while
calculating the CRC over the complete file.

`directional_art.json` is the human-readable build report. It names every
identity and offset and states the checksum and measured sizes. One resolved
team colour for all 112 bodies has an 896-byte compact index, a 931,840-byte
frame payload, and a 932,776-byte complete bank. Actual project builds include
only the body/team-colour drawings that project can name: Sevenfold's 56 are
466,408 bytes and Tarnholt plus the conformance demo's 15 are 124,960 bytes.

The Nintendo 64 build puts the bank at `rom:/art/directional.gdb` in DragonFS.
The PlayStation build stages it at `build-playstation/console-art/` and a disc
stores it as `DIRART.BIN`. Neither production renderer opens this packed bank
yet. The active N64 sprite and PS1 pose-paging paths above use the same source
art independently. A bounded reader for the packed bank remains a possible
way to reduce resident memory later.

Run the bank-format check with:

```sh
python3 platform/console_art/check-directional-bank.py
```

To capture a compiled PS1 scene for visual review, use
`platform/playstation/mipsync/scripts/capture-scene-review.sh` with the scene
manifest, direct-battle build directory and an empty output directory. It
checks the actual map/state identity and records frame-budget misses; it is
separate from the strict performance regression test.
