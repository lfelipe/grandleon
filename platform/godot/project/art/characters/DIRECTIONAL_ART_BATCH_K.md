# Directional character art: batch K

Batch K adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|pirates|first` | `pirates_archer_male_hd.png` | `pirates_archer_male_directions_hd` | rugged bearded man, blue feathered bandana, navy/gold sleeveless coat, leather armor, belt pouches, bolt quiver, wooden crossbow and sidearm |
| `beast|*|sengoku|second` | `sengoku_beast_female_hd.png` | `sengoku_beast_female_directions_hd` | athletic tan/cream female wolf, pointed ears, pale muzzle and chest, curled fluffy tail, rope harness, lamellar shoulder armor and wave-pattern barding |
| `commander|*|pirates|second` | `pirates_commander_female_hd.png` | `pirates_commander_female_directions_hd` | dark-skinned woman, long braids, navy tricorne and white feather, navy/gold long coat, weathered breastplate, rapier and brass spyglass |
| `healer|*|scifi|first` | `relay_healer_hd.png` | `relay_healer_directions_hd` | compact Relay healer, white/navy helmet and armor, amber visor, rear antenna, blue/gold split cape, cyan belt canisters and medical canister |
| `knight|*|undead|first` | `undead_knight_male_hd.png` | `undead_knight_male_directions_hd` | male skeleton, battered hood and rusted helmet, heavy weathered armor, ragged cloth, scarred blue/gold star shield and short claw-like blade |
| `mage|*|scifi|first` | `relay_mage_hd.png` | `relay_mage_directions_hd` | hooded Relay mage, white/navy armor, amber visor, layered blue rune scarf and cape, blue/gold tabard, cyan belt lights and compact geometric focus |
| `rogue|*|nature|first` | `nature_rogue_male_hd.png` | `nature_rogue_male_directions_hd` | male otter, dark brown fur, cream muzzle, long tail, leafy mantle, blue scarf and tabard, leather armor, pouches and paired daggers |
| `stormcaller|*|medieval|second` | `medieval_stormcaller_female_hd.png` | `medieval_stormcaller_female_directions_hd` | dark-haired woman with long braid and circlet, layered blue scale mantle, cream/blue/gold robes and tall silver cloud-finial staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each existing combat sheet above was supplied as the
sole identity, anatomy, equipment, palette, and rendering-style reference.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: wide 2:1 canvas; strict invisible 4×2 grid; genuine
north/rear idle A, idle B, walk A, walk B on the top row; genuine south/front
idle A, idle B, walk A, walk B on the bottom row; identical scale, baseline,
and pivot; generous transparent gutters; and distinct planted opposite steps.
It required preservation of body variant, face or visor, anatomy, equipment,
weapon handedness, palette, team-colour regions, proportions, silhouette, and
the reference's painted or crisp console-like finish. It rejected relabelled
side views, attacks, aiming or casting, magic and impact effects, ground,
shadows, haze, scenery, labels, visible grids, borders, extra figures, overlap,
cropping, and watermarks. The final column above records the identity-specific
details added to that prompt.

The pirate commander received one layout-only follow-up edit that uniformly
reduced and lowered all eight figures to restore the required transparent
safety gutter; identity, poses, equipment, and art direction were unchanged.
The Relay mage received one narrow follow-up edit to shift only broad
false-positive rear cape, scarf-back, and tabard-shadow panels toward neutral
charcoal/slate. Bright blue edges, cyan runes, gold trim, the geometric focus,
all front views, and the accepted poses and layout were preserved. Validation
thresholds were not relaxed.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch K adds 9,063,583
bytes of source PNGs and 3,675,089 bytes of masks (12,738,672 bytes total).
