#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Runs the Steel and Cedar Twin Gate visual proof through the real PS1 GPU
# executable and checks both the console's measured budgets and its captured
# framebuffers (five proof states, or six production-runtime review views).
# Optional GRANDLEON_PLAYSTATION_TWIN_GATE_FILM_DIR records every frame without
# weakening the same verdict.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
timeout_seconds="${GRANDLEON_PLAYSTATION_TWIN_GATE_TIMEOUT:-300}"
mode="quality"
if [ "${1:-}" = "--runtime" ]; then
    mode="runtime"
    shift
fi
if [ "$#" -ne 0 ]; then
    echo "usage: $(basename "$0") [--runtime]" >&2
    exit 2
fi
if [ "${mode}" = "runtime" ]; then
    name="grandleon_psx_twin_gate_runtime_capture"
    harness_floor=29
    guest_floor=22
else
    name="grandleon_psx_twin_gate_quality"
    harness_floor=27
    guest_floor=17
fi

for required in revision base_digest; do
    if [ -z "${!required}" ]; then
        echo "error: ${required} is not set." >&2
        exit 1
    fi
done
if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

image="grandleon/playstation-emulator:${revision}"
pinned_base="${base_image}@${base_digest}"
if ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${pinned_base}" >/dev/null 2>&1; then
        "${docker}" pull "${pinned_base}"
    fi
    "${docker}" build \
        --file "${repository_root}/platform/playstation/emulator/Containerfile" \
        --build-arg "PCSX_REDUX_REVISION=${revision}" \
        --tag "${image}" \
        "${repository_root}/platform/playstation/emulator"
fi
installed_revision="$("${docker}" run --rm "${image}" \
    cat /usr/local/PCSX_REDUX_REVISION)"
installed_revision="${installed_revision//[$'\r\n']/}"
if [ "${installed_revision}" != "${revision}" ]; then
    echo "error: ${image} has revision ${installed_revision}; wanted ${revision}." >&2
    exit 1
fi

executable_dir="${build_dir}/target/platform/playstation"
executable="${executable_dir}/${name}.ps-exe"
frame_dir="${build_dir}/twin-gate-${mode}-frames"
log="${build_dir}/twin-gate-${mode}.log"
if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist; build the quality target first." >&2
    exit 1
fi
rm -rf "${frame_dir}"
mkdir -p "${frame_dir}"

film_dir="${GRANDLEON_PLAYSTATION_TWIN_GATE_FILM_DIR:-}"
film_args=()
if [ -n "${film_dir}" ]; then
    mkdir -p "${film_dir}"
    film_args=(
        --env GRANDLEON_PLAYSTATION_FILM_DIR=/film
        --env GRANDLEON_PLAYSTATION_FILM_FROM=1
        --env GRANDLEON_PLAYSTATION_FILM_TO=5
        --volume "${film_dir}:/film"
    )
fi

"${repository_root}/scripts/assert-harness-verdict.sh" --self-test
set +e
timeout --kill-after=10 "${timeout_seconds}" \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_PLAYSTATION_FRAME_DIR=/out \
    --volume "${executable_dir}:/exe:ro" \
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
status=$?
set -e

if [ "${status}" -ne 0 ]; then
    echo "error: ${name} exited ${status}; log: ${log}" >&2
    tail -n 50 "${log}" >&2 || true
    exit 1
fi
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^HARNESS RESULT PASS " \
    --min-checks "${harness_floor}" \
    --what "the Twin Gate framebuffer observer"
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^RESULT PASS " --min-checks "${guest_floor}" \
    --what "the Twin Gate R3000A proof"
if [ "${mode}" = "runtime" ]; then
    python3 \
        "${repository_root}/platform/playstation/harness/check_twin_gate_runtime.py" \
        "${frame_dir}"
else
    python3 \
        "${repository_root}/platform/playstation/harness/check_twin_gate_quality.py" \
        "${frame_dir}"
fi

grep -E "^(VRAM|FRAME|HEAP|RESULT|HARNESS RESULT)" "${log}"
echo "Twin Gate ${mode} proof passed. Frames: ${frame_dir}; log: ${log}"
if [ -n "${film_dir}" ]; then
    echo "Film: ${film_dir} ($(find "${film_dir}" -name 'film-*.bin' | wc -l) frames)"
fi
