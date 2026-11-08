# Generic project export validation

Validated on 2026-11-08 with the pinned Pyrite, libdragon, Tiny3D and ares
versions recorded in this directory.

The same build wrapper produced two independent ROMs from two ordinary source
projects. It compiled each project's own GPK, derived its campaign and save
identity, generated its terrain presentation, and staged only the character
drawings required by that project:

| Source project | Battles | Drawings | Terrain families | ROM bytes | SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| The Bridge at Dawn demo | 1 | 6 | 6 | 1,703,936 | `03f31660029243d3d53776a544a3ee5539f8e518528f6685ce9cee613b385f3c` |
| Steel and Cedar | 5 | 12 | 9 | 1,818,624 | `e4969fcc989f8b3da79b0528190b14676f2c3f9f3edd8324d95aae10e6a5a9e0` |

The generated headers, terrain atlases, close plates, character counts and ROM
hashes differ. The focused project-preparation test repeats the demo export
byte-for-byte and also proves that a project beyond the nine-terrain budget is
refused with a useful diagnostic instead of receiving Steel assets:

```sh
TMPDIR=/path/with-space \
  python3 platform/nintendo64/pyrite/tests/project_prepare_test.py
```

Both exact ROMs booted in real-RSP ares with
`Nintendo64/ExpansionPak=false`. The demo log identifies
`game=grandleon.demo campaign=demo_campaign package=6304 drawings=6`,
renders the authored 6x4 map, exercises movement, actions and the optional
close handoff, commits its real defeat route and reaches its terminal. Its
settled 120-frame sample was:

```text
avg_frame_us=16710 min_frame_us=16451 max_frame_us=16893 fps_x100=6000
heap_used=1146984 heap_total=2690824 heap_free=1543840 rdram=4194304
battles=1 commands=5 enemy_commands=2 combat=1 slot_bytes=588
RESULT PASS PYRITE_GAMEPLAY
```

Steel and Cedar separately completed Bell Orchard, Cedar Deepwood, Cedar
Watch, Gate Village and Twin Gate Keep through the packaged campaign graph,
including story, battle moments, camera scrolling, combat presentation,
aftermath, SRAM commits and terminal progression. Its exact-4-MB sample was:

```text
avg_frame_us=16637 min_frame_us=6992 max_frame_us=17461 fps_x100=6000
heap_used=1288648 heap_total=2690328 heap_free=1401680 rdram=4194304
battles=5 commands=290 enemy_commands=170 combat=5 slot_bytes=1340
RESULT PASS PYRITE_GAMEPLAY
```

Evidence remains outside source control:

- demo: `/home/lstrano/.cache/grandleon-pyrite-generic-demo/ares-final/`
- Steel: `/home/lstrano/.cache/grandleon-pyrite-generic-steel/ares-final/`

Each directory contains the emulator log, final screenshot, Vulkan device
record and named checkpoint frames. Both logs begin with
`GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`.
