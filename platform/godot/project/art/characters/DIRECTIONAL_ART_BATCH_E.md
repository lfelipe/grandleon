# Directional character art: batch E

Batch E adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `commander|*|medieval|first` | `medieval_commander_male_hd.png` | `medieval_commander_male_directions_hd` | stocky older commander, grey moustache and beard, plumed steel helmet, silver plate, blue/gold heraldry and double-headed command axe |
| `beast|*|mythical|second` | `mythical_beast_female_hd.png` | `mythical_beast_female_directions_hd` | athletic female red-scaled dragon, long reptilian snout, rust-red crest, green eyes, cream plumage and patterned cloth |
| `healer|*|nature|first` | `nature_healer_male_hd.png` | `nature_healer_male_directions_hd` | male white rabbit, long ears, blue scarf, tan/cream coat, leaf tabard, herb satchel and gold-green crooked staff |
| `mage|*|pirates|second` | `pirates_mage_female_hd.png` | `pirates_mage_female_directions_hd` | female pirate mage, long dark curls, blue-grey tricorne, blue/gold coat, cream skirt and brass astrolabe focus |
| `commander|*|scifi|second` | `scifi_commander_female_hd.png` | `scifi_commander_female_directions_hd` | female commander, dark asymmetrical bob, white/slate exosuit, blue/gold coat panels and cyan energy sword |
| `healer|*|sengoku|first` | `sengoku_healer_male_hd.png` | `sengoku_healer_male_directions_hd` | elderly bald Japanese monk, white moustache and beard, cream/indigo robes and gold-ringed khakkhara staff |
| `rogue|*|undead|second` | `undead_rogue_female_hd.png` | `undead_rogue_female_directions_hd` | pale grey undead woman, windswept white hair, ragged layered leather, blue-grey torn cloth and paired short blades |
| `knight|*|nature|second` | `nature_knight_female_hd.png` | `nature_knight_female_directions_hd` | female golden-brown fox, long ears, leafy circlet, wood/bronze armor, blue leaf shield and curved leaf sword |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each committed combat sheet above was supplied as the
sole reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: strict invisible 4×2 grid; north/rear idle A, idle
B, walk A, walk B on the top row; south/front idle A, idle B, walk A, walk B on
the bottom row; identical scale and baseline; distinct planted steps; preserved
body type, equipment, handedness, palette, team-colour cloth, proportions, and
painted sprite style. It explicitly rejected relabelled side views, attacks,
effects, ground, shadows, scenery, labels, grids, borders, extra figures,
cropping, and watermarks. The final column above records the per-identity
details added to that prompt.

The mythical beast received one precise identity-preservation follow-up edit.
It restored the reference's longer reptilian snout, swept dragon crest, green
eyes, scale texture, and cream plumage in all eight cells while preserving the
accepted poses, layout, equipment, transparency, and female silhouette.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch E adds 9,829,657
bytes of source PNGs and 3,865,661 bytes of masks (13,695,318 bytes total).
