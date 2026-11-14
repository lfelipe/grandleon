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
`settlement_scenery.gd`: houses, a cart, well, garden, gate tower and wall
junctions. Both console generators check the source hash and reject stale
geometry. Regenerate with `platform/godot/tests/export_settlement_scenery.gd`.
PS1 stores each model once and places it separately from terrain pages; its
build audit still enforces main RAM and texture limits. Console prop animation
and the busier village's frame budget remain open work.

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
