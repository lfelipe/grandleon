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
of resident main RAM. A renderer-only probe, with 1,024-entry vertex and
triangle caches and 24 KiB packet buffers per frame, used 445,252 bytes
(`text=192,532`, `data=412`, `bss=252,308`). An 8x6 Bell Orchard board batched
into one terrain mesh rendered in about 9.6 ms on PCSX-Redux; emitting one mesh
per tile took about 93.8 ms and is prohibited by the production design.

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

