# What the scratch program photographed

Produced by `platform/playstation/scratch/run-scratch3d-film.sh`, which runs
`grandleon_psx_scratch3d.ps-exe` under the pinned PCSX-Redux with
`scratch3d_probe.lua` attached. Nothing here is a check and nothing is in a
gate; every file regenerates from that one command. The committed **stills**
are the framebuffer captures the preview renderer in `tools/placeholder_art`
was checked against; the same run also assembles short frame-counted GIF
**films**, not committed. They compare candidates the repository has already
decided between.

**Build first**: the script photographs whichever
`grandleon_psx_scratch3d.ps-exe` sits in `build-playstation/target/`, so after
a change run

```sh
cmake --build build --target grandleon_playstation_scratch3d
```

or the pictures will faithfully agree with the previous executable.

A still is the whole 320x240 display, uncropped. **The stills are also the mesh
pipeline's promotion proof**: the figures are generated tables in
`tools/placeholder_art`, and every committed still regenerates byte for byte
from them; `git status` after a run is the check.

Four sets, one per question.

## 1. Does a panning camera answer the far-cell objection?

Same board, pitch, focal length and distance; only the camera's focus
differs.

| File | Focus | Far row drawn at |
| --- | --- | --- |
| `pan-rest.png` | board centre, the measured camera unchanged | 22 px a tile |
| `pan-cursor-follow-ceiling.png` | 128 units, the furthest a cursor-following camera goes | 24 px |
| `pan-board-clamped.png` | 288 units, the board's own edge | 26 px |
| `pan-one-board-reach.png` | 576 units, one whole board past the edge | 34 px |

The near row is 34 px at rest, so the last picture fully answers the
objection. Seven eighths of it is backdrop, which is the finding. Films:
`pan-cursor-follow.gif` (the shipped cursor-chase idiom, the one to judge
fluidity by) and `pan-free-pan.gif` (d-pad drives the camera itself). The
magenta backdrop is deliberately a colour no art can produce.

## 2. Does a unit read better as a mesh than as a billboard?

The knight as a hand-built low-poly mesh beside the same archetype as a
screen-space billboard, at near, middle and far rows; the mesh is always the
left of a pair.

| File | What it shows |
| --- | --- |
| `mesh-beside-billboard.png` | all three pairs from the measured camera at rest |
| `mesh-near-row.png` / `mesh-mid-row.png` / `mesh-far-row.png` | each pair with the camera brought to it |
| `mesh-textured.png` | the mesh textured from the character cell instead of flat-shaded |
| `mesh-faction-ramps.png` | six mesh knights in the six faction colours, from the CLUTs the library already generates |
| `mesh-sixteen.png` | the full battle load: sixteen units, all mesh |

Films: `mesh-camera-sweep.gif` (both members of every pair drawn every
frame of a 32-frame sweep) and `mesh-motion.gif` (static camera, the shared
client's frame-counted motions applied identically to mesh and billboard; the
mesh also turns to face its walk and leans when hit, which a billboard
structurally cannot do).

## 2a. The fit

Set 2 settles flat shading and exposes a disproportion: a global two-tile
build draws the mesh 41 px on the far row and 25 on the near: backwards
with distance. The fix scales each unit about its own feet so its drawn
height equals its tile's drawn width. Set 2's files are the "before"; the
`mesh-fit-*.png` set re-shoots the same views under the fit, adds the archer
(the second archetype), and `mesh-fit-camera-sweep.gif` recomputes the fit
per frame. Worst on-display deviation from the billboard across 32 cameras
is 3 px.

## 2b. The match

Heights agree after 2a and the knights still read as different creatures:
one uniform scale makes width wear the height's correction (33 px drawn
against a 21 px sprite on the near row, 9 against 14 on the far). The fix
holds the mesh to its own sprite's opaque silhouette on both axes, width
split from height. The `mesh-match-*.png` set re-shoots 2a's views; drawn
aspect is 0.69–0.74 against the sprite's 0.69 at all three distances, and
`mesh-match-camera-sweep.gif`'s worst deviation across 32 cameras is 2 px.

## 2c. Does a *new* archetype read?

The mesh rules in `tools/placeholder_art/placeholder_art/meshes/rules.py` are
necessary, not sufficient: a `mage` passed every one of them and was refused
for not reading. So a commission is photographed as well as measured.
`mesh-commission.png` shows every archetype the *generator* commissioned,
matched to its own sprite and beside its own billboard, rows running near to
far. Worst width and height error against the sprites: 1 px, with no
per-archetype term in the rule.

## 2d. Every commission after the first two

One still per meshed archetype that is neither knight nor archer, at all
three pairs, named after the archetype by the program looping over the
generated header. Commissioning a mesh adds a file here without the scratch
being edited. Current: `mesh-match-rogue.png` (stance authored at ±26
against the knight's ±13, which is what separates two humanoids at this
size) and `mesh-match-beast.png` (the roster's one non-humanoid, whose body
runs along the axis this camera shows best).

## 3. How should the world past the board's edge be dressed?

Same board, same camera panned to the board's edge, against a dark slate
backdrop. The outside terrain is the map's own kinds and relief continued
outward; nothing was authored for it and no palette entry exists.

| File | Candidate | Costs |
| --- | --- | --- |
| `surround-none.png` | control: nothing outside the board | none |
| `surround-clut.png` | same textures through a second desaturated CLUT per kind per ring | 40 of 1,024 CLUT slots, nothing per frame |
| `surround-trim.png` | one full-colour ring under one semi-transparent framing band | one ring of ground, one overlay quad a cell |
| `surround-fog.png` | four full-colour rings under stacked backdrop passes | 620 overlay quads |

Films: `surround-clut.gif`, `surround-trim.gif` and `surround-fog.gif` are
sixteen frames each, side by side for comparison.
