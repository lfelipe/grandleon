# Steel and Cedar Medieval close-view pack

This is an asset-only, reviewed candidate pack for close combat and dialogue in
Steel and Cedar's **Gate Village** and **Twin Gate Keep**. It is deliberately
not registered in the runtime manifest yet. The six composites are shown in
[`medieval-close-pack-review.png`](../../../../../docs/screenshots/godot/medieval-close-pack-review.png).

## Technical contract

Each environment has three stable variants and follows the existing forest
pack's 1280×720, 8-bit sRGB contract. Layer order is panorama, transition,
midground, then props. Panorama is opaque; every other plate has alpha.

The generated panoramas include a continuous floor all the way through the
bottom edge. Each transition plate repeats the matching panorama's lower floor
pixels behind a soft alpha ramp beginning at row 390. That deliberate overlap
means there is no separately invented horizon or material join to reveal when
the layers are composed. `floor_strip_top` must remain `1.0`, so the live
fallback floor does not cover the source floor.

The middle 512 pixels of every midground layer and the middle 640 pixels of
every prop layer are fully transparent. Houses, columns, barrels, braziers and
other detail remain at the side edges. This leaves both combatant positions and
the dialogue text region readable. The existing conversation scrim still owns
its normal text-safe rectangle.

The two `pack.review.json` files record the proposed compatibility and filename
patterns. `runtime_registered` is false so review data cannot be mistaken for a
live manifest entry.

## Provenance

- Date: 2026-11-07.
- Authoring agent: Codex (GPT-5).
- Image mode: built-in OpenAI image-generation tool, generate with references.
- Image renderer/version/seed: not exposed by the built-in service; no
  `gpt-image-*` identifier is guessed.
- Requested format: wide 16:9 raster; transparent background for midground and
  prop masters.
- Returned format: 1672×941 PNG.
- Normalization: centered aspect crop and Lanczos resample to 1280×720; metadata
  stripped; alpha below 1% discarded on midground/prop plates. Panorama pixels
  were not repainted. Variant 1 flips its accepted side plate; variant 2 keeps
  the accepted geometry at 86% alpha. Transition plates are alpha-masked copies
  of their corresponding panorama floor.
- Village references at Git revision
  `a424e1be4a746fc99f611642d1cc596b9340fea4`:
  `medieval_house_townhouse.png`, `medieval_house_thatched.png`,
  `medieval_house_inn.png`, and `medieval_house_craftsperson.png`.
- Castle references at Git revision
  `f5a8510b94a859105aaf12ab7df1e9581a8417b4`:
  `medieval_castle_keep_exterior.png` and
  `medieval_castle_keep_cutaway.png`. The village townhouse was also supplied
  as a material bridge between the two families.

The accepted raw outputs were identified by these SHA-256 values before
normalization:

| Source | SHA-256 |
|---|---|
| Village panorama 0 | `471087aa6fd4b83a6ed580b916b7ae1976eaf28b4856143843452b02c40e8fb4` |
| Village panorama 1 | `a6340220bcc2a8415eaa0699c9107f72a35944e183b2d9bd5b9c279771ea0eb5` |
| Village panorama 2 | `efb670ae350fbccc59d58813e958e462795eee998ef4cb26421475aafe7b390a` |
| Castle panorama 0 | `e6f14f80fa61e4d62511429488a1db802c536343eea46eef50f53625051f5220` |
| Castle panorama 1 | `c3cf9b55358b8037e0a2c6b07ba0607fa2e0305baefb0599834af96f403648e7` |
| Castle panorama 2 | `1fb2bc85da0c2bb51da3cb41dfb437901a709e655571b16e7984c715195a130a` |
| Village midground master | `9077fbf5a5bfaa71aa19605e02b26bfb8be5f002a07d0efbf32198da97459aab` |
| Village prop master | `371cb395e8c79997d8066254944dacc74b77d7206f131fdd586a7e419f6151f7` |
| Castle midground master | `c64e3503ca8217e50543bdd82c7fd100820b5ac0d5838a53d8c2fbf9735fe689` |
| Castle prop master | `72cabe0cdcfcc30edef18a4d937df70805089e96478a0209a2c5e073f5964051` |

## Panorama prompts

The three village calls used this shared prompt:

```text
Use case: stylized-concept
Asset type: layered tactical-RPG close-combat and conversation environment, opaque panorama source
Input images: supplied Grandleon Medieval house cutouts are style, material, lighting, and rendering-density references only; do not copy their exact composition
Primary request: a wide medieval border-village panorama for Grandleon's Steel and Cedar campaign
Style/medium: polished stylized 3D painterly game render matching the supplied Grandleon Medieval cutouts; grounded fieldstone, plaster, dark oak, aged roof materials; softly modeled forms and crisp silhouettes, not photorealistic
Composition/framing: 16:9 straight-on close-battle backdrop; low horizon around 44% height; broad central 45% quiet enough for two large character sprites, effects, and dialogue text; upper-left and upper-right corners calm for status UI; continuous natural floor reaches the bottom edge with no horizon seam
Lighting/mood: warm late-afternoon key light with cool blue ambient shadows, calm but tactically alert
Color palette: warm sandstone, dark oak, muted plaster, red-brown roofs, restrained cedar green, deep desaturated blue sky
Constraints: opaque full-canvas environment; no people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern architecture, suburban proportions, exaggerated fantasy spires, strong central foreground prop, or black bands
```

Village panorama 0 used this exact standalone prompt:

```text
Use case: stylized-concept
Asset type: layered tactical-RPG close-combat and conversation environment, opaque panorama source for variant 0
Input images: the supplied Grandleon Medieval house cutouts are style, material, lighting, and rendering-density references only; do not copy their exact composition
Primary request: a wide medieval border-village panorama for Grandleon's Steel and Cedar campaign
Scene/backdrop: distant compact village street at late afternoon, warm fieldstone and dark-oak half-timber buildings, steep weathered red-brown tile and thatch roofs, a modest timber gate, layered rooflines and a few restrained cedar silhouettes in the far distance
Style/medium: polished stylized 3D painterly game render matching the supplied Grandleon Medieval cutouts; grounded materials, softly modeled forms, crisp silhouettes, not photorealistic
Composition/framing: 16:9 straight-on close-battle backdrop with a low horizon near 44% height; buildings form the distant upper half; keep the broad central 45% visually quiet for two large character sprites and effects; reserve the upper-left and upper-right corners for status UI; no foreground floor because a separate continuous floor layer will cover the lower half
Lighting/mood: warm late-afternoon key light, cool blue ambient shadows, calm but tactically alert
Color palette: warm sandstone, dark oak, muted plaster, red-brown roofs, restrained cedar green, deep desaturated blue sky
Constraints: opaque full-canvas environment; no people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, detached floating objects, modern architecture, suburban proportions, exaggerated fantasy spires, strong central focal prop, or black bands
```

The accepted result supplied a clean continuous floor despite that original
floor instruction, so normalization retained it to avoid creating a join.

Village panoramas 1 and 2 appended these exact suffixes to the shared prompt:

```text
Variant composition: a small cobbled market square seen from its open edge, lower two-story houses framing the far left and far right, a modest roofed well and empty awnings pushed into the distant side thirds, a narrow road receding off-center; keep the central foreground entirely open

Variant composition: the inside of a sturdy timber village gate with palisade and mixed thatch/tile cottages staggered into the distance, a quiet grass-and-packed-earth verge blending into worn paving, low carts and wood piles only at the extreme edges; keep the central foreground entirely open
```

The three castle calls used this shared prompt:

```text
Use case: stylized-concept
Asset type: layered tactical-RPG close-combat and conversation environment, opaque panorama source
Input images: supplied Grandleon Twin Gate Keep and Medieval house cutouts are style, material, lighting, camera, and rendering-density references only; retain the warm fieldstone/red-roof visual language but do not copy their exact composition
Primary request: a wide interior environment inside Twin Gate Keep for Grandleon's Steel and Cedar campaign
Style/medium: polished stylized 3D painterly game render matching the supplied Grandleon Medieval cutouts; grounded stone, dark oak, aged iron, cloth, and torchlight; softly modeled forms, crisp silhouettes, not photorealistic
Composition/framing: 16:9 straight-on close-battle backdrop with broad open foreground floor continuing naturally to the bottom edge; distant architecture occupies upper and side thirds; broad central 45% quiet enough for two large character sprites, effects, and dialogue text; upper-left and upper-right corners calm for status UI; no horizontal floor/backdrop seam
Lighting/mood: warm restrained torchlight with cool daylight/ambient fill, solemn border keep, readable rather than dark
Color palette: warm sandstone and grey fieldstone, dark oak, muted iron, desaturated red cloth, low amber light, cool slate-blue shadow
Constraints: opaque full-canvas environment; no people, units, weapons laid out for use, lettering, text, logos, watermark, interface, grid, modern objects, exaggerated fantasy magic, strong central foreground prop, or black bands
```

The exact castle variant suffixes were:

```text
Variant composition: an open stone throne chamber behind the main gate, low dais and plain period chair pushed into the far center background, paired wall torches, narrow Gothic windows, heavy side columns, uncluttered broad flagstone floor

Variant composition: a vaulted guard hall crossing the keep, heavy oak beams and shallow side alcoves, one distant open arch showing cool daylight, restrained red-and-gold banners on the far side walls, entirely open broad flagstone foreground

Variant composition: the roofed inner court at the meeting of two gate towers, crenellated stone walls and red-brown eaves in the distance, large open gate arch offset behind the fighters, a few braziers and stacked supplies restricted to extreme side edges, entirely open broad paved foreground
```

## Transparent layer prompts

The village midground master prompt was:

```text
Use case: stylized-concept
Asset type: transparent midground framing plate for a 16:9 tactical-RPG close-combat and conversation environment
Input images: supplied Grandleon Medieval village buildings are style, material, scale, lighting, and rendering-density references only
Primary request: one coherent transparent village-edge framing plate with the upper portions and silhouettes of two different medieval border-village buildings plus short fieldstone wall sections, one group anchored at the far left and one at the far right
Style/medium: polished stylized 3D painterly game render matching the supplied cutouts, grounded stone, dark oak, muted plaster, weathered red-brown roof
Composition/framing: very wide 16:9 canvas; all artwork confined to outer left 24% and outer right 24%; central 52% completely empty and transparent; lower 38% completely empty and transparent for the continuous floor and character feet; entire visible structures uncropped within their side zones
Lighting/mood: warm late-afternoon key, cool ambient shadows
Constraints: genuinely transparent background and empty center; no floor, sky, people, units, signs, text, logos, watermark, interface, grid, detached objects, modern architecture, or cast shadow beyond the side silhouettes
```

The village prop master prompt was:

```text
Use case: stylized-concept
Asset type: transparent foreground prop plate for a 16:9 tactical-RPG close-combat and conversation environment
Input images: supplied Grandleon Medieval village buildings are style, material, lighting, and rendering-density references only
Primary request: two restrained clusters of medieval village street props: one low cluster of a small capped barrel, split firewood, and short hitching post at the extreme left; one low cluster of a handcart wheel, tied sack, and low stone trough at the extreme right
Style/medium: polished stylized 3D painterly game render matching the supplied cutouts; grounded wood, iron, coarse cloth, and fieldstone
Composition/framing: very wide 16:9 transparent canvas; clusters stay inside outermost left/right 18%; central 64% completely empty and transparent; nothing taller than 30% of canvas; clear open lower-center space for two character sprites and dialogue
Lighting/mood: warm late-afternoon key, cool ambient shadows
Constraints: genuinely transparent background; no floor plane, scenery, buildings, people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, flowers, or magical objects
```

The castle midground master prompt was:

```text
Use case: stylized-concept
Asset type: transparent midground framing plate for a 16:9 tactical-RPG close-combat and conversation environment
Input images: supplied Grandleon Twin Gate Keep cutouts are style, material, scale, lighting, and rendering-density references only
Primary request: one coherent transparent keep-interior framing plate with a heavy fieldstone column, dark-oak roof brace, shallow pointed arch, and restrained red cloth banner on the far left, mirrored in construction but not identical on the far right
Style/medium: polished stylized 3D painterly game render matching the supplied keep; grounded stone, oak, aged iron, and cloth
Composition/framing: very wide 16:9 canvas; all artwork confined to outer left 23% and outer right 23%; central 54% completely empty and transparent; lower 35% completely empty and transparent for the continuous flagstone floor and character feet; silhouettes uncropped within side zones
Lighting/mood: warm restrained torch key, cool daylight ambient fill
Constraints: genuinely transparent background and empty center; no floor, sky, people, units, furniture, weapons, text, logos, watermark, interface, grid, modern objects, fantasy magic, or cast shadow beyond side silhouettes
```

The castle prop master prompt was:

```text
Use case: stylized-concept
Asset type: transparent foreground prop plate for a 16:9 tactical-RPG close-combat and conversation environment
Input images: supplied Grandleon Twin Gate Keep cutouts are style, material, lighting, and rendering-density references only
Primary request: two restrained keep-side prop clusters: one low iron brazier with a small warm flame and a closed oak supply chest at the extreme left; one low shield rack with two plain unmarked period shields and one tied sack at the extreme right
Style/medium: polished stylized 3D painterly game render matching the supplied keep; grounded iron, oak, leather, cloth, and restrained flame
Composition/framing: very wide 16:9 transparent canvas; clusters stay inside outermost left/right 18%; central 64% completely empty and transparent; nothing taller than 32% of canvas; clear open lower-center space for two character sprites and dialogue
Lighting/mood: warm restrained torchlight, cool ambient shadow
Constraints: genuinely transparent background; no floor plane, scenery, walls, people, units, loose weapons, heraldic text, lettering, logos, watermark, interface, grid, or fantasy magic
```

## Validation

The accepted pack passed these asset-only checks:

- all 24 layer PNGs are exactly 1280×720, 8-bit sRGB;
- all six panoramas are opaque and all 18 other plates retain alpha;
- every transition's top row is fully transparent and bottom row fully opaque;
- the center 512×720 area of all midground plates and center 640×720 area
  of all prop plates have an alpha maximum of zero after cleanup;
- all three panoramas in each environment have distinct SHA-256 values;
- the six ordered composites were reviewed at native size for floor continuity,
  uncropped side silhouettes, open combatant positions, and dialogue-panel
  contrast; no floor seam, text, watermark, unit, or modern object was found.

The source layers total approximately 24 MiB. The montage is review-only and
does not belong in the runtime pack.

## Integration hooks still required

1. Add the two entries from `pack.review.json` to
   `combat_backdrops/manifest.json`; keep the listed filenames, variant count,
   layer order, and `floor_strip_top: 1.0`.
2. Pass `battle_terrain_features` into
   `GrandleonCombatCinematic.enqueue()` and then into
   `GrandleonBackdropPackResolver.resolve_for_exchange()`. Pass the same array
   to `resolve_for_conversation()` for over-board dialogue.
3. Extend the resolver's local context with exact feature names under the focus
   cells. Prefer `medieval_village` for `village_house` on `gate_village`, and
   `medieval_castle` for walkable `castle_door`, `castle_floor`, or
   `castle_dais` on `twin_gate`. Do not select the interior pack for blocked
   `castle_wall` cells.
4. Include map/stage identity in compatibility and the stable seed. Terrain
   histograms alone cannot distinguish a paved village street from a paved
   castle floor.
5. Keep selection frozen for an attack/counter exchange. Conversation reuse
   should use the same pack resolver and may map the existing authored
   `throne_hall` backdrop to the castle pack between maps.
6. Add resolver and live-screen tests for both exact feature routes, three
   stable variants, missing-feature fallback, layer ordering, and a full-floor
  composition with no live strip.
