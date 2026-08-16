#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Plays the autopilot's script into the PlayStation turn executable, and
# requires the R3000A to reach the state the rules say it should, and to draw
# it.
#
# Invoked through the `grandleon_playstation_turn_check` CMake target and by the
# `grandleon.playstation_turn` test. It can also be run directly from the
# repository root, after the expectations have been derived and the executable
# built:
#
#   cmake --build build --target grandleon_playstation_turn_expectations
#   platform/playstation/scripts/build-playstation.sh
#   platform/playstation/scripts/run-playstation-turn.sh
#
# The order is the argument. The expectations are derived on the host from the
# engine's own queries *before* the executable exists, so no run can be made to
# pass by adjusting one. That order is checked rather than trusted: a derivation
# older than the sources it was derived from is refused by name before the
# emulator starts, because comparing this build against last week's answer
# prints a transcript mismatch in the exact shape a real regression takes.
#
# Four channels have to agree, and the run fails if any of them dissents:
#
#   - the emulator's process exit code;
#   - `HARNESS RESULT PASS n/n` from the Lua observer, which measures the GPU's
#     registers and every captured frame before believing a word the executable
#     says;
#   - `RESULT PASS n/n` from the joiner, which compares the console's transcript
#     against the host's derivation line for line, and every pixel the
#     executable claimed against the GPU's readback and the emulator's frame;
#   - the executable's own `RESULT PASS n/n`, which the joiner reads rather
#     than this script: it is a claim, and the joiner is where claims are
#     checked.
#
# Set `GRANDLEON_PLAYSTATION_FILM_FROM` and `GRANDLEON_PLAYSTATION_FILM_TO` and
# the observer's second camera is switched on as well: every composited frame
# between those two checkpoint ordinals is written out, on top of the one frame
# per checkpoint the joiner reads. That is what
# `cmake --build build --target grandleon_playstation_showcase` does, and it is
# this script rather than a copy of it for the reason the scratch run gives in
# reverse: the invocation is identical and only the observer's settings differ,
# so a second copy of the pinned-image logic would be a second thing to keep
# right.
# The film changes no check: it is written from a listener that touches none of
# the counters the verdict is computed from, so a filmed run still has to reach
# the same `RESULT PASS` on all four channels before any of its frames survive.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
# A whole scripted session under the interpreter, with a full repaint every
# frame the board moves. A hang detector rather than a budget.
timeout_seconds="${GRANDLEON_PLAYSTATION_TURN_TIMEOUT:-2400}"
# The autopilot build, because a check needs a run that presses its own buttons.
# `grandleon_psx_turn` is the same translation unit with no script linked into
# it: it waits on the pad, which under this headless emulator means it waits for
# ever. Booting it here would hang rather than fail, which is the worst way for
# a check to be wrong.
name="${GRANDLEON_PLAYSTATION_TURN_EXECUTABLE:-grandleon_psx_turn_autopilot}"
# The number of checks the observer must have made for this run to count, and
# what stops `HARNESS RESULT PASS 0/0` being read as a pass. It is a floor, not
# a number to match: the count is a function of how many checkpoints the script
# reaches, so it moves with the script rather than with the machine.
# `scripts/assert-harness-verdict.sh` holds the reasoning and decides. It is set
# by the caller rather than defaulted here so that the number lives beside the
# script whose choreography fixes it; zero, the default, is no floor at all and
# is for a hand-started debugging run. Both CMake targets that reach this name
# the same one, because a filmed run is required to be a run that passed. They
# are the check and the showcase, which is the check's own run with a camera on.
min_checks="${GRANDLEON_PLAYSTATION_TURN_MIN_CHECKS:-0}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
fi

for required in revision base_digest; do
    if [ -z "${!required}" ]; then
        echo "error: ${required} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-emulator:${revision}"

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

pinned_base="${base_image}@${base_digest}"
if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${pinned_base}" >/dev/null 2>&1; then
        echo "Pulling ${pinned_base}…" >&2
        "${docker}" pull "${pinned_base}"
    fi
    echo "Building ${image} from platform/playstation/emulator/Containerfile…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/playstation/emulator/Containerfile" \
        --build-arg "PCSX_REDUX_REVISION=${revision}" \
        --tag "${image}" \
        "${repository_root}/platform/playstation/emulator"
fi

installed_revision="$("${docker}" run --rm "${image}" cat /usr/local/PCSX_REDUX_REVISION)"
installed_revision="${installed_revision//[$'\r\n']/}"
if [ "${installed_revision}" != "${revision}" ]; then
    echo "error: ${image} was built from a different PCSX-Redux revision." >&2
    echo "  expected: ${revision}" >&2
    echo "  actual:   ${installed_revision}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi

# The negative control over the thing that decides this run, and it runs before
# the emulator rather than being a note somebody could stop believing. Four
# channels agree on this run, and one of them is a line in a log and the
# arithmetic in it, so the script that reads that line is first pointed at a
# battery of logs that measure nothing, absent through `0/0` through a
# diagnostic that merely echoes the verdict, and required to refuse every one.
# It costs no emulator and about a hundredth of a second.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test
# And the same for the other predicate that can quietly pass everything. The
# freshness guard below decides whether the transcript this run is compared
# against still answers for the code under it; a guard nobody has watched refuse
# is a guard nobody should trust.
"${repository_root}/scripts/assert-expectations-fresh.sh" --self-test

exe_dir="${build_dir}/target/platform/playstation"
executable="${exe_dir}/${name}.ps-exe"
expectations="${build_dir}/fordlight_autopilot.txt"
frame_dir="${build_dir}/turn-frames"

if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_playstation" >&2
    exit 1
fi
# That the file exists is not the question. A derivation older than the client
# it compiles answers for a build that is no longer here, and comparing this run
# against it prints a transcript mismatch that looks exactly like a regression
# in whatever was just changed. The CMake target regenerates before comparing;
# this path, the one the header above recommends for iterating, could not until
# now. `scripts/assert-expectations-fresh.sh` says why it refuses rather than
# deriving the file itself.
"${repository_root}/scripts/assert-expectations-fresh.sh" \
    --expectation "${expectations}" \
    --project "${repository_root}/games/tarnholt/source/project.json" \
    --regenerate grandleon_playstation_turn_expectations

rm -rf "${frame_dir}"
mkdir -p "${frame_dir}"

# The film's window, in checkpoint ordinals, and empty by default: with neither
# end set the observer's second camera never runs and this is exactly the check
# it has always been.
film_from="${GRANDLEON_PLAYSTATION_FILM_FROM:-}"
film_to="${GRANDLEON_PLAYSTATION_FILM_TO:-}"
film_dir="${build_dir}/showcase"
film_args=()
if [ -n "${film_from}" ] || [ -n "${film_to}" ]; then
    # The film always belongs to the run that produced it: a directory whose
    # contents depend on which run last half-finished is not evidence.
    rm -rf "${film_dir}"
    mkdir -p "${film_dir}"
    film_args=(
        --env "GRANDLEON_PLAYSTATION_FILM_DIR=/film"
        --env "GRANDLEON_PLAYSTATION_FILM_FROM=${film_from:-1}"
        --env "GRANDLEON_PLAYSTATION_FILM_TO=${film_to:-0}"
        --volume "${film_dir}:/film"
    )
fi

# The joiner is a host tool and is built here rather than by the root
# CMakeLists, because it is a single translation unit with no dependency on
# anything in this repository. That is deliberate: a harness that linked the
# engine could agree with the executable for the same wrong reason.
joiner="${build_dir}/playstation_turn_probe"
if [ ! -x "${joiner}" ] || \
   [ "${repository_root}/platform/playstation/harness/playstation_turn_probe.c" -nt "${joiner}" ]; then
    echo "Building the turn joiner…" >&2
    cc -std=c11 -O2 -Wall -Wextra -Werror \
        -o "${joiner}" \
        "${repository_root}/platform/playstation/harness/playstation_turn_probe.c"
fi

log="${build_dir}/turn-${name}.log"

set +e
timeout --kill-after=10 "${timeout_seconds}" \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_PLAYSTATION_FRAME_DIR=/out \
    --volume "${exe_dir}:/exe:ro" \
    --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
    --volume "${frame_dir}:/out" \
    ${film_args[@]+"${film_args[@]}"} \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -dofile /harness/playstation_turn_probe.lua \
        -loadexe "/exe/${name}.ps-exe" \
    > "${log}" 2>&1
run_status=$?
set -e

if [ "${run_status}" -eq 124 ] || [ "${run_status}" -eq 137 ]; then
    echo "error: ${name} did not finish in ${timeout_seconds}s." >&2
    tail -n 40 "${log}" >&2 || true
    exit 1
fi

# The verdict and the count behind it. `HARNESS RESULT PASS 0/0` carries the
# word and measured nothing, so the decision is made by the one script every
# console here shares rather than by a `grep` written again per caller.
if ! verdict="$(
        "${repository_root}/scripts/assert-harness-verdict.sh" \
            --log "${log}" --require "^HARNESS RESULT PASS " \
            --min-checks "${min_checks}" --what "the observer on ${name}"
    )"; then
    echo "The emulator exited ${run_status}. Log: ${log}" >&2
    grep "^HARNESS CHECK .* FAIL" "${log}" >&2 || echo "  (the observer never ran)" >&2
    exit 1
fi

if [ "${run_status}" -ne 0 ]; then
    echo "error: the observer says pass but the exit code is ${run_status}." >&2
    echo "The two channels disagree; log: ${log}" >&2
    exit 1
fi

if ! "${joiner}" "${log}" "${expectations}" "${frame_dir}"; then
    echo "error: what ${name} played is not what the rules say it should have." >&2
    echo "Log: ${log}" >&2
    exit 1
fi

printf '%s\n' "${verdict}"
echo "Frames: ${frame_dir} ($(find "${frame_dir}" -name 'frame-*.bin' | wc -l) checkpoints)"
if [ "${#film_args[@]}" -ne 0 ]; then
    if grep -q "^HARNESS FILM .* TRUNCATED" "${log}"; then
        echo "error: the film hit its frame ceiling; narrow the window." >&2
        exit 1
    fi
    filmed="$(find "${film_dir}" -name 'film-*.bin' | wc -l)"
    # A camera that was asked for and caught nothing is a failure of this run
    # and not of the machine under test, which is why it is asserted here,
    # after the four channels have already decided, rather than inside the
    # observer. It is asserted at all because it has happened: a listener the
    # emulator collected wrote no frames and every check still passed.
    if [ "${filmed}" -eq 0 ]; then
        echo "error: the film window caught no frames. Log: ${log}" >&2
        exit 1
    fi
    echo "Film: ${film_dir} (${filmed} console frames," \
        "checkpoints ${film_from:-1}..${film_to:-end})"
fi
echo "The PlayStation turn executable played its script. Full log: ${log}"
