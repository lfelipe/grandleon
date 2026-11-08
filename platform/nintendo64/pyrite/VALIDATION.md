# Steel and Cedar campaign milestone validation

Validated on 2026-11-08 from Grandleon revision
`fcd1d960` plus this Pyrite parity-audit milestone
and Steel and Cedar source hash
`1c8a95cb9b6d96322530db88945f30a29f4dfb9a1f02c9061ea16dc042a64893`.

The normal build wrapper verified the pinned Pyrite/libdragon/Tiny3D inputs,
compiled the real project into a 22,292-byte GPK, generated the 12 required
console drawings, cross-built the portable campaign, campaign-runtime,
simulation, tactics, package, client and storage libraries, ran the host camera
and two-player ownership tests, generated four original audio sources, and
produced this 1,671,168-byte ROM:

```text
grandleon_pyrite_steel_cedar.z64
SHA-256 0a2d20691c5a70b2f3678c5281e10349fa26ee1695383b116fff1d5c9112d964
```

The exact ROM booted in real-RSP ares with
`Nintendo64/ExpansionPak=false`. With empty 32 KiB SRAM it read the packaged
`choice` player policy, followed Title -> One Player -> New Game, displayed the
authored story nodes and completed the campaign's five real encounters in
order: Bell Orchard, Cedar Deepwood, Cedar Watch, Gate Village and Twin Gate
Keep. Each map came from the compiled package, used the authoritative
simulation/tactics command boundary, exercised both-axis clamped camera motion
on the larger boards and committed its real battle report to `CampaignSession`.
Packaged stage-open dialogue also ran inside each battle. A separate exact-4-MB
two-player run exercised the same five encounters with two independent cursor
and ownership states; the host test pins that one controller cannot select the
other controller's authored units.

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
avg_frame_us=16639 min_frame_us=8227 max_frame_us=16927 fps_x100=6000
heap_used=1178144 heap_total=2704200 heap_free=1526056 rdram=4194304
battles=5 commands=290 enemy_commands=170 combat=5 slot_bytes=1340
RESULT PASS PYRITE_GAMEPLAY
```

A second boot used the same ares SRAM file. The launch screen detected the
mode-specific save, selected Continue, resumed the terminal campaign state
through `CampaignSession`, preserved all five completed battles and passed
another 120-frame exact-4-MB sample at 59.99 fps with 1,571,160 heap bytes
free. The
runtime logs `GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`; the
build separately checks the ROM header so the proof cannot silently become an
artifact from the old renderer.

The ROM, SRAM image and emulator captures remain build artifacts rather than
tracked source. Current evidence is under
`/home/lstrano/.cache/grandleon-pyrite-parity-audit2/ares-1p/`; `fresh.log` proves the
complete fresh campaign, `resume.log` proves terminal-state cartridge resume,
and `fresh-trail/` contains named frames for every stage, story, forecast,
combat phase, aftermath and ending.
