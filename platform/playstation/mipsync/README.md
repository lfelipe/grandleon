# Mipsync PlayStation renderer

This directory is the in-progress replacement for Grandleon's PlayStation
renderer. It now boots real Steel and Cedar encounters, runs the shared
simulation and supports cursor movement, unit movement, Attack, every equipped
ability, Wait and the package-authored opposing turn. A compact forecast and
result UI plus the optional close-combat camera use the same disc and rules.
It is not the shipping path yet: the explicit baseline remains available for
comparison until saves, campaign flow, audio and hardware validation reach
parity. It is never used as a fallback.

Mipsync is pinned to commit `866f405a0e34b5925a8d832272fe52c657688430`.
The upstream source is not copied into Grandleon. Generic engine fixes live as
replayable `git format-patch` files under
`tools/upstream_patches/engines/mipsync`; Grandleon-specific package, simulation
and rendering adapters stay here.

## Package and simulation boundary

`grandleon_ps1_mipsync_scene_export` opens a real `.gpk`, loads the selected
encounter through `package_runtime`, creates the real simulation and writes a
renderer-neutral JSON build input. The target generator reconstructs the same
`EncounterDefinition`, and `game_bridge.cpp` is the deliberately narrow seam
between the shared rules and Mipsync scene entities. Mipsync types do not enter
the engine or package format.

```sh
cmake -S . -B build-mipsync-host -G Ninja \
  -DGRANDLEON_BUILD_TESTS=OFF -DGRANDLEON_DESKTOP_SDL=OFF
cmake --build build-mipsync-host \
  --target grandleon_ps1_mipsync_scene_export
build-mipsync-host/grandleon_ps1_mipsync_scene_export \
  path/to/game.gpk steel_and_cedar/bell_orchard 2 /tmp/bell-orchard.json
```

A successful export prints `GRANDLEON_PS1_RENDERER=MIPSYNC`. Content identities
are decimal strings in JSON because ordinary JSON numbers cannot preserve every
64-bit value.

## Build and prove the playable slice

The disc combines Grandleon's proven GCC 12.3/Nugget PlayStation toolchain with
the PSn00bSDK renderer libraries. The wrapper normalizes PSn00bSDK's symbol
tables for the Debian linker without changing code or data, applies the pinned
Mipsync patches, regenerates target data, builds the exact CUE/BIN and verifies
the embedded renderer identity and RAM budget.

```sh
PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/scripts/build-renderer-probe.sh \
  /tmp/bell-orchard.json /tmp/empty-mipsync-output

PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/tests/run-combat-probe.sh \
  /tmp/empty-mipsync-output/build /tmp/empty-probe-output

PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/tests/run-scroll-probe.sh \
  /tmp/deepwood-output/build /tmp/deepwood-probe-output
```

The combat probe boots the CUE through the pinned BIOS-style PCSX-Redux
interpreter, injects controller edges only at the public update boundary and
requires real movement, player Attack, an equipped healing ability, Wait and
seven package-authored opposing actions. It captures the live forecast, close
combat and completed board views. The scroll probe drives both axes on the real
14x9 Cedar Deepwood map and requires viewport clamp, one active terrain page
and zero missed VBlanks. See `VALIDATION.md` for exact hashes and measurements.

## Budgets and cutover gates

The generic Mipsync runtime cannot coexist with Grandleon's campaign runtime
unchanged. This target compiles only the renderer pieces it uses and sizes the
static caches for the scene. The playable-slice gate reserves 64 KiB for the
future Grandleon platform audio seam, 32 KiB for stack and 512 KiB for gameplay
heap inside the PlayStation's 2 MiB RAM. Omitting Mipsync's generic audio from
this target does not remove PlayStation music or effects from the production
plan.

Larger maps are split into overlapping viewport-sized terrain pages. Only the
closest page is visible, while the camera follows the cursor and clamps to the
authored playable rectangle. Trees and units outside the viewport are culled
only from presentation; the complete encounter remains in the shared
simulation. Each terrain family also contributes a small deterministic
low-poly form, such as a rock, shrub, reed, crop, bamboo stalk or rubble. These
forms are folded into the existing terrain pages, rather than becoming extra
draw calls, and every form belongs to exactly one overlapping page so scrolling
keeps the zero-missed-VBlank budget. The generated surroundings policy remains
portable across renderers (`natural_blend`, `extend_edges` or `fixed_terrain`).

Cutover requires all of the following on the exact CUE/BIN:

- BIOS-style emulator and real PlayStation hardware boot;
- a clear visual improvement over the explicit baseline;
- steady 60 Hz rendering with rule mutations scheduled as bounded redraw slices;
- main RAM, VRAM and packet headroom for the full campaign;
- real packages, complete encounters, save data and campaign flow;
- embedded `GRANDLEON_PS1_RENDERER=MIPSYNC` plus the fail-closed cutover audit.

Until those gates pass, rollback means selecting the separately named baseline
build. At cutover, release/export will point only to Mipsync and fail if it
cannot build; the baseline renderer code and assets will then be removed in the
same transition series.
