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

## Imported humanoid and authored drone

The full-voxel profile binds an imported, skinned humanoid GLB for the commander
and a scene-authored drone without changing the Last Signal package. Both are
in-place hierarchical models with idle, run, attack, ability, hurt, and defeat
clips. The commander deliberately exports DCC-shaped names such as
`Relay_Run`, `Relay_Strike`, and `Relay_Channel`; its binding maps those names
to the semantic vocabulary explicitly. The drone still uses the common
`move -> run` mapping. Rook and both beast units use these replacements while
unbound archetypes keep the procedural voxel fallback.

`tools/export_relay_commander.py` constructs the source armature, rigid-weighted
mesh, materials, and actions inside Blender and exports the self-contained
`relay_commander.glb`. The character has segmented ceramic armour, a helmet and
signal visor, relay core, shield, sword, and cape. Its sixteen-bone humanoid
skeleton is intentionally reusable as a starting topology rather than a
runtime contract. Run `scripts/generate-imported-characters.sh` only when the
asset must be regenerated; Blender is an authoring dependency, not a player or
bundle dependency. The exporter omits unused texture coordinates and produces
byte-identical output for the same basename.

`scripts/smoke-animated-characters.sh` imports the GLB, instantiates both
characters, checks the complete clip vocabulary and explicit remap, and samples
skeleton locomotion, weapon, ability, rotor, core, and defeat motion. The
ordinary graphics capture additionally proves scale, contact overlays, facing,
simultaneous board motion, impacts, delayed removal, and coexistence with
fallback characters in the real campaign. The imported commander is a real
production-shaped pipeline milestone, but remains a deliberately compact
low-poly asset rather than the final roster quality bar.

The next fidelity gate is an imported drone from the same normal DCC-to-GLB
workflow, followed by a second humanoid that exercises skeleton reuse. The
complete 3D direction should still be judged against the hybrid at the intended
viewing distance before the rest of the roster is converted. Skeleton reuse is
an art-production advantage, not part of the gameplay or package contract.
