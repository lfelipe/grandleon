# Directional character art

The board keeps combat poses in each character's existing action sheet. A
covered character also has a sibling `*_directions_hd.png` sheet for real
north- and south-facing movement:

| Row | Cell 0 | Cell 1 | Cell 2 | Cell 3 |
|---|---|---|---|---|
| Top | north idle A | north idle B | north walk A | north walk B |
| Bottom | south idle A | south idle B | south walk A | south walk B |

Both sheets are 1772×886 RGBA images with eight 443×443 cells and a ground
pivot at `(0.5, 0.94)`. The adjacent `*_team_mask.png` is mandatory. East and
west continue to use the original right-facing walk and its mirror, so combat
and side silhouettes do not change.

## Production coverage

The first slice deliberately spanned every visual style and several roles and
body types:

- medieval knight male
- sci-fi knight female
- mythical mage female
- nature beast male
- sengoku archer female
- undead healer male
- pirates rogue female

Directional batches A through M add another 105 identities, bringing actual
coverage to all 112 curated bodies. Their exact identity keys,
reference files, generation anchors, and accepted follow-up edits are recorded
in `DIRECTIONAL_ART_BATCH_A.md`, `DIRECTIONAL_ART_BATCH_B.md`,
`DIRECTIONAL_ART_BATCH_C.md`, `DIRECTIONAL_ART_BATCH_D.md`,
`DIRECTIONAL_ART_BATCH_E.md`, `DIRECTIONAL_ART_BATCH_F.md`,
`DIRECTIONAL_ART_BATCH_G.md`, `DIRECTIONAL_ART_BATCH_H.md`,
`DIRECTIONAL_ART_BATCH_I.md`, `DIRECTIONAL_ART_BATCH_J.md`,
`DIRECTIONAL_ART_BATCH_K.md`, `DIRECTIONAL_ART_BATCH_L.md`, and
`DIRECTIONAL_ART_BATCH_M.md`.

The explicit bindings live in `content/graphics_profiles.json`. Missing or
invalid directional metadata in an external pack falls back independently to
the ordinary side walk.

## Generation record

The first seven sheets were produced on 2026-11-07 with the OpenAI image
generator available through Codex, using each existing `*_hd.png` combat sheet
as the visual reference. Batches A through M used the same service and contract;
their per-identity provenance is linked above. The service response did not
expose an immutable model version, so recording a guessed model name here would
make later reproduction less reliable. The durable inputs are the committed
reference image, this prompt contract, the batch records, and the deterministic
normalization command.

Common prompt contract:

> Create a new transparent production tactical-game companion sheet for the
> exact character in the reference. Use an invisible four-column by two-row
> grid. The top row is genuine rear/north idle A, idle B, walk A, walk B. The
> bottom row is genuine front/south idle A, idle B, walk A, walk B. Preserve
> identity, costume, palette, equipment, proportions, rendering style, strong
> team-colour regions, scale, baseline, and transparent cell margins. Use
> distinct planted steps. Include exactly eight full-body figures and no
> scenery, ground, shadows, labels, grid, border, effects, or watermark. Do not
> reuse or relabel a side-facing pose.

Each prompt also named the reference character's defining equipment and style.
The pirates rogue result received one precise cleanup edit: remove only the
grey/white smoky residue around the scarf and restore transparency, without
redrawing or repositioning the figures.

Accepted results were normalized exactly once and the team masks were derived
from the existing reviewed per-body mask recipes:

```sh
python3 platform/console_art/prepare-directional-sheet.py \
  --input accepted-generator-result.png \
  --reference platform/godot/project/art/characters/medieval_knight_male_hd.png \
  --output platform/godot/project/art/characters/medieval_knight_male_directions_hd.png
```

The preparation tool rejects empty cells, unsafe gutters, and walk phases that
are too similar to idle. `platform/console_art/check-directional-art.py` then
checks the profile, source/mask dimensions, distinct front/back silhouettes,
walk phases, and projected console budget. It must pass whenever coverage is
expanded.
