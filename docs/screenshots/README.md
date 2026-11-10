# Documentation screenshots

Every image in this directory comes from a runnable project build or a
committed source asset. Rebuild the relevant capture after a visual change so
the documentation continues to show what people will actually use.

## Godot application

`godot/` contains the gameplay animation and Voxel + Sprites image used by the
main README. Rebuild both with:

```sh
platform/godot/scripts/capture-readme-media.sh
```

The script builds the Godot project, runs the real Emberbound package with
isolated user data, captures the supported presentation, records the guided
demo through its close-combat turns, and creates the compact attack-heavy
`gameplay.gif` used on GitHub. Internal renderer captures remain available
through `capture-graphics-prototypes.sh`.

The standalone combat-cinematic review clip is recorded from the same
production controller, without capturing the desktop or Godot boot screen:

```sh
platform/godot/scripts/record-combat-cinematic.sh \
  build-godot out/godot-combat-cinematic.mp4
```

`native-guide/` belongs to [Creating a game](../CREATING_A_GAME.md). Its six
short GIFs are built from real creator interactions under fresh per-capture
user data:

```sh
platform/godot/scripts/capture-creator-guide.sh
```

Pass `--player-shell path/to/pc-bundle` after building the PC bundle to show an
enabled Linux target in the final Export Center frame.

## Artwork roster

`art-roster/` belongs to [Artwork](../ARTWORK.md). Its animated previews and
the root README's compact theme-and-role sampler are rebuilt directly from the
committed Godot character sheets:

```sh
platform/godot/scripts/generate-art-roster.sh
```

## Console and terminal captures

The README currently shows independent Bell Orchard clips from the Pyrite
and Mipsync players. The Nintendo 64 clip uses the solo formation; PlayStation
uses co-op. They are real emulator output, but do not form a synchronized
comparison. The older renderer comparison has been removed.

For a future matched capture, use the scene and timing contract in
[renderer_comparison](../../tools/renderer_comparison/README.md), record all
three current players, then compose them with its helper. Do not retime
unrelated campaign footage to imply a shared scene.

| File | Source |
|---|---|
| `godot/gameplay.gif` | close combat from a recorded run of the Godot guided demo |
| `godot/voxel-sprites.png` | the supported Godot presentation |
| `native-guide/*.gif` | the visual creator walkthrough, captured under Xvfb |
| `art-roster/*.gif` | the committed 4×2 character sheets |
| `art-roster/readme-art-sampler.png` | a compact sample of the seven themes and eight roles |
| `nintendo64-console-renderer.gif` | Bell Orchard solo movement and close combat in the current Pyrite/Tiny3D player, captured under ares |
| `playstation-console-renderer.gif` | Bell Orchard co-op movement, attack and counterattack in the current Mipsync player, captured under PCSX-Redux |

See [CODING.md](../../CODING.md) for the reproducible toolchains needed by the
console captures.
