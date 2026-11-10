# Generic project export validation

## Accented text

Spleen 6×12 glyph banks preserve Latin-1 accents, including Portuguese
cedillas and tildes. Authored UTF-8 and already-decoded dialogue use separate
drawing paths, so wrapped text is never decoded twice. Actual production
captures show `Aço`, `Alguém`, `você` and `CONFIGURAÇÕES` correctly. Five host
regressions check decoding, wrapping, glyph pixels and controller projection.

The refreshed Bell Orchard recording uses the normal production ROM and
ordinary controller input. The emulator is clocked to 60 retraces per second;
the 30 fps recording retains the full move, attack, counterattack and return
without changing playback speed.

## Hardware feedback follow-up

The board is north-up, and both the D-pad and analog stick follow screen
axes. Unit selection shows movement reach. Attack is available only with a
legal target, starts on that target, and shows health and a combat forecast.
Menus choose a corner away from the units involved. Both armies use combat
close-ups; fatal actors remain through the animation and counters keep sides.

The title displays the Grandleon mark. Story and battle dialogue show the
speaker, including off-board cast members, and page complete lines. Settings
have independent saved Music and Sound Effects sliders; the visible reduced
motion option is removed. The demo includes Spanish and Brazilian Portuguese,
with language selection conditional on authored translations. Translated
prompts fit their panels.

The refreshed five-stage review completed 290 player commands, 170 opposing
commands and the 18-cell castle reveal in base 4 MiB ares. The final sample was
30.00 fps, with 1,160,696 bytes of free heap. Evidence:
`/tmp/grandleon-n64-v8-review-evidence/ares/`. Normal exports keep the review
driver disabled. Final hardware acceptance remains open.

## Current production artifact

The normal ROM keeps the automated review driver disabled. SHA-256:
`b4bfd851032a9c333513035601220919b7f4f05122dbac338a0ee71e34dd0900`.
The production source and evidence are in
`~/.cache/grandleon-pyrite-hardware-ux-v8/`. The exact ROM still needs a
manual hardware playthrough; scripted campaign completion does not establish
balance or controller feel.

The automated campaign driver is now disabled in normal exports. A separately
built production ROM logged `review_driver=0` and remained on its title screen
through an 18-second no-input boot. External controller input then reached
Bell Orchard without any automated movement. Automated campaign checks must
build with `GRANDLEON_N64_REVIEW_DRIVER=1`; pressing a physical controller
also retains manual control when a later encounter loads.

The explicit review ROM completed five battles, 290 commands, 170 opposing
commands, five combat presentations and the 18-cell castle reveal with 4 MiB
RAM. Its final sample measured 30.00 fps, with 1,258,880 bytes of free heap.
The corresponding evidence is under `ares/review05*` in the cache root below.
These scripted routes verify mechanics and rendering; they do not establish
campaign balance or hardware acceptance.

The castle review adds battlement silhouettes and a capture-driver cursor
move into the revealed room, followed by a camera-settling pause. The driver
does not alter visibility: normal campaign movement must reveal the room
first. The full campaign passed again at 30.00 fps, with 1,258,880 bytes of
free heap. The settled room frame is
`/home/lstrano/.cache/grandleon-pyrite-surface-quality/ares/room-trail/112-PYRITE_CASTLE_ROOM_REVIEW.png`.

The latest graphics review adds filtered detail to raised terrain, trees,
elevation faces, and castle masonry, plus compatible Godot panorama plates
loaded one stage at a time. The full accurate-emulator campaign passed with
base 4 MiB RAM: all five battles, 290 commands, 170 opposing commands, five
combat presentations, and the 18-cell castle closed/revealed checkpoints.
The final 120-frame sample measured 30.00 fps, a 33,997 microsecond maximum,
and 1,263,256 bytes of free heap. Evidence and transition frames are under
`/home/lstrano/.cache/grandleon-pyrite-surface-quality/ares/final*`.

Validated on 2026-11-09 with the pinned Pyrite, libdragon, Tiny3D and ares
versions recorded in this directory.

The same build wrapper produced two independent ROMs from two ordinary source
projects. It compiled each project's own GPK, derived its campaign and save
identity, generated its terrain presentation, and staged only the character
drawings required by that project:

| Source project | Battles | Drawings | Terrain families | ROM bytes | SHA-256 |
| --- | ---: | ---: | ---: | ---: | --- |
| The Bridge at Dawn demo | 1 | 6 | 6 | 1,785,856 | `66483d9e322e3d80c24435372e4283b6d22e27220007885b38c4f68639dc07e3` |
| Steel and Cedar | 5 | 12 | 9 | 1,884,160 | `baed417055f26251efd84dacb55e685ca1b8fda649fcee4474b54553ce47e444` |

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
avg_frame_us=33351 min_frame_us=24364 max_frame_us=34014 fps_x100=3000
heap_used=1355872 heap_total=2622936 heap_free=1267064 rdram=4194304
battles=5 commands=290 enemy_commands=170 combat=5 trees=82 forms=111
slot_bytes=1340
RESULT PASS PYRITE_GAMEPLAY
```

Evidence remains outside source control:

- demo ROM: `/home/lstrano/.cache/grandleon-pyrite-30fps-demo/`
- Steel ROM: `/home/lstrano/.cache/grandleon-release05-20261109/nintendo64/`
- accurate-emulator logs and named frames: `build-n64/ares/`

The ares evidence directory contains each emulator log, final screenshot,
Vulkan device record, and named checkpoint frames. Both logs begin with
`GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`.
