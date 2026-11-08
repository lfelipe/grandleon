# Steel and Cedar campaign milestone validation

Validated on 2026-11-08 from Grandleon revision
`49c1034721dc4337be2c342520f5e22ae825a595` and Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The normal build wrapper verified the pinned Pyrite/libdragon/Tiny3D inputs,
compiled the real project into a 22,292-byte GPK, generated the 12 required
console drawings, cross-built the portable campaign, campaign-runtime,
simulation, tactics, package, client and storage libraries, ran the host camera
test, and produced this 1,572,864-byte ROM:

```text
grandleon_pyrite_steel_cedar.z64
SHA-256 8564afb1bfb62d3fbdc1538108e223e9516d6ae5eab59626105669f91c4ede4c
```

The exact ROM booted in real-RSP ares with
`Nintendo64/ExpansionPak=false`. With an empty 32 KiB SRAM image it read the
packaged `choice` player policy, followed Title -> One Player -> New Game,
advanced the real prologue story node, prepared the real 8x6 Bell Orchard
campaign board, and finished that battle through ordinary simulation/tactics
commands. The session committed 28 accepted commands and 61 ordered battle
events, saved 848 used bytes, advanced the Orchard Camp story node, and
prepared the real 14x9 Cedar Deepwood board. The Cedar viewport then swept both
axes within the playable bounds and settled for this 120-frame sample:

```text
avg_frame_us=16717 min_frame_us=16421 max_frame_us=16899 fps_x100=6000
heap_used=1146032 heap_total=2743976 heap_free=1597944 rdram=4194304
bell_commands=28 enemy_commands=14 camera_span_x_x100=487
camera_span_y_x100=243
RESULT PASS PYRITE_GAMEPLAY
```

A second boot used the same ares SRAM file. The launch screen detected the
save, selected Continue, resumed directly at Cedar Deepwood through
`CampaignSession`, and passed the same exact-4-MB viewport/frame gate at 60
fps with 1,620,216 heap bytes free. The runtime logs
`GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`; the build separately
checks the ROM header so the proof cannot silently become an artifact from the
old renderer.

The ROM, save image and emulator captures remain build artifacts rather than
tracked source. Current evidence is under
`/home/lstrano/.cache/grandleon-pyrite-campaign/final-ares-2/`; `new.log`
proves fresh campaign progression, `resume.log` proves cartridge continuation,
and their trail directories contain the Bell, close-combat and Cedar viewport
landmarks.
