#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs the scratch measurement program with an observer attached, and turns what
# it photographed into pictures a person can look at.
#
#   platform/playstation/scratch/run-scratch3d-film.sh
#
# This is not a check and it is in no gate. `run-playstation.sh` runs the same
# executable and reads its text, which is all the numbers need; this attaches
# scratch3d_probe.lua to the same pinned emulator so that the frames the program
# names get written out, and then assembles them.
#
# Why a script of its own rather than a flag on run-playstation.sh: that script
# is gate machinery for three shipped executables, the scratch is neither gated
# nor shipped, and "run-playstation.sh needs nothing added to it" is a property
# worth keeping. Nothing here is on any gate's path.
#
# What comes out, into platform/playstation/scratch/evidence/: the stills and
# films that directory's README describes, one set per question the scratch
# asks. The stills are committed; the films are assembled beside them and are
# not.
#
# The still ordinals and the film windows are not hard-coded here: the guest
# prints `SHOT` lines saying which ordinal is which, and this reads them.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

# The emulator revision is not repeated here. There is exactly one pin in this
# repository and it lives in cmake/GrandleonPlayStation.cmake; a second copy in
# a scratch script is the sort of thing somebody later mistakes for authority.
# Read from there when the environment does not already carry it, which it does
# when this is reached through a CMake target.
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
if [ -z "${revision}" ]; then
    revision="$(sed -n '/set(GRANDLEON_PCSX_REDUX_REVISION/,/)/{s/^ *"\([0-9a-f]\{40\}\)".*/\1/p}' \
        "${repository_root}/cmake/GrandleonPlayStation.cmake")"
fi
if [ -z "${revision}" ]; then
    echo "error: could not read the pinned PCSX-Redux revision." >&2
    exit 1
fi
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
timeout_seconds="${GRANDLEON_PLAYSTATION_TIMEOUT:-3600}"
python="${GRANDLEON_PYTHON:-${repository_root}/tools/placeholder_art/.venv/bin/python}"

image="grandleon/playstation-emulator:${revision}"
exe_dir="${build_dir}/target/platform/playstation"
executable="${exe_dir}/grandleon_psx_scratch3d.ps-exe"
frame_dir="${build_dir}/scratch3d-frames"
evidence="${repository_root}/platform/playstation/scratch/evidence"
log="${build_dir}/scratch3d-film.log"

if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist." >&2
    echo "Build it first:" >&2
    echo "  cmake --build build --target grandleon_playstation_scratch3d" >&2
    exit 1
fi
if ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    echo "error: ${image} is not built." >&2
    echo "Run the scratch target once first; it builds the emulator image." >&2
    exit 1
fi
if [ ! -x "${python}" ]; then
    echo "error: ${python} is not executable; run scripts/setup.sh." >&2
    exit 1
fi

# The film always belongs to the run that produced it: a directory whose
# contents depend on which run last half-finished is not evidence.
rm -rf "${frame_dir}"
mkdir -p "${frame_dir}" "${evidence}"

set +e
timeout --kill-after=10 "${timeout_seconds}" \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_SCRATCH3D_FRAMES=/out \
    --volume "${exe_dir}:/exe:ro" \
    --volume "${repository_root}/platform/playstation/scratch:/scratch:ro" \
    --volume "${frame_dir}:/out" \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -dofile /scratch/scratch3d_probe.lua \
        -loadexe /exe/grandleon_psx_scratch3d.ps-exe \
    > "${log}" 2>&1
status=$?
set -e

if [ "${status}" -eq 124 ] || [ "${status}" -eq 137 ]; then
    echo "error: the filmed run did not finish in ${timeout_seconds}s." >&2
    tail -n 40 "${log}" >&2 || true
    exit 1
fi
if ! grep -q "^RESULT PASS" "${log}"; then
    echo "error: the filmed run did not report a pass. Log: ${log}" >&2
    grep -m5 "FAIL" "${log}" >&2 || true
    exit 1
fi
if [ "${status}" -ne 0 ]; then
    echo "error: the report says pass but the exit code is ${status}." >&2
    exit 1
fi

grep -m1 -o "^RESULT PASS.*" "${log}"
echo "Captured $(find "${frame_dir}" -name 'frame-*.bin' | wc -l) frames."

GRANDLEON_SCRATCH3D_LOG="${log}" \
GRANDLEON_SCRATCH3D_FRAME_DIR="${frame_dir}" \
GRANDLEON_SCRATCH3D_EVIDENCE="${evidence}" \
"${python}" "$(dirname "${BASH_SOURCE[0]}")/scratch3d_evidence.py"
