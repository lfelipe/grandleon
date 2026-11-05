# The console 3D experiment

The newer Godot character models do help. On Nintendo 64, a reduced commander
and rogue remain recognisable at board distance and run above the 30 fps target.
That result is now available as a separate experimental ROM. It does **not**
replace the normal sprite campaign renderer.

PlayStation does not get a 3D ROM in 0.4. Its full-board test reached only
18 fps, while the existing sprite renderer remained both faster and clearer.
The PlayStation captures stay here as useful research for a possible two-unit
combat close-up later.

## What you can build

These are two different N64 outputs:

- `build-n64.sh` builds the supported, playable sprite campaign ROM from an
  authored project.
- `build-character-prototype.sh` builds a fixed 3D comparison containing a
  commander, a rogue, a test board, three camera distances, and performance
  assertions. It is not a campaign and does not read editor projects.

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

## What was measured

All captures are native 320×240 emulator framebuffers. These are emulator
measurements, not measurements from original hardware.

| Platform and scene | Result |
|---|---:|
| N64, sixteen low-detail figures and board | 40 fps |
| N64, six high-detail figures | 66 fps |
| N64, two high-detail figures at action distance | 161 fps |
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
