# Directional character art: batch A

Batch A adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `knight|*|medieval|second` | `medieval_knight_female_hd.png` | `medieval_knight_female_directions_hd` | auburn high ponytail, silver plate, circlet, blue/gold shield and sword |
| `knight|*|scifi|first` | `relay_knight_hd.png` | `relay_knight_directions_hd` | sealed visor, bulky white/slate power armor, cyan sword and angular shield |
| `mage|*|mythical|first` | `mythical_mage_male_hd.png` | `mythical_mage_male_directions_hd` | dark hair, emerald/gold layered robes, rigid circular gold halo |
| `beast|*|nature|second` | `nature_beast_female_hd.png` | `nature_beast_female_directions_hd` | quadrupedal feline/wolf anatomy, leaf mane and tail, wood/bronze armor |
| `archer|*|sengoku|first` | `sengoku_archer_male_hd.png` | `sengoku_archer_male_directions_hd` | mature face, jingasa hat, layered armor, long bow and back quiver |
| `healer|*|undead|second` | `undead_healer_female_hd.png` | `undead_healer_female_directions_hd` | pale woman, deep hood, tattered robes, crooked lantern staff and satchel |
| `rogue|*|pirates|first` | `pirates_rogue_male_hd.png` | `pirates_rogue_male_directions_hd` | dark hair and beard, bandana/scarf, rope/hook details and twin daggers |
| `archer|*|medieval|first` | `medieval_archer_male_hd.png` | `medieval_archer_male_directions_hd` | blue hood and cloak, leather/mail armor, long bow and back quiver |

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

The undead healer received one precise follow-up edit before acceptance:
strengthen only the muted blue-grey heraldic cloth visibility, especially the
first north-walk cell, while preserving all figures, poses, equipment, layout,
and transparency. This allowed the existing reviewed mask threshold to pass;
the threshold was not weakened.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch A adds 9,973,824
bytes of source PNGs and 3,943,234 bytes of masks (13,917,058 bytes total).
