# Map feature artwork

These are transparent source renders for large landmarks on tactical maps. They
are intentionally larger than a terrain cell: gameplay uses the complete
footprint, while the image is drawn once as a coherent structure.

The first prototypes were generated on 2026-11-07 with Codex's built-in OpenAI
image-generation tool. The service did not expose a model-version identifier,
so one is not guessed here. `docs/screenshots/godot/voxel-sprites.png` was used
only as a camera and presentation reference.

## `village_house_2x2.png`

- Intended footprint: 2 × 2 cells, all blocked.
- Prompt summary: one warm medieval timber-and-stone house, fixed three-quarter
  isometric camera, readable when reduced to roughly 160 pixels, transparent
  background, no terrain base or people.
- Use it to form streets and short alternate routes, not as decoration units
  unexpectedly walk through.

This original house is the style reference. It is not the runtime building:
the first board prototype made its smooth one-story procedural replacement
look too modern. The Godot board now selects one of the four dedicated
Medieval cutouts below and keeps procedural geometry only as a missing-asset
fallback.

## Medieval village family

- `medieval_house_townhouse.png`: tall stone ground floor and jettied timber
  upper story.
- `medieval_house_thatched.png`: irregular thatched cottage with a masonry
  base and lean-to.
- `medieval_house_inn.png`: asymmetrical two-story inn with intersecting
  gables and a timber porch.
- `medieval_house_craftsperson.png`: shuttered workshop with a covered side
  bay and weathered wood-shingle roof.

All four are transparent runtime cutouts for the same blocked 2×2 footprint.
They were generated on 2026-11-07 with Codex's built-in OpenAI
image-generation tool, using `village_house_2x2.png` only as the rendering,
lighting, scale, and camera reference. The service did not expose a model
version identifier. The accepted source images were resampled to 768×768 for
the game; no subject or background edit was applied during that optimization.

The shared prompt required: “one complete building centered, three-quarter
isometric view from above, entire silhouette visible, square 2×2 tactical
footprint, polished stylized 3D game render, visibly medieval European, rough
irregular fieldstone, heavy dark oak framing, aged period roof materials,
small leaded-glass windows, forged iron hardware, genuinely transparent
background; no ground, people, foliage, flowers, text, watermark, modern
windows, modern doors, modern roofline, clean suburban proportions, or
detached props.” The four subject prompts were:

1. “A tall two-story medieval border-village townhouse with a thick fieldstone
   ground floor, strongly projecting jettied half-timber upper floor, steep
   uneven reddish clay-tile gabled roof with deep eaves, one massive irregular
   stone chimney, tiny leaded windows, and a recessed arched plank door.”
2. “A low but unmistakably medieval rural cottage with thick rough stone
   walls, a dark timber gable, a very steep shaggy thatched roof with an uneven
   ridge, a crooked stone smoke chimney, a small timber lean-to, tiny shuttered
   leaded windows, and a heavy arched oak door.”
3. “A compact two-story medieval village inn with an L-shaped fieldstone base,
   projecting half-timber upper floor, steep intersecting clay-tile gables,
   deep eaves, an open timber porch, tiny leaded windows, and an empty
   wrought-iron hanging-sign bracket with no text.”
4. “A narrow medieval craftsperson's house with a high irregular stone ground
   floor, a visibly jettied dark-oak-and-plaster upper workshop, a steep
   weathered wood-shingle roof with a small cross-gable, a masonry chimney,
   tiny leaded windows with shutters, an arched plank door, and a built-in
   covered side bay.”

## `ancient_tree_cluster_2x2.png`

- Intended footprint: 2 × 2 cells, all blocked.
- Prompt summary: one connected old-growth cluster with massive roots, two
  intertwined trunks, layered canopy, a fallen log and restrained rocks, fixed
  three-quarter isometric camera, transparent background.
- Use it as a forest landmark or route boundary. Smaller ordinary forest cells
  can still provide texture around it.

## Regeneration prompts

Both prompts used `docs/screenshots/godot/voxel-sprites.png` as a style and
camera reference and requested a genuinely transparent background.

**House:** “One medieval timber-and-stone village house for an isometric
tactical board, occupying an exact 2×2 diamond-grid footprint. Substantial but
welcoming two-story structure; timber framing, pale plaster, stone lower walls,
warm red-brown pitched roof, small chimney and simple door. Fixed three-quarter
isometric camera, roof and two walls readable, polished stylized 3D render with
painterly texture, clear at roughly 160 pixels. One isolated uncropped building;
no terrain base, grid, people, trees, fences, text, watermark, or shadow beyond
the footprint.”

**Ancient trees:** “One connected old-growth forest obstacle for an isometric
tactical board, occupying an exact 2×2 diamond-grid footprint. Two massive
intertwined trunks, broad roots, layered green canopy, one fallen log and a few
restrained rocks; clearly an impassable forest mass rather than four decorative
trees. Fixed three-quarter isometric camera, roots and trunks readable, polished
stylized 3D painterly render, clear at roughly 180 pixels. One isolated uncropped
cluster; no terrain base, grid, people, buildings, text, watermark, magical
glow, or distant shadow.”

## Twin Gate Keep

- `medieval_castle_keep_exterior.png` is the closed keep seen while the army
  crosses the outer court.
- `medieval_castle_keep_cutaway.png` keeps the same camera, scale and silhouette
  but opens the double doors and exposes the stone throne room, columns, dais,
  chair and torchlight.
- Intended footprint: 6 × 6 cells. Eighteen wall cells are blocked; two door,
  fourteen chamber and two dais cells are paved and walkable.

The pair was generated on 2026-11-07 with Codex's built-in OpenAI
image-generation tool. The service did not expose a model-version identifier,
so one is not guessed here. The cutaway was produced as an edit of the accepted
exterior so the two states align during a dissolve. Both accepted images were
resampled to 1024×936, and alpha below 25% was discarded to remove a faint
coloured fringe; the subject itself was not repainted during that cleanup.

**Exterior prompt:** “One imposing but readable medieval border keep for an
isometric tactical board, occupying an exact connected 6×6 footprint. Warm
fieldstone walls, two flanking towers, crenellations, steep weathered red-brown
roofs, and a closed central double oak door facing the lower-left approach.
Fixed three-quarter isometric camera from above, entire silhouette visible,
polished stylized 3D painterly game render matching the Medieval village family,
clear at board scale, genuinely transparent background. One isolated uncropped
castle; no terrain base, grid, people, units, foliage, text, watermark, modern
materials, detached props, or distant shadow.”

**Cutaway edit prompt:** “Keep the exact castle, camera, lighting, scale,
silhouette and transparent background. Open the central double doors and remove
the near wall and roof only where needed to reveal the room beneath: a readable
stone throne chamber with a tiled floor, columns, warm wall torches, a short
dais and one period chair. The result must align pixel-for-pixel with the closed
exterior for a smooth gameplay dissolve. No units, people, text, grid, terrain
base, modern objects, extra wings, or changed camera angle.”
