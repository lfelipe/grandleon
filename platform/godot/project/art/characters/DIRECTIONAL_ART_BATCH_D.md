# Directional character art: batch D

Batch D adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `beast|*|medieval|first` | `medieval_beast_male_hd.png` | `medieval_beast_male_directions_hd` | quadrupedal boar-bear, tusks, forehead/shoulder plate, blue lion barding |
| `commander|*|mythical|second` | `mythical_commander_female_hd.png` | `mythical_commander_female_directions_hd` | swept silver hair, blue military coat, gold epaulettes, curved saber |
| `mage|*|nature|first` | `nature_mage_male_hd.png` | `nature_mage_male_directions_hd` | horned owl man, leaf cloak, blue scarf, amber seed-crystal branch staff |
| `knight|*|pirates|second` | `pirates_knight_female_hd.png` | `pirates_knight_female_directions_hd` | dark braid, plumed morion, anchor coat, cutlass and ship-wheel buckler |
| `healer|*|scifi|second` | `scifi_healer_female_hd.png` | `scifi_healer_female_directions_hd` | segmented ponytail, medical headset, tech armor, canisters and injector |
| `rogue|*|sengoku|first` | `sengoku_rogue_male_hd.png` | `sengoku_rogue_male_directions_hd` | masked shinobi, tied ponytail, quilted tunic, throwing knives and sheathed sword |
| `knight|*|undead|second` | `undead_knight_female_hd.png` | `undead_knight_female_directions_hd` | pale undead woman, long white hair, dark plate, wavy sword and kite shield |
| `stormcaller|*|medieval|first` | `medieval_stormcaller_male_hd.png` | `medieval_stormcaller_male_directions_hd` | crowned bearded man, blue feather mantle, layered robes and cloud staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole identity reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: strict invisible 4x2 grid; north/rear idle A, idle
B, walk A, walk B on the top row; south/front idle A, idle B, walk A, walk B on
the bottom row; identical scale and baseline; distinct planted steps; preserved
body type, equipment, palette, team-colour regions, proportions, and painted
sprite style. It explicitly rejected relabelled side views, attacks, effects,
ground, shadows, scenery, labels, grids, borders, extra figures, cropping, and
watermarks. The final column above records the identity-specific details added
to that prompt.

The nature mage received one precise cleanup edit that replaced only the
generator's opaque white canvas and faint floor shading with alpha. The undead
knight received one precise cleanup edit that removed only gray/white residue,
halos, and floor haze. Both edits preserved the accepted figures, equipment,
poses, positions, scale, layout, and colors.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch D adds 10,106,646
bytes of source PNGs and 3,933,761 bytes of masks (14,040,407 bytes total).
