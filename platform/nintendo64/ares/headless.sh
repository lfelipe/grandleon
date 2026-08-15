#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs inside the grandleon/n64-ares container. Do not run on the host.
#
# Starts a virtual display, boots one ROM in ares, waits for the ROM's
# "RESULT" line on the ISViewer channel, captures a screenshot of the ares
# window, and exits 0 only if the result was a pass. run-ares.sh invokes this
# under `timeout`, and every wait below is bounded, so it cannot hang either
# way.
#
# Expects:
#   $1                 ROM path (read-only is fine; saves are diverted)
#   /out               writable directory for the log, the screenshot and the
#                      checkpoint trail, all named by the caller
#   ARES_RUN_SECONDS   deadline for the ROM to produce its RESULT line
#   ARES_SAVES_DIR     where ares keeps cartridge saves. The default is a
#                      throwaway inside the container, which is what every
#                      check that does not save wants. The persistence check
#                      points it at a mounted directory instead, so the
#                      cartridge outlives the emulator process, which is the
#                      whole of what that check is testing.
#   ARES_LOG_NAME      the log's name under /out, so two runs of one ROM do
#                      not overwrite each other's evidence.
#   ARES_TRAIL_NAME    likewise for the checkpoint screenshot trail.
#   ARES_LINGER_SECONDS
#                      how long to keep the emulator running after the ROM has
#                      reported. Zero, the default, kills it at once, which is
#                      what every check that does not save wants. The
#                      persistence check lingers, because ares flushes
#                      cartridge save memory on a timer and killing it a
#                      second after the verdict leaves a cartridge that was
#                      written perfectly and saved nowhere.
#   ARES_FILM_AFTER    the name of a checkpoint. Once the ROM has printed it,
#                      the display is grabbed in a tight loop instead of once a
#                      second, for ARES_FILM_SECONDS, into ARES_FILM_NAME under
#                      /out. Unset, the default, means no film.
#
#                      The trail is a settled frame by construction and this is
#                      the opposite camera: what a person would see if they
#                      were watching. It samples rather than counts, because a
#                      grab loop outside the emulator cannot know when a frame
#                      was presented. But ares on lavapipe runs an order of
#                      magnitude slower than a Nintendo 64 does, and the loop
#                      grabs several times faster than ares presents, so in
#                      practice every presented frame is caught more than once
#                      and the repeats are thrown away afterwards.
#   ARES_FILM_SECONDS  how long to film for, in wall-clock seconds.
#   ARES_FILM_GRABBERS how many capture loops run at once. One is not enough:
#                      see the loop itself for the arithmetic.
#   ARES_FILM_NAME     the film's directory under /out.

set -euo pipefail

rom="${1:?usage: headless.sh rom.z64}"
out=/out
deadline="${ARES_RUN_SECONDS:?ARES_RUN_SECONDS is not set}"
log="${out}/${ARES_LOG_NAME:-ares.log}"
saves="${ARES_SAVES_DIR:-/tmp/saves}"
trail="${out}/${ARES_TRAIL_NAME:-trail}"
linger="${ARES_LINGER_SECONDS:-0}"
film_after="${ARES_FILM_AFTER:-}"
film_seconds="${ARES_FILM_SECONDS:-8}"
film_grabbers="${ARES_FILM_GRABBERS:-6}"
film="${out}/${ARES_FILM_NAME:-film}"

# A virtual display. llvmpipe provides GLX on it, so the ares window renders
# on the CPU; :99 avoids colliding with anything a future container leaks in.
Xvfb :99 -screen 0 1024x768x24 &
xvfb_pid=$!
export DISPLAY=:99
for _ in $(seq 1 50); do
    xdpyinfo -display :99 >/dev/null 2>&1 && break
    sleep 0.2
done
xdpyinfo -display :99 >/dev/null 2>&1 || { echo "error: Xvfb did not start." >&2; exit 1; }

# lavapipe is selected by VK_ICD_FILENAMES (baked into the image). Record what
# the loader actually resolves, as an artifact, so the run proves no GPU was
# involved.
#
# Named after this run's log rather than fixed, for the same reason the log is:
# it is written on every run without exception, so a fixed name is the one file
# two concurrent runs would still have been writing at once after every other
# artifact had been separated.
vulkaninfo --summary 2>/dev/null | grep -E "deviceName|driverName|driverInfo|apiVersion" \
    | tee "${log%.log}-vulkan.txt" || true

# ares insists on writing saves; keep them off the read-only ROM mount.
mkdir -p "${saves}"

# The settings, all on the command line so the run does not depend on a
# settings file accreted by earlier runs:
#   Video/Driver "OpenGL 3.2"  the only real Linux video driver; GLX→llvmpipe
#   Audio/Driver None          no audio device exists here
#   Input/Driver None          no input device exists here
#   Input/Defocus Allow        CRITICAL: the default is Pause, and a window on
#                              an Xvfb display with no window manager never
#                              gains focus, so emulation would never start
#   General/HomebrewMode true  ares' homebrew affordances (stricter checks),
#                              and what makes it read the Advanced Homebrew
#                              ROM Header's save-type declaration at all
#   General/AutoSaveMemory     flush cartridge save memory to disk as it is
#                              written rather than only when the cartridge is
#                              unloaded. Every run here ends by killing the
#                              emulator, because a ROM has nowhere to return to
#                              and there is no clean unload to wait for.
#                              Without this a cartridge that was written
#                              perfectly leaves nothing on disk. Found the
#                              hard way by a persistence check whose first
#                              run passed every assertion and produced an
#                              empty save directory.
#   Paths/Saves ${saves}       divert save files from the ROM directory. For
#                              the persistence check this is a mounted host
#                              directory, so what one process writes the next
#                              process finds.
start_epoch=$(date +%s)
stdbuf -oL -eL ares \
    --system "Nintendo 64" \
    --no-file-prompt \
    --setting "Video/Driver=OpenGL 3.2" \
    --setting "Audio/Driver=None" \
    --setting "Input/Driver=None" \
    --setting "Input/Defocus=Allow" \
    --setting "General/HomebrewMode=true" \
    --setting "General/AutoSaveMemory=true" \
    --setting "Paths/Saves=${saves}/" \
    "${rom}" > "${log}" 2>&1 &
ares_pid=$!

# Wait for the ROM's verdict. The banner line marks boot, the RESULT line
# marks completion; both timestamps go to stdout for the wrapper to report.
booted=0
result=0
elapsed=0
# The screenshot trail: an autopilot ROM prints "CHECKPOINT <name>" and then
# holds the frame; each new line gets one photograph. Cleared per run so the
# trail always belongs to this ROM.
checkpoints_seen=0
rm -rf "${trail}"
# Likewise the film: it always belongs to the run that shot it.
if [ -n "${film_after}" ]; then rm -rf "${film}"; fi
# Screenshot-only mode: run for a fixed time and capture whatever is on screen,
# for ROMs that present a screen rather than print a verdict, such as the title
# screen or an interactive battle. The RESULT contract stays the default.
if [ -n "${ARES_SCREENSHOT_AFTER:-}" ]; then
    sleep "${ARES_SCREENSHOT_AFTER}"
    import -display :99 -window root "${out}/${ARES_SHOT_NAME:-screenshot.png}" \
        || echo "warning: screenshot capture failed." >&2
    kill "${ares_pid}" 2>/dev/null || true
    wait "${ares_pid}" 2>/dev/null || true
    kill "${xvfb_pid}" 2>/dev/null || true
    echo "screenshot-only run complete"
    exit 0
fi
while [ "${elapsed}" -lt "${deadline}" ]; do
    if [ "${booted}" -eq 0 ] && grep -q "grandleon n64 conformance" "${log}"; then
        booted=1
        echo "ROM booted after $(( $(date +%s) - start_epoch ))s"
    fi
    checkpoint_count="$(grep -c '^CHECKPOINT ' "${log}" 2>/dev/null || true)"
    if [ "${checkpoint_count:-0}" -gt "${checkpoints_seen}" ]; then
        checkpoints_seen="${checkpoint_count}"
        checkpoint_name="$(grep '^CHECKPOINT ' "${log}" | tail -n 1 \
            | awk '{print $2}' | tr -cd 'a-zA-Z0-9_-')"
        mkdir -p "${trail}"
        import -display :99 -window root \
            "${trail}/$(printf '%03d' "${checkpoints_seen}")-${checkpoint_name}.png" \
            || echo "warning: trail capture failed." >&2
        # The film starts where the run says it does, not where a stopwatch
        # says it does: the checkpoint the ROM just printed is a state the
        # check asserts, so the scene is named in the run's own vocabulary and
        # cannot slide when the emulator has a slow day.
        if [ -n "${film_after}" ] && [ "${checkpoint_name}" = "${film_after}" ]; then
            mkdir -p "${film}"
            echo "Filming from ${checkpoint_name} for ${film_seconds}s" \
                "with ${film_grabbers} grabbers"
            film_end=$(( $(date +%s) + film_seconds ))
            # One `import` costs about fifty milliseconds, nearly all of it
            # process start and the X round trip, so a single loop samples at
            # about twenty a second, around the rate ares presents at, which
            # is exactly the rate at which a six-frame blow can fall between
            # two grabs. Several loops at once cost nothing but cores, of
            # which this machine has plenty and ares uses few, and together
            # they oversample the emulator instead of racing it.
            #
            # Each grab is named by the nanosecond it was taken, so the frames
            # sort into the order they happened whichever grabber took them,
            # and two that landed on the same picture are thrown away by the
            # encoder rather than sorted out here.
            film_pids=""
            for _ in $(seq 1 "${film_grabbers}"); do
                (
                    while [ "$(date +%s)" -lt "${film_end}" ]; do
                        import -display :99 -window root \
                            "${film}/$(date +%s%N).png" 2>/dev/null || true
                    done
                ) &
                film_pids="${film_pids} $!"
            done
            # Named pids, never a bare `wait`: ares and Xvfb are background
            # jobs of this shell too, and waiting for those would wait for a
            # ROM that never exits.
            for pid in ${film_pids}; do wait "${pid}" || true; done
            echo "Filmed $(find "${film}" -name '*.png' | wc -l) grabs into ${film}"
            # Only ever once: the second call would film a different scene
            # under the first one's name.
            film_after=""
        fi
    fi
    if grep -q "^RESULT " "${log}"; then
        result=1
        echo "ROM reported after $(( $(date +%s) - start_epoch ))s"
        break
    fi
    if ! kill -0 "${ares_pid}" 2>/dev/null; then
        echo "error: ares exited before the ROM reported." >&2
        break
    fi
    sleep 1
    elapsed=$(( $(date +%s) - start_epoch ))
done

# Keep the machine on for a while, for a ROM whose evidence is what it wrote
# rather than what it printed. ares writes cartridge save memory to disk on a
# periodic flush, not on every write and not on a signal, so a run killed a
# second after its verdict leaves an empty save directory however correct the
# ROM was. The ROM loops for ever after reporting, so lingering costs only
# time.
if [ "${linger}" -gt 0 ] && kill -0 "${ares_pid}" 2>/dev/null; then
    echo "Lingering ${linger}s so the cartridge is flushed to disk"
    sleep "${linger}"
fi

# Capture the ares window, which is the ROM's own on-screen console, whether or
# not the run passed; a screenshot of a failure is the most useful kind. One
# extra second lets the VI present the frame that carries the final line.
if kill -0 "${ares_pid}" 2>/dev/null; then
    sleep 1
    import -display :99 -window root "${out}/${ARES_SHOT_NAME:-screenshot.png}" \
        || echo "warning: screenshot capture failed." >&2
fi

kill "${ares_pid}" 2>/dev/null || true
wait "${ares_pid}" 2>/dev/null || true
kill "${xvfb_pid}" 2>/dev/null || true

if [ "${result}" -eq 0 ]; then
    echo "error: no RESULT line within ${deadline}s." >&2
    exit 1
fi
grep -q "RESULT PASS" "${log}"
