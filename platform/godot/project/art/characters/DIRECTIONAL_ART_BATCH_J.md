# Directional character art: batch J

Batch J adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|nature|first` | `nature_archer_male_hd.png` | `nature_archer_male_directions_hd` | male red fox, white muzzle and tail tip, leaf cape, navy scarf and tabard, scale leather, leaf bow and quiver |
| `beast|*|pirates|first` | `pirates_beast_male_hd.png` | `pirates_beast_male_directions_hd` | male charcoal-blue eagle-gryphon, white crest, gold beak and talons, four legs, navy anchor barding and rope harness |
| `commander|*|mythical|first` | `mythical_commander_male_hd.png` | `mythical_commander_male_directions_hd` | older dark-skinned commander, gray hair and beard, gold diadem, bronze armor, blue lion cape and curved saber |
| `healer|*|pirates|second` | `pirates_healer_female_hd.png` | `pirates_healer_female_directions_hd` | female healer, dark braid, white sun headscarf, navy-and-cream dress, medical satchel, vials, bandages and brass lantern |
| `knight|*|sengoku|second` | `sengoku_knight_female_hd.png` | `sengoku_knight_female_directions_hd` | female knight, long black braid, crescent kabuto, dark lamellar armor, navy wave sleeves, rope sash and naginata |
| `mage|*|medieval|first` | `medieval_mage_male_hd.png` | `medieval_mage_male_directions_hd` | older white-haired mage, blue floppy hat and robes, satchel and book, carved staff with gold amber orb |
| `rogue|*|mythical|first` | `mythical_rogue_male_hd.png` | `mythical_rogue_male_directions_hd` | stern male rogue, swept black hair and silver side braid, purple rune scarf and panels, quilted armor and paired broad daggers |
| `stormcaller|*|nature|first` | `nature_stormcaller_male_hd.png` | `nature_stormcaller_male_directions_hd` | male stag, broad leafed antlers, brown-and-white fur, leaf mantle, blue leaf tabard, stone armor and forked branch staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. The existing combat sheets were inspected as the
identity references, and their defining visual anchors were carried into each
identity-specific prompt.

The shared prompt requested a new transparent production sprite sheet for the
exact identity: wide 2:1 canvas; strict invisible 4x2 grid; genuine north/rear
idle A, idle B, walk A, walk B on the top row; genuine south/front idle A, idle
B, walk A, walk B on the bottom row; consistent scale and silhouette; generous
transparent cell gutters; and distinct planted opposite steps. It required
preservation of body type, face, equipment, palette, team-colour regions,
proportions, and the painted tactical-RPG character style. It explicitly
rejected relabelled side views, attacks, effects, ground, shadows, scenery,
labels, visible grids, borders, extra figures, cropping, and overlap. The final
column above records the identity-specific details added to that prompt.

The mythical commander and nature stormcaller each received a precise
background-extraction pass after their initial result contained an off-white
canvas. The nature stormcaller then received one composition correction that
added transparent headroom around his antlers so every cell passed the safety
gutter contract. These corrections preserved the accepted identities,
equipment, directions, animation phases, palette, and 4x2 layout.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch J adds 9,427,171
bytes of source PNGs and 3,711,432 bytes of masks (13,138,603 bytes total).
