# Isolated phase-1 validation

Validated on 2026-11-07 against Grandleon revision
`eff04fcef78412d6115f92547b9c431907ddf32c` with Steel and Cedar source hash
`cd611a2b85c18eead6e8f549c7472c9fa8d3ecaa2d8eadf1f977b10cb2cb3e5e`.

The wrapper completed all of these from a generated output directory:

1. verified the Pyrite v0.10.0 release archive and AppImage;
2. cold-built and exported the pinned libdragon/Tiny3D toolchain;
3. built `grandleon_content_compile` and generated a 22,252-byte GPK from the
   real Steel and Cedar source;
4. generated the 12 required project drawings using the normal console-art
   pipeline and selected the five Bell Orchard identities;
5. cross-built the portable Grandleon libraries without building the existing
   Nintendo 64 target;
6. built an 835,584-byte Pyrite ROM through the official headless CLI.

The ROM then booted with Expansion Pak disabled under accurate real-RSP ares
using paraLLEl-RDP. It decoded the real 8x6 Bell Orchard encounter (48 terrain
cells, five units, 16 trees) and reported this 120-frame sample:

```text
avg_frame_us=16715 min_frame_us=16387 max_frame_us=17361 fps_x100=6000
heap_used=836056 heap_total=3206184 heap_free=2370128
RESULT PASS PYRITE_COMPARE
```

The validated ROM SHA-256 was
`6a2eeb010dc97851cc56dadcc66b899e74a8846aa414a2a74620480e9b5ea1ce`.
The ROM and emulator evidence remain build artifacts and are intentionally not
part of this source package. A later source or campaign change is expected to
change the hash; it must not change the runtime assertions or 4 MB boot gate.
