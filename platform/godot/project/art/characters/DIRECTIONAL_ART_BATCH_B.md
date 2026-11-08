# Directional character art: batch B

Batch B adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `commander|*|medieval|second` | `medieval_commander_female_hd.png` | `medieval_commander_female_directions_hd` | dark-skinned woman, plate armor, blue plume and lion tabard, halberd with lion pennant |
| `healer|*|mythical|first` | `mythical_healer_male_hd.png` | `mythical_healer_male_directions_hd` | white turban-cap, white/gold robes and cape, belt pouches, sunburst healing mace |
| `rogue|*|nature|second` | `nature_rogue_female_hd.png` | `nature_rogue_female_directions_hd` | russet fox woman, cream-tipped tail, leaf mantle, blue scarf, paired leaf daggers |
| `stormcaller|*|pirates|first` | `pirates_stormcaller_male_hd.png` | `pirates_stormcaller_male_directions_hd` | bearded pirate, feathered tricorn, blue cape, rope and compass, chained harpoon |
| `beast|*|scifi|first` | `relay_beast_hd.png` | `relay_beast_directions_hd` | quadrupedal purple relay beast, cyan reactor and eyes, silver claws, twin ducted rotors |
| `knight|*|sengoku|first` | `sengoku_knight_male_hd.png` | `sengoku_knight_male_directions_hd` | stern samurai, gold antler crest, black/bronze lamellar armor, blue jinbaori, sheathed katana |
| `archer|*|undead|second` | `undead_archer_female_hd.png` | `undead_archer_female_directions_hd` | pale undead woman, green eyes, white braid, ragged navy hood, bow and back quiver |
| `mage|*|medieval|second` | `medieval_mage_female_hd.png` | `medieval_mage_female_directions_hd` | silver bob, blue hooded robe, scroll cases and astrolabe charm, amber-orb staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: strict invisible 4x2 grid; north/rear idle A, idle
B, walk A, walk B on the top row; south/front idle A, idle B, walk A, walk B on
the bottom row; identical scale and baseline; distinct planted steps; preserved
body type, equipment, palette, team-colour regions, proportions, and painted
sprite style. It explicitly rejected relabelled side views, attacks, effects,
ground, shadows, scenery, labels, grids, borders, extra figures, cropping, and
watermarks. The final column above records the identity-specific details added
to that prompt.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch B adds 9,755,135
bytes of source PNGs and 3,935,330 bytes of masks (13,690,465 bytes total).
