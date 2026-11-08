# Isolated phase-2 gameplay and presentation validation

Validated on 2026-11-07 against Grandleon revision
`50179b2f38a957e970a794763ce10b7280c6f8a8` with Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The wrapper completed all of these from a generated output directory:

1. verified the Pyrite v0.10.0 release archive and AppImage;
2. cold-built and exported the pinned libdragon/Tiny3D toolchain;
3. built `grandleon_content_compile` and generated a 22,292-byte GPK from the
   real Steel and Cedar source;
4. generated the 12 required project drawings using the normal console-art
   pipeline and selected the five Bell Orchard identities;
5. cross-built the portable Grandleon libraries without building the existing
   Nintendo 64 target;
6. converted five board idle/walk sets and 15 close poses from the tracked
   Godot-derived art, generated an I8 contact shadow, and staged the tracked
   medieval combat plate as a 26,900-byte CI8 sprite;
7. built a 1,048,576-byte Pyrite ROM through the official headless CLI.

The ROM then booted with `Nintendo64/ExpansionPak=false` under accurate
real-RSP ares using paraLLEl-RDP. It decoded the real 8x6 Bell Orchard
encounter (48 terrain cells, five units, 40 deterministic map/apron trees, a
120-cell decorative surroundings window, 112 elevation vertices and a
38-vertex bell landmark), selected and moved Rowan along its
real planned route,
waited Rowan and Tomas, executed five real tactics commands for the opposing
side, presented three real attack exchanges with fixed attacker/defender sides
and exact tactical-camera/hash restoration, returned to side one in round one,
and reported this settled 120-frame sample:

```text
avg_frame_us=16715 min_frame_us=16455 max_frame_us=16991 fps_x100=6000
heap_used=1030320 heap_total=3077080 heap_free=2046760 rdram=4194304
commands=8 enemy_commands=5 round=1 combat=3 rowan=6,2
RESULT PASS PYRITE_GAMEPLAY
```

The validated ROM SHA-256 was
`6b0833b7503a311a4ce20220e125bf1319ef33da597b557f82000c6588fb45bb`.
The 15 compressed close sprites total 27,801 bytes in ROMFS, and the I8 shadow
sprite is 397 bytes. The board/close/return capture was made from this exact
ROM at native 320x240 after an exact-4-MB real-RSP run; no expansion-memory
run is accepted as evidence.
The ROM and emulator evidence remain build artifacts and are intentionally not
part of this source package. A later source or campaign change is expected to
change the hash; it must not change the runtime assertions or 4 MB boot gate.
