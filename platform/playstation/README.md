# PlayStation

Grandleon can turn a portable campaign into a PlayStation BIN/CUE image. It
runs the same campaign and combat rules as Godot and Nintendo 64, with pad
input, memory-card saves, and a 320×240 console presentation.

## Build a playable disc

```sh
cmake -S . -B build -DGRANDLEON_BUILD_TESTS=ON
cmake --build build --target grandleon_playstation_disc
```

Keep `build-playstation/disc/grandleon.bin` and `grandleon.cue` together, then
open the `.cue` in PCSX-Redux, another compatible emulator, or a compatible
FPGA system.

The image deliberately contains no Sony licence sector. It therefore does not
boot on an unmodified retail PlayStation.

The title screen offers only the player counts supported by the campaign, plus
**Help**. A new one-player campaign goes straight to its save choice and first
playable stage; founding or resuming does not add a roster-summary page. Fixed
equipped formations do not open roster or deployment management first.
Two-player mode waits for a second pad rather than silently changing the choice.

## Controls

- D-pad: move the active cursor or menu selection; hold to keep moving.
- X: confirm, select a unit, or choose a destination.
- O: cancel or go back.
- Triangle: open the selected unit's action menu.
- Start: open the battle menu.

If a campaign author lets players name a character, a new game shows a compact
keyboard for that character. Use the D-pad and X to type, O to erase, Start to
save, or Triangle to keep the current name. The same option appears as
**Choose name** or **Change name** when that character's company menu is
available. Fixed-name characters never show it, and continuing a save does not
repeat the new-game prompt.

The battle menu has **Show FPS**, which begins off in every disc image and can
be toggled while testing performance.

In two-player mode, each pad has its own cursor and menu state. Authored unit
ownership decides which characters each player commands.

## Graphics

There is one PlayStation renderer. It draws project-specific, team-coloured
character cutouts generated from the curated Godot roster over textured
tactical terrain. Attacks can switch to a closer terrain backdrop, then return
to the same board state. Tactical action menus use framed focus states,
Cross/Circle button badges, owner colours, and health meters, and open opposite
the selected character. The fixed status area remains 32 pixels high, so the
board is not reduced. The campaign contains no alternate graphics mode.

Save slots, Help, story pages, company and inventory management, aftermaths,
Stage selection, character details, and confirmations use the same framed
page and focus treatment. Footer glyphs use Cross, Circle, Triangle, and START;
story panels fit their actual lines so more of the authored backdrop remains
visible.

Every campaign and exported project uses the production textured diamond
terrain, visible elevation faces, diagonal painter order, scrolling camera,
and smaller board-scale character cutouts. Project identity may select
reviewed content, but never a second renderer. Steel and Cedar's roads, grass,
paving, ruins, houses, groves, and Twin Gate cells therefore share the same
ground-level tactical composition. Castle doors and daises are cell features,
and walls gain elevation without replacing or covering the playable map.

The interface typeface is drawn for Grandleon and released under the
repository's MIT licence; it is not converted from an external font. Its
editable bitmap source is `src/ui_font.h`. The PlayStation expands that source
into a transparent 4bpp atlas at boot, with proportional spacing, accented
Latin letters for English, Spanish and Brazilian Portuguese, and the four pad
marks. Pixel-measured rows clip or add an ellipsis inside their panels rather
than drawing through a border or meter.

The playable campaign executable is what release testing and disc boot testing
exercise. It is not paired with a separate renderer demo.

The generated north/south board bank is staged outside the PS-EXE and included
on a disc as `DIRART.BIN`. The production renderer does not read it yet, so it
does not consume main RAM or VRAM. A later bounded loader can read only the
moving character's four frames instead of making a large campaign executable
resident all at once.

## Build another campaign

```sh
cmake --build build --target grandleon_playstation_sevenfold_disc
```

That review build is written to
`build-playstation-sevenfold/disc/sevenfold-ps1.bin` and `.cue`, with the Stage
picker enabled. For another project, pass its source to
`platform/playstation/scripts/build-playstation.sh` and run `build-disc.sh`
against the same `GRANDLEON_PLAYSTATION_BUILD_DIR`.

The project is compiled and embedded during the build. Its authored art style,
body, archetype, and faction colours select drawings from the same curated art
source used by Godot.

## Verify the target

The standard PlayStation checks exercise the playable executable:

```sh
cmake --build build --target grandleon_playstation_check
cmake --build build --target grandleon_playstation_turn_check
cmake --build build --target grandleon_playstation_card_check
cmake --build build --target grandleon_playstation_campaign_check
cmake --build build --target grandleon_playstation_disc_check
```

Together they cover engine conformance, controller play and framebuffer output,
memory cards, complete campaign routes, and BIOS disc loading. The first run
also builds the pinned toolchain and emulator images, so it takes several
minutes.

The playable campaign and turn checks use the same textured isometric tactical
board as release discs and exported projects. Campaign-specific artwork can
change what is drawn, but there is no second flat or presentation-only board
renderer to drift away from normal play.

See [Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md) for
export limits and release expectations. Contributor setup is in
[Setting up](../../docs/SETUP.md).
