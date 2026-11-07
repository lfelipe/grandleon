#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

if [ -z "${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}" ] || \
   [ -z "${GRANDLEON_NUGGET_REVISION:-}" ] || \
   [ -z "${GRANDLEON_PCSX_REDUX_REVISION:-}" ]; then
    cmake -S "${repository_root}" -B "${repository_root}/build" \
        -DGRANDLEON_BUILD_TESTS=ON
    exec cmake --build "${repository_root}/build" \
        --target grandleon_playstation_sevenfold_2_5d_check
fi

build_dir="${GRANDLEON_PSX_SEVENFOLD_2_5D_BUILD_DIR:-${repository_root}/build-playstation-sevenfold-2-5d}"
"${repository_root}/platform/playstation/tools/generate-godot-closeups.py" \
    --output "${repository_root}/platform/playstation/prototype/generated/godot_closeup_ci4.h" \
    --check
"${repository_root}/platform/playstation/tools/generate-terrain-quality.py" \
    --output "${repository_root}/platform/playstation/prototype/generated/quality_terrain_ci4.h" \
    --check
"${repository_root}/platform/playstation/tools/generate-renderer-comparison-scene.py" \
    --scene "${repository_root}/tools/renderer_comparison/scene.json" \
    --output "${repository_root}/platform/playstation/prototype/generated/renderer_comparison_scene.h" \
    --check
"${repository_root}/platform/playstation/tools/generate-combat-backdrop.py" \
    --scene "${repository_root}/tools/renderer_comparison/scene.json" \
    --output "${repository_root}/platform/playstation/prototype/generated/combat_backdrop_ci8.h" \
    --check
GRANDLEON_PSX_SEVENFOLD_2_5D_BUILD_DIR="${build_dir}" \
    "${repository_root}/platform/playstation/scripts/build-sevenfold-2-5d.sh"

executable="${build_dir}/target/platform/playstation/grandleon_psx_sevenfold_2_5d.ps-exe"
if [ ! -f "${executable}" ]; then
    echo "error: PlayStation 2.5D renderer is missing: ${executable}" >&2
    exit 1
fi

frame_dir="${build_dir}/renderer-frames"
log="${build_dir}/sevenfold-2-5d-render.log"
mkdir -p "${frame_dir}"
find "${frame_dir}" -maxdepth 1 -type f -delete

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
    echo "error: PlayStation 2.5D renderer exited ${status}." >&2
    tail -n 80 "${log}" >&2 || true
    exit 1
fi
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^RESULT PASS " --min-checks 18 \
    --what "the PlayStation 2.5D renderer guest"
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^HARNESS RESULT PASS " --min-checks 561 \
    --what "the PlayStation 2.5D capture harness"

frames=(tactical-board combat-backdrop)
for index in $(seq -w 0 15); do frames+=("movement-${index}"); done
for index in $(seq -w 0 39); do frames+=("exchange-${index}"); done
for index in $(seq -w 0 51); do frames+=("comparison-${index}"); done
for name in "${frames[@]}"; do
    frame="${frame_dir}/${name}.bin"
    ppm="${frame_dir}/${name}.ppm"
    if [ ! -s "${frame}" ]; then
        echo "error: renderer captured no ${name} frame." >&2
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

python3 - "${frame_dir}" <<'PYEOF'
import pathlib
import sys
from PIL import Image

frame_dir = pathlib.Path(sys.argv[1])

def film(prefix: str, count: int, output: str, duration: int) -> None:
    frames = [
        Image.open(frame_dir / f"{prefix}-{index:02d}.ppm").convert("RGB")
        for index in range(count)
    ]
    frames[0].save(frame_dir / output, save_all=True,
                   append_images=frames[1:], duration=duration,
                   loop=0, disposal=2)

film("movement", 16, "movement.gif", 80)
film("exchange", 40, "exchange.gif", 100)

comparison = [
    Image.open(frame_dir / f"comparison-{index:02d}.ppm").convert("RGB")
    for index in range(52)
]
combat_durations = [100] * 12 + [110] * 7 + [80]
durations = ([580, 570] + [280, 280, 270, 270] + combat_durations +
             combat_durations + [240, 240, 240, 230] + [430, 420])
assert len(comparison) == len(durations) == 52
assert sum(durations) == 8150
comparison[0].save(frame_dir / "renderer-comparison-playstation.gif",
                   save_all=True, append_images=comparison[1:],
                   duration=durations, loop=0, disposal=2)
for output, index in {
    "comparison-board.png": 0,
    "comparison-focus.png": 3,
    "comparison-attack-impact.png": 18,
    "comparison-counter-impact.png": 38,
    "comparison-restored.png": 50,
}.items():
    comparison[index].save(frame_dir / output)
Image.open(frame_dir / "tactical-board.ppm").convert("RGB").save(
    frame_dir / "tactical-board.png")
Image.open(frame_dir / "combat-backdrop.ppm").convert("RGB").save(
    frame_dir / "combat-backdrop.png")
PYEOF

ffmpeg -hide_banner -loglevel error -y \
    -i "${frame_dir}/renderer-comparison-playstation.gif" \
    -vf "fps=30,format=yuv420p" -t 8.15 -an \
    "${frame_dir}/renderer-comparison-playstation.mp4"

grep '^BENCH ' "${log}"
echo "PlayStation Sevenfold 2.5D renderer passed: ${executable}"
echo "Native captures: ${frame_dir}"
