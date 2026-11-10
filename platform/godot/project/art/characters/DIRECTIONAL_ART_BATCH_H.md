# Directional character art: batch H

Batch H adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|mythical|first` | `mythical_archer_male_hd.png` | `mythical_archer_male_directions_hd` | bright-blue hair, purple rune hood and cloak, leather armor, ornate recurved bow and quiver |
| `beast|*|pirates|second` | `pirates_beast_female_hd.png` | `pirates_beast_female_directions_hd` | eagle-gryphon body, white crest, gray plumage, four taloned legs, anchor barding and rope harness |
| `commander|*|scifi|first` | `rook_commander_hd.png` | `rook_commander_directions_hd` | sealed white armor, amber visor, navy command cape, cyan belt canister and plasma sword |
| `healer|*|mythical|second` | `mythical_healer_female_hd.png` | `mythical_healer_female_directions_hd` | long brown braid, white sun headscarf, gold scarf and tabard, practical coat and cyan hand focus |
| `knight|*|nature|first` | `nature_knight_male_hd.png` | `nature_knight_male_directions_hd` | stocky ram man, enormous curled horns, leaf mantle, stone armor, oak shield and leaf sword |
| `mage|*|pirates|first` | `pirates_mage_male_hd.png` | `pirates_mage_male_directions_hd` | older gray-haired navigator, compass tricorn, chart satchel and brass armillary focus |
| `rogue|*|medieval|first` | `medieval_rogue_male_hd.png` | `medieval_rogue_male_directions_hd` | bearded dark hood, navy lion scarf, quilted leather, pouches, keys and paired daggers |
| `stormcaller|*|scifi|first` | `morrow_stormcaller_hd.png` | `morrow_stormcaller_directions_hd` | masked cyan visor, purple hood and rune cloak, rear antenna, segmented armor and twin-pronged focus |

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
sprite rendering style. Rook and Morrow explicitly retained the crisp pixel
art of their reference sheets. The prompt rejected relabelled side views,
attacks, effects, ground, shadows, scenery, labels, grids, borders, extra
figures, cropping, and watermarks. The final column above records the
identity-specific details added to that prompt.

The mythical archer received one precise correction to narrow and split only
the four rear cloaks, exposing his leather back, straps, sleeves, and quiver.
This kept team tint readable without overwhelming the rear silhouette and
preserved the accepted identity, equipment, poses, positions, scale, layout,
front row, and colours.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch H adds 9,711,321
bytes of source PNGs and 3,920,075 bytes of masks (13,631,396 bytes total).
