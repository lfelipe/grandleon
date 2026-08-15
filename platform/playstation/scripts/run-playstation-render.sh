#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs a PlayStation play executable and checks what it actually drew.
#
# Invoked through the `grandleon_playstation_render_check` CMake target and by
# the `grandleon.playstation_render` test. It can also be run directly from the
# repository root, after platform/playstation/scripts/build-playstation.sh has
# produced the executables.
#
#   platform/playstation/scripts/run-playstation-render.sh
#   platform/playstation/scripts/run-playstation-render.sh --rebuild-image
#
# A separate script from run-playstation.sh because it answers a different
# question with a different set of flags. That one runs an executable and reads
# its text; this one runs the same emulator with a Lua script attached, captures
# the composited display at an instant the executable names, and then joins the
# frame against the executable's own per-cell claims.
#
# Three channels have to agree, and the run fails if any two disagree:
#
#   - the emulator's process exit code;
#   - `HARNESS RESULT PASS n/n` from the Lua script, which measures the GPU's
#     registers and the frame before believing a word the executable says; and
#   - `RESULT PASS n/n` from the joiner, which compares every cell the
#     executable claimed against the pixel the emulator produced there.
#
# The executable's own `RESULT PASS n/n` is deliberately *not* one of them. It
# is a claim, and the point of this script is that a claim is checked.
#
# The word in the harness's verdict is not enough either: `HARNESS RESULT PASS
# 0/0` carries it and asserts nothing. So the count is required as well, against
# the floor stated beside the loop, and the harness is separately required to
# fail on an executable that draws nothing: the control at the foot of this
# script, whose absence fails the run.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
timeout_seconds="${GRANDLEON_PLAYSTATION_TIMEOUT:-600}"

# Both boards, unless the caller names one. The second exists to exercise
# raised terrain, which the Fordlight has none of.
executables=("${@:1}")
rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
    executables=()
fi
if [ "${#executables[@]}" -eq 0 ]; then
    executables=(grandleon_psx_play grandleon_psx_play_raised)
fi

for name in revision base_digest; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
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

# The negative control over the thing that decides each of these runs, and it
# runs before the emulator rather than being a note somebody could stop
# believing. The harness's own negative control is at the foot of this script.
# This is the one for the arithmetic that reads its verdict: a battery of logs
# that measure nothing, every one of which must be refused. It costs no emulator
# and about a hundredth of a second.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test

exe_dir="${build_dir}/target/platform/playstation"
frame_dir="${build_dir}/frames"
mkdir -p "${frame_dir}"

# The number of checks the Lua harness makes over one play executable: fifteen
# on the GPU's display registers and the frame's geometry, and three on the
# picture itself. It is fixed by the harness rather than by the board, which is
# why it is one number here and not one per executable. It is the same eighteen
# the render-less control at the foot of this script is measured against, where
# it fails ten of them.
min_checks=18

# The joiner is a host tool and is built here rather than by the root
# CMakeLists, because it is a dozen lines of build for a single translation unit
# with no dependency on anything in this repository. That is deliberate: a
# harness that linked the engine could agree with the executable for the same
# wrong reason.
joiner="${build_dir}/playstation_probe"
if [ ! -x "${joiner}" ] || \
   [ "${repository_root}/platform/playstation/harness/playstation_probe.c" -nt "${joiner}" ]; then
    echo "Building the frame joiner…" >&2
    cc -std=c11 -O2 -Wall -Wextra -Werror \
        -o "${joiner}" \
        "${repository_root}/platform/playstation/harness/playstation_probe.c"
fi

status=0
for name in "${executables[@]}"; do
    executable="${exe_dir}/${name}.ps-exe"
    if [ ! -f "${executable}" ]; then
        echo "error: ${executable} does not exist." >&2
        echo "Build it first: cmake --build build --target grandleon_playstation" >&2
        exit 1
    fi

    log="${build_dir}/render-${name}.log"
    frame="${frame_dir}/${name}.bin"
    rm -f "${frame}"

    # -softgpu is passed explicitly even though it is the default. The
    # screenshot API is implemented on the software rasteriser only: the base
    # class raises "Not yet implemented". The OpenGL renderer would also need a
    # GL context this container has no display server for.
    #
    # -dofile installs the harness. It is *not* -lua or -luafile; neither of
    # those exists, and the emulator has no --help to have found that out from.
    set +e
    timeout --kill-after=10 "${timeout_seconds}" \
        "${docker}" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env GRANDLEON_PLAYSTATION_FRAME=/out/frame.bin \
        --volume "${exe_dir}:/exe:ro" \
        --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
        --volume "${frame_dir}:/out" \
        --workdir /tmp \
        "${image}" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter \
            -dofile /harness/playstation_probe.lua \
            -loadexe "/exe/${name}.ps-exe" \
        > "${log}" 2>&1
    run_status=$?
    set -e

    if [ -f "${frame_dir}/frame.bin" ]; then
        mv "${frame_dir}/frame.bin" "${frame}"
    fi

    if [ "${run_status}" -eq 124 ] || [ "${run_status}" -eq 137 ]; then
        echo "error: ${name} did not finish in ${timeout_seconds}s." >&2
        tail -n 40 "${log}" >&2 || true
        status=1
        continue
    fi

    # The verdict and the count behind it. `HARNESS RESULT PASS 0/0` carries the
    # word and measured nothing, so the decision is made by the one script every
    # console here shares rather than by a `grep` written again per caller.
    if ! verdict="$(
            "${repository_root}/scripts/assert-harness-verdict.sh" \
                --log "${log}" --require "^HARNESS RESULT PASS " \
                --min-checks "${min_checks}" \
                --what "the render harness on ${name}"
        )"; then
        echo "The emulator exited ${run_status}. Log: ${log}" >&2
        grep "^HARNESS" "${log}" >&2 || echo "  (the harness never ran)" >&2
        status=1
        continue
    fi

    if [ "${run_status}" -ne 0 ]; then
        echo "error: the harness says pass but the exit code is ${run_status}." >&2
        echo "The two channels disagree; log: ${log}" >&2
        status=1
        continue
    fi

    if [ ! -s "${frame}" ]; then
        echo "error: ${name} produced no frame at ${frame}." >&2
        status=1
        continue
    fi

    if ! "${joiner}" "${log}" "${frame}" "${frame_dir}/${name}.ppm"; then
        echo "error: what ${name} drew is not what it said it drew." >&2
        echo "Log: ${log}" >&2
        status=1
        continue
    fi

    printf '%s\n' "${verdict}"
done

if [ "${status}" -ne 0 ]; then
    exit "${status}"
fi

# The negative control, and it runs every time rather than being a note in a
# README somebody could stop believing.
#
# A harness whose checks a blank screen would also satisfy is not evidence, so
# this points the same harness at the conformance executable, which never
# touches the GPU at all, and requires it to *fail*. It never signals the host
# either, so the capture is fired from the first vertical retrace instead;
# GRANDLEON_PLAYSTATION_CAPTURE_ON_VSYNC is what turns that on and it exists for
# no other purpose.
#
# Its absence is a failure of this run and not a note in passing: a harness that
# has not been shown to fail is a harness nobody knows the meaning of, and a
# green run that skipped its own control is exactly the thing this check exists
# to make impossible. The build that produces the play executables produces this
# one too, so there is no ordinary way to be here without it.
control="${GRANDLEON_PLAYSTATION_CONTROL:-grandleon_psx}"
control_log="${build_dir}/render-control-${control}.log"
if [ ! -f "${exe_dir}/${control}.ps-exe" ]; then
    echo "error: ${exe_dir}/${control}.ps-exe does not exist, so the harness" >&2
    echo "was never required to fail on something that draws nothing." >&2
    echo "Build it first: cmake --build build --target grandleon_playstation" >&2
    exit 1
fi

set +e
timeout --kill-after=10 "${timeout_seconds}" \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_PLAYSTATION_FRAME=/out/control.bin \
    --env GRANDLEON_PLAYSTATION_CAPTURE_ON_VSYNC=1 \
    --volume "${exe_dir}:/exe:ro" \
    --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
    --volume "${frame_dir}:/out" \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -dofile /harness/playstation_probe.lua \
        -loadexe "/exe/${control}.ps-exe" \
    > "${control_log}" 2>&1
set -e

if grep -q "^HARNESS RESULT PASS" "${control_log}"; then
    echo "error: the render-less ${control} passed the render harness." >&2
    echo "The harness is measuring nothing. Log: ${control_log}" >&2
    exit 1
fi
if ! grep -q "^HARNESS RESULT FAIL" "${control_log}"; then
    echo "error: the negative control never reached a verdict." >&2
    echo "Log: ${control_log}" >&2
    exit 1
fi
failed_checks="$(grep -c "^HARNESS CHECK .* FAIL" "${control_log}" || true)"
echo "Negative control: ${control} fails ${failed_checks} of the harness's checks."

echo "Every PlayStation play executable drew what it claimed."
