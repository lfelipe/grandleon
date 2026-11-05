# Sci-fi female roster prompt notes

Mode: built-in image generation with a genuinely transparent background.

Each selected image was deterministically resized from the generated 2:1
canvas to 1772x886, producing eight 443x443 cells. The targeted correction
prompts below were used because the original ability effects crossed their
cell boundaries. Generated fringe at alpha 3/255 or lower was cleared after
resizing so every cell has a fully transparent outer border.

## Stormcaller

References:

- `morrow_stormcaller_hd.png`: sci-fi role, action grammar and finish
- `mythical_stormcaller_female_hd.png`: independent female roster design
- `mythical_stormcaller_male_hd.png`: shared role and game language

Initial prompt:

```text
Use case: stylized-concept
Asset type: production sprite sheet for a tactical game
Input images: Image 1 is the Relay sci-fi stormcaller role, action grammar, camera, and finish reference. Images 2 and 3 show how independently designed women and men still share one roster language; use them only for compact proportions and practical character readability, not their fantasy clothing.
Primary request: create exactly eight full-body depictions of one consistent adult female sci-fi stormcaller in a transparent 4-column by 2-row production sheet. She must be an independently designed complementary character, not the hooded male with a changed face.
Subject: practical field weather-controller with a clearly female adult face and silhouette, compact tactical proportions about 3.5 heads tall, broad readable hands and boots, short silver undercut with a small swept crest, asymmetric high-collar weather mantle, segmented white and deep-navy armor, dark utility underlayer, cyan capacitors, and a compact two-pronged arc-conductor staff held in the same hand throughout. Give the mantle and armored coat a large clean violet team-colour region clearly separable from skin, hair, white/navy armor, and cyan effects. No sexualization.
Style/medium: polished painterly game sprite concept, strong thick dark contours, simplified readable materials, controlled saturation, same three-quarter tactical camera and rendering density as Image 1.
Composition/framing: canvas exactly twice as wide as tall; invisible 4x2 grid; one centered pose per equal cell with generous safe margins. Exact reading order: top row 1 idle A neutral, 2 idle B subtle breathing/arc check, 3 walk A leading step, 4 walk B alternate step; bottom row 5 attack wind-up raising the conductor, 6 attack follow-through sweeping an arc, 7 ability projecting a large cyan-violet storm-control disc/orb, 8 hurt/defeat recoiling backward. Keep identical face, hair, outfit, palette, equipment construction, equipment hand, scale, baseline, and three-quarter facing across all eight.
Scene/backdrop: genuinely transparent background only.
Lighting/mood: clean neutral roster lighting; cyan tech glow and restrained violet electrical energy only where action requires it.
Constraints: exactly eight poses and no extras; every figure fully contained within its own cell; no overlap or spill across cells; no cropped hair, staff, hands, feet, cape, or effects; no grid lines, captions, labels, text, scenery, floor, shadows, logos, or watermark. Do not make one pose larger than the others. Preserve clear idle/walk/attack/ability/hurt semantics.
```

Accepted effect correction:

```text
Use case: precise-object-edit
Asset type: transparent 4x2 tactical-game production sprite sheet
Input images: Image 1 is the edit target.
Primary request: change only the violet/cyan energy effects in bottom-row cells 1, 2, and 3. Clean away the saturated blocky blue/magenta speckling and ragged opaque residue around the weapon arcs and storm orb, replacing it with compact smooth painterly glow and clean antialiased transparent falloff. In the bottom-row third cell, reduce the storm-control disc/orb to about 68% of its current diameter and shift it left so the entire orb and glow are fully contained inside that third equal cell with a clear transparent margin before the fourth cell.
Constraints: preserve the woman’s identity, face, silver hair, anatomy, compact proportions, outfit, violet team-colour mantle and tabard, white/navy armor, conductor weapon, exact eight poses, action meanings, order, spacing, scale, baselines, and every non-effect design choice as closely as possible. Keep all characters at the same sizes. Change only the action energy effects. Exactly eight figures. Do not alter the bottom-row fourth hurt pose. No spill across cell boundaries; no hard rectangular clipping; no text, grid, scenery, floor, shadows, logos, watermark, or new objects. Genuinely transparent background.
```

## Healer

References:

- `relay_healer_hd.png`: sci-fi role, action grammar and finish
- `mythical_healer_female_hd.png`: independent female roster design
- `mythical_healer_male_hd.png`: shared role and game language

Initial prompt:

```text
Use case: stylized-concept
Asset type: production sprite sheet for a tactical game
Input images: Image 1 is the Relay sci-fi healer role, action grammar, camera, cyan medical-tech language, and painterly roster finish reference. Images 2 and 3 show independent female and male healer identities that still belong to one game; use them for compact practical proportions and role clarity, not their fantasy clothing.
Primary request: create exactly eight full-body depictions of one consistent adult female sci-fi field healer in a transparent 4-column by 2-row production sheet. She must be an independently designed complementary character, not the helmeted Relay healer with a changed face.
Subject: clearly female adult field medic with warm dark-brown skin, calm strong features, and a high practical braided ponytail emerging behind a compact open-face comms circlet. Compact tactical proportions about 3.5 heads tall, large readable hands and boots. Practical segmented white and deep-navy medical armor over a utility undersuit, an asymmetrical short medic coat/tabard and shoulder scarf with a large clean amber team-colour region, cyan diagnostic vials, and a short cylindrical healing projector held in the same hand throughout. Add a small forearm scanner on the other arm. Keep skin, dark hair, white/navy armor, amber garment, and cyan effects clearly separated. No sexualization and no form-fitting glamour armor.
Style/medium: polished painterly game sprite concept, strong thick dark contours, simplified readable materials, controlled saturation, same three-quarter tactical camera and rendering density as Image 1.
Composition/framing: canvas exactly twice as wide as tall; invisible 4x2 grid; one centered pose per equal cell with generous safe margins. Exact reading order: top row 1 idle A neutral, 2 idle B checking the healing projector, 3 walk A leading step, 4 walk B alternate step; bottom row 5 attack wind-up raising the projector, 6 attack follow-through firing a short cyan pulse, 7 ability projecting a large cyan medical shield/healing field with a simple cross-like geometric motif but no text, 8 hurt/defeat recoiling backward. Keep identical face, braid, outfit, palette, equipment construction, equipment hand, scale, baseline, and three-quarter facing across all eight.
Scene/backdrop: genuinely transparent background only.
Lighting/mood: clean neutral roster lighting; crisp cyan medical-tech glow only where action requires it.
Constraints: exactly eight poses and no extras; every figure fully contained within its own cell; no overlap or spill across cells; no cropped hair, projector, hands, feet, coat, braid, or effects; no grid lines, captions, labels, text, scenery, floor, shadows, logos, or watermark. Do not make one pose larger than the others. Preserve clear idle/walk/attack/ability/hurt semantics.
```

Accepted ability correction:

```text
Use case: precise-object-edit
Asset type: transparent 4x2 tactical-game production sprite sheet
Input images: Image 1 is the edit target.
Primary request: change only the cyan healing shield in the bottom-row third cell (the ability pose). Reduce that shield/effect to about 72% of its current diameter and shift it slightly left so the complete effect is fully contained inside the bottom-row third equal cell with a clear transparent margin before the fourth cell.
Constraints: preserve the woman’s identity, face, hair, anatomy, compact 3.5-head proportions, outfit, amber team-colour panels, white/navy armor, equipment, cyan vials, exact eight poses, action meanings, order, spacing, scale, baselines, rendering, outlines, transparency, and every other pixel-level design choice as closely as possible. Keep the ability character at the same size; change only the shield/effect. Exactly eight figures. Do not alter the bottom-row fourth hurt pose. No text, grid, scenery, shadows, logos, watermark, or new objects. Genuinely transparent background.
```

## Commander

References:

- `rook_commander_hd.png`: sci-fi role, action grammar and finish
- `mythical_commander_female_hd.png`: independent female roster design
- `mythical_commander_male_hd.png`: shared role and game language

Initial prompt:

```text
Use case: stylized-concept
Asset type: production sprite sheet for a tactical game
Input images: Image 1 is the Rook sci-fi commander role, exact action grammar, camera, cyan command-tech language, and painterly roster finish reference. Images 2 and 3 show independently designed female and male commanders that still belong to one game; use them for compact practical proportions and command presence, not their fantasy clothing.
Primary request: create exactly eight full-body depictions of one consistent adult female sci-fi commander in a transparent 4-column by 2-row production sheet. She must be an independently designed complementary commander, not Rook with a changed face.
Subject: clearly female veteran tactical officer with East Asian features, a stern mature face, and a short angular black bob with one silver temple streak under a slim open command headset. Compact tactical proportions about 3.5 heads tall, broad readable hands and boots. Practical segmented white and deep-navy officer armor over a utility undersuit, asymmetric armored shoulder mantle, a structured short command coat and split tabard with a large clean cobalt-blue team-colour region, small cyan status lights, and a short cyan command saber/baton held in the same hand throughout. Add a compact folded holographic standard beacon on her back/shoulder that remains consistent. Keep skin, hair, white/navy armor, blue garment, bronze fasteners, and cyan effects clearly separated. No sexualization.
Style/medium: polished painterly game sprite concept, strong thick dark contours, simplified readable materials, controlled saturation, same three-quarter tactical camera and rendering density as Image 1.
Composition/framing: canvas exactly twice as wide as tall; invisible 4x2 grid; one centered pose per equal cell with generous safe margins. Exact reading order: top row 1 idle A neutral command stance, 2 idle B checking the baton, 3 walk A leading step, 4 walk B alternate step; bottom row 5 attack wind-up raising the command saber, 6 attack follow-through with a compact cyan slash, 7 ability projecting a contained cyan holographic rally standard/command sigil, 8 hurt/defeat recoiling backward. Keep identical face, hair, outfit, palette, beacon construction, equipment hand, scale, baseline, and three-quarter facing across all eight.
Scene/backdrop: genuinely transparent background only.
Lighting/mood: clean neutral roster lighting; crisp cyan command-tech glow only where action requires it.
Constraints: exactly eight poses and no extras; every figure fully contained within its own cell; no overlap or spill across cells; no cropped hair, beacon, baton, hands, feet, coat, tabard, or effects; no grid lines, captions, labels, text, scenery, floor, shadows, logos, or watermark. Do not make one pose larger than the others. Preserve clear idle/walk/attack/ability/hurt semantics.
```

Accepted ability correction:

```text
Use case: precise-object-edit
Asset type: transparent 4x2 tactical-game production sprite sheet
Input images: Image 1 is the edit target.
Primary request: change only the cyan holographic rally standard/sigil in the bottom-row third cell. Reduce the complete standard and glow to about 70% of its current size and shift it left so the whole effect is fully contained inside the bottom-row third equal cell with a clear transparent margin before the fourth cell. Clean any ragged saturated cyan speckling around it into smooth painterly glow with antialiased transparent falloff.
Constraints: preserve the woman’s identity, East Asian face, black bob and silver temple streak, anatomy, compact 3.5-head proportions, outfit, cobalt-blue team-colour panels, white/navy armor, bronze trim, back beacon, command saber, exact eight poses, action meanings, order, spacing, scale, baselines, and every other design choice as closely as possible. Keep the ability character at the same size; change only the rally standard effect. Exactly eight figures. Do not alter the bottom-row fourth hurt pose. No spill across cell boundaries; no text, grid, scenery, floor, shadows, logos, watermark, or new objects. Genuinely transparent background.
```
