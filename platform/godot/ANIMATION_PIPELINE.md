# 3D character animation path

The selected first cabinet profile is voxel terrain with animated 2D
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
| `unit_moved` plus snapshot | unit and authoritative destination | run/walk, facing, overlapping per-unit interpolation |
| `unit_damaged` / `attack_missed` | target, actor, damage result | attack and hurt clips, impact flash, short camera impulse |
| `unit_restored` / `item_used` | target, actor, result | ability clip and recovery effect |
| `unit_talked` / `unit_defeated` | departing unit and actor | semantic clip and delayed removal |

The bridge emits stable IDs and simulation semantics, never Godot animation
names. The graphics profile maps those semantics to clips. An absent scene,
player, or clip falls back immediately, so animation can never block the
encounter thread.

The next fidelity step is an optional motion presentation record with a path
and recommended duration. Destination snapshots remain authoritative, but that
record would let authored rigs time footsteps and turns to a multi-cell route
instead of inferring one straight interpolation.

## First authored 3D spike

Use one humanoid and one drone rather than converting the whole roster:

1. Export glTF/GLB with a shared humanoid skeleton and in-place idle, walk,
   run, attack, hurt, and defeat clips.
2. Bind Kestrel's `rogue` archetype and one `beast` drone in a local art pack.
3. Verify direction changes, simultaneous movement, shadow contact, and scale
   at the fixed cabinet camera.
4. Tune semantic clip mappings and validate attack/impact alignment.
5. Only then decide whether to author the full 3D roster or retain the hybrid
   as a deliberate visual style.
