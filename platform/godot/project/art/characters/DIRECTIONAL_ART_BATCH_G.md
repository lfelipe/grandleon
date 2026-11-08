# Directional character art: batch G

Batch G adds eight unbound companion sheets and matching team masks. It follows
the cell, pivot, transparency, and filename contract in `DIRECTIONAL_ART.md`.
Profile/runtime integration is intentionally a separate reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|medieval|second` | `medieval_archer_female_hd.png` | `medieval_archer_female_directions_hd` | female archer, swept-back brown hair and long braid, mail and leather, blue cloak/scarf, blue/gold panels, long gold bow and quiver |
| `beast|*|mythical|first` | `mythical_beast_male_hd.png` | `mythical_beast_male_directions_hd` | massive male ram-dragon, dark shag and scales, curled black horns, long muzzle, claws, tail and cream/gold armor |
| `commander|*|nature|second` | `nature_commander_female_hd.png` | `nature_commander_female_directions_hd` | female white wolf, leafy cap, wood/bronze armor, blue/gold cape and tabard, tail and leaf-bladed command polearm |
| `healer|*|medieval|first` | `medieval_healer_male_hd.png` | `medieval_healer_male_directions_hd` | mature bearded healer, cream hood and mantle, blue/cream robes, sun tabard, herb satchel and sun-orb staff |
| `knight|*|mythical|first` | `mythical_knight_hd.png` | `mythical_knight_directions_hd` | broad male knight, closed dark violet/steel helmet, blue scale shield, curved sword and blue/gold tabard and cape |
| `mage|*|nature|second` | `nature_mage_female_hd.png` | `nature_mage_female_directions_hd` | female red panda, rust/cream markings, long braid and striped tail, leafy cloak, blue/gold tabard, gourds and small staff |
| `rogue|*|scifi|first` | `kestrel_rogue_hd.png` | `kestrel_rogue_directions_hd` | compact male infiltrator, teal hood and split scarf, hidden face with cyan visor, black/slate armor, cyan runes and twin energy daggers |
| `stormcaller|*|sengoku|second` | `sengoku_stormcaller_female_hd.png` | `sengoku_stormcaller_female_directions_hd` | Japanese woman, long dark hair and white flowers, cream sleeves, deep indigo hakama, rope and gourds, shide wand and fan |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. Each committed combat sheet above was supplied as the
sole reference image.

The shared prompt requested a new transparent production sprite sheet for the
exact referenced identity: strict invisible 4x2 grid; north/rear idle A, idle
B, walk A, walk B on the top row; south/front idle A, idle B, walk A, walk B on
the bottom row; identical scale and baseline; distinct planted steps; preserved
body type, equipment, handedness, palette, team-colour cloth, proportions, and
painted sprite style. It explicitly rejected relabelled side views, attacks,
effects, ground, shadows, scenery, labels, grids, borders, extra figures,
cropping, and watermarks. The final column above records the per-identity
details added to that prompt.

The nature commander received one layout-only follow-up edit that uniformly
reduced and lowered all eight figures to restore the required transparent
safety gutter; it did not alter identity, poses, equipment, or art direction.
The Kestrel rogue received two narrow follow-up edits: one restored the male
body silhouette from the reference, and one changed only broad false-positive
teal/blue backcloth and armor-shadow regions to charcoal/slate so the generated
team mask remains selective. Bright teal trim, runes, visor, lights, and energy
daggers were preserved. Validation thresholds were not relaxed.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch G adds 10,777,787
bytes of source PNGs and 4,250,908 bytes of masks (15,028,695 bytes total).
