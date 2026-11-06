# Godot character console experiment

This experiment asks one narrow question: can the current Godot character
sources produce recognisable PS1 and N64 figures, where the older `three-d`
work could not?

The answer is platform-specific. The current models materially improve role
recognition on N64, especially at a temporary close action camera. PS1 can now
show the complete roster across closer four-role pages and a two-character
high-detail attack view. Both consoles now use the same model library in a
playable Sevenfold 3D campaign and in a separate diagnostic viewer.

## Complete N64 roster coverage

The focused source-model comparison below still matters: it proves that a
careful reduction can retain the commander's shield and the rogue's paired
blades. It did not cover the roster. The Sevenfold generator now fills that
gap with deterministic procedural console geometry:

- seven styles: medieval, sci-fi, mythical, nature, Sengoku, Undead, Pirates;
- eight archetypes per style;
- male and female proportions for every archetype;
- a runtime-swappable team-colour material; and
- low and high LODs for all 112 combinations.

The curated character sheets are raster animation, not source meshes. These
112 models therefore use a shared console construction vocabulary rather than
claiming to reconstruct unseen 3D art. Oversized silhouette carriers make the
roles readable: shield and sword, bow and quiver, staff and hat, twin rods,
crook and satchel, shield and banner, paired blades and hood, or a horned
quadruped body. Each style then changes palette, headgear, armour, and outline.

`generated/sevenfold_roster_proxies.json` records every vertex and triangle
count plus the generator hash. Regenerate and check the ROM with:

```sh
python3 tools/console_3d/generate_sevenfold_roster.py
platform/nintendo64/scripts/check-sevenfold-3d.sh
```

The ROM presents 28 uncrowded strategic pages—low and high detail for every
style/body pair, with all eight roles at a time—on the same varied terrain
layouts as `games/sevenfold`. It then checks seven close views whose floor,
skyline, and sky colour come from the selected map terrain, followed by an
animated attack, hit, and counter-attack preview. Its current ares run passes
all 38 assertions. Low-detail pages measure 62–87 fps, high-detail pages
39–51 fps, terrain close views 132–163 fps, and the animated preview remains
at 158 fps in its slowest frame. All use depth. Emulator timing is a useful
gate, not a claim about original hardware.

After the checks, the ROM is interactive: **A** switches strategic detail,
the d-pad changes style or body, and **Start** opens the close view. In the
close view, **A** changes role, **B** plays the attack/counter preview, the
d-pad changes style or terrain, and **Start** returns to the board. The damage
callouts are illustrative and are not connected to campaign combat yet.

This proves full model availability and rendering headroom. The playable
Sevenfold 3D ROM uses low-detail versions of the same models on the board; this
viewer remains the faster way to inspect every combination and close view.

## Complete PlayStation roster coverage

PlayStation consumes the same generated 112-model matrix. Its dedicated
viewer uses the machine's flat-triangle GPU primitive, fixed-point perspective,
CPU painter ordering, runtime team-colour material, and clear red/blue ground
markers. It has two intentionally bounded views:

- two strategic pages with one body variant and four roles at a time; and
- a close attack page with the male and female high-detail versions of one
  role against a terrain-derived backdrop.

Build and run its full check with:

```sh
platform/playstation/scripts/check-sevenfold-3d.sh
```

The guest checks every low/high entry and renders all 56 four-role low/high
pages plus fourteen high-detail pairs. It currently passes `296/296`. The
independent GPU observer then checks the display registers and final 320×240
frame and passes `18/18`. The slowest low-detail page takes 100,363 timer
ticks (about 23.7 ms), the high-detail comparison 150,937 ticks (35.7 ms),
and the slowest attack pair 85,059 ticks (20.1 ms).

This is the diagnostic viewer rather than the campaign executable. The older
sixteen-model stress scene still takes 53.6 ms, so the playable 3D campaign
uses conservative low-detail board models and saves higher detail for bounded
close views instead of drawing sixteen high-detail models at once.

## What changed after the first proxy

The historical v1 commander replaced source components with console primitives
and produced a 296-triangle figure. It preserved broad proportions, but the
result was still too boxy and did not prove that different roles would read.
Only the more useful v2 result is carried into this branch.

V2 starts with the actual Blender meshes made by the current Godot export
scripts. It reduces each disconnected source component separately, protecting
thin weapons, hood tails, shoulder plates and other silhouette carriers. It
also compares two roles rather than one:

| Role | Source | Low LOD | High LOD |
|---|---:|---:|---:|
| Commander | 2,920 triangles | 342 triangles / 249 vertices | 524 / 340 |
| Rogue | 3,204 triangles | 330 triangles / 251 vertices | 528 / 350 |

The generated console palette converts Blender's scene-linear material values
to display RGB and adds the ambient component present in the Godot preview.
Without that conversion the rogue's graphite and teal collapse into black.

[`generated/character_proxies_v2.json`](generated/character_proxies_v2.json)
records the source files, their SHA-256 values and every LOD count. Large source
previews are in [`evidence/v2/source`](evidence/v2/source); they are useful for
checking reduction, but they are not console evidence.

## Native 320×240 evidence

| Camera | PlayStation | Nintendo 64 |
|---|---|---|
| Strategic, low LOD, near/mid/far with sprite references | [capture](evidence/v2/ps1/strategic-role-comparison.png) | [capture](evidence/v2/n64/001-v2-strategic.png) |
| Full board, sixteen low-LOD figures | [capture](evidence/v2/ps1/strategic-sixteen.png) | [capture](evidence/v2/n64/002-v2-strategic-sixteen.png) |
| Tactical, six high-LOD figures | [capture](evidence/v2/ps1/tactical-role-comparison.png) | [capture](evidence/v2/n64/003-v2-tactical.png) |
| Action, two high-LOD figures | [capture](evidence/v2/ps1/action-role-comparison.png) | [capture](evidence/v2/n64/005-v2-action.png) |
| Tactical-to-action transition | [GIF](evidence/v2/ps1/action-zoom.gif) | [GIF](evidence/v2/n64/action-zoom.gif) |

The sprite pairs in the PS1 strategic capture are world-space billboards at the
same board rows as the meshes. The two sprites in the upper-left of the N64
captures are the exact embedded sci-fi commander and rogue standing cells.
They are a same-frame reference, not a claim that the old artwork matches the
new model one-to-one.

The commander and rogue are now distinguishable by more than colour. The
commander keeps the bright face slit, crest, broad shield and sword. The rogue
keeps the asymmetric hood, narrow shoulders, sash and cyan/rose paired blades.
N64 preserves these cues at strategic distance. PS1 preserves them best in the
tactical and action views; the far strategic figures still depend mostly on
colour and outline.

## Measured result

Both paths use the repository's pinned toolchains and real 320×240 emulator
framebuffers. These are emulator measurements, not real-hardware timings.

| Platform and scene | Geometry including board | Measured frame | 30 fps |
|---|---:|---:|---:|
| PS1, six low LOD plus six reference billboards | 2,016 mesh triangles transformed | 21,021 µs | pass |
| PS1, sixteen low LOD | 5,376 mesh triangles transformed | 53,587 µs / 18 fps | **fail** |
| PS1, six high LOD | 3,156 mesh triangles transformed | 30,624 µs / 32 fps | pass, little margin |
| PS1, two high LOD at action camera | 1,052 mesh triangles transformed | 19,171 µs / 52 fps | pass |
| PS1, existing sixteen billboards plus board | sprite path | 1,585 µs / 630 fps | pass |
| PS1 Sevenfold, four low LOD roles plus board | 1,108–1,728 model triangles transformed | 23,704 µs / 42 fps | pass |
| PS1 Sevenfold, four high LOD roles for comparison | 1,988–2,908 model triangles transformed | 35,649 µs / 28 fps | **fail**, static comparison only |
| PS1 Sevenfold, two high LOD roles at action distance | up to 1,500 model triangles transformed | 20,090 µs / 49 fps | pass |
| N64, six low LOD, depth on | 2,232 triangles | 10,298 µs / 97 fps | pass |
| N64, sixteen low LOD, depth on | 5,592 triangles | 24,467 µs / 40 fps | pass |
| N64, six high LOD, depth on | 3,372 triangles | 14,975 µs / 66 fps | pass |
| N64, two high LOD at action camera, depth on | 1,268 triangles | 6,199 µs / 161 fps | pass |

The PS1 path transforms every listed model triangle, painter-orders turned
figures and then back-face culls during submission. Its sixteen-unit frame
submitted 2,339 model triangles after culling, but the CPU transform and draw
work still costs 53.6 ms. The N64 result is a passing `4/4` ares run; its RDP
depth path draws the conservative full triangle count.

[`evidence/v2/results.md`](evidence/v2/results.md) records the raw measurement
lines and exact toolchain pins. The complete historical PS1 scratch still
finishes `144/147` under the sci-fi style because of three pre-existing roster
fit assertions after these captures. The v2 phases themselves build and run,
but this is therefore experimental evidence rather than a passing release
gate.

## Camera and LOD conclusion

The test separates camera scale from model detail:

- Strategic uses a distance of 744 and low LOD. It preserves the full board
  and keeps near, middle and far figures away from the screen edge.
- Tactical uses 520 and high LOD with six visible figures. Central movement
  selection still has useful board context, but outer rows and columns are no
  longer visible. It should not replace the strategic camera.
- Action uses 360 and high LOD with two figures. The supplied GIFs interpolate
  from tactical to action distance rather than cutting. A game implementation
  should track the actor/target midpoint, clamp that focus to the board, hold
  only for the attack, and restore the player's prior strategic focus.

Zoom does not merely enlarge the v1 boxiness: the v2 action captures expose
the commander's shield/crest and the rogue's hood/twin blades as distinct
silhouettes. Even so, the current Godot source art is deliberately chunky, so
zoom cannot make it look like a smoothly modelled character.

Recommendation:

- N64: continue a genuine 3D prototype. Low LOD can support a full board and
  high LOD has ample tactical/action margin. Prefer a brief context-sensitive
  action zoom; keep the full-board camera for selection.
- PS1: keep billboards for strategic play. A context-sensitive 3D action cut-in
  is technically possible, but a permanently closer 3D board is neither fast
  enough nor clearer enough. The strongest hybrid tested here is therefore a
  sprite board that switches briefly to two high-LOD meshes for an action,
  rather than crossed planes or a six-unit close default.

## Regenerate and build

Generate the two shared LODs from the Godot sources in this checkout:

```sh
blender --background --python tools/console_3d/generate_v2_proxies.py -- \
  platform/godot/tools/export_relay_commander.py \
  platform/godot/tools/export_relay_rogue.py \
  tools/console_3d/generated/character_proxies_v2.h \
  tools/console_3d/evidence/v2/source
```

The generated JSON records the source and helper hashes. The prototype check
verifies those hashes before it builds, so changing a Godot model cannot leave
the reduced geometry silently stale.

Build or check the fixed N64 prototype:

```sh
platform/nintendo64/scripts/build-character-prototype.sh
platform/nintendo64/scripts/check-character-prototype.sh
```

Its source is
`platform/nintendo64/prototype/character_proxy_v2_rom.cpp`. The build generates
the two same-frame comparison sprites from the current art using the pinned
`mksprite`; copied sprite binaries are not kept here.

Build or check the complete PS1 viewer:

```sh
platform/playstation/scripts/build-sevenfold-3d.sh
platform/playstation/scripts/check-sevenfold-3d.sh
```

Its source is `platform/playstation/prototype/sevenfold_roster_exe.cpp`. The
check leaves a native capture under
`build-playstation-sevenfold-3d/frames/sevenfold-3d.ppm`.

Keep comparisons at native 320×240. Nearest-neighbour enlargement is fine for
presentation; cropping down to only the model defeats the board-scale test.
