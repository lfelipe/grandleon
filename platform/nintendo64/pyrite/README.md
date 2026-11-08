# Nintendo 64 Pyrite target (migration phase 2)

This directory is a self-contained candidate for
`platform/nintendo64/pyrite/`. It builds one deterministic, playable Steel and
Cedar Deepwood encounter slice through Pyrite without replacing
Grandleon's simulation, campaign package, or authoring pipeline.

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
/tmp/grandleon-pyrite-build/project/grandleon_pyrite_cedar_deepwood.z64
```

Set `GRANDLEON_PYRITE_N64_INST` to a previously exported toolchain directory to
skip the container build. The directory is accepted only when its two commit
marker files match `versions.env`.

## Boundary

`GpkRuntimeAdapter` is the only cartridge/package boundary. It owns the
ROM-loaded bytes, performs target-budget validation, and exposes a decoded
`EncounterLoadResult`. `GameplaySlice` owns a real `simulation::Encounter` and
routes cursor selection, movement and wait commands through its authoritative
`apply` boundary. The unattended side uses the real tactics planner. The
deterministic review sequence and a physical controller therefore exercise the
same input/update path instead of moving display-only actors.

The current controls are D-pad to move the cursor, A to select/move, Z to wait
and B to cancel. START toggles the optional combat presentation, L toggles its
reduced-motion entry/exit, B skips an active presentation, and holding R runs
it at 3x. Movement follows the authoritative multi-cell route at ten frames per
cell, with the generated walk pose and terrain-height interpolation rather than
sliding directly to the already-landed simulation position.

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

The tactical board now has filtered CI4 terrain materials, elevation
sidewalls, deterministic tree clusters, generated
directional walk poses and I8 falloff contact shadows. This is a material
quality and runtime-architecture proof, not campaign parity: player-issued
attacks and abilities, objectives, deployment, menus, saves, story traversal,
co-op and the remaining four Steel and Cedar battles are not exposed. The
deterministic opposing tactics are what trigger the three reviewed combat
exchanges. The close backdrop pack currently covers only this medieval
forest/grass/paved proof; audio and the remaining terrain/style stages still
need production coverage.

The authored tactical rectangle remains finite. The renderer consumes the
portable packaged `surroundings` contract directly: automatic mode extends
the nearest authored edge, while manual/fill/blend use the package's fully
materialized perimeter and solid uses its authored colour. Cedar Deepwood's
five-cell automatic apron is decorative only, carries no tactical grid or
highlight, adds deterministic edge foliage, and fades into one coarse ground
plane. Cursor, routing and selection remain limited to the authored 14x9
cells. The viewport follows those cells horizontally and vertically, centers
small maps, and clamps its focus before the decorative apron can become a pan
target. A host policy test pins those centering and clamp rules.

The runtime prints `GRANDLEON_RENDERER identity=pyrite-tiny3d fallback=none`.
The build also fails closed unless the ROM header identifies the Pyrite target;
this gives release/export cutover a machine-checkable artifact boundary rather
than relying on a filename. Proof/FPS text is off by default and can be toggled
with C-Up during development.

The Pyrite editor is not part of the runtime workflow. The checked-in scene is
a small declarative boot shell; the official headless CLI generates and builds
the engine project. See `THIRD_PARTY.md` before distribution.
