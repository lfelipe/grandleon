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

`native-guide/` belongs to [Creating a game](../CREATING_A_GAME.md). Its
screens are rendered by the real Godot creator under fresh per-capture user
data:

```sh
platform/godot/scripts/capture-creator-guide.sh
```

Pass `--player-shell path/to/pc-bundle` after building the PC bundle to also
capture an enabled Export Center.

## Artwork roster

`art-roster/` belongs to [Artwork](../ARTWORK.md). Its animated previews are
rebuilt directly from the committed Godot character sheets, including work in
progress on additional 0.4 styles:

```sh
platform/godot/scripts/generate-art-roster.sh
```

## Console and terminal captures

The remaining images come from the checks that exercise the matching runtime:

```sh
scripts/readme-screenshots.sh          # rebuild all eight
scripts/readme-screenshots.sh terminal # or one named capture
```

| File | Source |
|---|---|
| `godot/gameplay.gif` | close combat from a recorded run of the Godot guided demo |
| `godot/voxel-sprites.png` | the supported Godot presentation |
| `native-guide/*.png` | Godot creator integration fixtures under Xvfb |
| `art-roster/*.gif` | the committed 4×2 character sheets |
| `terminal.png` | the real terminal client's ANSI output, rendered by Chromium |
| `n64.png` | Emberbound's opening board under ares, with window chrome cropped |
| `psx.png` | the matching verified PlayStation board, cropped to remove its diagnostic backdrop |
| `n64-autopilot.gif` | the console autopilot run, one frame per checkpoint |
| `psx-turn.gif` | the PlayStation turn check, one frame per checkpoint |
| `n64-showcase.gif` | one stretch of the Nintendo 64 run |
| `psx-showcase.gif` | one activation of the PlayStation run |

Nintendo 64 animations can differ by a few frames between regenerations
because ares is captured from its wall-clock display. The PlayStation film is
byte-identical because its harness drives the emulator and writes each frame
in the same process.

See [CODING.md](../../CODING.md) for the reproducible toolchains needed by the
console captures.
