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
"${repository_root}/platform/playstation/tools/generate-godot-closeups.py" \
    --output "${repository_root}/platform/playstation/prototype/generated/godot_closeup_ci4.h" \
    --check
"${repository_root}/platform/playstation/tools/generate-terrain-quality.py" \
    --output "${repository_root}/platform/playstation/prototype/generated/quality_terrain_ci4.h" \
    --check
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
    "${frame_dir}/medieval-flat.bin" \
    "${frame_dir}/medieval-current.bin" \
    "${frame_dir}/medieval-low-angle.bin" \
    "${frame_dir}/medieval-screen-stage-rejected.bin" \
    "${frame_dir}/medieval-camera-v2.bin" \
    "${frame_dir}/medieval-camera-v2-96.bin" \
    "${frame_dir}/mythical-flat.bin" \
    "${frame_dir}/mythical-current.bin" \
    "${frame_dir}/mythical-low-angle.bin" \
    "${frame_dir}/mythical-screen-stage-rejected.bin" \
    "${frame_dir}/mythical-camera-v2.bin" \
    "${frame_dir}/overview-flat.bin" \
    "${frame_dir}/overview-hybrid.bin" \
    "${frame_dir}/overview-camera-v2.bin" \
    "${frame_dir}/medieval-flat.ppm" \
    "${frame_dir}/medieval-current.ppm" \
    "${frame_dir}/medieval-low-angle.ppm" \
    "${frame_dir}/medieval-screen-stage-rejected.ppm" \
    "${frame_dir}/medieval-camera-v2.ppm" \
    "${frame_dir}/medieval-camera-v2-96.ppm" \
    "${frame_dir}/mythical-flat.ppm" \
    "${frame_dir}/mythical-current.ppm" \
    "${frame_dir}/mythical-low-angle.ppm" \
    "${frame_dir}/mythical-screen-stage-rejected.ppm" \
    "${frame_dir}/mythical-camera-v2.ppm" \
    "${frame_dir}/overview-flat.ppm" \
    "${frame_dir}/overview-hybrid.ppm" \
    "${frame_dir}/overview-camera-v2.ppm" \
    "${frame_dir}/overview-movement.gif" \
    "${frame_dir}/overview-movement-camera-v2.gif" \
    "${frame_dir}/close-exchange-camera-v2.gif" \
    "${frame_dir}/renderer-comparison-playstation.gif" \
    "${frame_dir}/renderer-comparison-playstation.mp4" \
    "${frame_dir}/comparison-board.png" \
    "${frame_dir}/comparison-focus.png" \
    "${frame_dir}/comparison-attack-impact.png" \
    "${frame_dir}/comparison-counter-impact.png" \
    "${frame_dir}/comparison-restored.png" \
    "${frame_dir}/terrain-close-before.png" \
    "${frame_dir}/terrain-close-after.png" \
    "${frame_dir}/terrain-board-before.png" \
    "${frame_dir}/terrain-board-after.png"
for index in 00 01 02 03 04 05 06 07; do
    rm -f "${frame_dir}/movement-${index}.bin" \
        "${frame_dir}/movement-${index}.ppm"
done
for index in $(seq -w 0 15); do
    rm -f "${frame_dir}/movement-camera-v2-${index}.bin" \
        "${frame_dir}/movement-camera-v2-${index}.ppm"
done
for index in $(seq -w 0 39); do
    rm -f "${frame_dir}/close-exchange-camera-v2-${index}.bin" \
        "${frame_dir}/close-exchange-camera-v2-${index}.ppm"
done
for index in $(seq -w 0 51); do
    rm -f "${frame_dir}/comparison-sequence-${index}.bin" \
        "${frame_dir}/comparison-sequence-${index}.ppm"
done

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
    --log "${log}" --require "^RESULT PASS " --min-checks 32 \
    --what "the PlayStation texture experiment guest"
"${repository_root}/scripts/assert-harness-verdict.sh" \
    --log "${log}" --require "^HARNESS RESULT PASS " --min-checks 680 \
    --what "the PlayStation texture capture harness"

frames=( \
    medieval-flat medieval-current medieval-low-angle \
    medieval-screen-stage-rejected medieval-camera-v2 medieval-camera-v2-96 \
    medieval-terrain-before \
    mythical-flat mythical-current mythical-low-angle \
    mythical-screen-stage-rejected mythical-camera-v2 \
    overview-flat overview-hybrid overview-camera-v2 overview-terrain-before \
    movement-00 movement-01 movement-02 movement-03 \
    movement-04 movement-05 movement-06 movement-07
)
for index in $(seq -w 0 15); do
    frames+=("movement-camera-v2-${index}")
done
for index in $(seq -w 0 39); do
    frames+=("close-exchange-camera-v2-${index}")
done
for index in $(seq -w 0 51); do
    frames+=("comparison-sequence-${index}")
done
for name in "${frames[@]}"; do
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

python3 - "${frame_dir}" <<'PYEOF'
import pathlib
import sys

from PIL import Image

frame_dir = pathlib.Path(sys.argv[1])
frames = [
    Image.open(frame_dir / f"movement-{index:02d}.ppm").convert("RGB")
    for index in range(8)
]
frames[0].save(
    frame_dir / "overview-movement.gif",
    save_all=True,
    append_images=frames[1:],
    duration=120,
    loop=0,
    disposal=2,
)
camera_v2_frames = [
    Image.open(frame_dir / f"movement-camera-v2-{index:02d}.ppm").convert("RGB")
    for index in range(16)
]
camera_v2_frames[0].save(
    frame_dir / "overview-movement-camera-v2.gif",
    save_all=True,
    append_images=camera_v2_frames[1:],
    duration=80,
    loop=0,
    disposal=2,
)
exchange_frames = [
    Image.open(frame_dir / f"close-exchange-camera-v2-{index:02d}.ppm").convert("RGB")
    for index in range(40)
]
exchange_frames[0].save(
    frame_dir / "close-exchange-camera-v2.gif",
    save_all=True,
    append_images=exchange_frames[1:],
    duration=100,
    loop=0,
    disposal=2,
)
comparison_frames = [
    Image.open(frame_dir / f"comparison-sequence-{index:02d}.ppm").convert("RGB")
    for index in range(52)
]
# Exactly 8.15 seconds. Attack and counter impacts begin at 3.45 and 5.50,
# matching tools/renderer_comparison/scene.json without retiming the footage.
combat_durations = [100] * 12 + [110] * 7 + [80]
durations = (
    [580, 570] + [280, 280, 270, 270] +
    combat_durations + combat_durations +
    [240, 240, 240, 230] + [430, 420]
)
assert len(comparison_frames) == len(durations) == 52
assert sum(durations) == 8150
comparison_frames[0].save(
    frame_dir / "renderer-comparison-playstation.gif",
    save_all=True,
    append_images=comparison_frames[1:],
    duration=durations,
    loop=0,
    disposal=2,
)
for output, index in {
    "comparison-board.png": 0,
    "comparison-focus.png": 3,
    "comparison-attack-impact.png": 18,
    "comparison-counter-impact.png": 38,
    "comparison-restored.png": 50,
}.items():
    comparison_frames[index].save(frame_dir / output)
for output, source in {
    "terrain-close-before.png": "medieval-terrain-before.ppm",
    "terrain-close-after.png": "medieval-camera-v2.ppm",
    "terrain-board-before.png": "overview-terrain-before.ppm",
    "terrain-board-after.png": "overview-camera-v2.ppm",
}.items():
    Image.open(frame_dir / source).convert("RGB").save(frame_dir / output)
PYEOF

ffmpeg -hide_banner -loglevel error -y \
    -i "${frame_dir}/renderer-comparison-playstation.gif" \
    -vf "fps=30,format=yuv420p" -t 8.15 \
    -an "${frame_dir}/renderer-comparison-playstation.mp4"
comparison_duration="$(ffprobe -v error -show_entries format=duration \
    -of default=noprint_wrappers=1:nokey=1 \
    "${frame_dir}/renderer-comparison-playstation.mp4")"
awk -v actual="${comparison_duration}" 'BEGIN {
    if (actual < 8.14 || actual > 8.18) exit 1
}' || {
    echo "error: PlayStation renderer comparison duration is ${comparison_duration}s" >&2
    exit 1
}

grep '^BENCH ' "${log}"
echo "PlayStation Sevenfold texture experiment passed: ${executable}"
echo "Native comparison captures: ${frame_dir}/*.ppm"
echo "Native movement film: ${frame_dir}/overview-movement.gif"
echo "Native camera-v2 movement film: ${frame_dir}/overview-movement-camera-v2.gif"
echo "Native camera-v2 exchange film: ${frame_dir}/close-exchange-camera-v2.gif"
echo "Matched 8.15s renderer clip: ${frame_dir}/renderer-comparison-playstation.mp4"
echo "Matched native GIF: ${frame_dir}/renderer-comparison-playstation.gif"
echo "Terrain before/after stills: ${frame_dir}/terrain-*-before.png and *-after.png"
