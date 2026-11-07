# Console 3D

Grandleon now has playable 3D versions of the Sevenfold campaign for Nintendo
64 and PlayStation. They use the same maps, turns, combat, objectives, campaign
progress, and one/two-player rules as the sprite builds. Only the presentation
changes.

The graphics are an early production baseline. They are available now so the
whole campaign can be tested while model readability, terrain, animation,
cameras, effects, close-up attacks, and performance keep improving. Movement
uses the 3D models, and real hit/miss events switch to a terrain-aware
high-detail view with simulation-owned damage. Counters appear as the next
real combat event rather than as a renderer-authored demonstration.

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
sprite build. Both versions keep save data in SRAM. The 3D build animates the
moving model along its accepted route and uses high-detail models for real
attacks, misses, damage, and counters.

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

The N64 viewer currently passes `56/56` checks and records 54 captures. Its
capture trail includes two same-team pages showing all eight low-detail roles
from the opposite camera-facing three-quarter headings, matched flat/material
tactical boards for medieval and pirates, a matched eight-role 2.5D medieval
board with a short tile move, and proxy/hybrid close views for medieval and
mythical representatives. Its ares measurements are 62–87 fps for
eight low-detail models and 39–51 fps for eight high-detail models. The two
material tactical views measure 78 and 75 fps, versus 81 and 76 fps for the
same flat boards. Textured proxy close views measure 157 fps for medieval and
129 fps for mythical; curated hybrid close views measure 423–443 fps because
the detailed fighters become two RDP cutouts rather than thousands of proxy
triangles. The full 2.5D board measures about 327 fps while static and 312 fps
at its slowest animated frame.

The PlayStation viewer currently passes `352/352` guest checks, including
safe-picture checks for every auto-framed attack pair, and `18/18` independent
GPU checks. In the pinned interpreter, its slowest measured scenes are about
52 fps for four low-detail board models, 32 fps for the optional four-model
high-detail comparison, and 51 fps for two high-detail close-view models.
Animated PS1 3D presentation targets a locked 30 fps at every second NTSC
retrace; 32 fps would not produce stable console frame pacing. The automated
regression ceiling is 33 ms, not a real-hardware claim.

These are emulator measurements used to catch regressions, not promises about
original hardware. Real-hardware play remains part of the review process.

The N64 material and hybrid experiments are viewer-only for now. Press **Z** on
the board to compare the production, material, and, on the medieval page, 2.5D
board candidates. **B** moves the selected 2.5D unit with two alternating pose
frames. In a close view **Z** cycles the flat proxy, textured proxy, and the
medieval or mythical hybrid. The material path
uses two 32×32 CI4 greyscale atlases, deterministic UVs projected from existing
proxy coordinates, and the existing vertex palettes for style and team
identity. The tactical evidence keeps all eight roles, mixed male/female
bodies, mixed terrain, a blue move range, and a gold selected tile visible so
texture noise can be judged at actual board scale.

The hybrid uses actual curated Godot frames quantised into separate 80×80 CI4
cutouts. Red and blue versions come from the same team masks Godot uses. The
cutouts sit over textured 3D terrain with separate shadow and impact sprites;
the attack uses a frontal push, impact, and recoil instead of an orbit. One
cutout and its palette peak at 3,232 bytes of TMEM. The embedded hybrid sprites
total 28,688 bytes and grow the aligned viewer ROM by 32 KiB. Its animation
peaks at 2.35 ms in ares. Attack and counter keep their original left/right
screen anchors for the whole exchange; the counter runs right-to-left. The
curated prototype does not yet animate a counter, and adding one must preserve
that invariant.

The full-map candidate adds ten 56×56 cutouts: idle frames for eight distinct
medieval roles plus two rogue movement poses. Foot-position sorting gives
correct overlap; far-row scaling, tile anchors, health bars, and team chips
keep the board readable. These assets total 17,360 bytes, add 16 KiB to the
aligned ROM, and peak at 1,600 bytes of TMEM per character upload. Eight
characters and their shadows upload about 15.1 KiB per frame. The playable
campaign stays on its established presentation until this treatment has
MiSTer or original-style hardware validation.

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
