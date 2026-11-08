# Directional character art: batch F

Batch F adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `rogue|*|medieval|second` | `medieval_rogue_female_hd.png` | `medieval_rogue_female_directions_hd` | auburn curls, dark beret, navy lion cloak, leather armor, keys, rope, paired knives |
| `stormcaller|*|mythical|first` | `mythical_stormcaller_male_hd.png` | `mythical_stormcaller_male_directions_hd` | swept gray hair and beard, bronze feather mantle, crimson robes, eagle-headed crystal staff |
| `archer|*|nature|second` | `nature_archer_female_hd.png` | `nature_archer_female_directions_hd` | anthropomorphic lynx woman, spotted fur and ringed tail, leaf cape, carved bow and quiver |
| `commander|*|pirates|first` | `pirates_commander_male_hd.png` | `pirates_commander_male_directions_hd` | long dark hair, beard and curled mustache, feathered tricorn, naval coat, cutlass and spyglass |
| `mage|*|scifi|second` | `scifi_mage_female_hd.png` | `scifi_mage_female_directions_hd` | dark skin, silver braided bun, navy mantle, white armor, cyan gauntlet and segmented shoulder ring |
| `beast|*|sengoku|first` | `sengoku_beast_male_hd.png` | `sengoku_beast_male_directions_hd` | quadrupedal Akita war dog, enormous curled tail, rope harness, lamellar armor and wave-pattern barding |
| `mage|*|undead|first` | `undead_mage_male_hd.png` | `undead_mage_male_directions_hd` | exposed skull and hands, ragged blue hood and robes, chained grimoire and green-crystal wood staff |
| `archer|*|pirates|second` | `pirates_archer_female_hd.png` | `pirates_archer_female_directions_hd` | thick high ponytail, navy pirate coat, layered belts, compact crossbow and bolt quiver |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole identity reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: strict invisible 4x2 grid; north/rear idle A, idle
B, walk A, walk B on the top row; south/front idle A, idle B, walk A, walk B on
the bottom row; identical scale and baseline; distinct planted steps;
preserved body type, equipment, palette, team-colour regions, proportions, and
painted sprite style. It explicitly rejected relabelled side views, attacks,
effects, ground, shadows, scenery, labels, grids, borders, extra figures,
cropping, and watermarks. The final column above records the identity-specific
details added to that prompt.

The mythical stormcaller and pirates commander each received one precise
background-extraction pass after their initial result contained an off-white
canvas. Those passes removed only the canvas and faint background haze while
preserving the accepted figures, equipment, poses, positions, scale, layout,
and colours.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch F adds 9,711,422
bytes of source PNGs and 3,828,954 bytes of masks (13,540,376 bytes total).
