# Godot PC client

This is the PC presentation layer for Grandleon. Godot draws and accepts input;
the existing C++ simulation, campaign loader, package format, and tactics policy
remain authoritative. The SDL client stays as a small reference surface, and
the Nintendo 64 and PlayStation builds are unchanged.

See [PC_ROADMAP.md](PC_ROADMAP.md) for the current review, SteamOS test
checklist, known risks, and the ordered path from this test bundle to a
production PC release.
[CREATOR_ROADMAP.md](CREATOR_ROADMAP.md) defines the native create/play
architecture, standalone-game exports, Windows support, and console target
builder direction.

The separation is deliberate:

| Layer | Owns | Does not own |
|---|---|---|
| `.gpk` project | maps, characters, dialogue, rules, presentation choices | PC paths or local model settings |
| C++ extension | package loading, legal ranges, deterministic commands, AI fallback | rendering or controller layout |
| Godot project | rendering, animation, two-player input, optional model requests | combat rules or campaign outcomes |
| `pc.local.json` | one machine's model endpoint and feature switches | project content |
| `controls.local.json` | one machine's controller button mapping | project content or portable rules |

## Create and play projects

Open the session menu and choose **Projects / Create**. The native Projects
screen can create an empty canonical project or a complete playable starter,
lists projects stored under Godot's per-user data directory, and opens a native
map creator. The map creator previews the same replaceable terrain atlas as play
mode, provides brush, contiguous fill, and two-corner rectangle tools, supports
bounded command-level undo/redo, and resizes from the top-left origin with an
explicit clipping preview. The selected encounter's allied and enemy placements
are overlaid on the terrain, so map edits have gameplay context. Shrinks are
blocked while any encounter placement or deployment tile would fall outside
the new board. Saves pass through the validating native source layer, and the
edited draft plays immediately.
Compilation and scene handoff stay in-process; draft package bytes are not
written to the installation directory. Returning through the play-mode session
menu reopens the same project.

The current creator creates and resizes maps and edits terrain. Unit placement,
campaign, dialogue, and presentation editing arrive in the increments defined
by [CREATOR_ROADMAP.md](CREATOR_ROADMAP.md).

Godot is pinned to **4.7.2**, `godot-cpp` to **10.0.0-stable**, and the generated
bindings to the **4.7** API. The project uses the compatibility renderer to
cover a wider range of SteamOS hardware.

## Build a SteamOS test bundle

The portable bundle includes the official Godot binary, the native extension,
imported assets, and the compiled demo. It does not need system-wide Godot or
export templates.

```sh
platform/godot/scripts/build-steamos.sh
GRANDLEON_WINDOWED=1 out/grandleon-pc/launch-pc.sh
```

The first command downloads the pinned Linux editor binary when necessary and
verifies its SHA-512 checksum. Its output directory must not already exist, so
an old PC build is never silently overwritten. Pass another source project
or destination when needed:

```sh
platform/godot/scripts/build-steamos.sh \
  --project games/demo/source/project.json \
  --output out/last-signal-test
```

Copy that whole directory to the SteamOS machine. In desktop mode, add
`launch-pc.sh` as a non-Steam game, then return to gaming mode. The launcher
keeps Godot's data, config, and cache in the bundle's `runtime/` directory and
starts the two-player `last_signal` campaign full-screen. No home-directory or
host-specific path is compiled into the extension; the bundler checks the
native binary and refuses it if the checkout path remains.

The same builder can package one project as a player-only Linux PC game:

```sh
platform/godot/scripts/build-steamos.sh \
  --project path/to/project.json \
  --campaign campaign_id \
  --player-only \
  --output out/my-game
```

That form contains the compiled game but removes creator scenes and canonical
source templates. Its session menu has no creator entry. The ordinary bundle
retains both player and creator, so this is a packaging choice over the same
project format rather than a separate kind of game.

`BUILD-INFO.txt` identifies the source commit, clean/dirty build state, pinned
Godot version, and default campaign/profile without recording the checkout,
user, hostname, endpoint, or other machine-specific values.

Environment overrides are intentionally generic:

```sh
GRANDLEON_CAMPAIGN=last_signal GRANDLEON_PLAYERS=2 ./launch-pc.sh
GRANDLEON_GRAPHICS=diorama ./launch-pc.sh
GRANDLEON_WINDOWED=1 ./launch-pc.sh
```

The selected PC default is `voxel-sprites`. `pixel` remains the stable 2D
fallback, while `diorama` and `voxel` stay available for comparison.

## Local co-op

Each player has a cursor, selection, colour, and request queue. A lone
controller is Player 1 and can command the whole friendly team. Keyboard/mouse
may join the vacant Player 2 seat on its first gameplay input; a second
controller takes Player 2 directly. Connected pads are detected again after
startup, and a reconnect preserves every still-connected pad's player seat and
both cursor positions. The side panel and a short toast show which player
became ready or is waiting; unassigned extra pads cannot control either player
or the session menu.

Every essential flow is intentionally playable with one directional control
and five buttons: stick or d-pad moves the cursor, A confirms, B cancels, Y
selects the next action, X ends a character's turn, and Start begins play or
opens the session menu. Shoulder buttons and Back remain optional shortcuts,
not requirements. Configurable remapping is a production requirement so the PC
build can support compact controllers and other limited-button installations
without creating a separate game mode. The current build implements that
requirement through an optional local control profile while retaining the
stable default mapping above.

To remap buttons, copy `controls.example.json` to `controls.local.json` beside
the launcher and change values under `buttons`. Essential actions must remain
bound to distinct supported buttons; omitted values retain their defaults. Set
`previous_unit`, `next_unit`, or `menu_alt` to `none` when the controller lacks
those optional inputs. An unknown, disabled essential, or ambiguous mapping
activates the complete default profile instead of leaving the game partly
controllable. Supported button values are `a`, `b`, `x`, `y`, `start`, `back`,
`left_shoulder`, and `right_shoulder`; `none` is valid only for optional
actions. The local file is ignored by Git, excluded from bundles, and
discovered only at launch time.

Outside deployment and dialogue, the configured menu button (Start by default)
or optional alternate (Back by default) opens a controller-native session menu.
The stick or d-pad chooses an action and the configured confirm button (A by
default) or Enter confirms it, including
cycling all four graphics profiles against the live encounter without changing
its simulation state, disabling camera impulse for motion-sensitive players,
and switching all interface text between Normal and Large. B, Start, or Escape
resumes directly under the default mapping, while X or Q returns to Steam.
Opening the menu is the first confirmation, preventing a single accidental
gameplay press from closing the PC session.

Co-op campaigns should author `sideBlocks`. That engine rule carries no
exclusive unit activation: one character can move, a second can move, and the
first can then act. The Godot bridge accepts both players' commands while the
previous visual action is still resolving. Commands are serialized only at the
deterministic simulation boundary, where collisions and conflicts have one
portable answer. The board keeps one animation track per unit, so two accepted
moves received in the same rendered frame animate together.

This is not a second real-time simulation. It is concurrent local control of a
team phase, preserving replays and package compatibility on every target.

## Optional local models

The demo is complete with models disabled. Its authored dialogue, built-in
enemy tactics, talk branch, and ending all remain available offline.

To test an OpenAI-compatible local server, copy the example beside the bundle
launcher and edit the copy:

```sh
cp pc.example.json pc.local.json
```

`pc.local.json` is ignored by Git. The default endpoint is loopback, remote
endpoints are refused unless `allowRemote` is explicitly enabled, and an API
token is read from the environment variable named by `tokenEnv` rather than
stored in project content. No request is made while `enabled` is false.

Conversation integration appends a small, validated exchange only to scenes
listed in `content/demo_ai_hooks.json`; authored lines are never replaced.
Speaker names, line counts, lengths, and markup are bounded before display.

Tactical integration gives the model a numbered list of legal candidates made
from engine queries. Candidate zero is always the deterministic built-in plan.
The model returns one index, the native session validates the selected command
again, and a timeout, malformed response, out-of-range choice, or refused
proposal uses candidate zero. The model never supplies arbitrary simulation
state or bypasses `Encounter::apply`.

## Replaceable graphics

`scripts/art_library.gd` is the current PC art boundary. The package publishes
only portable menu indices—terrain kind/theme and character
archetype/colour/style/figure. The art library maps those values to resources.
Rules and source projects do not name Godot files.

The checked-in project does not duplicate generated art. CMake stages
`tools/placeholder_art/assets/modern` into `res://assets`, including its
manifest. A replacement pack can preserve the manifest vocabulary, or a new
renderer can implement the same lookups for `.glb` models, voxel chunks, or a
different sprite atlas. See [GRAPHICS_DIRECTIONS.md](GRAPHICS_DIRECTIONS.md) for
the evaluated paths. The comparison set is generated by the runnable Godot
profiles—not image-generation prompts—and is reproduced outside the repository
with `scripts/capture-graphics-prototypes.sh`.

Four launchable profiles exercise the choice without changing the
package or rules: `pixel` uses the existing sheets, `diorama` uses lit 3D
terrain with the real sheets as billboard characters, `voxel-sprites` uses
box-built terrain with those same characters, and `voxel` builds both terrain
and character blockouts from 3D boxes. Select one with
`--graphics=PROFILE` in a developer run or `GRANDLEON_GRAPHICS=PROFILE` in the
SteamOS bundle. During play, the session menu can replace only the renderer and
rebind the current board, cursors, selections, and highlights so the four
directions can be compared on the target PC without restarting the campaign.

`content/graphics_profiles.json` is the versioned PC art contract for camera,
lighting, sprite scale, renderer choice, and the terrain/material palette. To
tune one PC without changing a project or committing machine details, copy
`graphics.example.json` beside the launcher as `graphics.local.json` and edit
only the values to override. `cameraImpulse` and bounded `textScale` values
supply the session's initial accessibility settings; the controller menu may
then change either for the current run. The launcher discovers that ignored
file at run time; its path and contents are never compiled into the bundle.

An authored pack may also bind `assets.terrainScenes` by terrain name and
`assets.characterScenes` by archetype to project resources. Imported `.glb`
files and `.tscn` scenes both arrive as `PackedScene` resources in Godot. Each
scene uses a one-cell origin and the manifest's `sceneScale`; surface heights
remain explicit in the same profile. Missing or invalid bindings fall back per
asset to procedural terrain or the existing sprite, so an artist can replace
the roster incrementally instead of delivering an all-or-nothing pack.
The recommended profile exercises this boundary with an authored low-poly
forest grove, mountain edge, animated water surface, and relay ruin. Their
animation and shaders belong to the scenes, not the board renderer, so a
replacement may remove or redesign them freely.

A terrain binding may be a direct scene path or a context-aware binding. The
current road selects and rotates a bridge scene when a cardinal neighbour is
water, without teaching the renderer an art filename:

```json
{
  "road": {
    "scene": "res://art/terrain/relay_road.tscn",
    "whenAdjacent": {
      "water": {
        "scene": "res://art/terrain/relay_bridge.tscn",
        "rotateToAdjacent": true
      }
    }
  }
}
```

An absent contextual scene still falls back to the binding's base scene; an
absent or invalid binding falls back to the procedural tile.

Authored character bindings may additionally name an `AnimationPlayer` and map
semantic `idle`, `move`, `attack`, `ability`, `hurt`, and `defeat` states to the
clips exported by that asset. Locomotion and facing already respond to accepted
board movement. See [ANIMATION_PIPELINE.md](ANIMATION_PIPELINE.md) for the
presentation-event seam needed for timed attacks, impacts, and defeat.

The generated pack currently supplies four 32×32 pose cells plus a separate
standing image. Their size is not a renderer rule: the art manifest publishes
each sheet's cell dimensions and columns, a normalised ground pivot, and named
idle, move, attack, ability, item, talk, hurt, and defeat sequences with their
frame indices and timing. Both sprite renderers consume that data. Idle units
use the standing image, moving units loop only the two walk poses, and combat
events hold the attack or cast pose while their procedural or replaceable
effects play. A higher-resolution pack may change cell dimensions without a
package, simulation, or renderer change.

## Developer build

```sh
cmake -S . -B build-godot \
  -DGRANDLEON_BUILD_GODOT=ON \
  -DGRANDLEON_GODOT_API_VERSION=4.7 \
  -DGRANDLEON_WERROR=ON
cmake --build build-godot --target grandleon_godot_project
```

The runnable project is staged at
`build-godot/platform/godot/project/app`. Compile a package into that directory,
run one editor import to generate the extension/import cache, then launch Godot
with `--path`.

To build and capture all four graphics profiles under the same campaign state:

```sh
platform/godot/scripts/capture-graphics-prototypes.sh
```

To record the paced, model-off campaign reel used for remote visual review:

```sh
platform/godot/scripts/record-demo-reel.sh
```

To validate default, remapped, disabled-optional, and rejected ambiguous
controller profiles—including their live interface labels:

```sh
platform/godot/scripts/smoke-control-profile.sh
```
To verify stable controller seats through connect, disconnect, reconnect,
keyboard-plus-pad, and extra-pad cases:

```sh
platform/godot/scripts/smoke-controller-seats.sh
```

To validate manifest-driven sprite dimensions, pivots, frame selection, timing,
legacy defaults, and a non-32-pixel replacement sheet:

```sh
platform/godot/scripts/smoke-art-library.sh
```

To create a project, paint and undo/redo a real map cell, save and compile the
result, play it, and return to the same editor session:

```sh
platform/godot/scripts/smoke-map-editor.sh
```
The reel is an actual 1280x720 Godot capture. It advances authored dialogue,
queues two friendly moves without serial player turns, and lets the built-in
opposition respond. Set `GRANDLEON_DEMO_SECONDS` or pass build and output paths
as the first two arguments when a different duration or destination is useful.
Set `GRANDLEON_DEMO_GRAPHICS` to `pixel`, `diorama`, `voxel-sprites`, or
`voxel` to record the same sequence through another replaceable renderer.
Set `GRANDLEON_DEMO_CONFIG` to an untracked local model configuration to record
the same reel with conversation or opposing-side model hooks enabled. Model,
host, and token settings stay outside both the project and the resulting video.
