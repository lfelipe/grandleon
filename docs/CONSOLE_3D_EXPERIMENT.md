# The console 3D experiment

The newer Godot character models do help. On Nintendo 64, a reduced commander
and rogue remain recognisable at board distance and run above the 30 fps target.
The N64 experiment now also has a complete procedural roster: seven styles,
eight archetypes, male and female bodies, and runtime team colours. These
results are available as separate experimental ROMs. They do **not** replace
the normal sprite campaign renderer.

PlayStation does not get a 3D ROM in 0.4. Its full-board test reached only
18 fps, while the existing sprite renderer remained both faster and clearer.
The PlayStation captures stay here as useful research for a possible two-unit
combat close-up later.

## What you can build

These are three different N64 outputs:

- `build-n64.sh` builds the supported, playable sprite campaign ROM from an
  authored project.
- `build-character-prototype.sh` builds a fixed 3D comparison containing a
  commander, a rogue, a test board, three camera distances, and performance
  assertions. It is not a campaign and does not read editor projects.
- `build-sevenfold-3d.sh` builds the complete 3D roster on the seven varied
  maps from `games/sevenfold`. It compares both detail levels and includes an
  experimental terrain-aware attack view. It is not campaign gameplay.

Build the prototype:

```sh
platform/nintendo64/scripts/build-character-prototype.sh
```

Build a self-contained review folder with the ROM and its notices:

```sh
platform/nintendo64/scripts/build-character-prototype.sh \
  --bundle out/n64-3d-character-prototype
```

Run the complete reproducibility and ares check:

```sh
platform/nintendo64/scripts/check-character-prototype.sh
```

The ROM is named `grandleon-n64-3d-character-prototype.z64` so it cannot be
confused with a generated campaign ROM.

Build and check the complete roster viewer:

```sh
platform/nintendo64/scripts/build-sevenfold-3d.sh
platform/nintendo64/scripts/check-sevenfold-3d.sh
```

That ROM is named `grandleon-n64-sevenfold-3d.z64`. The check validates all
112 generated model combinations, boots the ROM in ares, captures 28
style/body/detail pages, seven terrain-aware close views, and one animated hit
frame, and applies the frame budget throughout. Its current result is `38/38`.

## What complete coverage means

The complete roster is `7 styles × 8 archetypes × 2 bodies = 112 models`.
Every style/body page shows all eight roles at a readable board distance,
spaced across the map rather than stacked into one crowded formation. Half use
the blue team slot and half the red team slot; team colour is replaced at
runtime rather than baked into separate meshes.

These are deterministic procedural low-poly console models. The curated Godot
character artwork is raster animation, so there is no hidden set of 112 source
meshes to reduce. The generator records the exact model counts and produces a
low and high LOD for each combination. The roster ROM now tests both at the
same strategic camera. Its close view uses two high-detail models and builds
the floor, skyline, and sky colour from the selected Sevenfold terrain.

After the automated review, the ROM remains interactive. On the strategic
view, **A** switches detail, the d-pad changes style or body, and **Start**
opens the close view. There, **A** changes role, **B** plays an illustrative
attack and counter-attack, the d-pad changes style or terrain, and **Start**
returns to the board. The damage numbers are a presentation test, not results
from the shared combat engine.

## What was measured

All captures are native 320×240 emulator framebuffers. These are emulator
measurements, not measurements from original hardware.

| Platform and scene | Result |
|---|---:|
| N64, sixteen low-detail figures and board | 40 fps |
| N64, six high-detail figures | 66 fps |
| N64, two high-detail figures at action distance | 161 fps |
| N64 Sevenfold strategic pages, eight low-detail roles, depth on | 62–87 fps |
| N64 Sevenfold strategic pages, eight high-detail roles, depth on | 39–51 fps |
| N64 Sevenfold terrain-aware close view, two high-detail roles | 132–163 fps |
| N64 Sevenfold animated attack/counter preview | 158 fps at its slowest frame |
| PlayStation, sixteen low-detail figures | 18 fps |
| PlayStation, six high-detail figures | 32 fps |
| PlayStation, two high-detail figures at action distance | 52 fps |
| PlayStation, existing sprite board | 630 fps in the same harness |

The important improvement is not just speed. The commander retains a broad
shield, crest and sword; the rogue retains a hood, narrow silhouette and paired
blades. Zooming closer makes those role cues easier to read. It still does not
turn these deliberately chunky sources into smooth character models.

## See the comparison

The complete method, source hashes and capture table are in
[tools/console_3d/README.md](../tools/console_3d/README.md).

- [N64 strategic board](../tools/console_3d/evidence/v2/n64/002-v2-strategic-sixteen.png)
- [N64 action view](../tools/console_3d/evidence/v2/n64/005-v2-action.png)
- [N64 camera transition](../tools/console_3d/evidence/v2/n64/action-zoom.gif)
- [PlayStation strategic board](../tools/console_3d/evidence/v2/ps1/strategic-sixteen.png)
- [PlayStation action view](../tools/console_3d/evidence/v2/ps1/action-role-comparison.png)
- [Raw measurements and pins](../tools/console_3d/evidence/v2/results.md)

## Reproducibility and licence

The N64 prototype uses its own pinned preview libdragon and Tiny3D image. The
supported campaign toolchain is unchanged. Generated LOD metadata records the
Godot source and Blender-helper hashes, and the check refuses stale generated
geometry before it starts the ROM.

Tiny3D is MIT licensed. Its complete notice is kept under `third_party/tiny3d`
and is copied into every staged prototype review bundle.
