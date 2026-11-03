# 3D character animation path

The selected first PC profile is voxel terrain with animated 2D
characters. It is deliberately compatible with a later authored 3D roster; it
does not make sprite movement the permanent presentation contract.

## Asset contract

`content/graphics_profiles.json` may bind a character archetype to an imported
Godot or glTF scene:

```json
{
  "assets": {
    "characterScenes": {
      "rogue": {
        "scene": "res://art/characters/rogue.glb",
        "scale": 1.0,
        "animationPlayer": "AnimationPlayer",
        "clips": {
          "idle": "Rogue_Idle",
          "move": "Rogue_Run",
          "attack": "Rogue_Attack"
        }
      }
    }
  }
}
```

The semantic clip vocabulary is `idle`, `move`, `attack`, `ability`, `hurt`,
and `defeat`. A binding may override any clip name because DCC tools and art
packs need not use the engine's spelling. Missing scenes, players, or clips
fall back safely instead of invalidating the game package.

The board already plays `move` while an accepted position change is being
interpolated, rotates the model toward travel, and returns to `idle` at the
destination. Simulation coordinates remain authoritative; imported root motion
must not decide where a unit ends.

## Implemented presentation stream

The native session bridge now forwards every accepted simulation result as a
semantic `presentation` event before the resulting snapshot. The renderer uses
those events without changing or delaying the deterministic encounter:

| Event | Presentation data | Animation use |
|---|---|---|
| `unit_moved` plus snapshot | unit, legal route, recommended duration, and authoritative destination | route-aware run/walk, facing, elevation, and overlapping per-unit interpolation |
| `unit_damaged` / `attack_missed` | target, actor, damage result, originating action | attack or cast clips, melee slash or ranged projectile, impact flash, short camera impulse |
| `unit_restored` / `item_used` | target, actor, result, originating action | ability clip, travelling magic and recovery aura |
| `unit_talked` / `unit_defeated` | departing unit and actor | semantic clip and delayed removal |

The bridge emits stable IDs and simulation semantics, never Godot animation
names. The graphics profile maps those semantics to clips. An absent scene,
player, or clip falls back immediately, so animation can never block the
encounter thread.

The presentation event also carries the accepted action category and content
identity. This is deliberately outside the deterministic simulation state: it
lets every renderer tell a sword swing, projectile, spell, and item aura apart
without guessing from damage numbers. Built-in effects are procedural and the
`effects` block in a graphics profile can replace their colours and billboard
textures independently for a local art pack. Missing effect textures fall back
one by one to procedural marks. Imported character scenes still replace the
matching semantic attack or ability clip through the existing clip map.

The sprite fallback follows the same semantic states. Its generated manifest
publishes the standing image and frame-sheet asset kinds, per-sheet cell size
and column count, a normalised ground pivot, frame indices, loop policy, and
frames per second. The 2D and 3D renderers therefore do not assume 32-pixel
cells or four animation frames. Idle, walk, attack, and cast poses are selected
by movement and presentation events rather than looping the complete sheet.
Directional sprites mirror relative to the camera while travelling and face
their target before attack or ability presentation. A replacement binding can
declare whether its native art faces right and can disable mirroring for a
front-facing or intentionally asymmetric sheet. Procedural and imported 3D
figures use the same target-facing event without changing animation assets.
Invalid or absent animation metadata falls back to the shipped layout without
changing simulation behavior.

An accepted move now carries an optional motion record. The native bridge uses
the same portable route planner as the Nintendo 64 client over the pre-command
snapshot: engine-reachable cells plus friendly occupied cells that may be
crossed but not used as a destination. The route contains each cell after the
origin and ends at the simulation event's destination. Its recommended duration
uses the shared six frames per tile at 60 Hz, or 100 ms per cell.
An art profile may tune that recommendation with its bounded `moveSpeed` value
to fit an imported rig's gait without changing the portable route record.

Both Godot renderers validate adjacency, endpoint, and duration before using a
record. A malformed or absent record falls back as one straight presentation
leg; it never changes the snapshot or blocks a command. Independent per-unit
samplers let two co-op moves overlap. The 3D sampler follows every corner,
turns the scene toward the current leg, and interpolates authored surface
heights while the 2D sampler uses the same cell path for sprite animation.
Imported root motion remains in-place decoration: the settled position is
always the destination in the subsequent authoritative snapshot.

## Imported roster and shared humanoid topology

The full-voxel profile binds imported, skinned GLBs for the commander, rogue,
stormcaller, and drone without changing the Last Signal package. Each is an
in-place hierarchical model with idle, run, attack, ability, hurt, and defeat
clips. The commander exports names such as `Relay_Run`, the rogue uses
`Kestrel_Sprint` and `Kestrel_Crosscut`, the stormcaller uses `Morrow_Glaive`
and `Morrow_Arc`, and the drone uses `Drone_Hover` and `Drone_Scamper`. Each
binding maps its exported names to the semantic vocabulary explicitly. Every
unit in The Last Signal now uses an imported replacement; other unbound
archetypes keep the procedural voxel fallback.

`tools/export_relay_commander.py`, `tools/export_relay_rogue.py`,
`tools/export_relay_stormcaller.py`, and `tools/export_relay_drone.py` construct
rigid-weighted meshes, materials, and actions inside Blender and export
self-contained GLBs. The commander has
segmented ceramic armour, a helmet and signal visor, relay core, shield, sword,
and cape. The rogue has a hood, split visor, veil, dual signal knives, and a
separate action set. The stormcaller has a cowl, mask, mantle, signal glaive,
arc focus, and another separate action set. All three call the same
humanoid-armature constructor, producing the exact same sixteen bone names,
parents, and rest transforms. The drone has articulated legs and jaw, twin
rotors, a relay core, and a fifteen-bone quadruped skeleton.
Shared primitives and export rules live in `tools/blender_character_tools.py`;
the skeletons themselves remain art choices rather than runtime contracts. Run
`scripts/generate-imported-characters.sh` only when the assets must be
regenerated. Blender is an authoring dependency, not a player or bundle
dependency. All exporters omit unused texture coordinates and produce
byte-identical output for the same basename.

`scripts/smoke-animated-characters.sh` imports all four GLBs, checks the
complete clip vocabulary and explicit remaps, compares every shared humanoid
bone name, parent, and rest transform, and samples skeleton locomotion, weapons,
abilities, rotors, cores, and defeat motion. The ordinary graphics capture additionally
proves scale, contact overlays, facing, simultaneous board motion, impacts,
delayed removal, and the complete imported roster in the real campaign.
The imported set is a real production-shaped pipeline milestone, but remains a
deliberately compact low-poly set rather than the final roster quality bar.

The complete 3D direction should now be judged against the hybrid at the
intended viewing distance before the rest of the roster is converted. Skeleton
reuse is an art-production advantage, not part of the gameplay or package
contract.
