# PC arcade review and roadmap

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

The branch is ready for a first cabinet validation, not for a content-complete
release. The portable bundle includes pinned Godot, compiled demo content, the
native extension, imported assets, and a two-player launcher. It contains no
machine-specific model or graphics configuration.

## What is already proven

| Area | Current evidence |
|---|---|
| Portable rules | Godot submits intents to the same C++ session used by the other clients; `.gpk` content does not name Godot resources. |
| Simultaneous co-op control | Both players have independent cursors and request queues. A smoke mode requires two friendly moves submitted together to be accepted before it captures a frame. |
| Deterministic conflict handling | Commands overlap visually but serialize at the simulation boundary; a stale or conflicting command is refused instead of mutating state out of order. |
| Offline campaign | The Last Signal has authored opening, terms, talk outcome, battle outcome, and ending dialogue with the model disabled. |
| Optional model dialogue | Model lines append only to allowlisted scenes and pass speaker, count, length, and markup validation. Authored lines are never replaced. |
| Optional model tactics | The model selects an index from legal engine-generated candidates. Candidate zero is the built-in plan and remains the fallback for every failure mode. |
| Replaceable art | Terrain and character scene bindings accept `.tscn` or `.glb` assets per archetype. Missing bindings or clips fall back asset by asset. |
| Combat presentation | Native result events drive semantic attack, ability, hurt, defeat, impact, and delayed-removal presentation without blocking simulation. |
| Cabinet-safe exit | Start or Back opens a controller-native session menu; quitting requires an explicit second X press, while A, B, or Start resumes. |
| SteamOS packaging | The release bundle runs without a system Godot install and keeps its writable state under its own `runtime/` directory. |
| Privacy | The bundler rejects exact checkout paths and local override files. Endpoint, model, token environment name, and art overrides live only in ignored runtime files. |

## Tonight's cabinet test

Test the default `voxel-sprites` profile first, then compare `diorama` and
`voxel` only after the input loop is comfortable.

1. Add `launch-arcade.sh` as a non-Steam game and start it from Gaming Mode.
2. Confirm 1280x720 and the cabinet's native mode both frame the board and side
   panel without clipping.
3. Connect two controllers before launch, then repeat with Player 2 joining
   after launch and after a disconnect/reconnect.
4. Move both friendly characters at once. Check that their animations overlap,
   each cursor remains responsive, and a destination conflict produces a clear
   refusal rather than a stuck unit.
5. Play once with no `arcade.local.json`. The complete authored story and
   deterministic enemy must remain available.
6. If a local model is configured, unplug or stop it mid-request. The status
   should report fallback and the encounter should continue with candidate
   zero.
7. Watch contact shadows, sprite scale, health bars, text size, dark-detail
   visibility, and camera impulse at the actual viewing distance.
8. Open the session menu with Start and Back. Resume with A and B, then return
   to Steam with X and confirm the launcher regains focus.
9. Leave the game running through several rounds and check frame pacing,
   controller focus, heat, and memory on the cabinet hardware.

Record the GPU, display mode, controller models, frame-pacing observations, and
which graphics profile is preferred. Do not record endpoint names, tokens, LAN
addresses, usernames, or absolute install paths in project content or issues.

## Next milestones

### 1. Close the cabinet loop

- Fix any Gaming Mode focus, scaling, controller identity, or reconnect issue
  found tonight.
- Extend the controller-driven session menu with graphics profile, text scale,
  camera impulse, and audio-level settings.
- Add explicit join/leave feedback and persistent controller glyphs rather
  than generic button letters.
- Add an automated Godot smoke target to CI once a pinned headless display and
  Godot cache are practical there.

Exit criterion: two people can launch, join, finish Last Signal, reconnect a
controller, and quit without keyboard intervention.

### 2. Replace blockout terrain incrementally

- Author one cohesive Halcyon Relay terrain pack: ground slab, water, bridge,
  tree, mountain edge, ruin, and relay beacon.
- Bind those scenes through `graphics_profiles.json`; do not add renderer-side
  filename rules.
- Move sprite cell size, frame count, pivot, and timing into the manifest
  before accepting a higher-resolution character pack.
- Measure the authored pack under the compatibility renderer on the cabinet
  before adding MultiMesh batching. These boards are small enough that art
  direction and replaceability matter first.

Exit criterion: the selected profile uses authored terrain with no per-project
Godot paths and still falls back cleanly when one asset is removed.

### 3. Prove one production 3D animation path

- Import one humanoid and one drone with in-place idle, walk/run, attack, hurt,
  and defeat clips.
- Map semantic clips in the profile and validate facing, scale, shadow contact,
  simultaneous locomotion, impacts, and removal timing.
- Add an optional motion record with route and recommended duration so a rig
  can time multi-cell locomotion without letting root motion own game state.
- Decide on a full 3D roster only after this spike is visibly better than the
  hybrid at cabinet distance.

Exit criterion: the authored models can replace two archetypes without a rule,
campaign, or package change and without degrading the sprite fallback.

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

- Add music, action/UI audio, and per-player feedback with independent volume
  controls.
- Persist campaign progress and user settings under the bundle runtime path,
  with explicit reset and migration behavior.
- Add attract mode, clean return-to-launcher behavior, and cabinet-safe idle
  handling.
- Review text scaling, colour contrast, camera impulse reduction, and input
  remapping as accessibility features rather than post-release polish.

Exit criterion: the PC client is a maintainable product surface rather than a
developer presentation harness, while the portable engine remains the source
of gameplay truth.

## Known risks

- The current terrain and voxel characters are deliberate blockouts; lighting
  cannot substitute for authored silhouettes, materials, and animation.
- Godot presentation has runtime smoke coverage but is not yet a first-class
  CTest target.
- Controller behavior has been exercised under Linux/Xvfb, not yet on the
  target SteamOS cabinet and its exact controllers.
- The current demo recorder is silent because game audio has not been authored.
- Model integration is bounded and optional, but a protected LAN endpoint still
  needs an external token and model selection supplied at runtime.
- The portable bundle is large because it carries the pinned Godot binary; a
  smaller export-template build can be evaluated after the cabinet loop is
  stable.
