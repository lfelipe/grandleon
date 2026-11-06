# Console 3D: what works now

The newer character work does produce recognisable low-poly figures on both
Nintendo 64 and PlayStation. All seven styles, eight archetypes, and male and
female bodies are available on both consoles, with red and blue team colours.

This does not mean the normal console games have switched to 3D characters.
The playable campaign ROMs still use sprites. The 3D outputs are separate,
interactive review programs that let us judge models, camera distance,
terrain, animation, and performance without putting an experiment in the
supported campaign path.

## What can I run?

| Output | Playable campaign? | Purpose |
|---|---|---|
| N64 sprite ROM | Yes | Sevenfold campaign, with a 1P/2P choice |
| N64 3D ROM | No | Complete 3D roster, seven maps, and attack/counter preview |
| PS1 sprite executable/disc | Yes | Supported campaign player |
| PS1 3D executable | No | Complete 3D roster and close attack preview |

Build both Sevenfold N64 review ROMs together:

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

The output folder contains `sevenfold-n64-sprite.z64`,
`sevenfold-n64-3d.z64`, and a short manifest explaining the difference. The
sprite build includes the debug Stage picker and live FPS display.

Build and check the PS1 3D viewer:

```sh
cmake --build build --target grandleon_playstation_sevenfold_3d_check
```

Package the interactive viewer as an emulator-friendly BIN/CUE disc:

```sh
cmake --build build --target grandleon_playstation_sevenfold_3d_disc
```

This produces `grandleon_psx_sevenfold_3d.ps-exe` for interactive use and a
separate check executable. The check visits every model, captures a real
320×240 GPU frame, and verifies the image under PCSX-Redux.

The console-specific scripts are also available directly:

```sh
platform/nintendo64/scripts/check-sevenfold-3d.sh
platform/playstation/scripts/check-sevenfold-3d.sh
```

## What is in the roster?

The matrix is `7 styles × 8 archetypes × 2 bodies = 112 models`. Each model
has a low and high detail version. Low detail is intended for a board view;
high detail is used when only a few characters are close to the camera.

These are deterministic procedural console models. The curated Godot
characters are raster animation, not hidden source meshes, so the console
generator uses a shared construction vocabulary: shield and sword, bow and
quiver, staff and hat, twin rods, crook and satchel, shield and banner, paired
blades and hood, or a horned quadruped. Style-specific armour, headgear,
palette, and silhouette keep the seven settings coherent.

Team colour is a runtime material on both consoles. The PS1 viewer also puts a
blue or red marker under each model, so allegiance remains clear even when a
style's own palette is similar to a team colour.

## Nintendo 64 result

The N64 viewer shows all eight roles on each strategic page. **A** changes
detail, the d-pad changes style or body, and **Start** opens the close view.
There, **A** changes role, **B** plays the illustrative attack and counter,
the d-pad changes style or terrain, and **Start** returns to the board.

The current ares run passes `38/38` checks:

| Scene | Measured result |
|---|---:|
| Eight low-detail models on a board | 62–87 fps |
| Eight high-detail models on a board | 39–51 fps |
| Two high-detail models in the close view | 132–163 fps |
| Slowest frame of the attack/counter preview | 158 fps |

The N64 therefore has enough measured headroom for continued 3D gameplay
experiments. It is still a viewer today: it does not consume campaign combat
state, and the damage callouts are illustrative.

## PlayStation result

The PS1 viewer splits the eight archetypes across two readable four-role
strategic pages and uses two high-detail models in its close attack view.
**Left/Right** changes style, **Up/Down** changes body or terrain, **Cross**
changes role page or attack role, **Triangle** changes strategic detail,
**Circle** plays the strike, and **Start** switches views.

The check currently passes `296/296` guest assertions and `18/18` independent
GPU assertions. In the pinned interpreter, including a GPU readback that waits
for submitted drawing to finish, the slowest measured pages were:

| Scene | Slowest measured frame |
|---|---:|
| Four low-detail models on the 3D board | 23.7 ms, about 42 fps |
| Four high-detail models for static comparison | 35.7 ms, about 28 fps |
| Two high-detail models in the attack view | 20.1 ms, about 49 fps |

An older stress test with sixteen different reduced models reached only 18
fps. That result still matters: it is why the supported PS1 campaign keeps its
clear sprite board. The new viewer succeeds by showing one body variant and
four roles at a time, then spending the high-detail budget on the two models
that matter during an attack. A future campaign integration should use the
same conservative shape: sprites for normal play and an optional 3D close-up,
not sixteen high-detail models all the time.

## Measurements and source

All timings above are emulator measurements at native 320×240, not claims
about original hardware. The exact model counts, source hashes, earlier
commander/rogue comparison, and raw captures live under
[`tools/console_3d`](../tools/console_3d/README.md).

The N64 viewer uses pinned preview versions of libdragon and Tiny3D. Tiny3D is
MIT licensed, its notice is kept under `third_party/tiny3d`, and the normal N64
campaign toolchain remains unchanged. The PS1 viewer uses the same pinned
MIPS-I toolchain and GPU runtime as the supported PS1 builds.
