#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_dir="${2:-${repo_root}/docs/screenshots/godot}"

case "${build_dir}" in
    /*) ;;
    *) build_dir="$(pwd -P)/${build_dir}" ;;
esac
case "${output_dir}" in
    /*) ;;
    *) output_dir="$(pwd -P)/${output_dir}" ;;
esac

for command in ffmpeg gifsicle mktemp; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Required README capture tool not found: %s\n' "${command}" >&2
        exit 69
    fi
done

mkdir -p "${build_dir}" "${output_dir}"
temporary_dir="$(mktemp -d "${build_dir}/readme-media.XXXXXX")"
cleanup() {
    rm -r -- "${temporary_dir}"
}
trap cleanup EXIT

profiles_dir="${temporary_dir}/profiles"
"${script_dir}/capture-graphics-prototypes.sh" "${build_dir}" "${profiles_dir}"
cp -- "${profiles_dir}/voxel-sprites.png" "${output_dir}/voxel-sprites.png"

GRANDLEON_DEMO_SECONDS=34 \
GRANDLEON_DEMO_GRAPHICS=voxel-sprites \
    "${script_dir}/record-demo-reel.sh" \
    "${build_dir}" "${temporary_dir}/gameplay.mp4"

ffmpeg -hide_banner -loglevel warning -y \
    -ss 20.75 -t 9 -i "${temporary_dir}/gameplay.mp4" \
    -filter_complex \
    '[0:v]fps=10,scale=640:-1:flags=lanczos,split[video][palette_input];[palette_input]palettegen=max_colors=96[palette];[video][palette]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle' \
    "${temporary_dir}/gameplay.gif"
gifsicle -O3 --lossy=45 --colors 96 \
    "${temporary_dir}/gameplay.gif" -o "${output_dir}/gameplay.gif"

# The wide reel proves the complete interaction, but its menus and small
# figures are poor locomotion evidence. Record a clean pair of multi-cell moves
# and keep them at a closer crop and higher frame rate for the human artwork
# guide and review surface.
GRANDLEON_DEMO_SECONDS=24 \
GRANDLEON_DEMO_GRAPHICS=voxel-sprites \
GRANDLEON_DEMO_MODE=movement-reel \
    "${script_dir}/record-demo-reel.sh" \
    "${build_dir}" "${temporary_dir}/movement.mp4"

ffmpeg -hide_banner -loglevel warning -y \
    -ss 18.5 -t 4.5 -i "${temporary_dir}/movement.mp4" \
    -filter_complex \
    '[0:v]fps=15,crop=720:540:160:70,scale=720:-1:flags=lanczos,split[video][palette_input];[palette_input]palettegen=max_colors=128[palette];[video][palette]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle' \
    "${temporary_dir}/movement.gif"
gifsicle -O3 --lossy=35 --colors 128 \
    "${temporary_dir}/movement.gif" -o "${output_dir}/movement.gif"

printf 'README Godot media: %s\n' "${output_dir}"
