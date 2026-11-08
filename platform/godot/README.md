# Godot PC application

This is Grandleon's main player and campaign creator. One application opens the
included games, local projects, creation tools, settings, and exports.

## Build and run it

From the repository root:

```sh
platform/godot/scripts/build-steamos.sh --output out/grandleon-pc
GRANDLEON_WINDOWED=1 out/grandleon-pc/launch-pc.sh
```

The resulting folder is self-contained. It includes the pinned Godot runtime,
Grandleon's native game engine, its art, and the two sample campaigns. A system
Godot installation is not required.

## Play

The home screen includes:

- **The Tarnholt Line**, a one-player campaign;
- **Emberbound: The Last Dawn**, playable by one or two local players; and
- campaigns created or imported on this computer.

Each local player has their own cursor and units. A solo game gives the complete
allied company to Player 1. Campaigns that allow either mode ask before launch.

The battlefield gives most of the window to the map. Unit details appear only
while a character is selected, and action menus choose a clear position rather
than covering the units or cells needed for the current decision. Settings
include remappable controls, text size, volume, camera impulse, cinematic
motion, and optional battle close-ups.

Battle close-ups move from the live board into a terrain-based view of the
attack, damage or miss, counterattack, and defeat. Hold **Confirm** to speed one
up or press **Cancel** to return to the board. Turning them off changes only the
presentation, never the combat result.

Conversations spoken during a battle reuse the same backdrop pools as close
combat. The speaking character's nearby terrain chooses a stable composition
for the whole scene. The current pools cover the painted Cedar Deepwood
forest, Bell Orchard, Cedar Watch, Gate Village, and walkable areas inside
Twin Gate Keep. Campaign-specific Medieval pools require the exact authored
map and landmark; a similar terrain mix elsewhere cannot select them. Between
battles, an authored **Deep Wood** backdrop uses the forest pool directly.
Other authored backdrops keep their clear colour treatment until a matching
art pool exists, so missing scenery never hides the portrait or text.

## Create

Choose **Create / Edit Games**, then **New Campaign**. A starter campaign is
already playable and can be changed a piece at a time. The creator supports:

- terrain painting, map resizing, surroundings, and unit placement;
- deployment, objectives, local co-op ownership, and two-player scaling;
- story scenes and conversations inside battles;
- characters, roles, gear, abilities, growth, rewards, and supplies;
- linear chapters, alternate routes, and endings; and
- immediate testing in the normal player.

[Creating a game](../../docs/CREATING_A_GAME.md) is the short visual tour. The
[artwork roster](../../docs/ARTWORK.md) shows the included themes, characters,
animations, team colours, and terrain coverage.

## Export

**Finish & Export** offers the targets installed on the current machine:

- **Portable Project** creates an editable `.grandleon.zip`.
- **Linux PC** creates a standalone player-only folder.
- **Nintendo 64** creates a `.z64` when its optional builder is installed.
- **PlayStation** creates a `.bin` and `.cue` when its optional builder is
  installed.

Console builders are independent downloads. They work offline after
installation, and a missing builder disables only its own target. See
[Playing on Nintendo 64 and PlayStation](../../docs/CONSOLE_RENDERER.md) for
the current console experience and limitations.

## Files and backups

Projects, imports, exports, recovery data, settings, and campaign saves live in
the user's application-data directory. They are not written into the installed
program folder.

A portable project contains its game source and project-owned assets. It does
not include usernames, computer paths, credentials, controller identities,
local settings, recovery data, or player saves.

Export a `.grandleon.zip` at useful milestones while the creator is still
evolving. Projects from 0.2 and 0.3 are migrated when imported into 0.4, but a
backup remains the safest way to try a newer editor.

## Work on the application

For a faster incremental development build:

```sh
cmake -S . -B build-godot \
  -DGRANDLEON_BUILD_GODOT=ON \
  -DGRANDLEON_GODOT_API_VERSION=4.7 \
  -DGRANDLEON_WERROR=ON
cmake --build build-godot --parallel --target grandleon_godot_project
```

Godot is pinned to 4.7.2 and `godot-cpp` to 10.0.0-stable. Run the complete PC
release check with:

```sh
platform/godot/scripts/smoke-pc-release.sh --bundle out/grandleon-pc
```

Rebuild the public screenshots and visual creator guide with:

```sh
platform/godot/scripts/capture-readme-media.sh
platform/godot/scripts/capture-creator-guide.sh
```

Presentation implementation notes live in [GRAPHICS.md](GRAPHICS.md),
[UI_GUIDE.md](UI_GUIDE.md), and [ANIMATION_PIPELINE.md](ANIMATION_PIPELINE.md).
They are maintainer references rather than player or creator instructions.
