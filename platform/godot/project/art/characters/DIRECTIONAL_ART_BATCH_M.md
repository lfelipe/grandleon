# Directional character art: batch M

Batch M adds the final nine unbound companion sheets and matching team masks.
It follows the cell, pivot, transparency, and filename contract in
`DIRECTIONAL_ART.md`. Profile/runtime integration is intentionally a separate
reviewed change.

| Identity key | Reference | New companion stem | Prompt-specific identity anchors |
|---|---|---|---|
| `archer|*|undead|first` | `undead_archer_male_hd.png` | `undead_archer_male_directions_hd` | male skeleton, green eyes, ragged blue hood and cloak, bone-cross tabard, pale-arrow quiver and crooked longbow |
| `beast|*|undead|second` | `undead_beast_female_hd.png` | `undead_beast_female_directions_hd` | female bone-wolf quadruped, skull face, silver mane and tail, exposed ribs, rope harness and torn heraldic barding |
| `commander|*|undead|first` | `undead_commander_male_hd.png` | `undead_commander_male_directions_hd` | skeletal king, green eyes, battered crown, heavy bronze plate, torn blue royal cape and pennanted poleaxe |
| `commander|*|undead|second` | `undead_commander_female_hd.png` | `undead_commander_female_directions_hd` | pale undead queen, long silver braid, crown, bronze plate, blue royal cape and pennanted poleaxe |
| `rogue|*|sengoku|second` | `sengoku_rogue_female_hd.png` | `sengoku_rogue_female_directions_hd` | female kunoichi, high braided black ponytail, navy scarf and wave-trim panels, quilted shinobi clothing and paired short daggers |
| `rogue|*|undead|first` | `undead_rogue_male_hd.png` | `undead_rogue_male_directions_hd` | gaunt gray undead man, green eyes, wide-brim hat, ragged blue scarf and tails, pouches and unequal paired blades |
| `stormcaller|*|pirates|second` | `pirates_stormcaller_female_hd.png` | `pirates_stormcaller_female_directions_hd` | mature pirate woman, long silver braid and curls, brass earrings, cream blouse, navy wave skirt, rope tassels and three-pronged staff |
| `stormcaller|*|scifi|second` | `scifi_stormcaller_female_hd.png` | `scifi_stormcaller_female_directions_hd` | brown-skinned woman, silver undercut, white-charcoal armor, cyan modules, purple split cape and compact crescent baton |
| `stormcaller|*|sengoku|first` | `sengoku_stormcaller_male_hd.png` | `sengoku_stormcaller_male_directions_hd` | older bearded ritualist, black cap, indigo-and-cream robes, white pom-poms, gourds, feather fan and bronze-ring staff |

## Generation record

The sheets were generated on 2026-11-07 with the built-in OpenAI image
generator exposed through Codex. The service response did not expose an
immutable model version. The existing combat sheets were inspected as the
identity references, and their defining visual anchors were carried into each
identity-specific prompt.

The shared prompt requested a new transparent production sprite sheet for the
exact identity: wide 2:1 canvas; strict invisible 4x2 grid; genuine north/rear
idle A, idle B, walk A, walk B on the top row; genuine south/front idle A, idle
B, walk A, walk B on the bottom row; consistent scale and silhouette; generous
transparent cell gutters; and distinct planted opposite steps. It required
preservation of body type, face, equipment, palette, team-colour regions,
proportions, and the painted tactical-RPG character style. It explicitly
rejected relabelled side views, attacks, effects, ground, shadows, scenery,
labels, visible grids, borders, extra figures, cropping, and overlap. The final
column above records the identity-specific details added to that prompt.

The undead male commander and pirate female stormcaller received precise
background-extraction passes after their initial results contained off-white
or semi-transparent canvas noise. The undead archer, Sengoku female rogue,
pirate female stormcaller, sci-fi female stormcaller, and Sengoku male
stormcaller received composition-only corrections to establish safe cell
gutters around tall hair, bows, or staffs. The undead archer, undead male rogue,
and pirate female stormcaller then received narrow cloth-colour corrections so
all frames retained the reference slate-blue team-tint regions and passed the
existing reviewed mask recipes. These corrections preserved the accepted
identities, equipment, directions, animation phases, and 4x2 layout.

Every accepted result was normalized exactly once with
`platform/console_art/prepare-directional-sheet.py`. That tool generated the
adjacent mask from the existing reference-body recipe and validated all eight
cells, safety gutters, walk deltas, and mask coverage. Batch M adds 10,040,495
bytes of source PNGs and 4,087,430 bytes of masks (14,127,925 bytes total).
