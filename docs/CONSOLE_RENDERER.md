# Console renderer

Nintendo 64 and PlayStation each ship one Grandleon renderer. There is no
graphics-mode choice in a campaign or in the editor.

The renderer uses hardware-sized versions of the curated character artwork,
team-colour masks, textured tactical terrain, and terrain-derived full-frame
backdrops for close battles. Movement, damage, counterattacks, objectives and
saves still come from the shared engine; the renderer only presents accepted
state.

## What ships in 0.4

Sevenfold Gate is the console showcase. Its seven stages collectively exercise:

- all seven character styles and eight archetypes;
- male and female characters on both sides;
- all thirteen terrain kinds across varied layouts;
- one-player and local two-player launch modes;
- movement, attacks, misses, damage, counterattacks and defeat; and
- a debug Stage picker and live FPS display in review builds.

The renderer-check programs are automated visual tests, not alternate game
modes.

## Nintendo 64

Build the playable campaign and renderer check:

```sh
cmake --build build --target grandleon_n64_sevenfold_roms
cmake --build build --target grandleon_n64_sevenfold_renderer_check
```

The review folder is `build-n64-sevenfold-roms/`:

- `sevenfold-n64.z64` is the playable campaign.
- `sevenfold-n64-renderer-check.z64` is the automated visual diagnostic.

The renderer check uses the pinned libdragon/Tiny3D toolchain and runs under
the pinned ares build. It verifies asset provenance, TMEM limits, a tactical
board, eased tile movement, an attack/counterattack close view, health changes,
and return to the board. The current gate reports `11/11`.

## PlayStation

Run the native renderer capture gate with:

```sh
cmake --build build --target grandleon_playstation_sevenfold_renderer_check
```

It builds `grandleon_psx_sevenfold_renderer.ps-exe`, runs it in the pinned
PCSX-Redux interpreter, and captures native 320×240 frames. The check covers
the tactical board, sixteen eased movement frames, forty attack/counterattack
frames, and the 52-frame matched comparison sequence. The current run reports
`18/18` guest checks and `561/561` independent framebuffer checks.

The diagnostic executable is not the release disc. The PlayStation campaign
disc is built through the normal campaign target and consumes the same portable
Sevenfold project as the N64 and Godot builds.

## Performance contract

PlayStation presentation is budgeted for a stable 30 fps, not an unstable
32 fps average. The emulator regression ceiling is 33 ms per presented frame.
The current slowest captured tactical frame is roughly 4.4 ms in the pinned
interpreter; close battle and full-map comparison frames remain below 1 ms.

Nintendo 64 checks also require each tactical, movement and close sequence to
remain within a 33 ms frame budget. Emulator measurements catch regressions;
the final release still receives a MiSTer or original-style hardware smoke test.

## Ongoing graphics work

Console graphics will keep improving after 0.4. The next work is richer and
less repetitive tactical terrain, clearer neighbour transitions, more varied
clustered scenery, additional terrain-specific close backdrops, and smoother
character animation. These improvements must preserve walkable-cell clarity,
fixed attack/counterattack sides, deterministic output, and measured memory and
frame budgets.
