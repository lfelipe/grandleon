# Console visual architecture prototypes

These targets answer a visual question before production gameplay changes:
what is the smallest rendering architecture that makes each older console look
like itself rather than like a flat tile mock-up?

`generate-twin-gate-assets.py` reads the real Steel and Cedar Twin Gate map and
the approved closed/cutaway keep pair. It refuses a changed 20×14 map or any
change to the connected 6×6 keep footprint, then emits console-native derived
textures and a shared scene header. The five keep states are closed, three
ordered-dither transition steps, and open. No external art is used.

The two source plates are
`platform/godot/project/art/map_features/medieval_castle_keep_exterior.png`
and `medieval_castle_keep_cutaway.png`. The generator trims their transparent
padding, preserves one shared 192×176 composition, adds a one-pixel dark
silhouette rim, applies an original 4×4 ordered colour perturbation and uses a
deterministic median-cut palette. Source and output hashes are recorded in
`twin_gate_assets.json`; two consecutive runs must reproduce the same bytes.

The Nintendo 64 proof uses bilinear-filtered CI4 ground on shaded isometric
triangles, visible elevation sides, CI8 keep tiles, soft shadows and filtered
curated characters. The PlayStation proof uses affine CI4 terrain triangles,
ordered-dithered CI8 keep planes, hard silhouettes and a compact layered HUD.
Both show the same authored approach, threshold and revealed-room state.

The prototypes are intentionally separate from campaign runtime. They do not
change movement, collision, objectives, placements or saves. A production
decision comes only after native 320×240 captures and frame/memory measurements
are reviewed.

The N64 proof includes a matched flat-board baseline followed by the exterior,
threshold and room-revealed states. Its pinned ares gate passes 6/6 with a
16,467 μs worst frame (60 fps). The 90 CI8 landmark tiles embed as 242,640
bytes; each streamed tile is 64×32, exactly 2,048 texel bytes plus a 512-byte
TLUT, and the complete proof ROM is 573,440 bytes. The optimization that makes
the frame budget is architectural: the terrain atlas is uploaded once per
frame and reused by all 280 shaded cells.
