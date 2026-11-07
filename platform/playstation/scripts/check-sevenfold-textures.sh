#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

# Enter through CMake when invoked directly so the emulator/toolchain pins have
# the same single source of truth as every other PlayStation check.
if [ -z "${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}" ] || \
   [ -z "${GRANDLEON_NUGGET_REVISION:-}" ] || \
   [ -z "${GRANDLEON_PCSX_REDUX_REVISION:-}" ]; then
    cmake -S "${repository_root}" -B "${repository_root}/build" \
        -DGRANDLEON_BUILD_TESTS=ON
    exec cmake --build "${repository_root}/build" \
        --target grandleon_playstation_sevenfold_texture_check
fi

build_dir="${GRANDLEON_PSX_SEVENFOLD_3D_BUILD_DIR:-${repository_root}/build-playstation-sevenfold-3d}"
GRANDLEON_PSX_SEVENFOLD_3D_BUILD_DIR="${build_dir}" \
    "${repository_root}/platform/playstation/scripts/build-sevenfold-3d.sh"

executable="${build_dir}/target/platform/playstation/grandleon_psx_sevenfold_texture_experiment.ps-exe"
if [ ! -f "${executable}" ]; then
    echo "error: PlayStation texture experiment is missing: ${executable}" >&2
    exit 1
fi

frame_dir="${build_dir}/texture-frames"
log="${build_dir}/sevenfold-texture-render.log"
mkdir -p "${frame_dir}"
rm -f \
    "${frame_dir}/baseline.bin" \
    "${frame_dir}/medieval-textured.bin" \
    "${frame_dir}/mythical-textured.bin" \
    "${frame_dir}/baseline.ppm" \
    "${frame_dir}/medieval-textured.ppm" \
    "${frame_dir}/mythical-textured.ppm"

docker="${GRANDLEON_DOCKER:-docker}"
image="grandleon/playstation-emulator:${GRANDLEON_PCSX_REDUX_REVISION}"
set +e
timeout --kill-after=10 300 \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_PLAYSTATION_FRAME_DIR=/out \
    --volume "${build_dir}/target/platform/playstation:/exe:ro" \
    --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
    --volume "${frame_dir}:/out" \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -dofile /harness/playstation_texture_probe.lua \
        -loadexe "/exe/$(basename "${executable}")" \
    > "${log}" 2>&1
status=$?
set -e

if [ "${status}" -ne 0 ]; then
    echo "error: PlayStation texture experiment exited ${status}." >&2
    tail -n 80 "${log}" >&2 || true
    exit 1
fi
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^RESULT PASS " --min-checks 11 \
    --what "the PlayStation texture experiment guest"
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^HARNESS RESULT PASS " --min-checks 18 \
    --what "the PlayStation texture capture harness"

for name in baseline medieval-textured mythical-textured; do
    frame="${frame_dir}/${name}.bin"
    ppm="${frame_dir}/${name}.ppm"
    if [ ! -s "${frame}" ]; then
        echo "error: texture experiment captured no ${name} frame." >&2
        exit 1
    fi
    python3 - "${frame}" "${ppm}" <<'PYEOF'
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_bytes()
if len(source) != 320 * 240 * 2:
    raise SystemExit(f"unexpected frame size: {len(source)}")
out = bytearray(b"P6\n320 240\n255\n")
for offset in range(0, len(source), 2):
    value = source[offset] | source[offset + 1] << 8
    out.extend(((value & 31) * 255 // 31,
                ((value >> 5) & 31) * 255 // 31,
                ((value >> 10) & 31) * 255 // 31))
pathlib.Path(sys.argv[2]).write_bytes(out)
PYEOF
done

grep '^BENCH ' "${log}"
echo "PlayStation Sevenfold texture experiment passed: ${executable}"
echo "Native comparison captures: ${frame_dir}/*.ppm"
