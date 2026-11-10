# Bell Orchard and Cedar Watch close-view packs

These are production packs for the two Steel and Cedar Medieval environments.
Combat and on-map conversations share the exact same route-and-landmark-gated
selection. The six reviewed composites are shown in
[`steel-cedar-orchard-watch-review.png`](../../../../../docs/screenshots/godot/steel-cedar-orchard-watch-review.png).

Bell Orchard is bright, rural, and welcoming enough for the tutorial. Cedar
Watch is a cool dusk frontier relay built to withstand a twenty-round siege.
The lighting, architecture, floor materials, and edge props keep the two stages
visually distinct even when the same Medieval character cutouts are used.

Production-resolver captures: [Bell Orchard](../../../../../docs/screenshots/godot/bell-orchard-production-backdrop.png)
and [Cedar Watch](../../../../../docs/screenshots/godot/cedar-watch-production-backdrop.png).
These are not hand-composited review plates: the capture harness asks the
shipping resolver for an over-board conversation and presents the returned
four layers through `GrandleonConversationBackdrop`.

## Technical contract

Both packs have three stable variants. Every layer is a 1280×720, 8-bit sRGB
PNG in panorama, transition, midground, props order. Panoramas are opaque; the
other plates retain alpha. The panorama floor continues through the bottom
edge. Each transition is an alpha-masked copy of the same panorama, with a soft
ramp from transparent at row 390 to opaque at the bottom edge. Runtime
`floor_strip_top` must remain `1.0`, because these sources already provide the
complete floor.

The middle 512 pixels of every midground plate and the middle 640 pixels of
every prop plate are fully transparent. Trees, fences, watch braces, supplies,
and signal equipment remain at the edges, leaving the combatant positions and
conversation text area readable.

Variant 0 uses the accepted side plates, variant 1 mirrors them, and variant 2
uses the accepted geometry at 86% alpha. Each environment's three panoramas are
separate generated scenes, so stable selection changes the actual location and
not only the framing props.

## Provenance and references

- Date: 2026-11-07.
- Authoring agent: Codex (GPT-5).
- Image mode: built-in OpenAI image-generation tool, style-transfer and
  precise-object edits from committed Grandleon references.
- Model/version/seed limitation: the built-in service did not expose an
  immutable image-model identifier, model version, or sampling seed. No
  `gpt-image-*` name is guessed. The durable reproduction record is the exact
  prompt, committed reference, accepted raw-output SHA-256, and normalization
  procedure below.
- Returned format: 1672×941, 8-bit sRGB PNG; transparent masters included an
  alpha channel.
- Normalization: centered 16:9 crop, Lanczos resample to 1280×720, metadata
  stripped, and alpha below 1% discarded. The transparent safety corridors
  were then cleared exactly. Panorama pixels were not repainted. Transition
  RGB is copied from its matching panorama and receives only the vertical alpha
  ramp described above.

All references were read from Git revision
`67f2494edca433c9c6dc7f155fa6a7383286c89b`:

- Orchard panoramas: `medieval_village/panorama_0.png`,
  `panorama_1.png`, and `panorama_2.png` respectively.
- Orchard side plates: `medieval_village/midground_village_0.png` and
  `medieval_village/props_0.png`.
- Watch panoramas: `medieval_castle/panorama_2.png` for all three variants.
- Watch side plates: `medieval_castle/midground_castle_0.png` and
  `medieval_castle/props_0.png`.

Accepted raw outputs had these hashes before normalization:

| Source | SHA-256 |
|---|---|
| Bell Orchard panorama 0 | `31843b985dc2089735765fc3bff5361a2d8f674ebd1f7aa45ab5f98952ee8b2a` |
| Bell Orchard panorama 1 | `442f3f4fce8a8af214ea1bc4ba69200de16589498faee0307eeec29362004998` |
| Bell Orchard panorama 2 | `36c662270b81beefbdc2656cbc421e813c4f5792f8c4cf73e2eaa10c5ead3940` |
| Cedar Watch panorama 0 | `51e81586fff0886559893692b05c2d7682c75a1c2d08da5e0327cd6fb5a9b6c6` |
| Cedar Watch panorama 1 | `5aab1a8cabc3e430ac9816a638775d74b75186800db9dca808b7bb1ee0997e87` |
| Cedar Watch panorama 2 | `aef97aa9d2125e3d0d52eda172120eb25b709889e472590ffd3ff686ac7da7f5` |
| Bell Orchard midground master | `1eb875b0c7b079200996cb6dc690afdee514c2ca621f1925563459f3d89bd362` |
| Bell Orchard prop master | `1ef19bc3e39bb828fe561e5639095e1c44b0948fbfd9fe4439cdf276acdaafeb` |
| Cedar Watch midground master | `f5f5ef7c5218facf2e2f2037345f8ae622e47c1588e314a6c64d6dca6019e239` |
| Cedar Watch prop master | `91246b1d68b809f454a912cad2ecef45f89679033dee0fa4e2dde5f14b0d9fe7` |

## Exact panorama prompts

Bell Orchard variant 0:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Bell Orchard variant 0
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, material density, warm key/cool ambient lighting, low horizon, broad open foreground, and crisp silhouettes
Primary request: transform the village into the Bell Orchard tutorial battlefield while preserving only the established visual language and compositional contract
Scene/backdrop: a peaceful working orchard outside a medieval border settlement; orderly old apple and pear trees recede along both side thirds, split-rail orchard fences, low dry-stone walls, and a modest timber bell frame with a single plain bronze bell pushed into the far-left background; distant cedar hills and a tiny fieldstone cider shed at far right
Composition/framing: wide straight-on 16:9 close-battle view; low horizon around 43 percent; continuous warm packed-earth-and-short-grass floor naturally reaches the bottom edge with no seam; central 48 percent open and quiet for two large character sprites, attack effects, and dialogue; upper corners calm for status UI; all strong props edge-weighted
Lighting/mood: clear warm morning after dew, welcoming and readable tutorial atmosphere, soft cool shadows
Color palette: honeyed earth, muted spring grass, restrained red and green fruit, weathered oak, grey fieldstone, cedar green, clean blue sky
Constraints: opaque full-canvas environment; no people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, modern orchard machinery, fantasy magic, giant fruit, strong central foreground prop, detached floating objects, black bands, or visible floor/horizon seam
```

Bell Orchard variant 1:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Bell Orchard variant 1
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, material density, low horizon, open floor, and crisp silhouettes
Primary request: transform the village square into a distinct orchard lane while preserving the established visual language and close-view composition
Scene/backdrop: a broad grass-and-packed-earth lane between old apple and pear orchard rows; a low drystone terrace and woven willow fence at far left, a small roofed harvest shelter at far right, distant cedar hills, empty baskets and a hand water trough only in distant side thirds
Composition/framing: straight-on 16:9; low horizon near 43 percent; continuous warm earth and short-grass floor reaches bottom edge with no seam; central 48 percent entirely open and quiet for two large character sprites, combat effects, and dialogue; upper corners calm; orchard trunks and props edge-weighted
Lighting/mood: fresh clear late morning, gentle dappled light, safe readable tutorial atmosphere
Color palette: warm earth, spring grass, weathered oak, grey fieldstone, restrained red and golden fruit, cedar green, blue sky
Constraints: opaque full canvas; no people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, machinery, fantasy magic, giant fruit, central focal prop, detached objects, black bands, or floor seam
```

Bell Orchard variant 2:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Bell Orchard variant 2
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, material density, low horizon, open floor, and crisp silhouettes
Primary request: transform the fortified village gate into the far bell meadow of the orchard while preserving the established visual language and close-view composition
Scene/backdrop: the orchard's outer meadow with widely spaced fruit trees at the side thirds; a modest fieldstone bell shrine with one timber crossbeam and plain bronze bell off-center in the distant left third; a low orchard wall and open timber farm gate in the distant right third; cedar hills beyond
Composition/framing: straight-on 16:9; low horizon near 43 percent; broad continuous grass-and-worn-earth floor reaches bottom edge with no seam; central 48 percent open and quiet for two characters, effects, and dialogue; upper corners calm; strong silhouettes restricted to outer thirds
Lighting/mood: mild golden afternoon, calm but alert, very readable
Color palette: honey grass, muted green leaves, restrained burgundy fruit, grey fieldstone, oak, cedar green, deep blue sky
Constraints: opaque full canvas; no people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, machinery, fantasy magic, giant fruit, strong central prop, detached objects, black bands, or floor seam
```

Cedar Watch variant 0:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Cedar Watch variant 0
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, grounded stone/oak material density, broad open floor, warm practical lights, cool ambient shadows, and crisp silhouettes
Primary request: transform the castle court into Cedar Watch, a fortified wilderness relay that must survive repeated attacks for twenty rounds
Scene/backdrop: open central muster court of a remote cedar-forest watch station; low ruined fieldstone watch platform across the far background, heavy dark-oak palisade and covered parapet pushed to side thirds, tall cedar silhouettes beyond, two restrained signal braziers far left and right, several repaired stone patches showing age
Composition/framing: straight-on 16:9; low horizon near 43 percent; continuous worn flagstone, packed earth, and sparse grass floor reaches bottom edge with no seam; central 48 percent open for two large units, combat effects, and dialogue; upper corners calm; defensive structures and supplies edge-weighted
Lighting/mood: late blue-gold dusk before a long siege, tense and steadfast but clearly readable, restrained firelight
Color palette: cool slate-blue, cedar green, weathered dark oak, grey fieldstone, muted rust-red cloth, small amber signals
Constraints: opaque full canvas; no people, units, loose weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, fantasy magic, giant flames, strong central foreground prop, black bands, or floor seam
```

Cedar Watch variant 1:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Cedar Watch variant 1
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, grounded stone/oak material density, broad open floor, and crisp silhouettes
Primary request: transform the castle court into the western approach inside Cedar Watch, visually distinct from the bright Bell Orchard tutorial
Scene/backdrop: interior of a remote timber-and-stone frontier watch; a broken low stone curtain and heavy cedar-log palisade line the far side; an offset open western gate reveals a narrow forest road; a squat repaired watch stair and empty beacon basket sit in the distant left third; cedar trunks and hills beyond
Composition/framing: straight-on 16:9; low horizon near 43 percent; uninterrupted broad floor of worn pavers blending into packed earth reaches bottom edge; central 48 percent open and quiet for two units, effects, and dialogue; gate and structures remain in side thirds; upper corners calm
Lighting/mood: cool overcast late afternoon with restrained warm torch accents, sustained defensive pressure, readable rather than dark
Color palette: cool greys, desaturated cedar green, dark oak, weathered brown earth, low amber accents, muted red signal cloth
Constraints: opaque full canvas; no people, units, loose weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, fantasy magic, giant flame, central foreground clutter, black bands, or floor seam
```

Cedar Watch variant 2:

```text
Use case: style-transfer
Asset type: opaque panorama source for a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop, Cedar Watch variant 2
Input image: edit target and strict reference for Grandleon's established stylized 3D painterly Medieval rendering, camera height, grounded stone/oak material density, broad open floor, and crisp silhouettes
Primary request: transform the castle court into Cedar Watch's eastern signal yard, visually distinct from Bell Orchard
Scene/backdrop: broad yard below a weathered stone signal tower with part of its upper wall ruined; tower offset into the far-right third, cedar-log rampart and short covered walkway across the far-left third, multiple distant beacon baskets on the ridge beyond, forested slopes and cold sky; closed supply crates only at extreme edges
Composition/framing: straight-on 16:9; low horizon near 43 percent; continuous worn paver-and-earth floor reaches bottom edge with no seam; central 48 percent open and low-detail for units, effects, and dialogue; all strong architecture off-center and edge-weighted; upper corners calm
Lighting/mood: early evening after rain, cool clear air, small warm signal glows, vigilant and resilient, highly readable
Color palette: wet slate, grey stone, dark cedar wood, deep forest green, cool blue, restrained amber and muted red
Constraints: opaque full canvas; no people, units, loose weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, fantasy magic, huge flames, central foreground prop, black bands, or floor seam
```

## Exact transparent-layer prompts

Bell Orchard midground:

```text
Use case: precise-object-edit
Asset type: genuinely transparent midground framing plate for Bell Orchard, a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop
Input image: edit target and strict reference for Grandleon's transparent plate layout, polished stylized 3D painterly Medieval rendering, scale, lighting, and edge confinement
Primary request: replace the village houses with two coherent orchard-edge framing groups: one old apple-tree trunk and upper branch canopy with short woven fence and low fieldstone wall anchored at the far left; one different pear-tree trunk and upper branch canopy with split-rail fence anchored at the far right
Composition/framing: very wide 16:9 canvas; all artwork confined to outer left 24 percent and outer right 24 percent; central 52 percent completely empty and transparent; lower central 38 percent completely empty and transparent for continuous floor, character feet, and dialogue; silhouettes fully contained within side zones
Lighting/mood: bright warm morning key with soft cool ambient shadow
Constraints: genuinely transparent background; no floor, sky, scenery across the center, people, units, weapons, signs, text, logos, watermark, interface, grid, buildings, modern objects, giant fruit, central branches, detached objects, or cast shadows beyond side silhouettes
```

Bell Orchard props:

```text
Use case: precise-object-edit
Asset type: genuinely transparent foreground prop plate for Bell Orchard, a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop
Input image: edit target and strict reference for Grandleon's transparent prop layout, polished stylized 3D painterly Medieval rendering, scale, warm lighting, and edge confinement
Primary request: replace the village street props with two restrained orchard harvest clusters: at the extreme left, one low slatted apple crate, one shallow woven fruit basket, and a folded picking cloth; at the extreme right, one capped cider barrel, one small pear basket, and a low wooden water bucket
Composition/framing: very wide 16:9 transparent canvas; clusters remain inside outermost left and right 18 percent; central 64 percent fully empty and transparent; nothing taller than 28 percent of canvas; lower center entirely clear for two character sprites and dialogue
Lighting/mood: warm morning key, cool soft shadows
Constraints: genuinely transparent background; no floor plane, scenery, trees, buildings, people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, giant fruit, magical objects, detached objects, or cast shadows beyond clusters
```

Cedar Watch midground:

```text
Use case: precise-object-edit
Asset type: genuinely transparent midground framing plate for Cedar Watch, a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop
Input image: edit target and strict reference for Grandleon's transparent plate layout, polished stylized 3D painterly Medieval rendering, scale, cool dusk lighting, and edge confinement
Primary request: replace the keep columns with frontier-watch framing groups: at far left, a heavy cedar-log palisade corner, short covered guard brace, and broken low fieldstone footing; at far right, a different cedar-log watch brace with repaired stone footing and one narrow muted red signal cloth
Composition/framing: wide 16:9 canvas; all artwork confined to outer left 23 percent and outer right 23 percent; central 54 percent completely empty and transparent; lower central 35 percent completely empty and transparent for continuous floor, unit feet, and dialogue; silhouettes fully contained
Lighting/mood: cool blue dusk ambient with restrained warm side firelight, vigilant and readable
Constraints: genuinely transparent background; no floor, sky, central wall, people, units, weapons, signs, lettering, text, logos, watermark, interface, grid, modern objects, fantasy magic, flames crossing the center, detached objects, or cast shadows beyond side silhouettes
```

Cedar Watch props:

```text
Use case: precise-object-edit
Asset type: genuinely transparent foreground prop plate for Cedar Watch, a layered 16:9 tactical-RPG close-combat and in-game conversation backdrop
Input image: edit target and strict reference for Grandleon's transparent prop layout, polished stylized 3D painterly Medieval rendering, scale, dusk lighting, and edge confinement
Primary request: replace the keep props with two restrained watch-supply clusters: at the extreme left, one low iron signal brazier with a small controlled flame and one closed dark-oak message chest; at the extreme right, one short rack holding a rolled muted-red signal cloth, one tied supply sack, and one low water cask
Composition/framing: very wide 16:9 transparent canvas; clusters stay inside outermost left and right 18 percent; central 64 percent fully empty and transparent; nothing taller than 31 percent; lower center clear for two unit sprites and dialogue
Lighting/mood: cool dusk ambient, restrained warm fire key, readable
Constraints: genuinely transparent background; no floor plane, scenery, walls, people, units, loose weapons, shields, heraldry, signs, lettering, text, logos, watermark, interface, grid, modern objects, fantasy magic, giant flame, detached objects, or cast shadows beyond clusters
```

## Production compatibility

The registered runtime route identities are:

| Pack | Source stage/map | Runtime stage/map | Runtime encounter |
|---|---|---|---|
| Bell Orchard | `bell_orchard` | `935b03cf7490233c` | `4c67d9e3fd41410e` |
| Cedar Watch | `cedar_watch` | `0079abb4797ff5b4` | `a91cac5b07854756` |

Route identity is required because generic farmland or paved terrain should
not select campaign-specific scenery. The source maps expose these portable
landmarks without changing their terrain kinds or movement rules:

- Bell Orchard: source terrain IDs `farmland_orchard_tree`,
  `forest_orchard_tree`, and `paved_orchard_bell`, exposed as
  `orchard_tree` and `orchard_bell`.
- Cedar Watch: source terrain IDs `ruins_watch_ruin` and
  `paved_signal_beacon`/`road_signal_beacon`, exposed as `watch_ruin` and
  `signal_beacon`.

The manifest combines the exact `stage_ids` and `map_ids` with each pack's
`nearby_features_any`; it does not rely on terrain histograms alone. Over-board
stage dialogue therefore selects the same pack as combat. Off-map scenes keep
their current authored portable backdrops and safe fallback behavior.

## Validation

The production layers passed these structural checks before registration:

- all 24 layers are exactly 1280×720, 8-bit sRGB;
- all six panoramas are opaque, and all 18 transition/midground/prop plates
  retain alpha;
- every transition has a fully transparent top row, a fully opaque bottom row,
  and RGB pixels identical to its panorama;
- every midground's center 512×720 pixels and every prop plate's center
  640×720 pixels have an alpha maximum of zero;
- all three panorama SHA-256 values are distinct within each environment;
- the six composites were reviewed at native size for continuous floor,
  uncropped edge silhouettes, readable central character/dialogue space, and
  visual separation between tutorial orchard and survival watch;
- no text, watermark, unit, modern object, black band, or floor seam was found.

The 24 runtime layers total approximately 25 MiB. The montage is
review-only and must not be added to the runtime pack.

The shared resolver keeps one of three variants stable for an exchange or
conversation, composes panorama/transition/midground/props in that order, and
uses the pack's full floor. A wrong route or missing nearby landmark retains
the normal safe fallback.
