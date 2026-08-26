#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs the PlayStation conformance executable in a pinned PCSX-Redux container
# and fails unless it reports that every check passed.
#
# Invoked through the `grandleon_playstation_check` CMake target and by the
# `grandleon.playstation` test. It can also be run directly from the repository
# root, after platform/playstation/scripts/build-playstation.sh has produced an
# executable.
#
#   platform/playstation/scripts/run-playstation.sh
#   platform/playstation/scripts/run-playstation.sh --rebuild-image
#
# The verdict is read twice, from two independent channels, because this target
# has both and a gate that uses only one throws away the stronger evidence:
#
#   - the process exit code, which the executable sets by writing to
#     PCSX-Redux's control port; and
#   - the `RESULT PASS n/n` line on stdout, which arrives over the BIOS
#     teletype.
#
# Both must agree. A run where the exit code says zero and no report ever
# reached stdout is a channel that has stopped working, not a pass: an emulator
# that silently discards the debug channel makes a broken program look fine.
#
# GRANDLEON_PLAYSTATION_MIN_CHECKS is the number of assertions the executable
# must have reached for the run to count, and it is what stops an emptied
# assertion table's `RESULT PASS 0/0` being read as a pass. It is a floor
# rather than a number to match; `scripts/assert-harness-verdict.sh` holds the
# reasoning and makes the decision. Zero, the default, is no floor at all, which
# is right because this script is also how a program that *measures* rather than
# checks is run: such a program reports on these same two channels, and the size
# of its count is the thing being measured rather than a thing to require.
# Every caller that is a check names a floor; `grandleon_playstation_check`
# names 58.
#
# GRANDLEON_PLAYSTATION_EXPECT is the line the log must contain, and it is
# anchored, because an unanchored `RESULT PASS` is also satisfied by a
# diagnostic line that merely mentions the verdict.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
executable_name="${GRANDLEON_PLAYSTATION_EXECUTABLE:-grandleon_psx.ps-exe}"
expected="${GRANDLEON_PLAYSTATION_EXPECT:-^RESULT PASS }"
min_checks="${GRANDLEON_PLAYSTATION_MIN_CHECKS:-0}"
# Wall-clock seconds before the run is killed. PCSX-Redux has no cycle limit
# and no timeout of its own: an executable that never writes to the control
# port leaves the emulator spinning in its main loop for ever. The bound has to
# come from outside. The whole run is a few seconds under the interpreter; this
# is a hang detector, not a budget.
timeout_seconds="${GRANDLEON_PLAYSTATION_TIMEOUT:-300}"

for name in revision base_digest; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
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

# The negative control over the thing that decides this run, and it runs before
# the emulator rather than being a note somebody could stop believing.
#
# The whole of this run's evidence is a line in a log and the arithmetic in it,
# so the script that reads that line is first pointed at a battery of logs that
# measure nothing (absent, silent, `0/0`, partial, under the floor, countless,
# and a diagnostic that merely echoes the verdict) and required to refuse every
# one. It costs no emulator and about a hundredth of a second. This console has
# five entry points and no common driver to hang it off, so each of them carries
# it: a check whose predicate was never proved able to fail is the one this
# guards against, and that is a property of the run, not of who started it.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test

executable="${build_dir}/target/platform/playstation/${executable_name}"
if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_playstation" >&2
    exit 1
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

log="${build_dir}/emulator-${executable_name%.ps-exe}.log"

# Four of these five flags are load-bearing and none of them is guessable.
#
#   -no-ui      selects a no-op text UI: no GLFW, no OpenGL, no window, and no
#               display server. It also implicitly enables stdout, which is why
#               -stdout is not passed as well.
#   -run        is not optional. -loadexe does not load anything by itself; the
#               executable is injected by hijacking the BIOS shell when the CPU
#               reaches 0x80030000, and without -run the CPU never starts, so
#               the emulator sits at a prompt for ever having loaded nothing.
#   -bios       is not optional either, for the same reason: with no BIOS there
#               is no shell to hijack. OpenBIOS is MIT and ships in the same
#               repository as the emulator, so this gate needs no copyrighted
#               firmware, which is the one worry a BIOS-booted machine raised.
#   -testmode   is what makes the control port set the process exit code. With
#               it the emulator quits; without it, it logs "PSX software
#               requested an exit" and *pauses*, which in a script is a hang.
#   -interpreter runs the interpreter rather than the dynamic recompiler. It is
#               the slower and the more trustworthy of the two, and upstream
#               runs its own suites both ways.
set +e
timeout --kill-after=10 "${timeout_seconds}" \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --volume "${build_dir}/target/platform/playstation:/exe:ro" \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -loadexe "/exe/${executable_name}" \
    > "${log}" 2>&1
status=$?
set -e

if [ "${status}" -eq 124 ] || [ "${status}" -eq 137 ]; then
    echo "error: the conformance executable did not finish in ${timeout_seconds}s." >&2
    tail -n 40 "${log}" >&2 || true
    exit 1
fi

# The verdict, and the count behind it. The word `PASS` is not the evidence:
# `RESULT PASS 0/0` carries the word and asserts nothing. The decision is made
# by the one script every console here shares rather than by a `grep` written
# again per caller.
if ! verdict="$(
        "${repository_root}/scripts/assert-harness-verdict.sh" \
            --log "${log}" --require "${expected}" \
            --min-checks "${min_checks}" --what "${executable_name}"
    )"; then
    echo "PCSX-Redux exited ${status}. Log: ${log}" >&2
    tail -n 40 "${log}" >&2 || echo "  (no output at all)" >&2
    exit 1
fi

if [ "${status}" -ne 0 ]; then
    echo "error: the report says pass but the exit code is ${status}." >&2
    echo "The two channels disagree; log: ${log}" >&2
    exit 1
fi

printf '%s\n' "${verdict}"
echo "PlayStation executable ${executable_name} passed. Full log: ${log}"
