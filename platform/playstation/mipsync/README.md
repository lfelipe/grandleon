# Mipsync PlayStation renderer

This directory is the in-progress replacement for Grandleon's PlayStation
renderer. It is not a shipping path yet. The existing PlayStation player
remains an explicit comparison build until this path reaches campaign, input,
UI, save and performance parity. It is never used as a fallback.

Mipsync is pinned to commit `866f405a0e34b5925a8d832272fe52c657688430`.
The upstream source is not copied into Grandleon. Generic engine fixes live as
replayable `git format-patch` files under
`tools/upstream_patches/engines/mipsync`; Grandleon-specific conversion and
runtime code stays here.

## Package boundary

`grandleon_ps1_mipsync_scene_export` opens a real `.gpk`, loads the selected
encounter through `package_runtime`, creates the real simulation and writes a
renderer-neutral JSON snapshot. The target-side converter consumes that file;
Mipsync types and assumptions do not enter the engine or package format.

```sh
cmake -S . -B build-mipsync-host -G Ninja \
  -DGRANDLEON_BUILD_TESTS=OFF -DGRANDLEON_DESKTOP_SDL=OFF
cmake --build build-mipsync-host \
  --target grandleon_ps1_mipsync_scene_export
build-mipsync-host/grandleon_ps1_mipsync_scene_export \
  path/to/game.gpk steel_and_cedar/bell_orchard 2 /tmp/bell-orchard.json
```

A successful export prints `GRANDLEON_PS1_RENDERER=MIPSYNC`. Content identities
are decimal strings in the JSON because ordinary JSON numbers cannot preserve
every 64-bit value.

## Renderer budget and gates

The generic Mipsync runtime is not small enough to coexist with Grandleon's
campaign runtime unchanged. The first measured full build used about 1.53 MiB
of resident main RAM. The exact Bell Orchard renderer-only disc now uses
384,428 bytes (`text=256,916`, `data=412`, `bss=127,100`), including 114,688
bytes of real terrain and Godot-derived character textures. Its visible scene
uses 320 of 1,024 cached vertices and 176 of 1,024 cached triangles. The atlas
reserves another 81,920 VRAM bytes in its bounded texture region.

The BIOS-style PCSX-Redux interpreter measured 241 HBlanks (about 15.32 ms)
average and 242 maximum over 600 post-warmup frames, with zero missed VBlanks.
An earlier 8x6 board emitted as one mesh rendered in about 9.6 ms; emitting one
mesh per tile took about 93.8 ms. Per-tile draw objects are therefore prohibited
by the production design. Authored terrain remains one batched mesh, the
decorative apron uses coarse noninteractive chunks, and neighbouring forest
cells select deterministic volumetric tree clusters.

The replacement cannot cut over until the exact CUE/BIN passes all of these:

- boots through a BIOS-style emulator and real PlayStation hardware;
- is visually better than the explicit baseline at the same encounter;
- stays within one 16.67 ms frame at steady state with no missed VBlanks;
- leaves enough main RAM, VRAM and packet space for the campaign runtime;
- supports the real package, full encounter, controls, UI, save and campaign;
- embeds `GRANDLEON_PS1_RENDERER=MIPSYNC` in the executable and passes the
  fail-closed source/artifact cutover audit.

Until those gates pass, rollback means selecting the separately named baseline
build. At cutover, release/export will point only to Mipsync and fail if it
cannot build; the baseline source, art, generators and tests will be deleted in
that same transition series.

The renderer-only probe does not compile Mipsync's generic audio system. That
does **not** remove PlayStation audio from Grandleon: the platform audio seam and
its RAM/SPU budget remain independent, ready for the shared music and effects
work.

Build the exact renderer disc after exporting a manifest:

```sh
PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/scripts/build-renderer-probe.sh \
  /tmp/bell-orchard.json /tmp/empty-mipsync-output
```

The wrapper checks out the pinned upstream revision, applies the reviewed patch
series, regenerates all target data, builds the CUE/BIN, verifies the embedded
renderer identity and fails if resident size exceeds the 512 KiB renderer-probe
gate. See `VALIDATION.md` for the current exact evidence and remaining gates.

