#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the Nintendo 64 ROMs once and runs this console's checks over them.
#
#   platform/nintendo64/scripts/check-n64.sh
#                                     build once, run every check at once
#   platform/nintendo64/scripts/check-n64.sh --check play
#                                     build, then run one of them
#   platform/nintendo64/scripts/check-n64.sh --check play --check campaign
#                                     build, then run those two at once
#
# **Invoked through CMake, not by hand.** The first form is
# `grandleon_n64_check_all`; the second is `grandleon_n64_check`, `_play_check`,
# `_autopilot_check` and `_campaign_check`. The toolchain pins, the libdragon
# commit and the base image digest, are CMake cache variables in
# `cmake/GrandleonNintendo64.cmake` and reach the build script through the
# environment, so a run started from a shell instead dies at
# `GRANDLEON_LIBDRAGON_COMMIT is not set`. It dies loudly, and pinning a
# toolchain in one place is worth more than a second way to start a run, so the
# command lines above are how the targets call this rather than an invitation.
#
# **Why one script and not four targets that each build.** Every check needs
# every ROM, so four targets that each ran build-n64.sh would queue on that
# script's `flock`: one build, three waits, four times the cost of the thing
# none of them changes. Measured on an idle 28-core host, that arrangement takes
# 383 s and this script takes 291 s: 86 s of build, and then 4 s, 9 s, 98 s and
# 205 s at once. Nothing about a run is given up for it: no check is dropped,
# shortened or made conditional.
#
# It is deliberately not a cache. There is one build, it happens on every run,
# and it happens before any emulator starts, so there is no question of what a
# run was made against. That is the property a build directory which remembers
# things cannot offer.
#
# **What a check is is defined here, once.** The CMake targets and the ctest
# lanes both come through this script, so the ROM a check runs, the budget it
# runs under and the number of assertions it must clear cannot drift between the
# two ways of asking for it.
#
# **The floors.** A verdict of `RESULT PASS` says the ROM did not fail; it does
# not say the ROM checked anything, and `RESULT PASS 0/0` is what an emptied
# assertion table prints. So each check states the number of assertions its ROM
# has to reach, `run-ares.sh` passes it to `scripts/assert-harness-verdict.sh`,
# and a run under the floor is a failure whatever word it printed. The numbers
# are floors rather than equalities because the count moves (see that script's
# header, and the measurements beside each check below).
#
# Each concurrent check writes its output to ${build_dir}/checks/<name>.log and
# a one-line verdict beside it. The verdicts are what the summary is built from,
# and a check that left none is reported as a failure rather than passed over:
# a runner that quietly ran three of four would otherwise look like a speedup.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64}"
ares="${repository_root}/platform/nintendo64/ares"

# Every check this console has, in the order a reader wants them: what the
# machine is, what it draws, what it does when played, and what it keeps.
all_checks="conformance play autopilot campaign"

usage() {
    echo "usage: $(basename "$0") [--check NAME]..." >&2
    echo "checks: ${all_checks}" >&2
    exit 2
}

selected=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --check)
            [ "$#" -ge 2 ] || usage
            case " ${all_checks} " in
                *" $2 "*) ;;
                *) echo "error: no such check: $2" >&2; usage ;;
            esac
            # Asked for twice is refused rather than obeyed: two runs of one
            # check would be two emulator processes writing one check's log,
            # screenshot and trail, and the evidence of both would be worth
            # nothing.
            case " ${selected[*]-} " in
                *" $2 "*)
                    echo "error: ${2} was asked for twice." >&2
                    usage
                    ;;
            esac
            selected+=("$2")
            shift 2
            ;;
        *) usage ;;
    esac
done
if [ "${#selected[@]}" -eq 0 ]; then
    # shellcheck disable=SC2206 # the list is a fixed set of bare words
    selected=(${all_checks})
fi

# One check, one run. `run-ares.sh` is given the ROM rather than left to its
# default so that the ROM each check runs is stated here beside the check's
# name, and the campaign's two-boot harness is called as the one check whose
# subject is what the first boot left behind.
run_check() {
    local name="$1"
    case "${name}" in
        conformance)
            # 39 assertions, the same set the host suite makes.
            GRANDLEON_N64_ARES_MIN_CHECKS=39 \
                "${ares}/run-ares.sh" \
                "${build_dir}/platform/nintendo64/grandleon_n64.z64"
            ;;
        play)
            # 26 framebuffer assertions, fixed by the two boards it samples:
            # the campaign's first, and the first the game authors whose turns
            # are taken in blocks, which is the only kind of board a character
            # can be standing finished on, and so the only one the greyed
            # drawing can be photographed on.
            GRANDLEON_N64_ARES_MIN_CHECKS=26 \
                "${ares}/run-ares.sh" \
                "${build_dir}/platform/nintendo64/grandleon_n64_probe.z64"
            ;;
        autopilot)
            # The autopilot holds every checkpoint on screen long enough to
            # photograph, so it is given a longer budget than the default.
            #
            # Its count is the one here that genuinely moves with the run,
            # because how far a timed replay gets decides how many checkpoints
            # it reaches. Measured at 134 over the five rounds the script
            # plays; the floor sits below that on purpose, and still leaves no
            # room for a table that stopped asserting.
            GRANDLEON_N64_ARES_SECONDS=600 \
                GRANDLEON_N64_ARES_MIN_CHECKS=120 \
                "${ares}/run-ares.sh" \
                "${build_dir}/platform/nintendo64/grandleon_n64_autopilot.z64"
            ;;
        campaign)
            # Two boots of one ROM over one cartridge, and they assert
            # different things: the first founds a campaign and checks 70, the
            # second resumes it and checks 52.
            GRANDLEON_N64_ARES_MIN_CHECKS_FOUND=70 \
                GRANDLEON_N64_ARES_MIN_CHECKS_RESUMED=52 \
                "${ares}/run-ares-persistence.sh"
            ;;
        *)
            echo "error: no such check: ${name}" >&2
            return 2
            ;;
    esac
}

what_failed() {
    case "$1" in
        conformance) echo "the conformance ROM did not report a pass" ;;
        play) echo "the render probe did not report a pass" ;;
        autopilot) echo "the autopilot did not report a pass" ;;
        campaign) echo "the campaign did not survive a restarted emulator" ;;
        *) echo "the check failed" ;;
    esac
}

# The negative control, and it runs before anything else rather than being a
# note somebody could stop believing.
#
# The whole of this console's evidence is a line in a log and the arithmetic in
# it. So the thing that reads that line is pointed at a battery of logs that
# measure nothing (absent, silent, `0/0`, partial, under the floor, countless,
# and a diagnostic that merely echoes the verdict) and is required to refuse
# every one. It costs no emulator and no measurable time, so there is no run
# this cannot afford. `run-playstation-render.sh` is the precedent.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test

echo
echo "== Building every Nintendo 64 ROM once =="
"${repository_root}/platform/nintendo64/scripts/build-n64.sh"

run_dir="${build_dir}/checks"
mkdir -p "${run_dir}"

if [ "${#selected[@]}" -eq 1 ]; then
    # Asked for one check, which is what somebody debugging one wants: its
    # output goes to the terminal as it happens, and to its log as well.
    name="${selected[0]}"
    echo
    echo "== ${name} =="
    set +e
    run_check "${name}" 2>&1 | tee "${run_dir}/${name}.log"
    status="${PIPESTATUS[0]}"
    set -e
    if [ "${status}" -ne 0 ]; then
        echo "error: ${name}: $(what_failed "${name}")" >&2
        exit 1
    fi
    exit 0
fi

# A previous run's evidence is removed rather than left to be read as this
# one's. The verdict files are how a check proves it ran at all, so a stale one
# is the difference between "passed" and "never started".
for name in "${selected[@]}"; do
    rm -f "${run_dir}/${name}.log" "${run_dir}/${name}.verdict"
done

echo
echo "== Running ${#selected[@]} checks at once =="
pids=()
for name in "${selected[@]}"; do
    echo "  ${name}: ${run_dir}/${name}.log"
    (
        started="$(date +%s)"
        set +e
        run_check "${name}" > "${run_dir}/${name}.log" 2>&1
        status=$?
        set -e
        printf '%s %s\n' "${status}" "$(( $(date +%s) - started ))" \
            > "${run_dir}/${name}.verdict"
        exit "${status}"
    ) &
    pids+=("$!")
done

statuses=()
seconds=()
for index in "${!selected[@]}"; do
    name="${selected[index]}"
    if wait "${pids[index]}"; then
        status=0
    else
        status=$?
    fi
    # The check's own verdict file, not the exit code alone. A job that was
    # killed before it ran, or one whose subshell never reached its command,
    # has no verdict to show and is counted as a failure here.
    took="?"
    recorded=""
    if [ -f "${run_dir}/${name}.verdict" ]; then
        read -r recorded took < "${run_dir}/${name}.verdict" || true
        if [ "${recorded}" != "${status}" ]; then
            echo "error: ${name} recorded ${recorded} and exited ${status}" >&2
            status=1
        fi
    else
        echo "error: ${name} left no verdict behind; it did not run" >&2
        status=1
    fi
    statuses+=("${status}")
    seconds+=("${took}")
    if [ "${status}" -eq 0 ]; then
        echo "  ${name} passed in ${took}s"
    else
        echo "  ${name} FAILED after ${took}s"
    fi
done

echo
failures=()
for index in "${!selected[@]}"; do
    name="${selected[index]}"
    echo "== ${name} =="
    if [ "${statuses[index]}" -eq 0 ]; then
        tail -n 12 "${run_dir}/${name}.log" 2>/dev/null \
            || echo "  (no output)"
    else
        # A failure's whole log, because the reason is somewhere in it.
        cat "${run_dir}/${name}.log" 2>/dev/null || echo "  (no output)"
        failures+=("${name}")
    fi
    echo
done

echo "== Nintendo 64 checks =="
for index in "${!selected[@]}"; do
    if [ "${statuses[index]}" -eq 0 ]; then
        result="pass"
    else
        result="FAIL"
    fi
    printf '  %-12s %s  %5ss  %s\n' \
        "${selected[index]}" "${result}" "${seconds[index]}" \
        "${run_dir}/${selected[index]}.log"
done

if [ "${#failures[@]}" -ne 0 ]; then
    echo
    for name in "${failures[@]}"; do
        echo "error: ${name}: $(what_failed "${name}")" >&2
    done
    echo "${#failures[@]} of ${#selected[@]} Nintendo 64 checks failed: ${failures[*]}" >&2
    exit 1
fi

echo
echo "All ${#selected[@]} Nintendo 64 checks passed."
