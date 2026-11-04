# README screenshots

`native-guide/` belongs to [../CREATING_A_GAME.md](../CREATING_A_GAME.md). Its
screens are rendered by the real Godot creator under fresh per-capture user
data:

```sh
platform/godot/scripts/capture-creator-guide.sh
```

Pass `--player-shell path/to/pc-bundle` after building a release candidate to
also capture an enabled Export Center. `guide/` is the archived pre-0.3 creator
walkthrough and is not referenced by current user documentation. Everything
else here is the README's runtime evidence.

`art-roster/` belongs to [../ARTWORK.md](../ARTWORK.md). Its eight animated
previews are rebuilt directly from the committed Godot character sheets:

```sh
platform/godot/scripts/generate-art-roster.sh
```

Generated, never hand-taken:

```sh
scripts/readme-screenshots.sh          # all eight
scripts/readme-screenshots.sh editor   # or one: editor, terminal, n64,
                                       #         psx, autopilot, psx-turn,
                                       #         n64-showcase, psx-showcase
```

| File | Source |
|---|---|
| `native-guide/*.png` | Godot creator integration fixtures under Xvfb |
| `art-roster/*.gif` | the eight committed 4×2 Godot animation sheets |
| `editor.png` | archived pre-0.3 creator capture |
| `terminal.png` | the real terminal client's ANSI output, rendered by Chromium |
| `n64.png` | the render probe under ares, window chrome cropped |
| `psx.png` | the first frame the PlayStation turn check compared |
| `n64-autopilot.gif` | the console autopilot's whole run, a frame per checkpoint |
| `psx-turn.gif` | the PlayStation turn check's whole run, a frame per checkpoint |
| `n64-showcase.gif` | one stretch of that run, filmed frame by frame |
| `psx-showcase.gif` | one activation of that run, filmed frame by frame |

Each comes out of the check that already runs, so a picture cannot drift from
the run that produced it without the run changing too. Re-run after a visual
change and commit the result: a stale screenshot misleads more quietly than a
stale document.

The Nintendo 64 animations differ by a few frames between regenerations. ares
is a separate program on a wall clock and the only way to see what it drew is
to grab its display from outside, so how many frames a busy machine yields is
not fixed. The PlayStation's film is byte-identical, because its harness drives
the emulator a frame at a time in the same process that writes them.

Needs the repo-local Chromium (`CODING.md`, Reproducible toolchains), the host
build (`build/`), the ROMs (`build-n64/`), the ares container, and the
PCSX-Redux toolchain and emulator containers.
