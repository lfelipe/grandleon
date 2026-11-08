# Nintendo 64 Pyrite target (campaign-parity work)

This directory is Grandleon's self-contained candidate Nintendo 64 renderer.
It builds one Steel and Cedar ROM through Pyrite while continuing to use
Grandleon's simulation, compiled campaign package, saves and authoring
pipeline. The ROM starts at a small title screen, follows the campaign's own
one/two-player launch policy, offers New Game or Continue from cartridge SRAM,
and follows the complete five-battle Steel and Cedar route—from Bell Orchard
through Twin Gate Keep and the ending—through the real campaign graph and
battle commits.

**Migration status:** this is the intended replacement renderer, but it remains
an active, non-shipping integration path until export, campaign and gameplay
parity are complete. It is
not connected to CMake, campaign export, release ROMs, or a player/creator
renderer choice. Grandleon still has one Nintendo 64 renderer. Once this path
has gameplay, save, input, UI, and campaign parity and proves a material visual
improvement on real-style hardware, it should replace that renderer rather
than become a second selectable option.

The target deliberately contains no ROM, compiled GPK, generated cutout, or
vendored Pyrite source. `scripts/build.sh` derives all game inputs from the
repository:

- `games/steel_and_cedar/source/project.json` is compiled to a temporary GPK;
- the normal console-art generator creates temporary character cutouts from
  the authoritative Godot sheets and masks;
- the checked-in Nintendo 64 material atlas is staged into the temporary
  Pyrite project;
- the checked-in medieval forest/grass/paved combat plate is converted to a
  streamed CI8 sprite in the temporary project;
- Grandleon's portable runtime libraries are cross-compiled independently of
  the current production Nintendo 64 executable.

## Build

From a Grandleon checkout:

```sh
platform/nintendo64/pyrite/scripts/build.sh "$PWD" /tmp/grandleon-pyrite-build
```

The wrapper requires Docker or Podman, CMake, Python 3, curl, unzip, and the
AppImage runtime's normal host libraries. It creates a pinned toolchain image,
exports that toolchain into the build cache, verifies Pyrite and toolchain
pins, and leaves the ROM at:

```text
/tmp/grandleon-pyrite-build/project/grandleon_pyrite_steel_cedar.z64
```

Set `GRANDLEON_PYRITE_N64_INST` to a previously exported toolchain directory to
skip the container build. The directory is accepted only when its two commit
marker files match `versions.env`.

## Boundary

`GpkRuntimeAdapter` owns the ROM-loaded package bytes and performs target-budget
validation. `CampaignSession` reads the packaged Steel and Cedar graph,
prepares each encounter with the campaign roster, commits all five completed
battles and saves each result through a bounded 32 KiB SRAM adapter.
`GameplaySlice` owns the resulting real `simulation::Encounter` and routes
cursor selection, movement and wait commands through its authoritative
`apply` boundary. The deterministic review sequence and a physical controller
therefore share the same input and simulation path instead of moving
display-only actors.

On the launch screens, use the D-pad to choose and A or START to confirm; A
also advances each authored story line. One-player and two-player progress use
separate SRAM slots. In battle, each local player uses their own controller,
cursor and authored unit ownership. Use the D-pad to move the cursor and A to
select and move. A on the selected character, or Z while one is selected,
opens a compact action panel for Attack, every equipped ability, Wait, or Cancel. Attack
and ability targeting show the authoritative hit, damage, counter, restoration,
area and refusal forecast before A commits the real simulation command; the
result panel then reports the actual hit, miss, healing or refusal. B backs
out. START opens persistent battle settings for the close view, reduced
motion, audio, FPS display and Help. C-Up toggles FPS directly, L toggles
reduced motion, B skips an active presentation, and holding R runs it at 3x.
Movement follows the authoritative
multi-cell route at ten frames per cell, with the generated walk pose and
terrain-height interpolation rather than sliding directly to the already-landed
simulation position.

This phase also exercises a complete live combat handoff. The camera stores
the current tactical pose, keeps its yaw/pitch fixed while it dollies toward
the combat pair, then crossfades during the final ten fly-in frames to a
purpose-built 4:3 terrain stage. Original 84px Godot-derived idle/attack/hurt
cutouts share a fixed floor baseline, readable center gap and I8 contact
shadows; compact team-framed HP bars and a short impact/result widget stay
clear of both silhouettes. The art is streamed by the
RDP in screen space, so the first attacker stays physically left and the
defender right across the counterattack without exceeding one CI4 TMEM upload.
The return restores the exact saved tactical pose and verifies that the
authoritative encounter hash did not change during presentation. Full timing
is 1.10 s in / 0.95 s out; reduced motion uses a direct 8/6-frame settle;
fast-forward is 3x and skip restores immediately.

The tactical board has filtered CI4 terrain materials, elevation sidewalls,
deterministic tree clusters, generated directional walk poses and I8 falloff
contact shadows. Campaign launch, player-count selection, authored story
dialogue, interactive attacks and abilities, all five Steel and Cedar battles,
aftermath/objective results, the ending and cartridge saving now run in this
ROM, but this is still not full player-facing parity. A packaged deployment
screen is entered only when the encounter authors a deployment rule; Steel and
Cedar's current fixed formations correctly skip it. Objective results,
fallen/recruited counts and the campaign outcome are shown after every
committed battle. Packaged stage-open, character-fall and character-talk
moments use the same dialogue loader during battle. A small original generated
audio set provides ambience plus UI, impact and victory cues without tracking
binary audio sources. Complete art and close-stage coverage for every exported
unit, terrain and landmark remain.
The bounded review driver uses the real tactics policy for both sides to finish
each battle and presents one complete optional close handoff per encounter; it
is test scaffolding, not a second simulation. Physical player attacks continue
to use the close setting for every command. The close backdrop pack currently
covers only this medieval forest/grass/paved proof; the remaining terrain/style
stages still need production coverage.

The authored tactical rectangle remains finite. The renderer consumes the
portable packaged `surroundings` contract directly: automatic mode extends
the nearest authored edge, while manual/fill/blend use the package's fully
materialized perimeter and solid uses its authored colour. Cedar Deepwood's
five-cell automatic apron is representative: each map's apron is decorative
only, carries no tactical grid or highlight, adds deterministic edge foliage,
and fades into one coarse ground plane. Cursor, routing and selection remain
limited to authored cells. The viewport follows playable cells horizontally
and vertically, centers small maps, and clamps its focus before the decorative
apron can become a pan target. A host policy test pins those rules.

The runtime prints `GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`.
The build also fails closed unless the ROM header identifies the Pyrite target;
this gives release/export cutover a machine-checkable artifact boundary rather
than relying on a filename. Proof/FPS text is off by default and can be toggled
with C-Up during development.

The Pyrite editor is not part of the runtime workflow. The checked-in scene is
a small declarative boot shell; the official headless CLI generates and builds
the engine project. See `PARITY.md` for the fail-closed cutover checklist and
`THIRD_PARTY.md` before distribution.
