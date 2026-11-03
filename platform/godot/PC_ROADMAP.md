# PC review and roadmap

This document tracks the PC/SteamOS surface without changing the portable game
format or the deterministic rules shared by the SDL, Nintendo 64, and
PlayStation clients.

## Current decision

Use Godot as the PC presentation layer and keep the existing C++ simulation,
package runtime, and tactics policy authoritative. Start production from the
`voxel-sprites` profile: voxel terrain provides depth, lighting, props, and a
replaceable 3D art boundary while animated sprites preserve the current roster
and console lineage. Keep the other three profiles runnable as references and
fallbacks.

The branch is ready for a first SteamOS PC validation, not for a content-complete
release. The portable bundle includes pinned Godot, compiled demo content, the
native extension, imported assets, and a two-player launcher. It contains no
machine-specific graphics or control configuration.

## What is already proven

| Area | Current evidence |
|---|---|
| Portable rules | Godot submits intents to the same C++ session used by the other clients; `.gpk` content does not name Godot resources. |
| Simultaneous co-op control | Both players have independent cursors and request queues. A smoke mode requires two friendly moves submitted together to be accepted before it captures a frame. |
| Stable controller seats | A lone controller is Player 1. Reconciliation preserves every still-connected pad and cursor across enumeration changes, fills only a vacant seat on reconnect, and ignores extra devices. Keyboard/mouse can join a vacant second seat. Privacy-safe toasts distinguish ready, replacement, keyboard fallback, waiting, and ignored transitions without displaying a controller identity. A headless smoke covers launch, disconnect, reconnect, reorder, assignment, and transition-notice cases. |
| Compact controls | Every essential flow uses one stick or d-pad plus five configurable buttons. The default is A, B, X, Y, and Start; shoulders and Back are optional shortcuts that can be disabled. Analog navigation uses one shared high-deadzone, hysteretic filter across menus and gameplay: each complete deflection yields one cardinal step, diagonals cannot double-step, and d-pad response remains immediate. Settings offers only Deliberate and Extra Firm, persists the choice across the home screen and gameplay, and layers it over a persistent machine-private profile that may tune the safe base thresholds and replace physical button legends without recording a controller identity. Packaged acceptance proves that the real launcher carries the profile across both scenes. The play HUD follows the last active player and shows only the present goal and relevant buttons. Invalid, ambiguous, or unsafe local profiles fall back atomically to the complete default. |
| Deterministic conflict handling | Commands overlap visually but serialize at the simulation boundary; a stale or conflicting command is refused instead of mutating state out of order. |
| Offline campaign | The included campaign uses authored dialogue and deterministic enemy actions without a network dependency. |
| Replaceable art | Terrain and character scene bindings accept `.tscn` or `.glb` assets per archetype. The recommended profile binds authored water, road/contextual bridge, forest, mountain, sand, ruins, and grass, covering every Last Signal terrain while other kinds fall back per tile. Sprite cell dimensions, sheet layout, pivot, named sequences, and timing come from the art manifest. Its complete Last Signal sprite roster binds independent 443-pixel-cell replacements for Rook, Kestrel, Morrow, and the relay beast. Full Voxel inherits the terrain pack and replaces the same encounter roster with imported, skinned GLBs. Commander, rogue, and stormcaller reuse one humanoid topology without sharing meshes or actions; the beast has a distinct quadruped rig. Per-binding display scales keep those different proportions within one tactical cell. Missing bindings, clips, or metadata fall back asset by asset. |
| Live graphics and comfort controls | The controller session menu cycles all four renderers around the same authoritative snapshot, cursors, selections, and highlights, can disable camera impulse, and switches every interface label between normal and large text. The graphics capture smoke exercises the complete live cycle with both comfort states. |
| Persistent presentation settings | Graphics profile, camera impulse, text size, stick-input firmness, and the three-level sound-effects volume survive a restart in a versioned, atomic, presentation-only user-data file. Explicit launch overrides win without rewriting it; malformed settings fall back or recover without exposing paths or entering project data. |
| PC presentation audio | A PC-only autoload provides deterministic, license-free defaults for focus, confirm, cancel, dialogue, movement, weapon, magic, impact, miss, restoration, item, talk, endurance, and defeat cues. Semantic native presentation events select combat cues, simultaneous movement is de-duplicated per event batch, and one Full/Low/Off setting controls the bounded player pool. No audio choice enters portable content. |
| Persistent campaigns | Completed battles use the same versioned campaign envelope and migration registry as the other clients. Atomic checkpoints are isolated by package and campaign identity under runtime data and resume in a second process. The controller Save / Load page provides three manual slots: saving copies only complete between-battle state, while loading can restart from a populated slot anywhere. New Campaign uses a separate two-confirm automatic-progress reset without removing manual copies. Integration smokes prove write, restart, resume, copy, refusal, erase, and fresh start. |
| Company management | A controller-native screen renders the shared roster, store, kits, fielded set, authored capacity, and engine refusal. Field, bench, give, take, and proceed gestures commit through the portable campaign session and save immediately. An integration smoke performs every gesture against a real two-member campaign before opening its battle. |
| Private performance readout | The Settings page shows rolling FPS, p95 and peak frame times, window size, and internal canvas size. The five-second sample remains in memory and never records hardware, display, controller, user, path, or network identities. |
| Combat presentation | Native result events drive semantic attack, ability, hurt, defeat, impact, and delayed-removal presentation without blocking simulation. Sprite, procedural, and imported characters face their travel or combat target before the matching animation. |
| Route-aware locomotion | Accepted moves carry the shared portable route and a per-cell duration derived from console presentation timing. Both Godot renderers validate and follow every leg independently, including overlapping co-op moves, while snapshots remain authoritative and malformed records fall back safely. |
| Controller-safe menus | Start or the optional Back shortcut opens a five-choice root menu. Save slots and settings have their own short pages and contextual one-line control legend. Cancel walks back and resumes only from the root; Exit Game must be focused and confirmed, with no instant controller quit shortcut. |
| SteamOS packaging | The release bundle runs without a system Godot install, keeps writable state under its own `runtime/` directory, and includes a 512-pixel icon at a stable bundle-relative path for desktop and Steam shortcuts. One aggregate release gate runs the native suite, every bounded Godot integration smoke, and exact packaged acceptance. |
| Simple application entry | Ordinary launches open a controller-first home screen listing the included game and local project titles, plus Create / Edit and Exit. It displays no paths or machine identifiers, while automated test launches retain a direct route into play. |
| Privacy | The bundler rejects exact checkout paths and local override files. Endpoint, model, token environment name, and art overrides live only in ignored runtime files. |

## SteamOS PC test

Test the default `voxel-sprites` profile first, then compare `diorama` and
`voxel` only after the input loop is comfortable.

1. Add `launch-pc.sh` as a non-Steam game and start it from Gaming Mode.
2. Confirm 1280x720 and the target monitor's native mode both frame the board and side
   panel without clipping.
3. Launch with one controller and confirm it controls Player 1 and can command
   the complete friendly team using only stick/d-pad, A, B, X, Y, and Start.
   Then connect a second controller and repeat after a disconnect/reconnect.
   If the hardware layout differs, copy `controls.example.json` to
   `runtime/local/controls.local.json`, remap it locally, and repeat without
   committing it. That private profile survives a release-symlink upgrade.
4. Move both friendly characters at once. Check that their animations overlap,
   each cursor remains responsive, and a destination conflict produces a clear
   refusal rather than a stuck unit.
5. Play once with no `runtime/local/pc.local.json`. The complete authored story and
   deterministic enemy must remain available.
6. If a local model is configured, unplug or stop it mid-request. The status
   should report fallback and the encounter should continue with candidate
   zero.
7. Watch contact shadows, sprite scale, health bars, text size, dark-detail
   visibility, and camera impulse at the actual viewing distance.
8. At the Company screen, return an item to the store, give it back, bench and
   re-field one member, and start the battle using only the compact controls.
   Confirm each change updates the screen before the next input.
9. At the Company screen, save checkpoints 1 and 2. Load checkpoint 1 during a
   later battle and confirm it returns to the complete between-battle state.
   Close and relaunch to verify automatic resume still works. Use **New
   Campaign**, confirm it twice, and verify it starts fresh while the manual
   checkpoints remain available.
10. Open the session menu with Start. Walk through Save / Load and Settings,
   return with B, resume from the root, then focus **Exit Game** and confirm the
   launcher regains focus.
11. Leave the game running through several rounds, open Settings, and note its
   rolling FPS, p95, peak, window, and canvas values. Check those
   alongside controller focus, heat, and memory on the target PC; the game
   deliberately does not save this diagnostic sample.

Record the GPU, display mode, controller models, frame-pacing observations, and
which graphics profile is preferred. Do not record endpoint names, tokens, LAN
addresses, usernames, or absolute install paths in project content or issues.

## Next milestones

### 1. Close the PC input and launch loop

- Fix any Gaming Mode focus, scaling, controller identity, or reconnect issue
  found tonight.
- Validate the Full/Low/Off sound-effects control and cue balance through the
  target speakers; keep the compact setting on the existing Settings page.
- Keep the existing join/leave toasts and persistent profile-driven button
  legends visible while validating actual controller reconnect behavior.
- Add an automated Godot smoke target to CI once a pinned headless display and
  Godot cache are practical there.

Exit criterion: two people can launch, join, finish Last Signal, reconnect a
controller, and quit without keyboard intervention.

### 2. Polish and validate the authored relay terrain

- The complete Last Signal vocabulary now resolves through one manifest-bound
  Halcyon Relay pack: animated water, cobbled road and contextual bridge,
  forest, mountain, sand, ruins, and grass all have one-cell authored scenes.
- A deterministic variant list rotates asymmetric detail without adding
  randomness to portable content. A replacement pack can point those entries
  at genuinely different scenes without changing renderer code.
- Full Voxel inherits the same terrain binding instead of duplicating it; an
  absent or invalid scene still falls back per tile.
- Measure both 3D profiles under the compatibility renderer on the target PC
  before adding MultiMesh batching. These boards are small enough that art
  direction and replaceability matter first.

Exit criterion: the target PC confirms that the authored pack is readable and
stable at its native display mode, with acceptable frame pacing and clean
fallback when one replacement is removed.

### 3. Tune the production 3D animation path

- The imported commander now proves a normal Blender-to-GLB path with a
  sixteen-bone skeleton, skinned mesh, seven material surfaces, six in-place
  clips, and explicit exported-name mapping. It is reproducible from checked-in
  source and uses a profile-owned display scale calibrated against one tactical
  cell rather than baking presentation size into the asset.
- The beast now uses a second imported GLB with a fifteen-bone quadruped rig,
  seven material surfaces, and six explicitly mapped clips. Both exporters use
  the same deterministic authoring helpers and remain independent replacements.
- The rogue now reuses the commander's exact sixteen-bone hierarchy with a
  distinct hooded mesh, dual weapons, seven materials, and six new clips.
- The stormcaller reuses that hierarchy again with a distinct cowl, mantle,
  signal glaive, arc focus, materials, and six-clip action set. The complete
  Last Signal encounter roster is now imported while other unbound archetypes
  continue to prove per-asset fallback.
- The optional motion record now carries an engine-derived route and recommended
  duration; use it to tune imported rigs' gait and turn clips while keeping
  root motion in place.
- Hybrid sprites now mirror against their camera-relative travel direction and
  all 3D paths turn toward a combat target before weapon or magic presentation.
  Replacement sheets declare their native facing and may opt out of mirroring.
- Compare the complete imported encounter against the hybrid at the intended
  viewing distance, then tune gait, turn, attack timing, silhouette, and
  material response from the target-PC observations.

Exit criterion: the complete encounter roster remains legible in motion on the
target PC without a rule, campaign, or package change and without degrading the
sprite or procedural fallback paths.

### 4. Productize the local-model option

- Capture and review one real model-enabled run through an authenticated local
  OpenAI-compatible endpoint.
- Add a small connection test that reports DNS/connect/auth/model errors
  without displaying the endpoint, model identifier, token, or response body.
- Decide independently whether conversation and tactics should be enabled;
  neither feature should silently imply the other.
- Add latency and failure telemetry that remains local and contains only
  durations and coarse result categories.
- Keep the no-model path in every release test and never make a campaign wait
  indefinitely for inference.

Exit criterion: an unavailable, slow, malformed, or adversarial model cannot
stall play, invent a command, replace authored dialogue, or disclose local
configuration on screen or in the bundle.

### 5. Complete the PC product surface

- Add an authored music layer and per-player feedback with a separate music
  volume control; replace or tune the included UI/combat defaults from target
  listening feedback.
- Refine the controller-native Company screen from target-PC feedback, adding
  richer character presentation without moving inventory or roster rules out
  of the portable campaign layer.
- Keep campaign checkpoints and presentation settings as separate contracts;
  neither belongs in portable project archives or local-model configuration.
- Add attract mode, clean return-to-launcher behavior, and controller-safe idle
  handling.
- Review text scaling, colour contrast, camera impulse reduction, and input
  remapping as accessibility features rather than post-release polish.

Exit criterion: the PC client is a maintainable product surface rather than a
developer presentation harness, while the portable engine remains the source
of gameplay truth.

## Secondary console improvements

PC presentation remains the first priority, but shared art and presentation
work should improve the Nintendo 64 and PlayStation clients where their budgets
allow it:

- Audit both renderers against the shared standing, walk, attack, and cast pose
  semantics; fill missing state transitions without changing simulation timing.
- Down-convert new source terrain and character art through the generator, then
  measure palette, texture-memory, and legibility costs independently for each
  console profile.
- Add palette-safe weapon, projectile, spell, and recovery marks where the
  existing presentation stream provides enough timing information.
- Extend deterministic render or frame probes before accepting a visual change
  that is difficult to exercise in ordinary host tests.

Console work must not narrow the PC renderer, and PC assets must not silently
raise either console's memory or palette requirements.

## Known risks

- The current terrain and three imported character assets remain a
  deliberately compact art pack. The commander proves real rigging, materials,
  and GLB animation, but final roster silhouettes and motion still need an
  artist-quality pass.
- Godot presentation remains outside CTest, but the aggregate PC release gate
  now runs every bounded runtime smoke after the native CTest suite. CI still
  needs a pinned headless display and Godot cache before adopting that gate.
- Controller behavior has been exercised under Linux/Xvfb, not yet on the
  target SteamOS PC and its exact controllers.
- The installed game has UI and combat cues, but the current Xvfb demo recorder
  does not route system audio into its video capture.
- Model integration is bounded and optional, but a protected LAN endpoint still
  needs an external token and model selection supplied at runtime.
- The portable bundle is large because it carries the pinned Godot binary; a
  smaller export-template build can be evaluated after the PC launch loop is
  stable.
