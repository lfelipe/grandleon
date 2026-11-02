# Godot arcade client

This is the PC presentation layer for Grandleon. Godot draws and accepts input;
the existing C++ simulation, campaign loader, package format, and tactics policy
remain authoritative. The SDL client stays as a small reference surface, and
the Nintendo 64 and PlayStation builds are unchanged.

See [ARCADE_ROADMAP.md](ARCADE_ROADMAP.md) for the current review, tonight's
cabinet checklist, known risks, and the ordered path from this test bundle to a
production PC surface.

The separation is deliberate:

| Layer | Owns | Does not own |
|---|---|---|
| `.gpk` project | maps, characters, dialogue, rules, presentation choices | PC paths or local model settings |
| C++ extension | package loading, legal ranges, deterministic commands, AI fallback | rendering or controller layout |
| Godot project | rendering, animation, two-player input, optional model requests | combat rules or campaign outcomes |
| `arcade.local.json` | one machine's model endpoint and feature switches | project content |

Godot is pinned to **4.7.2**, `godot-cpp` to **10.0.0-stable**, and the generated
bindings to the **4.7** API. The project uses the compatibility renderer to
cover a wider range of SteamOS hardware.

## Build a SteamOS test bundle

The portable bundle includes the official Godot binary, the native extension,
imported assets, and the compiled demo. It does not need system-wide Godot or
export templates.

```sh
platform/godot/scripts/build-steamos.sh
GRANDLEON_WINDOWED=1 out/grandleon-arcade/launch-arcade.sh
```

The first command downloads the pinned Linux editor binary when necessary and
verifies its SHA-512 checksum. Its output directory must not already exist, so
an old cabinet build is never silently overwritten. Pass another source project
or destination when needed:

```sh
platform/godot/scripts/build-steamos.sh \
  --project games/demo/source/project.json \
  --output out/last-signal-test
```

Copy that whole directory to the SteamOS machine. In desktop mode, add
`launch-arcade.sh` as a non-Steam game, then return to gaming mode. The launcher
keeps Godot's data, config, and cache in the bundle's `runtime/` directory and
starts the two-player `last_signal` campaign full-screen. No home-directory or
host-specific path is compiled into the extension; the bundler checks the
native binary and refuses it if the checkout path remains.

`BUILD-INFO.txt` identifies the source commit, clean/dirty build state, pinned
Godot version, and default campaign/profile without recording the checkout,
user, hostname, endpoint, or other machine-specific values.

Environment overrides are intentionally generic:

```sh
GRANDLEON_CAMPAIGN=last_signal GRANDLEON_PLAYERS=2 ./launch-arcade.sh
GRANDLEON_GRAPHICS=diorama ./launch-arcade.sh
GRANDLEON_WINDOWED=1 ./launch-arcade.sh
```

The selected cabinet default is `voxel-sprites`. `pixel` remains the stable 2D
fallback, while `diorama` and `voxel` stay available for comparison.

## Local co-op

Each player has a cursor, selection, colour, and request queue. With two
controllers, each owns one. With one controller, keyboard/mouse is Player 1 and
the controller is Player 2. Connected pads are detected again after startup;
both d-pad and left stick work.

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
cp arcade.example.json arcade.local.json
```

`arcade.local.json` is ignored by Git. The default endpoint is loopback, remote
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
SteamOS bundle.

`content/graphics_profiles.json` is the versioned PC art contract for camera,
lighting, sprite scale, renderer choice, and the terrain/material palette. To
tune one cabinet without changing a project or committing machine details, copy
`graphics.example.json` beside the launcher as `graphics.local.json` and edit
only the values to override. The launcher discovers that ignored file at run
time; its path and contents are never compiled into the bundle.

An authored pack may also bind `assets.terrainScenes` by terrain name and
`assets.characterScenes` by archetype to project resources. Imported `.glb`
files and `.tscn` scenes both arrive as `PackedScene` resources in Godot. Each
scene uses a one-cell origin and the manifest's `sceneScale`; surface heights
remain explicit in the same profile. Missing or invalid bindings fall back per
asset to procedural terrain or the existing sprite, so an artist can replace
the roster incrementally instead of delivering an all-or-nothing pack.

Authored character bindings may additionally name an `AnimationPlayer` and map
semantic `idle`, `move`, `attack`, `ability`, `hurt`, and `defeat` states to the
clips exported by that asset. Locomotion and facing already respond to accepted
board movement. See [ANIMATION_PIPELINE.md](ANIMATION_PIPELINE.md) for the
presentation-event seam needed for timed attacks, impacts, and defeat.

The current sprite contract is four 32×32 animation cells in a horizontal
strip, plus 47-variant terrain blobs. Before accepting externally produced HD
art, the next asset-contract change should move cell size, frame count, pivot,
and animation timing into the manifest; the renderer must not acquire a second
set of filename rules.

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

The reel is an actual 1280x720 Godot capture. It advances authored dialogue,
queues two friendly moves without serial player turns, and lets the built-in
opposition respond. Set `GRANDLEON_DEMO_SECONDS` or pass build and output paths
as the first two arguments when a different duration or destination is useful.
Set `GRANDLEON_DEMO_CONFIG` to an untracked local model configuration to record
the same reel with conversation or opposing-side model hooks enabled. Model,
host, and token settings stay outside both the project and the resulting video.
