# Cedar Deepwood close-view pack

This reviewed pack replaces the original low-detail Godot relay capture used
for forest combat and conversations. The old capture was useful for proving
the resolver, but its cone trees, flat tile floor, and empty navy field were
not release-quality artwork.

The three opaque panoramas are complete 1280×720 compositions. They include a
continuous walkable foreground, so the manifest sets `floor_strip_top` to
`1.0` and the close view does not draw a separate live floor across the art.
The existing transition, midground, and prop filenames remain transparent
plates to preserve the shared four-role pack contract. Future reviewed edge
details can be added to those plates without changing campaign data or the
resolver.

## Provenance

- Date: 2026-11-07.
- Authoring agent: Codex.
- Image mode: built-in OpenAI image-generation tool.
- Model/version/seed: not exposed by the built-in service; no model identifier
  or seed is guessed.
- Returned format: 1672×941 PNG.
- Normalization: direct Lanczos resample to 1280×720, 8-bit sRGB, metadata
  stripped. The returned aspect ratio differs from 16:9 by less than 0.02%.
- Runtime changes after generation: none to the panorama pixels beyond that
  resample.

Accepted raw-output SHA-256 values:

| Variant | SHA-256 |
|---|---|
| Sunlit clearing | `a462e2beae659346f790e3dc90ef3b2cb5fee0a80710b5b0c8d2fbb1825b3a41` |
| Rock-line trail | `929eef9befcbe975f025b4ec1d0a4c8d2aa8746a66e15fbccb9c58b76fe39689` |
| Standing-stone crossroads | `14ccaff55f6216bcda72e01b62a6042ef4d2f19fb7d3b9ebfeb35b429747da74` |

## Prompt set

All three calls used these shared requirements:

```text
Use case: stylized-concept
Asset type: 16:9 battle and in-game conversation backdrop for Grandleon's Godot player
Primary request: a polished, high-detail ancient cedar forest clearing for Cedar Deepwood, suitable behind two large character cutouts and a dialogue panel
Style/medium: richly painted premium tactical-RPG game background, warm storybook realism, coherent with the detailed Medieval village and castle art; materially detailed bark, moss, soil, stone and foliage; not photorealistic
Composition/framing: exact wide 16:9 landscape; keep the middle 55% and lower central stage visually open for two characters; push dense trunks, roots, rocks, ferns and props to the side thirds; horizon around 48% height; full scene from canopy through walkable foreground
Constraints: no people, creatures, interface, text, logo or watermark; no flat vector shapes, cone-shaped low-poly trees, black/empty background, split-screen, or hard floor/background cut line
```

The exact scene additions were:

```text
Variant 0: monumental sunlit cedar trunks frame a broad earthen clearing, with moss-covered boulders, a winding path, layered forested hills and subtle mist; clear late-afternoon light, muted gold and blue-green distance.

Variant 1: a sheltered morning clearing with a winding side trail and low broken line of natural mossy boulders used as cover; exposed roots, ferns, a shallow woodland rise, cooler dappled light and mist.

Variant 2: an after-rain forest crossroads with weathered standing stones and fragments of an old boundary marker only at the far edges; wet earth, roots, ferns, distant highlands and restrained sun shafts.
```

## Validation

The Godot import, conversation-backdrop smoke, combat-cinematic smoke, and
production Steel and Cedar capture must pass before these images are treated
as reviewed. The private review page should show runtime conversation and
combat frames rather than the raw panoramas.
