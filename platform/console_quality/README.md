# Console visual architecture prototypes

These targets answer a visual question before production gameplay changes:
what is the smallest rendering architecture that makes each older console look
like itself rather than like a flat tile mock-up?

`generate-twin-gate-assets.py` reads the real Steel and Cedar Twin Gate map and
the approved closed/cutaway keep pair. It refuses a changed 20×14 map or any
change to the connected 6×6 keep footprint, then emits console-native derived
textures and a shared scene header. It also emits an N64-only 96×88 board pack
for the distance at which the keep appears during play. The five keep states
are closed, three
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

The original proofs remain separate for matched before/after review. The N64
architecture is also wired to the exact authored Twin Gate footprint in the
playable campaign. That integration changes only presentation and camera
framing; it does not change movement, collision, objectives, placements, or
saves. Other maps keep the ordinary production path and allocate no keep art.

The N64 proof includes a matched flat-board baseline followed by the exterior,
threshold and room-revealed states. Its pinned ares gate passes 6/6 with a
16,467 μs worst frame (60 fps). The 90 CI8 landmark tiles embed as 242,640
bytes; each streamed tile is 64×32, exactly 2,048 texel bytes plus a 512-byte
TLUT, and the complete proof ROM is 573,440 bytes. The optimization that makes
the frame budget is architectural: the terrain atlas is uploaded once per
frame and reused by all 280 shaded cells.

The production-shaped N64 gate uses the real scrolling 16×11 camera over the
20×14 map. Five board-distance states use thirty 48×32 CI4 tiles in total;
only six are uploaded for a frame. The detailed 64×32 CI8 pack is retained for
close framing and only the requested state is loaded. The ares production gate
passes 13/13 at a measured 21,905 μs worst frame (45 fps), with 28,080 bytes of
board cache, 76,608 bytes peak after one detailed close state, 270,720 bytes of
packaged keep art, and a 2,080,768-byte ROM.
