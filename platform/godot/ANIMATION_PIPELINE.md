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

## Presentation events to add after cabinet validation

Movement can be inferred from snapshots, but good combat animation needs an
explicit presentation stream. Extend the native session bridge with events
that describe accepted simulation results without changing them:

| Event | Presentation data | Animation use |
|---|---|---|
| `motion` | unit, path, duration | walk/run, facing, footstep timing |
| `action` | actor, semantic verb, target, wind-up | attack or ability clip |
| `impact` | target, damage/heal result | hit reaction, particles, camera impulse |
| `defeat` | unit, removal timing | defeat clip before hiding the model |

The bridge should emit stable IDs and semantic verbs, never Godot animation
names. The graphics profile maps those semantics to clips. A timeout or absent
clip must advance presentation immediately, because animation can never block
the deterministic encounter thread.

## First authored 3D spike

Use one humanoid and one drone rather than converting the whole roster:

1. Export glTF/GLB with a shared humanoid skeleton and in-place idle, walk,
   run, attack, hurt, and defeat clips.
2. Bind Kestrel's `rogue` archetype and one `beast` drone in a local art pack.
3. Verify direction changes, simultaneous movement, shadow contact, and scale
   at the fixed cabinet camera.
4. Add the presentation events above, then validate attack/impact alignment.
5. Only then decide whether to author the full 3D roster or retain the hybrid
   as a deliberate visual style.
