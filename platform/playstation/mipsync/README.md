# Mipsync PlayStation renderer

This directory contains Grandleon's sole PlayStation renderer, built on
Mipsync. It boots the full five-encounter Steel and Cedar
campaign, saves and resumes on a PlayStation memory card, and supports both
one- and two-player campaigns. Battles use the shared simulation for movement,
Attack, equipped abilities, carried items, Talk, Wait, reinforcements,
objectives, and package-authored opposing turns. Stage-opening, Talk, and
character-fall conversations come from the game package and remain localized.
In two-player battles each physical controller
keeps its own visible cursor and can command only its authored units; choosing
two players is not merely a larger roster driven by controller one. A compact
forecast and result UI plus the optional close-combat camera use the same disc
and rules. The 0.5 gameplay-parity paths are in place; audio, real-hardware
review, and continued graphics and control polish remain. Mipsync is never
used as a fallback, and the removed renderer is not retained as one.

Solo and co-op are generated from their complete authored encounter variants,
not by removing Player 2 from the co-op battle. That preserves different
starts, opposition, reinforcement timing and tuning. The campaign probe boots
all five exact 1P definitions as well as the full 2P save-and-resume route.

Board movement follows the authoritative reachable route at ten presented
frames per tile. It uses both authored walk poses, left/right facing, full-size
north/south views, terrain height, a small footfall lift, and a dithered
contact cue baked into the existing character texture. The action menu
stays out of the way until the unit stops, and input waits for the presentation
to settle. Entering an authored hidden area reveals it only when the rendered
unit reaches the doorway rather than when the rules commit the destination.

Characters use an 8-bit indexed texture bank with a shared stage palette;
terrain materials use separate 16-colour palettes. All nine character poses
retain 64×64 detail. The six walking poses
share a VRAM slot and are uploaded on demand; idle, attack, and hurt images
stay resident independently. This allows identical character types to appear
on both sides of a fight. Native terrain forms use bark, foliage, and stone
materials, grouped within terrain pages to limit texture changes.
Compatible close-combat scenes use the existing Godot panorama artwork,
split into four texture-page-safe strips with its own palette. The camera
places the actors on the panorama's foreground rather than its horizon.

When a game authors a deployment area, its tiles are marked on the live board.
Cross selects or places a character through the shared simulation and Start
begins battle. A fixed ready-to-play formation skips this step completely.

Help is available from the title and the in-battle Start menu. Close view,
reduced motion and FPS display can be changed there and are kept on the memory
card; retail play starts with FPS hidden.

Mipsync is pinned to commit `866f405a0e34b5925a8d832272fe52c657688430`.
The upstream source is not vendored in the Grandleon repository. Release
packaging puts the pinned source and its licence inside the optional offline
builder. Generic engine fixes live as replayable `git format-patch` files under
`tools/upstream_patches/engines/mipsync`; Grandleon-specific package, simulation
and rendering adapters stay here.

## Package and simulation boundary

`grandleon_ps1_mipsync_scene_export` opens a real `.gpk`, loads the selected
encounter and its campaign through `package_runtime`, creates the real
simulation, and writes a renderer-neutral JSON build input. The target
generator reconstructs the same encounter and package-authored campaign graph.
`game_bridge.cpp` is the narrow seam between the shared rules and Mipsync scene
entities; Mipsync types do not enter the engine or package format.

```sh
cmake -S . -B build-mipsync-host -G Ninja \
  -DGRANDLEON_BUILD_TESTS=OFF -DGRANDLEON_DESKTOP_SDL=OFF
cmake --build build-mipsync-host \
  --target grandleon_ps1_mipsync_scene_export
build-mipsync-host/grandleon_ps1_mipsync_scene_export \
  path/to/game.gpk steel_and_cedar/bell_orchard 2 /tmp/bell-orchard.json
```

A successful export prints `GRANDLEON_PS1_RENDERER=MIPSYNC`. Content identities
are decimal strings in JSON because ordinary JSON numbers cannot preserve every
64-bit value.

## Build and prove the playable slice

The disc combines Grandleon's proven GCC 12.3/Nugget PlayStation toolchain with
the PSn00bSDK renderer libraries. The wrapper normalizes PSn00bSDK's symbol
tables for the Debian linker without changing code or data, applies the pinned
Mipsync patches, regenerates target data, builds the exact CUE/BIN and verifies
the embedded renderer identity and RAM budget.

`scripts/build-project.sh` is the production path used by the installed
creator. It reads the first (selected) campaign from `project.json`, builds each
encounter as a disc stage, honors its one/two/choice player policy, and writes
`game.bin` plus `game.cue`. `tests/run-project-launch-probe.sh` boots either the
small direct-entry demo or Steel and Cedar from those generated files.

```sh
PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/scripts/build-renderer-probe.sh \
  /tmp/bell-orchard.json /tmp/empty-mipsync-output

PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/tests/run-combat-probe.sh \
  /tmp/empty-mipsync-output/build /tmp/empty-probe-output

PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/tests/run-scroll-probe.sh \
  /tmp/deepwood-output/build /tmp/deepwood-probe-output

PSN00BSDK_ROOT=/path/to/PSn00bSDK \
platform/playstation/mipsync/tests/run-visibility-probe.sh \
  /tmp/twin-gate-output/build /tmp/twin-gate-probe-output

platform/playstation/mipsync/tests/run-battle-moment-probe.sh \
  /tmp/conversation-output /tmp/conversation-probe-output

platform/playstation/mipsync/tests/run-item-probe.sh \
  /tmp/item-output /tmp/item-probe-output

platform/playstation/mipsync/tests/run-deployment-probe.sh \
  /tmp/deployment-output /tmp/deployment-probe-output
```

To review motion rather than still frames, run
`scripts/capture-review-motion.sh` against a full campaign build. It records a
real routed move and attack from the CUE/BIN and writes a browser-ready MP4.

The combat probe boots the CUE through the pinned BIOS-style PCSX-Redux
interpreter, injects controller edges only at the public update boundary and
requires real animated movement, Player 1 Attack and healing, Player 2 Wait,
and the package-authored opposing turn. It captures the live forecast, close
combat and completed board views. The scroll probe drives both axes on the real
14x9 Cedar Deepwood map and requires viewport clamp, one active terrain page
and zero missed VBlanks. The visibility probe uses ordinary controls on the
real one-player Twin Gate encounter, captures the covered room, walks Rowan
through the door, and requires the authored 18-cell reveal before capturing
the room again. Focused action probes drive a package-authored Talk conversation,
consume a carried healing item only after the unit is wounded, and arrange a
real unit inside an authored deployment area before starting battle. The campaign
probe additionally starts, saves, resumes, and traverses all five encounters
from the same disc in its complete one- and two-player forms. See
`VALIDATION.md` for the current measurements and hashes.

## Budgets and validation

The generic Mipsync runtime cannot coexist with Grandleon's campaign runtime
unchanged. This target compiles only the renderer pieces it uses and sizes the
static caches for the scene. Music plays from six CD audio tracks; a resident
51 KiB SPU ADPCM bank supplies interface, movement and combat effects. Music
and Effects have independent saved controls on the title and battle menus.
The playable-slice gate includes the audio code and bank in resident memory,
and retains an additional 64 KiB platform reserve, 32 KiB for stack and
512 KiB for gameplay heap inside the PlayStation's 2 MiB RAM.

Larger maps are split into overlapping viewport-sized terrain pages. Only the
closest page is visible, while the camera follows the cursor and clamps to the
authored playable rectangle. Units outside the viewport are culled only from
presentation; the complete encounter remains in the shared simulation.
Terrain follows a continuous low-poly surface instead of a flat board. Ground
materials use the matching Godot terrain palettes, while sparse native
conifers, outcrops, reeds, crops, bamboo, rubble, and ice forms provide height
and recognizable silhouettes without turning every cell into a separate
object. Characters and the cursor follow the same surface. The distant fog
colour follows the dominant natural terrain, and clustered surroundings plus
opt-in seam fill keep the board from reading as a noisy grid floating in a blue
void. The player presents at a measured 30 fps while controller polling and
tactical rules continue at 60 Hz. Alternating retraces hold the completed front
buffer, giving shaped terrain and close combat a predictable two-VBlank render
budget without changing game speed. The close view repeats its dominant
terrain material across a floor that extends beyond the camera, so it no
longer exposes the edge of a small floating quad. The generated surroundings
policy stays portable across renderers (`natural_blend`, `extend_edges` or
`fixed_terrain`).

Release validation checks the following on the exact CUE/BIN:

- BIOS-style emulator and real PlayStation hardware boot;
- a clear visual improvement over the explicit baseline;
- steady 30 fps board and close-combat presentation with 60 Hz input and rules;
- main RAM, VRAM and packet headroom for the full campaign;
- real packages, complete encounters, save data and campaign flow;
- embedded `GRANDLEON_PS1_RENDERER=MIPSYNC` plus the fail-closed renderer audit.

Release and creator exports already point only to Mipsync and fail clearly if
it cannot build. There is no comparison renderer or alternate PlayStation
graphics path in the repository.
