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
system, or from a flash cartridge. The review build includes a Stage picker so
every art style, archetype, and terrain can be inspected without replaying the
complete campaign.

Choose **1 Player** or **2 Players** on the title screen. A new campaign goes
straight to its save choice and first playable stage; founding or resuming does
not add a roster-summary page. Fixed equipped formations do not open roster or
deployment management first. **Help** keeps the full controls available without
placing a mandatory instructions screen in the launch path.

## Controls

- D-pad: move the active cursor or menu selection; hold to keep moving.
- A: confirm, select a unit, or choose a destination.
- B: cancel or go back.
- Z: open the selected unit's action menu.
- Start: open the battle menu.

If a campaign author lets players name a character, a new game shows a compact
keyboard for that character. Use the D-pad and A to type, B to erase, Start to
save, or Z to keep the current name. The same option appears as **Choose name**
or **Change name** when that character's company menu is available. Fixed-name
characters never show it, and continuing a save does not repeat the new-game
prompt.

The battle menu has **Show FPS**, which begins off in every ROM and can be
toggled while testing performance.

In two-player mode, each controller has its own cursor and menu state. Authored
unit ownership decides which characters each player commands.

## Graphics

There is one Nintendo 64 renderer. It draws project-specific, team-coloured
character cutouts generated from the curated Godot roster over textured
tactical terrain. Attacks can switch to a closer terrain backdrop, then return
to the same board state. Tactical action menus use framed focus states, A/B
button badges, owner colours, and health meters, and open opposite the selected
character so the choice does not cover them. The campaign contains no alternate
graphics mode.

The same framed interface is used for save slots, Help, story pages, company
and inventory management, aftermaths, Stage selection, character details, and
confirmations. Focused rows remain visible without relying on colour alone,
and footer badges use the controller's A, B, Z, and START labels.

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
