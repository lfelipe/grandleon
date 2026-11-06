# Godot PC application

Godot is Grandleon's supported PC player and campaign creator. It owns the
interface, rendering, animation, audio, and local input. The shared C++ engine
still owns legal actions, combat, objectives, campaign state, saves, and enemy
decisions.

| Layer | Responsibility |
|---|---|
| Portable project and `.gpk` package | Maps, characters, story, rules, and portable presentation choices |
| C++ GDExtension | Compilation, package loading, simulation, campaigns, saves, and tactics |
| Godot project | Creator screens, player interface, graphics, animation, sound, and controllers |
| Runtime directory | One machine's saves, settings, imports, exports, and recovery data |

Nothing in the Godot presentation changes the deterministic game state or adds
machine details to a portable project.

## Build and run

Build the self-contained Linux application from the repository root:

```sh
platform/godot/scripts/build-steamos.sh --output out/grandleon-pc
GRANDLEON_WINDOWED=1 out/grandleon-pc/launch-pc.sh
```

The bundle includes the pinned Godot runtime, the native extension, imported
assets, and two compiled examples: **The Tarnholt Line** for one player and
**Emberbound: The Last Dawn** with a one/two-player launch choice. It does not
require a system Godot installation.

For an incremental developer build:

```sh
cmake -S . -B build-godot \
  -DGRANDLEON_BUILD_GODOT=ON \
  -DGRANDLEON_GODOT_API_VERSION=4.7 \
  -DGRANDLEON_WERROR=ON
cmake --build build-godot --parallel --target grandleon_godot_project
```

Godot is pinned to 4.7.2, `godot-cpp` to 10.0.0-stable, and the generated
extension API to Godot 4.7.

## Home, play, and local co-op

Ordinary launches open a controller-first home screen containing both included
campaigns, local projects, **Create / Edit Games**, and **Exit**. Each campaign
states whether it launches for one player, requires two, or asks at launch. The
screen displays titles, not paths or internal project identifiers.

Play supports one or two local controls. Each player has an independent cursor,
selection, and command queue. Friendly movement and presentation may overlap,
while accepted commands still enter the shared simulation deterministically.
Solo play controls the complete allied company.

The player includes:

- story scenes and in-battle dialogue;
- deployment, objectives, forecasts, danger zones, and tactical actions;
- deterministic built-in enemy turns;
- a company screen for fielding characters and moving items;
- automatic campaign progress plus three manual checkpoints;
- remappable compact controls and stable controller seats;
- Normal/Large text, camera-impulse control, and sound volume; and
- a private performance readout that is never saved or exported.

The battle board uses almost the complete window. The top strip holds only the
stage, round, and active side; the bottom strip shows only actions usable now.
Unit statistics appear in a temporary card while a character is inspected.
Action menus and the card choose a clear position from live board geometry so
they avoid characters, cursors, and highlighted action tiles.

[UI_GUIDE.md](UI_GUIDE.md) records the battlefield information and interaction
rules. [ANIMATION_PIPELINE.md](ANIMATION_PIPELINE.md) documents the semantic
events shared by sprite, procedural, and imported-character presentation.

## Create a campaign

Choose **Create / Edit Games**. **New Campaign** asks for a title and creates a
small working campaign instead of an empty database. Authors can then:

- paint, fill, resize, and surround maps;
- place allied and opposing characters;
- assign allied placements to Player 1 or Player 2;
- add two-player-only opponents;
- edit deployment and defeat, protect, or survive goals;
- write ordinary and in-battle conversations;
- create and tune characters, roles, sides, gear, abilities, growth, and rewards;
- add chapters, outcomes, alternate routes, and endings;
- test the in-memory draft in the ordinary player; and
- undo, redo, save, copy, import, and export complete projects.

The UI uses visible names and focused tasks. Stable source identifiers remain
behind the creator boundary. Saves validate the complete project before an
atomic replacement, and testing a draft never overwrites the installed game's
campaign progress.

[Creating a game](../../docs/CREATING_A_GAME.md) is the human walkthrough.

## Export Center

**Finish & Export** offers only targets the installation can build:

- **Portable Project** writes an editable `.grandleon.zip`.
- **Linux PC** writes a standalone player-only bundle when the verified player
  shell is installed.
- **Nintendo 64** writes a `.z64` when its optional builder is installed.
- **PlayStation** writes a matching `.bin` and `.cue` when its optional builder
  is installed.

The installed console builders currently use the established sprite
presentation. The playable 3D console work in 0.4 is a separate Sevenfold
release target; the creator does not yet offer a renderer choice for arbitrary
projects.

The console builders are independent, versioned downloads. They run offline in
a restricted local sandbox and do not require a source checkout or container
runtime on the author's machine. Before publishing output, the host validates
the builder inventory, expected files, hashes, executable permissions,
ROM/disc structure, and private-path boundary.

The maintainer-facing payload format is documented in
[Console builder payloads](../../docs/CONSOLE_BUILDER_PAYLOADS.md).

## Graphics and artwork

The supported presentation uses authored 3D voxel terrain and seven curated
high-resolution sprite directions: sci-fi, mythical, medieval, nature,
Sengoku, Undead, and Pirates. Every set provides male and female designs for
all eight archetypes, with reusable masks for all six faction colours.
Experimental renderers remain available only to focused developer checks;
they are not player settings.

[GRAPHICS.md](GRAPHICS.md) describes the presentation, current coverage, replacement
boundary, and capture commands. The human-facing animated roster is in
[Artwork and animation roster](../../docs/ARTWORK.md).

## Files and privacy

Created projects, imports, exports, recovery snapshots, controls, presentation
settings, and campaign saves live under Godot's per-user data directory. A
portable bundle redirects those locations below its own `runtime/` directory.

Portable project archives contain only canonical project source and
project-owned assets. They exclude:

- usernames, hostnames, and absolute paths;
- credentials and network endpoints;
- controller identities and machine-specific mappings;
- presentation settings, recovery files, and saves; and
- creator code from player-only exports.

The optional local automation channel under `runtime/automation` exists for
tests and support. It reads visible text, sends bounded input, and requests
Godot screenshots without opening a network listener. Set
`GRANDLEON_AUTOMATION=0` to disable it.

## Validation

The aggregate PC gate builds the native tests, runs every bounded Godot smoke,
and validates the packaged bundle:

```sh
platform/godot/scripts/smoke-pc-release.sh \
  --bundle out/grandleon-pc
```

Useful focused checks include:

```sh
platform/godot/scripts/smoke-art-library.sh build-godot
platform/godot/scripts/smoke-animated-characters.sh build-godot
platform/godot/scripts/smoke-controller-seats.sh build-godot
platform/godot/scripts/smoke-motion-path.sh build-godot
platform/godot/scripts/smoke-map-editor.sh build-godot
platform/godot/scripts/smoke-story-editor.sh build-godot
platform/godot/scripts/smoke-army-editor.sh build-godot
platform/godot/scripts/smoke-campaign-editor.sh build-godot
```

Capture current renderer and creator documentation from the executable:

```sh
platform/godot/scripts/capture-readme-media.sh
platform/godot/scripts/capture-creator-guide.sh
```

Every script uses isolated runtime data and bounded child processes. The
captures in the root README come from these commands.
