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

### Twin Gate richer production path

Steel and Cedar's authored Twin Gate battle now exercises the richer renderer
inside the playable campaign, not only in a separate demo. The exact 20×14 map
uses the normal textured, shaded terrain and a scrolling 16×11 view. Its exact
connected 6×6 keep starts closed, opens when the player's unit reaches its
door or interior, and reveals the real room units and boss. Movement,
collision, objectives, placements, saves, and campaign progression are not
rewritten for the effect.

```sh
cmake --build build --target grandleon_n64_twin_gate_runtime_check
```

The board uses the resident CI4 terrain atlas, filtered character cutouts, and
a six-upload CI4 keep image at board distance. Close framing loads one detailed
CI8 state rather than caching every detailed state. Ordinary maps allocate no
keep art. The pinned ares run passes 13/13 with a 21,905 μs worst frame
(45 fps), 28,080 bytes resident during the board, 76,608 bytes at the close
view's peak, 270,720 packaged art bytes, and a 2,080,768-byte review ROM. Each
board tile occupies 768 texel bytes plus a 32-byte palette in TMEM; a detailed
close tile occupies 2,048 texel bytes plus a 512-byte palette.

The gate writes native 320×240 approach, exterior, threshold, revealed-room,
and close frames under
`build-n64-twin-gate-runtime/ares/native-twin-gate-runtime/`. The west approach
is deliberately mostly road, farmland, grass, and stone so the check cannot
pass on the landmark while leaving the playable ground flat. The older fixed
camera comparison remains available as
`grandleon_n64_twin_gate_quality_check`, but it is not the playable path.

North/south board frames are already packaged outside the executable at
`rom:/art/directional.gdb`. They are not selected by the production renderer
yet: keeping that switch off means this preparation adds cartridge bytes but
does not consume RDRAM or change frame time. The renderer will enable them only
with its bounded moving-character cache and hardware checks in place.

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
