# Nintendo 64 Pyrite target

This directory contains Grandleon's Nintendo 64 renderer.
It builds an authored Grandleon source project through Pyrite while continuing
to use the same simulation, compiled campaign package, saves and authoring
pipeline as the other players. The ROM starts at a small title screen, follows
the selected campaign's one/two-player launch policy, offers New Game or
Continue from cartridge SRAM, and advances through the real campaign graph and
battle commits.

The normal CMake, creator payload and release-export paths all call this
renderer. Grandleon has no second Nintendo 64 presenter or compatibility
fallback; an unavailable pinned dependency makes the export fail with a clear
diagnostic.

The renderer deliberately presents at 30 fps on NTSC systems. Authored
movement and cinematic timings remain expressed in 60 Hz ticks and advance at
the same real-world speed. The recovered rendering budget enables the filtered
terrain atlas, denser scenery, and stronger silhouettes used by the production
ROM.

Raised terrain, trees, elevation faces, and castle masonry use filtered
surface detail while retaining their authored colours. Close combat loads the
compatible Godot panorama for each stage, keeping just one plate in memory.
Scenes without compatible artwork retain a terrain-coloured backdrop.

The target deliberately contains no ROM, compiled GPK, generated cutout, or
vendored Pyrite source. `scripts/build.sh` derives the game inputs from the
selected source project:

- the selected `project.json` is compiled to a temporary GPK;
- the normal console-art generator creates temporary character cutouts from
  the authoritative Godot sheets and masks;
- a deterministic CI4 terrain atlas and terrain-derived CI8 combat plate are
  generated from the selected campaign's own terrain vocabulary;
- a generated header binds the selected campaign, encounter order, save
  identity, terrain slots and every staged character drawing;
- Grandleon's portable runtime libraries are cross-compiled independently of
  the current production Nintendo 64 executable.

## Build

Normal ROMs always wait for player input. For an automated emulator review,
set `GRANDLEON_N64_REVIEW_DRIVER=1` when building. This creates a test ROM that
plays through the campaign; do not distribute it as a playable release. The
boot log records `review_driver=0` for a normal ROM and `1` for a review ROM.
The export gate opts into the driver explicitly. Physical takeover also
disables it for subsequent battles in that run.

From a Grandleon checkout:

```sh
platform/nintendo64/pyrite/scripts/build.sh "$PWD" /tmp/grandleon-pyrite-build
```

That command builds Steel and Cedar for developer convenience. Pass a source
project as the third argument to build something else:

```sh
platform/nintendo64/pyrite/scripts/build.sh \
  "$PWD" /tmp/grandleon-pyrite-demo \
  "$PWD/games/demo/source/project.json"
```

An unsupported project is refused with a human diagnostic; the exporter never
substitutes Steel content. The current base-memory contract accepts one
unambiguous campaign victory route, up to 16 distinct character drawings and
up to nine recognized terrain families.

The wrapper requires Docker or Podman, CMake, Python 3, curl, unzip, and the
AppImage runtime's normal host libraries. It creates a pinned toolchain image,
exports that toolchain into the build cache, verifies Pyrite and toolchain
pins, and leaves the ROM at:

```text
/tmp/grandleon-pyrite-build/project/grandleon_pyrite_game.z64
```

Set `GRANDLEON_PYRITE_N64_INST` to a previously exported toolchain directory to
skip the container build. The directory is accepted only when its two commit
marker files match `versions.env`.

## Boundary

`GpkRuntimeAdapter` owns the ROM-loaded package bytes and performs target-budget
validation. `CampaignSession` reads the packaged campaign graph, prepares
each encounter with its roster, commits completed battles and saves each
result through a bounded 32 KiB SRAM adapter.
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

The tactical board has filtered CI4 terrain materials, shared edges between
coplanar neighbours, elevation sidewalls where heights differ, deterministic
tree clusters, generated directional walk poses and I8 falloff contact
shadows. Campaign launch, player-count selection, authored story
dialogue, interactive attacks and abilities, packaged battles,
aftermath/objective results, terminal progression and cartridge saving run in
the generated ROM. A packaged deployment
screen is entered only when the encounter authors a deployment rule; Steel and
Cedar's current fixed formations correctly skip it. Objective results,
fallen/recruited counts and the campaign outcome are shown after every
committed battle. Packaged stage-open, character-fall and character-talk
moments use the same dialogue loader during battle. A small original generated
audio set provides ambience plus UI, impact and victory cues without tracking
binary audio sources. Every exported unit receives board and close-stage art.
Board terrain combines its generated texture with elevation faces and bounded
rendered forms: rocks, shrubs, reeds, crops, bamboo, rubble and kerbs are
derived from the authored terrain family. Farmland and bamboo no longer reuse
forest trees. Terrain and landmark detail continues to improve within the same
renderer and hardware budgets.
The bounded review driver uses the real tactics policy for both sides to finish
each battle and presents one complete optional close handoff per encounter; it
is test scaffolding, not a second simulation. Physical player attacks continue
to use the close setting for every command. The generated close plate
represents the campaign's dominant terrain family; authored multi-layer close
backdrops and project-specific landmarks remain a production-quality gap.

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
