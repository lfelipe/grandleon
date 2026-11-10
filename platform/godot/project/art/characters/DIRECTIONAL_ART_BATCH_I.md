# Directional character art: batch I

Batch I adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|mythical|second` | `mythical_archer_hd.png` | `mythical_archer_directions_hd` | female archer, long auburn braid, purple-and-gold cloak and tabard, dark leather armor, white-feathered quiver and ivory-gold recurved bow |
| `beast|*|scifi|second` | `scifi_beast_female_hd.png` | `scifi_beast_female_directions_hd` | low female quadrupedal cyber-drake, purple armor, charcoal limbs, cyan eyes/core/rings, dorsal fins and segmented scorpion tail |
| `commander|*|sengoku|first` | `sengoku_commander_male_hd.png` | `sengoku_commander_male_directions_hd` | stocky male commander, black moustache and beard, sun-crested kabuto, dark lamellar armor, indigo-gold haori, yari and sashimono |
| `healer|*|nature|second` | `nature_healer_female_hd.png` | `nature_healer_female_directions_hd` | female hedgehog, layered brown-and-cream quills, blue headband/scarf/tabard, cream coat, herb basket and amber-seed living staff |
| `knight|*|pirates|first` | `pirates_knight_male_hd.png` | `pirates_knight_male_directions_hd` | broad male privateer, feathered tricorne, weathered breastplate, navy-gold coat and anchor tabard, cutlass and ship-wheel buckler |
| `mage|*|sengoku|first` | `sengoku_mage_male_hd.png` | `sengoku_mage_male_directions_hd` | male onmyoji, moustache and goatee, tied black hair, tall eboshi, prayer beads, indigo wave-pattern robes, fan and paper talismans |
| `rogue|*|mythical|second` | `mythical_rogue_female_hd.png` | `mythical_rogue_female_directions_hd` | dark-skinned female rogue, short silver curls, pointed ears with gold wing ornaments, purple cape/tunic and paired straight/crescent blades |
| `stormcaller|*|undead|second` | `undead_stormcaller_female_hd.png` | `undead_stormcaller_female_directions_hd` | regal undead woman, gray-green skin, long white hair, spiked crown, black feather mantle/fan, layered dark gown and crescent skull staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole identity and rendering-style reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: wide 2:1 canvas; strict invisible 4x2 grid; genuine
north/rear idle A, idle B, walk A, walk B on the top row; genuine south/front
idle A, idle B, walk A, walk B on the bottom row; identical scale and baseline;
generous transparent cell gutters; and distinct planted opposite steps. It
required preservation of body variant, face, equipment, handedness, palette,
team-colour regions, proportions, and the painted tactical-RPG sprite style.
It explicitly rejected relabelled side views, attacks, magic or impact effects,
ground, shadows, scenery, labels, visible grids, borders, extra figures,
cropping, overlap, and watermarks. The final column above records the
identity-specific details added to that prompt.

The nature healer received one precise background-extraction pass after the
initial result contained an off-white canvas and faint floor haze. That pass
removed only the canvas and haze while preserving all eight figures, equipment,
poses, positions, scale, layout, colours, and antialiased edges.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch I adds 9,434,775
bytes of source PNGs and 3,832,001 bytes of masks (13,266,776 bytes total).
