# Nintendo 64 Pyrite target (migration phase 1)

This directory is a self-contained candidate for
`platform/nintendo64/pyrite/`. It builds one real Steel and Cedar Bell Orchard
board through Pyrite without replacing Grandleon's simulation, campaign
package, or authoring pipeline.

**Migration status:** this is an active, non-shipping integration path. It is
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
/tmp/grandleon-pyrite-build/project/grandleon_pyrite_bell_orchard.z64
```

Set `GRANDLEON_PYRITE_N64_INST` to a previously exported toolchain directory to
skip the container build. The directory is accepted only when its two commit
marker files match `versions.env`.

## Boundary

`GpkRuntimeAdapter` is the only cartridge/package boundary. It owns the
ROM-loaded bytes, performs target-budget validation, and exposes a decoded
`EncounterLoadResult`. `SteelCedarBoard` is presentation only: it selects Bell
Orchard, builds Pyrite/Tiny3D draw buffers, and maps the five representative
unit types to generated cutouts. Replacing this proof board with camera,
selection, and UI presenters does not change package decoding or simulation.

The Pyrite editor is not part of the runtime workflow. The checked-in scene is
a small declarative boot shell; the official headless CLI generates and builds
the engine project. See `THIRD_PARTY.md` before distribution.
