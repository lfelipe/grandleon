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

The castle-gate draft is not included yet: its generated alpha retained a broad
dark halo that would stain the board underneath. It remains a concept until a
clean cutout and a deliberate passable centre-cell footprint are ready.
