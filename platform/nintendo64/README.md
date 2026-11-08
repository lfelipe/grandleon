# Nintendo 64

Grandleon can turn any portable project into a Nintendo 64 ROM. The ROM uses
the same campaign and combat rules as the Godot player, with controller input,
cartridge saves, local co-op, and graphics designed for the hardware.

There is one Nintendo 64 player. It is built on the pinned Pyrite, Tiny3D and
libdragon revisions under `platform/nintendo64/pyrite/`; a missing dependency
is an export error, never a reason to fall back to another renderer.

## Build a ROM

From the repository root:

```sh
platform/nintendo64/scripts/build-n64.sh \
  --project games/steel_and_cedar/source/project.json
```

The ROM is written to `build-n64/platform/nintendo64/grandleon_n64.z64` by
default. Set `GRANDLEON_N64_BUILD_DIR` to keep another build separate.

The first build downloads the pinned Pyrite source and prepares a pinned
libdragon/Tiny3D toolchain container, so it takes longer than later builds.

## Controls

- D-pad: move the cursor or menu focus.
- A: confirm, select a unit, or choose a destination.
- B: cancel or go back.
- Z: open actions for the selected unit.
- Start: open the battle menu.

One-player games use controller one. In local co-op, each player has their own
cursor and can command only units assigned to them by the campaign. The battle
menu contains Help, sound and presentation settings, and the Show FPS toggle;
FPS is off by default.

## What the renderer does

The tactical board is a scrolling isometric scene with textured terrain,
elevation faces, surrounding non-playable scenery, props, shadows and
team-coloured character cutouts. Attacks can switch to larger character art on
a terrain-aware backdrop, show the counterattack and result, then restore the
same board position.

Campaign authors do not make a separate N64 map. The exporter reads the same
project package and generated artwork as the other players, then fits it to the
cartridge, RDRAM and frame budgets. Unsupported project size or missing art is
reported during export.

## Verify the target

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_n64_check
```

The gate exports both the small demo and Steel and Cedar through the normal
creator path, checks that each ROM identifies this renderer with no fallback,
and boots both in the pinned ares emulator as an exact 4 MB Nintendo 64.

See [Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md)
for export expectations and [Setting up](../../docs/SETUP.md) for contributor
setup.
