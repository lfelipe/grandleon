# Isolated phase-2 gameplay and presentation validation

Validated on 2026-11-08 against Grandleon revision
`cf13def5b6c460b9a8ca99a5ada5627510812f74` with Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The wrapper completed all of these from a generated output directory:

1. verified the Pyrite v0.10.0 release archive and AppImage;
2. cold-built and exported the pinned libdragon/Tiny3D toolchain;
3. built `grandleon_content_compile` and generated a 22,292-byte GPK from the
   real Steel and Cedar source;
4. generated the 12 required project drawings using the normal console-art
   pipeline and selected the five Cedar Deepwood identities;
5. cross-built the portable Grandleon libraries without building the existing
   Nintendo 64 target;
6. converted five board idle/walk sets and 15 close poses from the tracked
   Godot-derived art, generated an I8 contact shadow, and staged the tracked
   medieval combat plate as a 26,900-byte CI8 sprite;
7. ran the host camera centering/clamp test and built a 1,146,880-byte Pyrite
   ROM through the official headless CLI.

The ROM then booted with `Nintendo64/ExpansionPak=false` under accurate
real-RSP ares using paraLLEl-RDP. It decoded the real 14x9 Cedar Deepwood
encounter (126 terrain cells, seven units, 35 deterministic map/apron trees,
the package's automatic depth-five policy rendered as 330 non-interactive
surrounding cells, and 156 elevation vertices). Normal cursor input swept to
both playable corners while the viewport clamped its focus to the authored
rectangle, then selected and moved Rowan along his real planned route,
waited Rowan and Tomas, executed eight real tactics commands for the opposing
side, presented two real attack exchanges with fixed attacker/defender sides
through the purpose-built backdrop, baseline/contact-shadow cutouts and compact
HP/result widgets, restored the exact tactical camera/hash, returned to side one in round one,
and reported this settled 120-frame sample:

```text
avg_frame_us=16716 min_frame_us=16467 max_frame_us=16897 fps_x100=6000
heap_used=1132832 heap_total=3067496 heap_free=1934664 rdram=4194304
commands=11 enemy_commands=8 round=1 combat=2 rowan=2,1
camera_span_x_x100=487 camera_span_y_x100=243
RESULT PASS PYRITE_GAMEPLAY
```

The validated ROM SHA-256 was
`02023b71dbbb03ff1ee438aa49600b160540a28e48329c3ed9b302aa371151a4`.
The 15 compressed close sprites total 27,801 bytes in ROMFS, and the I8 shadow
sprite is 397 bytes. The board/close/return capture was made from this exact
ROM at native 320x240 after an exact-4-MB real-RSP run; no expansion-memory
run is accepted as evidence.
The ROM and emulator evidence remain build artifacts and are intentionally not
part of this source package. A later source or campaign change is expected to
change the hash; it must not change the runtime assertions or 4 MB boot gate.
