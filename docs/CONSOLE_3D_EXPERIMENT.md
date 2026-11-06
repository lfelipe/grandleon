# Console 3D

Grandleon now has playable 3D versions of the Sevenfold campaign for Nintendo
64 and PlayStation. They use the same maps, turns, combat, objectives, campaign
progress, and one/two-player rules as the sprite builds. Only the presentation
changes.

The graphics are an early production baseline. They are available now so the
whole campaign can be tested while model readability, terrain, animation,
cameras, effects, close-up attacks, and performance keep improving.

## What can I run?

| Output | What it is for |
|---|---|
| `sevenfold-n64-sprite.z64` | Playable N64 campaign using the established sprite renderer |
| `sevenfold-n64-3d.z64` | Playable N64 campaign using the 3D board renderer |
| `sevenfold-n64-3d-viewer.z64` | Diagnostic viewer for inspecting every model and close-up scene |
| PlayStation sprite disc | Playable campaign using the established sprite renderer |
| `sevenfold-ps1-3d.bin` / `.cue` | Playable PlayStation campaign using the 3D board renderer |
| `grandleon_psx_sevenfold_3d.ps-exe` | Diagnostic viewer for inspecting every model and close-up scene |

The Sevenfold campaign covers all seven visual styles, all eight archetypes,
male and female bodies, both teams, and varied terrain. Debug builds also have
a Stage picker and visible frame rate so individual scenes can be reached
quickly.

## Build the Nintendo 64 versions

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
```

The output folder is `build-n64-sevenfold-roms/`. The 3D campaign is the file
named `sevenfold-n64-3d.z64`; the similarly named `-viewer` file is the roster
diagnostic rather than the game.

The campaign starts with the same one-player or two-player choice as the
sprite build. Both versions keep save data in SRAM.

## Build the PlayStation version

```sh
cmake --build build --target grandleon_playstation_sevenfold_3d_disc
```

This creates `sevenfold-ps1-3d.bin` and `sevenfold-ps1-3d.cue` under
`build-playstation-sevenfold-3d/disc/`. Open the `.cue` in an emulator or on a
compatible test system. Like the regular project disc, it has no Sony licence
sector and does not boot on an unmodified retail PlayStation.

The PlayStation build uses one player when only the first controller is
connected and local two-player mode when a second controller is present.

## Models and team colours

The roster contains `7 styles × 8 archetypes × 2 bodies = 112 models`. Every
model has:

- a low-detail version for the board;
- a high-detail version for views containing only a few characters; and
- a runtime team material, currently blue for Player 1 and red for Player 2.

The curated Godot characters are raster animation rather than source meshes.
The console models are therefore deterministic low-poly interpretations. Large
role-specific shapes—shield, bow, staff, rods, crook, banner, paired blades,
or quadruped body—keep the archetypes readable, while armour, headgear,
palette, and silhouette distinguish each setting.

The playable campaigns package only the model combinations their stages use.
The diagnostic viewers retain all 112 combinations so no roster gap can be
hidden by campaign selection.

## Current checks and measurements

The viewers remain useful as automated renderer checks:

```sh
cmake --build build --target grandleon_n64_sevenfold_3d_check
cmake --build build --target grandleon_playstation_sevenfold_3d_check
```

The N64 viewer currently passes `38/38` checks. Its ares measurements are
62–87 fps for eight low-detail models, 39–51 fps for eight high-detail models,
and 132–163 fps for two high-detail models in the close view.

The PlayStation viewer currently passes `296/296` guest checks and `18/18`
independent GPU checks. In the pinned interpreter, its slowest measured scenes
are about 42 fps for four low-detail board models, 28 fps for the optional
four-model high-detail comparison, and 49 fps for two high-detail close-view
models.

These are emulator measurements used to catch regressions, not promises about
original hardware. Real-hardware play remains part of the review process.

## Ongoing graphics work

Playable does not mean visually complete. Console graphics are a continuous
roadmap track across releases. Work will keep focusing on:

- clearer silhouettes and better differences between archetypes;
- richer terrain, depth, variation, and transitions;
- closer, better-composed attack scenes;
- movement and attack animation that reads as motion rather than sliding;
- effects, team identity, and UI that remain clear without covering action;
- stable frame pacing and measured performance on original-style hardware.

The sprite builds remain useful visual and gameplay baselines while the 3D
renderers improve. Both presentations use the same game rules and campaign
content.
