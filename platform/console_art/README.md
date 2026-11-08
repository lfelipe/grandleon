# Shared console character art

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
Godot uses these views on vertical route legs. Missing or invalid optional
metadata still falls back safely to the side walk. The compact-console
generator reports which project drawings have source coverage and their
projected CI4 cost; console runtime integration remains deliberately disabled
until the active renderer-specific visual/performance gate is complete.

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
stores it as `DIRART.BIN`. Neither production renderer opens the bank yet, so
the runtime switch remains off and current executable, RDRAM, RAM, TMEM and
VRAM behaviour is unchanged. The next phase is a bounded four-frame moving-unit
cache on each console.

Run the bank-format check with:

```sh
python3 platform/console_art/check-directional-bank.py
```
