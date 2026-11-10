# Directional character art: batch C

Batch C adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `beast|*|medieval|second` | `medieval_beast_female_hd.png` | `medieval_beast_female_directions_hd` | grey quadrupedal she-wolf, yellow eyes, silver head/shoulder plate, blue/gold side cloth and bushy tail |
| `knight|*|mythical|second` | `mythical_knight_female_hd.png` | `mythical_knight_female_directions_hd` | athletic auburn high ponytail, silver plate, long blue/gold tabard, matching shield and sword |
| `commander|*|nature|first` | `nature_commander_male_hd.png` | `nature_commander_male_directions_hd` | broad anthropomorphic lion, golden mane, leaf/leather/bronze armor, blue/gold cape and curved leaf polearm |
| `healer|*|pirates|first` | `pirates_healer_male_hd.png` | `pirates_healer_male_directions_hd` | older white-bearded pirate, tricorne, blue coat, cream sun tabard, medical satchel, lantern and cudgel |
| `archer|*|scifi|second` | `scifi_archer_female_hd.png` | `scifi_archer_female_directions_hd` | short auburn hair, cyan visor, white/slate segmented armor, blue cape and cyan high-tech bow/quiver |
| `mage|*|sengoku|second` | `sengoku_mage_female_hd.png` | `sengoku_mage_female_directions_hd` | long dark hair and flower, indigo/grey kimono, cream sleeves, red obi cords and fan |
| `stormcaller|*|undead|first` | `undead_stormcaller_male_hd.png` | `undead_stormcaller_male_directions_hd` | large horned undead warrior, ragged blue cloak/tabard and massive bone-headed storm hammer |
| `healer|*|medieval|second` | `medieval_healer_female_hd.png` | `medieval_healer_female_directions_hd` | mature silver-haired woman, white sun headscarf, long braid, blue/cream healer robes, satchel, staff and sun charm |

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

The nature commander received a background-extraction cleanup that removed
only the generated white background and retained the eight figures. The sci-fi
archer received one precise follow-up edit that neutralized only the deepest
central cape folds in the first two north-facing idle cells. This removed false
team-mask matches while preserving the figures, equipment, poses, layout, and
transparency. Both sheets then passed the existing reviewed mask threshold; no
threshold was weakened.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch C adds 10,252,025
bytes of source PNGs and 4,009,946 bytes of masks (14,261,971 bytes total).
