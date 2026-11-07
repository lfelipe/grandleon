# Nintendo 64

Grandleon's Nintendo 64 target runs the same compiled projects and tactical
rules as the Godot player. The console layer supplies controller input, SRAM
saves, audio timing and an RDP renderer; it does not reimplement campaign or
combat rules.

## Play Sevenfold Gate

Configure the repository once, then build the review artifacts:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_n64_sevenfold_roms
```

Open `build-n64-sevenfold-roms/sevenfold-n64.z64` in ares, on a flash cartridge,
or on a compatible FPGA system. The main menu lets you choose one or two local
players. Review builds include a Stage picker and live FPS display.

`sevenfold-n64-renderer-check.z64` in the same folder is an automated visual
diagnostic. It is not a second renderer or a playable campaign variant.

## Controls

- D-pad: move the active cursor or menu selection.
- A: confirm, select a unit, or choose a destination.
- B: cancel or go back.
- Z: open the selected unit's action menu.
- Start: open the battle menu.

Each local player has an independent cursor, selection and menu state. Campaign
ownership decides which units each controller can command.

## Renderer

The console has one renderer. It draws curated, team-coloured character
cutouts over deterministic textured terrain and uses terrain-derived backdrops
for close battle checks. Tactical range, selected cells, health and team
identity remain visible at native resolution.

The renderer check is built with pinned libdragon and Tiny3D revisions. Run it
under the pinned ares build with:

```sh
cmake --build build --target grandleon_n64_sevenfold_renderer_check
```

The gate verifies asset provenance, TMEM limits, an eight-role tactical board,
eased movement, attack and counterattack framing, health changes, and return to
the board. Its current result is `11/11`.

The checked-in cutouts are regenerated from the curated Godot sheets and team
masks:

```sh
platform/nintendo64/scripts/generate-sevenfold-cutout-assets.py
platform/nintendo64/scripts/generate-sevenfold-terrain-assets.py
platform/nintendo64/scripts/generate-sevenfold-backdrop-assets.py
```

## Build another project

The normal build accepts a portable source project:

```sh
platform/nintendo64/scripts/build-n64.sh \
  --project games/tarnholt/source/project.json \
  --targets grandleon_n64_campaign
```

The project is compiled and embedded during the cross-build. Character style,
body, archetype and team drawings are selected from authored data; a campaign
does not need console-specific rules or a second source file.

## Automated checks

Run every supported Nintendo 64 check with:

```sh
cmake --build build --target grandleon_n64_check_all
```

The suite covers conformance, rendered pixels, a played campaign route,
two-controller co-op and SRAM persistence. `scripts/local-ci.sh --n64` runs
that suite and the renderer check.

The first run builds pinned toolchain and emulator images and therefore takes
several minutes. Later runs reuse those images. Set `GRANDLEON_DOCKER` while
configuring CMake if the container runtime is not named `docker`.

Current renderer design, performance budgets and remaining graphics work are
summarized in [Console renderer](../../docs/CONSOLE_RENDERER.md).
