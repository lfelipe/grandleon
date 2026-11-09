# Generic project export validation

Validated on 2026-11-08 with the pinned Pyrite, libdragon, Tiny3D and ares
versions recorded in this directory.

The same build wrapper produced two independent ROMs from two ordinary source
projects. It compiled each project's own GPK, derived its campaign and save
identity, generated its terrain presentation, and staged only the character
drawings required by that project:

| Source project | Battles | Drawings | Terrain families | ROM bytes | SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| The Bridge at Dawn demo | 1 | 6 | 6 | 1,785,856 | `66483d9e322e3d80c24435372e4283b6d22e27220007885b38c4f68639dc07e3` |
| Steel and Cedar | 5 | 12 | 9 | 1,884,160 | `5be6295207a08755e251c65c93935fc647cb07e3c44d2644a598ecbe79a363ea` |

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
avg_frame_us=33423 min_frame_us=32877 max_frame_us=34079 fps_x100=3000
heap_used=1213928 heap_total=2623464 heap_free=1409536 rdram=4194304
battles=1 commands=5 enemy_commands=2 combat=1 trees=4 forms=8 slot_bytes=588
RESULT PASS PYRITE_GAMEPLAY
```

Steel and Cedar separately completed Bell Orchard, Cedar Deepwood, Cedar
Watch, Gate Village and Twin Gate Keep through the packaged campaign graph,
including story, battle moments, camera scrolling, combat presentation,
aftermath, SRAM commits and terminal progression. Twin Gate recorded its
18-cell castle as closed, crossed the doorway through the ordinary routed
movement path, and then recorded the same region as revealed. Its exact-4-MB
sample was:

```text
avg_frame_us=33348 min_frame_us=24024 max_frame_us=33979 fps_x100=3000
heap_used=1355856 heap_total=2622920 heap_free=1267064 rdram=4194304
battles=5 commands=290 enemy_commands=170 combat=5 trees=82 forms=111
slot_bytes=1340
RESULT PASS PYRITE_GAMEPLAY
```

Evidence remains outside source control:

- demo ROM: `/home/lstrano/.cache/grandleon-pyrite-30fps-demo/`
- Steel ROM: `/home/lstrano/.cache/grandleon-pyrite-30fps-quality/`
- accurate-emulator logs and named frames: `build-n64/ares/`

The ares evidence directory contains each emulator log, final screenshot,
Vulkan device record, and named checkpoint frames. Both logs begin with
`GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`.
