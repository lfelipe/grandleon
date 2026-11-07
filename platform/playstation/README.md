# PlayStation

Grandleon's PlayStation target runs the same compiled projects and tactical
rules as the Godot and Nintendo 64 players. The console layer supplies pad
input, memory-card saves, audio timing, a 320×240 GPU presentation, and disc
bootstrapping; it does not maintain a second version of the game rules.

## Build a playable disc

Configure the repository once, then build the campaign disc:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_playstation_disc
```

The result is `build-playstation/disc/grandleon.bin` together with
`build-playstation/disc/grandleon.cue`. Keep both files together and open the
`.cue` in PCSX-Redux, another compatible emulator, or a compatible FPGA
system.

The image deliberately contains no Sony licence sector. That keeps the build
free of Sony-owned boot data, but it also means a stock unmodified PlayStation
will refuse the disc.

## Controls

- D-pad: move the active cursor or menu selection.
- X: confirm, select a unit, or choose a destination.
- O: cancel or go back.
- Triangle: open the selected unit's action menu.
- Start: begin the battle or open its menu.

Both controller sockets are supported. Each local player has an independent
cursor, selection and menu state, while the shared engine serializes accepted
commands. A campaign decides whether it launches for one player, two players,
or chooses based on whether the second pad is connected.

## Renderer

The console has one renderer. It draws curated, team-coloured character
cutouts over deterministic textured terrain and uses terrain-derived
backdrops for close battles. Tactical range, selected cells, health and team
identity remain visible at native resolution.

Run the Sevenfold visual gate with:

```sh
cmake --build build --target grandleon_playstation_sevenfold_renderer_check
```

This builds `grandleon_psx_sevenfold_renderer.ps-exe`, runs it in the pinned
PCSX-Redux interpreter, and captures the board, eased movement, attack,
counterattack and return-to-board sequence. It is an automated visual
diagnostic, not another renderer or a second game mode. The current gate
reports `18/18` guest checks and `561/561` independent framebuffer checks.

## Build another project

The normal cross-build accepts any portable source project in the repository:

```sh
platform/playstation/scripts/build-playstation.sh \
  --project games/sevenfold/source/project.json \
  --targets grandleon_playstation_campaign \
  --stage-picker
```

To turn that executable into a disc, retain the same build directory and run:

```sh
platform/playstation/scripts/build-disc.sh
```

The project is compiled and embedded during the build. Character style, body,
archetype and team drawings are selected from authored data; a campaign does
not need PlayStation-specific rules or a second source file.

## Automated checks

The individual checks are useful while working on one area:

```sh
cmake --build build --target grandleon_playstation_check
cmake --build build --target grandleon_playstation_render_check
cmake --build build --target grandleon_playstation_turn_check
cmake --build build --target grandleon_playstation_card_check
cmake --build build --target grandleon_playstation_campaign_check
cmake --build build --target grandleon_playstation_disc_check
```

Together they cover engine conformance, rendered pixels, controller play,
memory-card persistence, complete campaign routes, and the BIOS disc-loading
path. `scripts/local-ci.sh --playstation` runs the supported PlayStation suite
and the renderer check.

The first run builds pinned toolchain and emulator images and therefore takes
several minutes. Later runs reuse those images. Set `GRANDLEON_DOCKER` while
configuring CMake if the container runtime is not named `docker`.

Current renderer design, performance budgets and remaining graphics work are
summarized in [Console renderer](../../docs/CONSOLE_RENDERER.md).
