# Cross-renderer comparison capture

This is Godot's repeatable reference scene for comparing presentation work on
PC, Nintendo 64, and PlayStation. It uses the production Godot board, combat
planner, character library, camera flight, and close-view controller. Nothing
in the capture forecasts or changes combat state.

The machine-readable source of truth is
[`tools/renderer_comparison/scene.json`](../../tools/renderer_comparison/scene.json).
The recorder reads that file directly; the tables below are its human-readable
summary.

## Scene contract

The board is 6×4. `F` is forest (terrain 2), `G` is grass (terrain 9), and `S`
is paved stone (terrain 12). Coordinates start at `(0, 0)` in the upper-left.

```text
F F G S S F
F G G S S F
F G G S G F
F F G S F F
```

Automatic forest/grass/stone surroundings extend three cells beyond the map.
Only these two units are present:

| Unit | Portable fields | Start | Before | Accepted result |
|---|---|---:|---:|---:|
| Auren | medieval knight, male/figure 0, blue/colour 0, ID 10 | `(1, 2)` | 12/12 HP | countered for 4, 8/12 HP |
| Ilyra | medieval knight, female/figure 1, red/colour 1, ID 20 | `(4, 1)` | 11/11 HP | hit for 3, 8/11 HP |

Auren attacks Ilyra first. Ilyra's legal counter is the second accepted event.
The close view keeps Auren on the left and Ilyra on the right; the counter
travels right-to-left without swapping their positions. The selected blue
unit and red target cell remain visible on the opening board. The graphics
profile is **Voxel + Sprites**, cinematic motion is **Full**, and camera impulse
is enabled.

## Timing contract

The recorder advances at a fixed 30 frames per second. The expected movie is
about 8.17 seconds (245 frames); the named entry point rejects a result outside
0.05 seconds of the canonical 8.15-second duration.

| Time | Landmark |
|---:|---|
| `0.00–1.15` | Tactical board hold |
| `1.15–2.25` | Real 3D camera flies toward and reframes the pair |
| `2.25–4.30` | Auren's left-to-right attack and `−3` result |
| `4.30–6.35` | Ilyra's right-to-left counter and `−4` result |
| `6.35–7.30` | Close view dissolves; board camera reverses and restores |
| `7.30–8.15` | Settled board hold |

The extracted comparison stills are deliberately inside stable visual moments:

| File | Time | What it proves |
|---|---:|---|
| `board.png` | `0.50` | Starting units, terrain, selection, and camera |
| `focus.png` | `1.70` | Actual board-camera move rather than an overlay cut |
| `attack_impact.png` | `3.45` | Male attack, female reaction, and right-side damage |
| `counter_impact.png` | `5.50` | Stable sides and right-to-left counter |
| `restored.png` | `7.55` | Final HP state and exact tactical-camera return |

## Record it

From the repository root:

```sh
platform/godot/scripts/record-cross-renderer-comparison.sh
```

The default outputs are:

- `out/renderer-comparison/godot.mp4`, H.264 at 1280×720 and 30 fps; and
- `out/renderer-comparison/godot-frames/*.png`, the five landmarks above.

Custom build, movie, and frame locations are positional arguments:

```sh
platform/godot/scripts/record-cross-renderer-comparison.sh \
  build-godot /tmp/godot-comparison.mp4 /tmp/godot-comparison-frames
```

The Godot adapter lives in `tests/combat_cinematic_capture.gd`. It loads units,
cells, terrain, HP, events, and hold durations from the canonical JSON. The
script reuses `record-combat-cinematic.sh`, reads landmark names and times from
that same contract, verifies codec, resolution, frame rate, and duration, then
extracts the stills. The simulation inputs and rendered timeline are
deterministic. Exact compressed bytes can still differ between FFmpeg versions,
so review the named frames and motion instead of treating the MP4 hash as a
compatibility contract.
