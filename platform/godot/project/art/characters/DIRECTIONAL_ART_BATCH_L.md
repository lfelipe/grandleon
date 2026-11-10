# Directional character art: batch L

Batch L adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|scifi|first` | `relay_archer_hd.png` | `relay_archer_directions_hd` | masculine Relay archer, white/navy helmet and armor, amber visor, rear antenna, blue/gold half cape, cyan lights and long cyan-lit rail rifle |
| `beast|*|undead|first` | `undead_beast_male_hd.png` | `undead_beast_male_directions_hd` | large male undead dire wolf, exposed canine skull, dark bristled mane and tail, bone rib armor, claws and tattered blue/gold star barding |
| `commander|*|sengoku|second` | `sengoku_commander_female_hd.png` | `sengoku_commander_female_directions_hd` | female Japanese commander, dark tied hair, gold-horned kabuto, indigo lamellar armor, blue/gold jinbaori, crescent command polearm, fan and sashimono |
| `healer|*|sengoku|second` | `sengoku_healer_female_hd.png` | `sengoku_healer_female_directions_hd` | elderly Japanese woman, silver hair bun, warm lined face, cream/indigo robes, blue shawl, herb satchels and small gold-ringed healing staff |
| `mage|*|undead|second` | `undead_mage_female_hd.png` | `undead_mage_female_directions_hd` | pale gray-green undead woman, long flowing white hair, slender silhouette, layered dark blue-gray tattered gown, star tabard and crooked pale-green-orb staff |
| `rogue|*|scifi|second` | `scifi_rogue_female_hd.png` | `scifi_rogue_female_directions_hd` | female infiltrator, pale face, short asymmetrical silver hair, angular hood, fitted black/slate armor, teal rune cape and twin cyan energy daggers |
| `stormcaller|*|mythical|second` | `mythical_stormcaller_female_hd.png` | `mythical_stormcaller_female_directions_hd` | dark-skinned woman, thick silver-white braids, gold ornaments, black mantle, crimson/deep-violet gold-trimmed robes and short blue-crystal staff |
| `stormcaller|*|nature|second` | `nature_stormcaller_female_hd.png` | `nature_stormcaller_female_directions_hd` | female deer, gray-brown doe face, branching antlers, long pale hair, leafy crown, blue/gold robes, hooves and forked feathered branch staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole identity, equipment, palette, and rendering-style reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: wide 2:1 canvas; strict invisible 4×2 grid; genuine
north/rear idle A, idle B, walk A, walk B on the top row; genuine south/front
idle A, idle B, walk A, walk B on the bottom row; identical scale, baseline,
and pivot; generous transparent gutters; and distinct planted opposite steps.
It required preservation of body variant, face or skull, anatomy, equipment,
weapon handedness, palette, team-colour regions, proportions, and silhouette.
It explicitly rejected relabelled side views, attacks, aiming or casting,
magic and impact effects, ground, shadows, haze, scenery, labels, visible
grids, borders, extra figures, overlap, cropping, and watermarks. The final
column above records the identity-specific details added to that prompt.

The mythical stormcaller received one precise background-extraction pass after
the accepted eight-figure result contained an off-white canvas. That pass
removed only the canvas and faint surrounding haze while preserving her
identity, equipment, poses, positions, scale, layout, colours, and antialiased
edges.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch L adds 9,089,416
bytes of source PNGs and 3,600,204 bytes of masks (12,689,620 bytes total).
