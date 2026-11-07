# Nintendo 64

Grandleon can turn a portable campaign into a Nintendo 64 ROM. The ROM uses the
same campaign and combat rules as the Godot player, with controller input, SRAM
saves, and a presentation designed for the console.

## Play the 0.4 showcase

Build Sevenfold Gate from the repository root:

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_n64_sevenfold_roms
```

Open `build-n64-sevenfold-roms/sevenfold-n64.z64` in ares, on a compatible FPGA
system, or from a flash cartridge. The review build shows live FPS and includes
a Stage picker so every art style, archetype, and terrain can be inspected
without replaying the complete campaign.

Choose **1 Player** or **2 Players** on the title screen. A new campaign goes
straight to its save choice and first playable stage; fixed formations do not
open a roster or deployment screen first. **Help** keeps the full controls
available without placing a mandatory instructions screen in the launch path.

## Controls

- D-pad: move the active cursor or menu selection.
- A: confirm, select a unit, or choose a destination.
- B: cancel or go back.
- Z: open the selected unit's action menu.
- Start: open the battle menu.

In two-player mode, each controller has its own cursor and menu state. Authored
unit ownership decides which characters each player commands.

## Graphics

There is one Nintendo 64 renderer. It draws project-specific, team-coloured
character cutouts generated from the curated Godot roster over textured
tactical terrain. Attacks can switch to a closer terrain backdrop, then return
to the same board state. The campaign contains no alternate graphics mode.

The campaign ROM itself is what release testing exercises. It is not paired
with a separate renderer demo.

## Build another campaign

```sh
platform/nintendo64/scripts/build-n64.sh \
  --project games/tarnholt/source/project.json \
  --targets grandleon_n64_campaign
```

The project is compiled and embedded during the cross-build. Its authored art
style, body, archetype, and faction colours select drawings from the same
curated art source used by Godot.

## Verify the target

```sh
cmake --build build --target grandleon_n64_check_all
```

This runs engine conformance, framebuffer checks, a played route, two-controller
co-op, and SRAM persistence in the pinned ares environment. The first run also
builds the pinned toolchain and emulator images, so it takes several minutes.

See [Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md) for
export limits and release expectations. Contributor setup is in
[Setting up](../../docs/SETUP.md).
