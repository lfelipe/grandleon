#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Regenerates the README's screenshots into docs/screenshots/.
#
#   scripts/readme-screenshots.sh            all eight
#   scripts/readme-screenshots.sh editor     just one surface
#
# Surfaces: editor, terminal, n64, psx, autopilot, psx-turn, n64-showcase,
#           psx-showcase.
#
# Everything is driven, nothing is hand-cropped: the editor shot comes from
# Chromium against a production build, the terminal shot from the real client's
# ANSI output rendered by Chromium, the Nintendo 64 shot from the render probe
# running under ares, and the PlayStation shot from the first frame its turn
# check compared, which needs no crop at all because
# `PCSX.GPU.takeScreenShot()` hands over the 320x240 active display and nothing
# else. Re-run after a visual change and commit the PNGs.
#
# Two cameras, and the difference matters. The two *trail* animations are
# built from checkpoints, which are settled frames by construction: the
# harnesses photograph the board at the moment the run has something to assert
# about it, so nothing in flight can make an assertion lucky. That is the right
# rule for evidence and the wrong one for showing a person what the machine
# does: a walk cycle, a lunge, a flinch and a shimmering river are exactly the
# things a settled frame is designed not to catch. The two *showcase*
# animations are the same runs filmed frame by frame instead, over a window
# named in the run's own checkpoints. All of them still have to pass: a film is
# only ever made of a run that reported a pass in the same process that took the
# pictures.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${repository_root}/docs/screenshots"
mkdir -p "${out}"
export PLAYWRIGHT_BROWSERS_PATH="${repository_root}/.playwright-browsers"
# The three PlayStation arms decode raw 15-bit frames, and share one decoder
# rather than carrying three copies of the widening rule.
export PYTHONPATH="${repository_root}/scripts${PYTHONPATH:+:${PYTHONPATH}}"
# The autopilot build's log, because a screenshot needs a run that played
# itself: `run-playstation-turn.sh` names the log after the executable it boots.
playstation_log="${repository_root}/build-playstation/turn-grandleon_psx_turn_autopilot.log"

want() {
    [ "$#" -eq 1 ] || return 0
    [ "$1" = "${surface}" ]
}

surface="${1:-all}"

if [ "${surface}" = "all" ] || [ "${surface}" = "editor" ]; then
    echo "== editor"
    (cd "${repository_root}/editor" && npx vite build >/dev/null 2>&1)
    node "${repository_root}/scripts/readme-shot-editor.mjs" \
        "${out}/editor.png"
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "terminal" ]; then
    echo "== terminal"
    "${repository_root}/scripts/readme-shot-terminal.sh" "${out}/terminal.png"
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "n64" ]; then
    echo "== n64"
    "${repository_root}/platform/nintendo64/ares/run-ares.sh" \
        "${repository_root}/build-n64/platform/nintendo64/grandleon_n64_probe.z64" \
        >/dev/null
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - "$@" <<'PYEOF'
import sys
from PIL import Image
image = Image.open("build-n64/ares/screenshot-probe.png")
# Trim the ares window chrome: menu bar above, status bar below, desktop
# margins either side. The emulator viewport is what the README wants.
image.crop((108, 82, 876, 656)).save("docs/screenshots/n64.png")
PYEOF
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "psx" ]; then
    echo "== psx"
    # The board this console opens on, which is the first frame the turn check
    # compared: the emulator's own capture of the composited display at the
    # instant the executable named, joined against the executable's per-cell
    # claims and the GPU's readback before this file existed. Rebuilding the
    # check is what refreshes the shot, exactly as on the other two consoles.
    #
    # No crop. `PCSX.GPU.takeScreenShot()` crops VRAM to the active display, so
    # a pixel here is a pixel the renderer computed, including the magenta the
    # board is standing on, which is the backdrop `turn_exe.cpp` picked because
    # nothing the art can draw is that colour. It is on the page because it is
    # on the screen.
    cmake --build "${repository_root}/build" \
        --target grandleon_playstation_turn_check >/dev/null
    GRANDLEON_PSX_LOG="${playstation_log}" \
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - <<'PYEOF'
import os

import readme_psx_frames as psx

log = os.environ["GRANDLEON_PSX_LOG"]
width, height = psx.frame_size(log)
frame = psx.load("build-playstation/turn-frames/frame-0000.bin", width, height)
frame.save("docs/screenshots/psx.png", optimize=True)
print(f"  {width}x{height}, checkpoint {psx.checkpoint_names(log)[0]}")
PYEOF
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "autopilot" ]; then
    echo "== autopilot"
    # The console autopilot already drives the real interactive code paths and
    # photographs itself at every checkpoint, so the animation is the test's own
    # evidence rather than a recording made beside it. Rebuilding the check is
    # what refreshes the trail.
    cmake --build "${repository_root}/build" \
        --target grandleon_n64_autopilot_check >/dev/null
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - <<'PYEOF'
import glob
import os
from PIL import Image

paths = sorted(glob.glob("build-n64/ares/trail-autopilot/*.png"))
if not paths:
    raise SystemExit("no autopilot trail to animate")

frames = []
holds = []
for path in paths:
    # The same crop the still uses: ares window chrome off, viewport kept.
    frames.append(
        Image.open(path).convert("RGB").crop((108, 82, 876, 656))
        .convert("P", palette=Image.ADAPTIVE, colors=256)
    )
    name = os.path.basename(path)
    # A title card and a killing blow are worth pausing on; a forecast is worth
    # reading, and a whole information sheet is worth reading twice as long as
    # a forecast, because it is ten lines of numbers rather than one.
    # Everything else is a beat, except the draught the run ends on, which is
    # the last thing on screen and wants the same pause a title card gets.
    if "info-sheet" in name:
        holds.append(3000)
    elif "title" in name or "after-ability" in name or "after-item" in name:
        holds.append(2000)
    elif "forecast" in name or "controls" in name:
        holds.append(1500)
    else:
        holds.append(1100)

frames[0].save(
    "docs/screenshots/n64-autopilot.gif",
    save_all=True,
    append_images=frames[1:],
    duration=holds,
    loop=0,
    optimize=True,
    disposal=2,
)
print(f"  {len(frames)} frames, {sum(holds) / 1000:.1f}s")
PYEOF
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "psx-turn" ]; then
    echo "== psx-turn"
    # The same idea again, on a third console: every checkpoint the
    # PlayStation turn check compared, in the order the console reached them.
    # These frames are the check's own evidence, each joined against the
    # executable's per-cell claims and the GPU's readback, so this animation
    # cannot drift from the run that produced it without the run changing too.
    #
    # The pacing is keyed off the checkpoint names read out of the run's log,
    # not off an ordering written down here, and it is deliberately the Mega
    # Drive trail's pacing: the two consoles play the same script over the same
    # board through the same client, so a reader watching both should be
    # watching them at the same speed.
    cmake --build "${repository_root}/build" \
        --target grandleon_playstation_turn_check >/dev/null
    GRANDLEON_PSX_LOG="${playstation_log}" \
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - <<'PYEOF'
import os

from PIL import Image

import readme_psx_frames as psx

log = os.environ["GRANDLEON_PSX_LOG"]
width, height = psx.frame_size(log)
frames = psx.frames_in("build-playstation/turn-frames", width, height)
names = psx.checkpoint_names(log)
if len(names) != len(frames):
    raise SystemExit(
        f"{len(frames)} frames but {len(names)} checkpoints; the run and its "
        "log disagree"
    )

holds = []
for name in names:
    # A sheet is all of a character's numbers and has to be read; a menu row is
    # a decision; a cursor step is a beat. `autopilot` above holds the same
    # kinds of screen for the same lengths.
    if "sheet" in name:
        holds.append(1600)
    elif "menu" in name:
        holds.append(470)
    elif "select" in name or "refused" in name or "open" in name:
        holds.append(650)
    elif "hover" in name:
        holds.append(190)
    else:
        holds.append(390)

quantised = [
    frame.convert("P", palette=Image.ADAPTIVE, colors=256) for frame in frames
]
quantised[0].save(
    "docs/screenshots/psx-turn.gif",
    save_all=True,
    append_images=quantised[1:],
    duration=holds,
    loop=0,
    optimize=True,
    disposal=2,
)
print(f"  {len(frames)} frames, {sum(holds) / 1000:.1f}s")
PYEOF
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "n64-showcase" ]; then
    echo "== n64-showcase"
    # The autopilot again, filmed rather than photographed. It reaches the
    # archer's forecast, and from there twenty seconds of the display are
    # grabbed by six loops at once: the shot lands with a lunge, a flash and a
    # flinch, red answers, and the mage is picked up and walked down the ford's
    # southern road cell by cell. The river shimmers underneath the whole of
    # it, because the rotation runs off the board's own frame counter and the
    # autopilot's waits leave the board alone long enough for it to turn.
    cmake --build "${repository_root}/build" \
        --target grandleon_n64_showcase >/dev/null
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - <<'PYEOF'
import glob
import hashlib
from PIL import Image

paths = sorted(glob.glob("build-n64/ares/showcase/*.png"))
if not paths:
    raise SystemExit("no Nintendo 64 film to animate")

# The grabs are named by the nanosecond they were taken, so sorting them is
# putting them back in the order they happened. Several loops grab at once and
# ares presents far more slowly than they sample, so most grabs are repeats of
# the one before; what is left after the repeats is, frame for frame, what the
# emulator put on the screen. The same crop the still uses.
frames = []
repeats = []
previous = None
for path in paths:
    image = Image.open(path).convert("RGB").crop((108, 82, 876, 656))
    digest = hashlib.sha256(image.tobytes()).digest()
    if digest == previous:
        repeats[-1] += 1
        continue
    previous = digest
    frames.append(image)
    repeats.append(1)

# Two holds and no arithmetic on the clock. How many times a grabber caught the
# same picture is a fact about how busy this machine was, not about the ROM, so
# it decides only *which* of two holds a frame gets and never how long one is:
# the counts come out sharply bimodal, one to eight grabs while something is
# moving and fifteen to a hundred while the console is between gestures, so the
# classification is stable even though the counts are not.
durations = [200 if n > 10 else 65 for n in repeats]

# One palette for the whole film, sampled across it, so every frame quantises
# into the same table and the encoder can write only the part that moved. With
# a palette per frame the same animation costs seven times the bytes.
sample = Image.new("RGB", (768, 574 * 8))
for i, frame in enumerate(frames[:: max(1, len(frames) // 8)][:8]):
    sample.paste(frame, (0, i * 574))
master = sample.quantize(colors=256, method=Image.MEDIANCUT)
quantised = [f.quantize(palette=master, dither=Image.NONE) for f in frames]

quantised[0].save(
    "docs/screenshots/n64-showcase.gif",
    save_all=True,
    append_images=quantised[1:],
    duration=durations,
    loop=0,
    optimize=True,
    disposal=1,
)
print(f"  {len(frames)} frames of {len(paths)} grabs, "
      f"{sum(durations) / 1000:.1f}s")
PYEOF
fi

if [ "${surface}" = "all" ] || [ "${surface}" = "psx-showcase" ]; then
    echo "== psx-showcase"
    # The PlayStation turn run, filmed over checkpoints 22 to 30: the archer's
    # whole activation. It is picked up, the board lights every tile it can
    # reach, the row of things it could do opens, and it walks the bank. Every
    # one of those is a movement, which is the reason to film rather than
    # sample: the reach spreading and the figure crossing are what a settled
    # frame is designed not to catch.
    #
    # The window is set in `cmake/GrandleonPlayStation.cmake` and asserted below
    # by the names of the checkpoints it actually filmed, because a window of
    # numbers goes quietly wrong the moment a moment is added or removed.
    #
    # `grandleon_playstation_showcase` is the turn check's own run with the
    # observer's second camera switched on, so the joiner still compares every
    # transcript line and every claimed pixel: a film is only ever made of a run
    # that passed in the same process that took the pictures.
    cmake --build "${repository_root}/build" \
        --target grandleon_playstation_showcase >/dev/null
    GRANDLEON_PSX_LOG="${playstation_log}" \
    "${repository_root}/tools/placeholder_art/.venv/bin/python" - <<'PYEOF'
import glob
import hashlib
import os

from PIL import Image

import readme_psx_frames as psx

log = os.environ["GRANDLEON_PSX_LOG"]
width, height = psx.frame_size(log)
paths = sorted(glob.glob("build-playstation/showcase/film-*.bin"))
if not paths:
    raise SystemExit("no PlayStation film to animate")

# The film is a window of checkpoint *numbers*, and checkpoint numbers move: any
# change that adds or removes a moment renumbers everything after it. Left
# unchecked, the window would go on filming nine frames and every sentence about
# what they show would stay confidently wrong. The animation itself gives no
# sign, because nine frames of something else look exactly as convincing.
#
# So the window states what it is, and the run has to still agree. These names
# are the archer's activation: picked up, the board lighting what it can reach,
# the row of things it could do, and the walk landing.
expected_window = [
    "24-select", "25-menu", "26-menu", "27-aiming", "28-menu",
    "29-menu", "30-menu", "31-menu", "32-acted"
]
filmed_window = psx.checkpoint_names(log)[23:32]
if filmed_window != expected_window:
    raise SystemExit(
        "the filmed window is not the archer's activation any more.\n"
        f"  expected {expected_window}\n"
        f"  found    {filmed_window}\n"
        "Checkpoints have been renumbered. Re-choose the window from the run's\n"
        "own checkpoint list, set GRANDLEON_PLAYSTATION_FILM_FROM/TO in\n"
        "cmake/GrandleonPlayStation.cmake, and rewrite what this film is said\n"
        "to show there, here, and in docs/screenshots/README.md."
    )

# No crop: `PCSX.GPU.takeScreenShot()` hands over the active display and nothing
# else. Every file here is one console frame, in order: the observer writes one
# per vertical retrace, which on a single-buffered machine is exactly where a
# picture is finished.
frames = []
held = []
previous = None
for path in paths:
    image = psx.load(path, width, height)
    digest = hashlib.sha256(image.tobytes()).digest()
    if digest == previous:
        held[-1] += 1
        continue
    previous = digest
    frames.append(image)
    held.append(1)

# A frame the console held for n frames is held for n sixtieths of a second,
# because that is what happened: the observer counts retraces, not wall clock.
# Floored and capped for two reasons: a gesture the console plays in a fifth of
# a second has to stay legible, and the cursor's pulse leaves the picture
# untouched for a third of a second at a time between its two phases.
durations = [max(60, min(300, round(n * 1000 / 60))) for n in held]

sample = Image.new("RGB", (width, height * 8))
for i, frame in enumerate(frames[:: max(1, len(frames) // 8)][:8]):
    sample.paste(frame, (0, i * height))
master = sample.quantize(colors=256, method=Image.MEDIANCUT)
quantised = [f.quantize(palette=master, dither=Image.NONE) for f in frames]

quantised[0].save(
    "docs/screenshots/psx-showcase.gif",
    save_all=True,
    append_images=quantised[1:],
    duration=durations,
    loop=0,
    optimize=True,
    disposal=1,
)
print(f"  {len(frames)} frames of {len(paths)} console frames, "
      f"{sum(durations) / 1000:.1f}s")
PYEOF
fi

echo "Wrote ${out}"
