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

for command in ffmpeg mktemp; do
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

"${script_dir}/capture-graphics-prototypes.sh" "${build_dir}" "${output_dir}"

GRANDLEON_DEMO_SECONDS=20 \
GRANDLEON_DEMO_GRAPHICS=voxel-sprites \
    "${script_dir}/record-demo-reel.sh" \
    "${build_dir}" "${temporary_dir}/gameplay.mp4"

ffmpeg -hide_banner -loglevel warning -y \
    -ss 8.5 -t 6 -i "${temporary_dir}/gameplay.mp4" \
    -filter_complex \
    '[0:v]fps=12,scale=640:-1:flags=lanczos,split[video][palette_input];[palette_input]palettegen=max_colors=128[palette];[video][palette]paletteuse=dither=bayer:bayer_scale=3' \
    "${output_dir}/gameplay.gif"

printf 'README Godot media: %s\n' "${output_dir}"
