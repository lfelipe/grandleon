# Steel and Cedar campaign milestone validation

Validated on 2026-11-08 from Grandleon revision
`5157978004930701ade10d605f3282626f6d9b3d` plus this Pyrite milestone
and Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The normal build wrapper verified the pinned Pyrite/libdragon/Tiny3D inputs,
compiled the real project into a 22,292-byte GPK, generated the 12 required
console drawings, cross-built the portable campaign, campaign-runtime,
simulation, tactics, package, client and storage libraries, ran the host camera
test, and produced this 1,622,016-byte ROM:

```text
grandleon_pyrite_steel_cedar.z64
SHA-256 dada3285cafb0c68b52dcbec4e2de0c55b880267dd4969935f6184a65cd53174
```

The exact ROM booted in real-RSP ares with
`Nintendo64/ExpansionPak=false`. With empty 32 KiB SRAM it read the packaged
`choice` player policy, followed Title -> One Player -> New Game, displayed the
authored story nodes and completed the campaign's five real encounters in
order: Bell Orchard, Cedar Deepwood, Cedar Watch, Gate Village and Twin Gate
Keep. Each map came from the compiled package, used the authoritative
simulation/tactics command boundary, exercised both-axis clamped camera motion
on the larger boards and committed its real battle report to `CampaignSession`.

The action review opened the same menu as physical input, exposed Attack and
every equipped ability by package name, produced authoritative ability and
attack forecasts/results, and exercised one optional board-to-close-to-board
combat handoff per battle. All five fixed formations correctly reported that
packaged deployment was not required. Their aftermath screens reported the
real victory, objective count, fallen/recruited count and campaign outcome
before advancing to the next story node. The run reached the terminal after
290 accepted player-side commands, 170 enemy commands and five reviewed combat
handoffs, with 1,340 used save bytes.

The terminal held a settled frame for this exact-4-MB sample:

```text
avg_frame_us=16641 min_frame_us=8041 max_frame_us=16805 fps_x100=5999
heap_used=1160872 heap_total=2715976 heap_free=1555104 rdram=4194304
battles=5 commands=290 enemy_commands=170 combat=5 slot_bytes=1340
RESULT PASS PYRITE_GAMEPLAY
```

A second boot used the same ares SRAM file. The launch screen detected the
save, selected Continue, resumed the terminal campaign state through
`CampaignSession`, preserved all five completed battles and passed another
120-frame exact-4-MB sample at 60 fps with 1,596,136 heap bytes free. The
runtime logs `GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`; the
build separately checks the ROM header so the proof cannot silently become an
artifact from the old renderer.

The ROM, SRAM image and emulator captures remain build artifacts rather than
tracked source. Current evidence is under
`/home/lstrano/.cache/grandleon-pyrite-full/ares1/`; `fresh.log` proves the
complete fresh campaign, `resume.log` proves terminal-state cartridge resume,
and `fresh-trail/` contains named frames for every stage, story, forecast,
combat phase, aftermath and ending.
