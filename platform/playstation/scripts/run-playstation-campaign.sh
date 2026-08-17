#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Plays a whole campaign into the PlayStation campaign executable, twice, over
# one memory card. The R3000A has to reach the state the rules say it should,
# and to draw it.
#
# Invoked through the `grandleon_playstation_campaign_check` CMake target and by
# the `grandleon.playstation_campaign` test. It can also be run directly from
# the repository root, after the expectations have been derived and the
# executable built:
#
#   cmake --build build --target grandleon_playstation_campaign_expectations
#   platform/playstation/scripts/build-playstation.sh
#   platform/playstation/scripts/run-playstation-campaign.sh
#
# The order is the argument, and it is `run-playstation-turn.sh`'s: the
# expectations are derived on the host from the engine's own queries *before*
# the executable exists, so no run can be made to pass by adjusting one. That
# order is checked rather than trusted: a derivation older than the sources it
# was derived from is refused by name before the emulator starts, because
# comparing this build against last week's answer prints a transcript mismatch
# in the exact shape a real regression takes.
#
# ---------------------------------------------------------------------------
# Two passes, because a campaign is the thing a save is for
#
#   1. founding   the card holds nothing. The title, the slot screen, the
#                 company as it was founded, the authored story, the management
#                 stage, the battle fought to its objective, the aftermath, and
#                 the save.
#   2. resuming   a *different emulator process* over the same card image. The
#                 slot screen now offers what the first pass left, and the
#                 campaign comes back standing where it stopped.
#
# Which script the executable plays is decided by what the card is holding and
# by nothing the executable carries. The second pass therefore proves the save
# by reaching screens the first pass's expectations do not contain. A card that
# forgot would replay the founding script and fail on the first line.
#
# Four channels have to agree in each pass, and the run fails if any of them
# dissents. They are the same four `run-playstation-turn.sh` names: the
# emulator's exit code, the Lua observer's verdict over the GPU's registers and
# every captured frame, the joiner's line-for-line comparison of the transcript
# and every claimed pixel, and the executable's own verdict, which the joiner
# reads.
#
# The observer is read on its count as well as on its word, against a floor of
# its own for each campaign and each pass: `HARNESS RESULT PASS 0/0` is what an
# emptied assertion table prints. The floors are below.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
# A whole campaign under the interpreter, with a full repaint every frame the
# board moves and a memory card written between screens. A hang detector rather
# than a budget.
timeout_seconds="${GRANDLEON_PLAYSTATION_CAMPAIGN_TIMEOUT:-3000}"
# The shipped campaigns, and both unless a caller names one: a save path that
# works on one campaign and not the other is a save path nobody has tested.
campaigns="${GRANDLEON_PLAYSTATION_CAMPAIGNS:-tarnholt_line demo_campaign}"

for name_of in revision base_digest; do
    if [ -z "${!name_of}" ]; then
        echo "error: ${name_of} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-emulator:${revision}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
fi

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

# The negative control over the thing that decides each pass, and it runs before
# the emulator rather than being a note somebody could stop believing. One of the
# four channels is a line in a log and the arithmetic in it, so the script that
# reads that line is first pointed at a battery of logs that measure nothing
# (absent, silent, `0/0`, partial, under the floor, countless, and a diagnostic
# that merely echoes the verdict) and required to refuse every one. It costs no
# emulator and about a hundredth of a second.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test
# And the same for the other predicate that can quietly pass everything. The
# freshness guard below decides whether the transcripts these two passes are
# compared against still answer for the code under them; a guard nobody has
# watched refuse is a guard nobody should trust.
"${repository_root}/scripts/assert-expectations-fresh.sh" --self-test

exe_dir="${build_dir}/target/platform/playstation"

# Which executable carries which campaign. One build of `src/turn_exe.cpp` per
# project, because a console build carries one project's bytes.
#
# The autopilot builds, for the reason `run-playstation-turn.sh` gives at
# greater length: the played builds of these wait on a pad this emulator does
# not have, so booting one here would hang instead of failing.
executable_for() {
    case "$1" in
        tarnholt_line) echo "grandleon_psx_campaign_autopilot" ;;
        demo_campaign) echo "grandleon_psx_campaign_demo_autopilot" ;;
        *) echo "error: no executable carries '$1'." >&2; exit 2 ;;
    esac
}

# And which project each was derived from, which the freshness guard below needs
# and CMake already knows: the campaign expectation rule passes the same JSON.
project_for() {
    case "$1" in
        tarnholt_line) echo "games/tarnholt/source/project.json" ;;
        demo_campaign) echo "games/demo/source/project.json" ;;
        *) echo "error: no project carries '$1'." >&2; exit 2 ;;
    esac
}

# How many checks the observer has to have made in a pass for that pass to count,
# and what stops an emptied assertion table's `HARNESS RESULT PASS 0/0` being
# read as a pass.
#
# One number per campaign and per pass, not one for all four, because the count
# is a function of how many checkpoints the script reaches and the four scripts
# are nothing like each other in length: founding Tarnholt photographs a whole
# authored campaign and resuming one only picks it up where it stopped. A single
# floor would have to be the smallest of the four, and would let the founding
# passes lose three quarters of their table and still come back green.
#
# The numbers are the counts these four passes reach, taken from their own
# verdict lines. They move only when the campaign or the observer moves, and
# the observer counts on checkpoints the executable signals, never on frames or
# on time. There is no run-to-run drift for a floor to have to allow for, and a
# pass that comes in under one has either stopped part way or stopped asserting.
# Content that adds a checkpoint moves the count: re-derive a number by running
# the check and reading that pass's own `HARNESS RESULT PASS n/n` line. Content
# that changes a *battle* moves it too, in either direction, because these
# scripts are recorded rather than written: widening the Fordlight moved the
# board's derived seed and therefore its dice, the recorded policy needed nine
# fewer presses to finish the fight it now fights, and a script that settles
# fewer times asserts fewer things. The number below came from that pass.
min_checks_for() {
    case "$1 $2" in
        "tarnholt_line found") echo 319 ;;
        "tarnholt_line resume") echo 29 ;;
        "demo_campaign found") echo 65 ;;
        "demo_campaign resume") echo 25 ;;
        *) echo "error: no assertion floor is recorded for '$1 $2'." >&2; exit 2 ;;
    esac
}

for campaign in ${campaigns}; do
    executable="${exe_dir}/$(executable_for "${campaign}").ps-exe"
    if [ ! -f "${executable}" ]; then
        echo "error: ${executable} does not exist." >&2
        echo "Build it first: cmake --build build --target grandleon_playstation" >&2
        exit 1
    fi
    # That the four files exist is not the question. A derivation older than the
    # client it compiles answers for a build that is no longer here, and both
    # passes would be compared against it line for line and print a mismatch in
    # the exact shape a regression takes. The CMake target regenerates before
    # comparing; this path, the one the header above recommends for iterating,
    # could not until now. `scripts/assert-expectations-fresh.sh` says why it
    # refuses rather than deriving the files itself.
    for mode in found resume; do
        "${repository_root}/scripts/assert-expectations-fresh.sh" \
            --expectation "${build_dir}/${campaign}_${mode}.txt" \
            --project "${repository_root}/$(project_for "${campaign}")" \
            --regenerate grandleon_playstation_campaign_expectations
    done
done

# The joiner is a host tool and is built here rather than by the root
# CMakeLists, for the reason the turn check gives: it is a single translation
# unit with no dependency on anything in this repository, deliberately, since a
# harness that linked the engine could agree with the executable for the same
# wrong reason. It is the *same* joiner: a campaign checkpoint and a board
# checkpoint are the same block of claims.
joiner="${build_dir}/playstation_turn_probe"
if [ ! -x "${joiner}" ] || \
   [ "${repository_root}/platform/playstation/harness/playstation_turn_probe.c" -nt "${joiner}" ]; then
    echo "Building the campaign joiner…" >&2
    cc -std=c11 -O2 -Wall -Wextra -Werror \
        -o "${joiner}" \
        "${repository_root}/platform/playstation/harness/playstation_turn_probe.c"
fi

cards="${build_dir}/campaign-cards"
rm -rf "${cards}"
mkdir -p "${cards}"

run_pass() {
    local campaign="$1"
    local mode="$2"
    local name
    name="$(executable_for "${campaign}")"
    local frame_dir="${build_dir}/campaign-frames-${campaign}-${mode}"
    local log="${build_dir}/campaign-${campaign}-${mode}.log"
    local status=0

    rm -rf "${frame_dir}"
    mkdir -p "${frame_dir}"

    set +e
    timeout --kill-after=10 "${timeout_seconds}" \
        "${docker}" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env GRANDLEON_PLAYSTATION_FRAME_DIR=/out \
        --volume "${exe_dir}:/exe:ro" \
        --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
        --volume "${frame_dir}:/out" \
        --volume "${cards}:/cards" \
        --workdir /tmp \
        "${image}" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter \
            -memcard1 "/cards/${campaign}.mcd" \
            -dofile /harness/playstation_turn_probe.lua \
            -loadexe "/exe/${name}.ps-exe" \
        > "${log}" 2>&1
    status=$?
    set -e

    if [ "${status}" -eq 124 ] || [ "${status}" -eq 137 ]; then
        echo "error: the ${mode} pass did not finish in ${timeout_seconds}s." >&2
        tail -n 40 "${log}" >&2 || true
        exit 1
    fi
    # The verdict and the count behind it, decided by the one script every
    # console here shares rather than by a `grep` written again per caller.
    local verdict
    if ! verdict="$(
            "${repository_root}/scripts/assert-harness-verdict.sh" \
                --log "${log}" --require "^HARNESS RESULT PASS " \
                --min-checks "$(min_checks_for "${campaign}" "${mode}")" \
                --what "the observer on the ${campaign} ${mode} pass"
        )"; then
        echo "The emulator exited ${status}. Log: ${log}" >&2
        grep "^HARNESS CHECK .* FAIL" "${log}" >&2 \
            || echo "  (the observer never ran)" >&2
        exit 1
    fi
    if [ "${status}" -ne 0 ]; then
        echo "error: the observer says pass and the exit code is ${status}." >&2
        echo "The two channels disagree; log: ${log}" >&2
        exit 1
    fi
    # The executable chooses its own script from the card. A pass that played
    # the wrong one would be compared against the wrong expectations, so which
    # one it played is checked here rather than inferred from the comparison
    # passing.
    if ! grep -q "^SCRIPT ${mode} " "${log}"; then
        echo "error: the ${mode} pass did not play the ${mode} script." >&2
        grep -m1 "^SCRIPT " "${log}" >&2 || echo "  (it named no script)" >&2
        exit 1
    fi
    if ! "${joiner}" "${log}" "${build_dir}/${campaign}_${mode}.txt" "${frame_dir}"; then
        echo "error: what the ${mode} pass played is not what the rules say." >&2
        echo "Log: ${log}" >&2
        exit 1
    fi
    printf '%s\n' "${verdict}"
    echo "   frames: $(find "${frame_dir}" -name 'frame-*.bin' | wc -l)"
}

for campaign in ${campaigns}; do
    echo "== ${campaign}: founding on an empty card"
    run_pass "${campaign}" found
    if [ ! -s "${cards}/${campaign}.mcd" ]; then
        echo "error: the founding pass wrote no card image." >&2
        exit 1
    fi
    written="$(md5sum < "${cards}/${campaign}.mcd")"

    echo "== ${campaign}: resuming it in a new process, over the same card"
    run_pass "${campaign}" resume
    if [ "${written}" != "$(md5sum < "${cards}/${campaign}.mcd")" ]; then
        echo "error: a pass that only resumed rewrote the card." >&2
        exit 1
    fi
    echo "   the card is byte for byte what the founding pass wrote"
done

echo "The PlayStation played ${campaigns} through and picked each up again."
echo "Cards: ${cards}"
