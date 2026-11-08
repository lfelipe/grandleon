# Steel and Cedar campaign milestone validation

Validated on 2026-11-08 from Grandleon revision
`1875f848ddd3984883961e3cd76e05783516c2b2` plus this Pyrite milestone
and Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The normal build wrapper verified the pinned Pyrite/libdragon/Tiny3D inputs,
compiled the real project into a 22,292-byte GPK, generated the 12 required
console drawings, cross-built the portable campaign, campaign-runtime,
simulation, tactics, package, client and storage libraries, ran the host camera
test, and produced this 1,605,632-byte ROM:

```text
grandleon_pyrite_steel_cedar.z64
SHA-256 b95322fb3eb896dfc619633d8df0c0697e735f22ddb8384c844c0aa3d00270b9
```

The exact ROM booted in real-RSP ares with
`Nintendo64/ExpansionPak=false`. With an empty 32 KiB SRAM image it read the
packaged `choice` player policy, followed Title -> One Player -> New Game,
displayed all three lines of the real prologue story, and prepared the real 8x6
Bell Orchard campaign board. The deterministic controller opened the same
action panel as physical input (Attack/Wait/Cancel for Rowan and the four-row
ability variant for Tomas), exposed every equipped ability by package name,
obtained an accepted Field Mend preview, obtained a
98%/1-damage/counter forecast from the authoritative simulation, and exercised
the same result and optional close-presentation path used by an interactive
attack. It then finished Bell through ordinary simulation/tactics commands.
The session committed 28 accepted commands and 61 ordered battle events,
showed the real victory/objective aftermath, displayed both Orchard Camp story
lines, saved 848 used bytes, and prepared the real 14x9 Cedar Deepwood board.
Both campaign encounters were proven to omit deployment because their packaged
formations author no deployment rule. The Cedar viewport then swept both axes within
the playable bounds and settled for this 120-frame sample:

```text
avg_frame_us=16715 min_frame_us=16445 max_frame_us=16874 fps_x100=6000
heap_used=1146168 heap_total=2717096 heap_free=1570928 rdram=4194304
bell_commands=28 enemy_commands=14 camera_span_x_x100=487
camera_span_y_x100=243
RESULT PASS PYRITE_GAMEPLAY
```

A second boot used the same ares SRAM file. The launch screen detected the
save, selected Continue, resumed directly at Cedar Deepwood through
`CampaignSession`, and passed the same exact-4-MB viewport/frame gate at 60
fps with 1,593,304 heap bytes free. The runtime logs
`GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`; the build separately
checks the ROM header so the proof cannot silently become an artifact from the
old renderer.

The ROM, save image and emulator captures remain build artifacts rather than
tracked source. Current evidence is under
`/home/lstrano/.cache/grandleon-pyrite-parity1/ares/`; `new.log`
proves fresh campaign progression, `resume.log` proves cartridge continuation,
and their trail directories contain the Bell, close-combat and Cedar viewport
landmarks.
