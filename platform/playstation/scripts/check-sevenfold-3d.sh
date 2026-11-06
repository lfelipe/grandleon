#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

# See build-sevenfold-3d.sh: direct invocations enter through the CMake target
# so the toolchain and emulator pins still have exactly one source of truth.
if [ -z "${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}" ] || \
   [ -z "${GRANDLEON_NUGGET_REVISION:-}" ] || \
   [ -z "${GRANDLEON_PCSX_REDUX_REVISION:-}" ]; then
    cmake -S "${repository_root}" -B "${repository_root}/build" \
        -DGRANDLEON_BUILD_TESTS=ON
    exec cmake --build "${repository_root}/build" \
        --target grandleon_playstation_sevenfold_3d_check
fi

build_dir="${GRANDLEON_PSX_SEVENFOLD_3D_BUILD_DIR:-${repository_root}/build-playstation-sevenfold-3d}"
skip_build=0
rebuild_argument=()

usage() {
    echo "usage: $(basename "$0") [--skip-build] [--rebuild-image]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-build) skip_build=1; shift ;;
        --rebuild-image) rebuild_argument=(--rebuild-image); shift ;;
        *) usage ;;
    esac
done

# Refuse generated data that no longer matches its recorded generator. Both
# console paths consume this same file, so this is one provenance question.
python3 - "${repository_root}" <<'PYEOF'
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
source = root / "tools/console_3d/generate_sevenfold_roster.py"
header = root / "tools/console_3d/generated/sevenfold_roster_proxies.h"
report = json.loads(
    (root / "tools/console_3d/generated/sevenfold_roster_proxies.json")
    .read_text(encoding="utf-8")
)
if hashlib.sha256(source.read_bytes()).hexdigest() != report["generator_sha256"]:
    raise SystemExit("Sevenfold 3D generator changed; regenerate the roster")
if hashlib.sha256(header.read_bytes()).hexdigest() != report["generated_header_sha256"]:
    raise SystemExit("Sevenfold 3D header changed; regenerate the roster")
if report["totals"]["combinations"] != 112 or len(report["models"]) != 112:
    raise SystemExit("Sevenfold 3D report does not cover 7 x 8 x 2 models")
print("Sevenfold 3D roster provenance passed: 112 combinations")
PYEOF

if [ "${skip_build}" -eq 0 ]; then
    GRANDLEON_PSX_SEVENFOLD_3D_BUILD_DIR="${build_dir}" \
        "${repository_root}/platform/playstation/scripts/build-sevenfold-3d.sh" \
        "${rebuild_argument[@]}"
fi

executable="${build_dir}/target/platform/playstation/grandleon_psx_sevenfold_3d_check.ps-exe"
if [ ! -f "${executable}" ]; then
    echo "error: Sevenfold 3D check executable is missing: ${executable}" >&2
    exit 1
fi

# The ordinary runner checks the guest's report and process exit code. The
# render harness run below independently checks the GPU registers and captures
# the final action frame.
GRANDLEON_PLAYSTATION_BUILD_DIR="${build_dir}" \
GRANDLEON_PLAYSTATION_EXECUTABLE="$(basename "${executable}")" \
GRANDLEON_PLAYSTATION_MIN_CHECKS=290 \
    "${repository_root}/platform/playstation/scripts/run-playstation.sh"

revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
docker="${GRANDLEON_DOCKER:-docker}"
image="grandleon/playstation-emulator:${revision}"
frame_dir="${build_dir}/frames"
log="${build_dir}/sevenfold-3d-render.log"
mkdir -p "${frame_dir}"
rm -f "${frame_dir}/frame.bin"

set +e
timeout --kill-after=10 300 \
    "${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env GRANDLEON_PLAYSTATION_FRAME=/out/frame.bin \
    --volume "${build_dir}/target/platform/playstation:/exe:ro" \
    --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
    --volume "${frame_dir}:/out" \
    --workdir /tmp \
    "${image}" \
    stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter \
        -dofile /harness/playstation_probe.lua \
        -loadexe "/exe/$(basename "${executable}")" \
    > "${log}" 2>&1
status=$?
set -e

if [ "${status}" -ne 0 ]; then
    echo "error: PlayStation Sevenfold 3D render check exited ${status}." >&2
    tail -n 50 "${log}" >&2 || true
    exit 1
fi
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^RESULT PASS " --min-checks 290 \
    --what "the PlayStation Sevenfold 3D guest"
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^HARNESS RESULT PASS " --min-checks 16 \
    --what "the PlayStation Sevenfold 3D render harness"
if [ ! -s "${frame_dir}/frame.bin" ]; then
    echo "error: the PlayStation Sevenfold 3D check captured no frame." >&2
    exit 1
fi

python3 - "${frame_dir}/frame.bin" "${frame_dir}/sevenfold-3d.ppm" <<'PYEOF'
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

echo "PlayStation Sevenfold 3D check passed: ${executable}"
echo "Captured frame: ${frame_dir}/sevenfold-3d.ppm"
