#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds Sevenfold in the two N64 forms useful to reviewers:
#
#   sprite  the complete playable campaign, including its 1P/2P choice
#   2-5d    the fixed-camera board and close-battle renderer gate

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
renderer=both
stage_picker=0
output_dir="${GRANDLEON_N64_SEVENFOLD_OUTPUT_DIR:-${repository_root}/build-n64-sevenfold-roms}"
rebuild_argument=()

usage() {
    echo "usage: $(basename "$0") [--renderer sprite|2-5d|both]" \
         "[--stage-picker] [--output DIR] [--rebuild-image]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --renderer)
            [ "$#" -ge 2 ] || usage
            renderer="$2"
            shift 2
            ;;
        --stage-picker) stage_picker=1; shift ;;
        --output)
            [ "$#" -ge 2 ] || usage
            output_dir="$2"
            shift 2
            ;;
        --rebuild-image) rebuild_argument=(--rebuild-image); shift ;;
        *) usage ;;
    esac
done
case "${renderer}" in
    sprite|2-5d|both) ;;
    *) usage ;;
esac

case "${output_dir}" in
    /*) ;;
    *) output_dir="$(pwd -P)/${output_dir}" ;;
esac
mkdir -p "${output_dir}"

sprite_build="${GRANDLEON_N64_SEVENFOLD_SPRITE_BUILD_DIR:-${repository_root}/build-n64-sevenfold-sprite}"
two_five_d_build="${GRANDLEON_N64_SEVENFOLD_2_5D_BUILD_DIR:-${repository_root}/build-n64-sevenfold-2-5d}"

if [ "${renderer}" = sprite ] || [ "${renderer}" = both ]; then
    picker_argument=()
    [ "${stage_picker}" -eq 0 ] || picker_argument=(--stage-picker)
    GRANDLEON_N64_BUILD_DIR="${sprite_build}" \
        "${repository_root}/platform/nintendo64/scripts/build-n64.sh" \
        --project "${repository_root}/games/sevenfold/source/project.json" \
        --targets grandleon_n64_campaign \
        "${picker_argument[@]}" "${rebuild_argument[@]}"
    sprite_rom="${sprite_build}/platform/nintendo64/grandleon_n64_campaign.z64"
    cp "${sprite_rom}" "${output_dir}/sevenfold-n64-sprite.z64"
fi

if [ "${renderer}" = 2-5d ] || [ "${renderer}" = both ]; then
    GRANDLEON_N64_SEVENFOLD_2_5D_BUILD_DIR="${two_five_d_build}" \
        "${repository_root}/platform/nintendo64/scripts/build-sevenfold-2-5d.sh" \
        "${rebuild_argument[@]}"
    two_five_d_rom="${two_five_d_build}/platform/nintendo64/grandleon-n64-sevenfold-2-5d.z64"
    cp "${two_five_d_rom}" "${output_dir}/sevenfold-n64-2-5d.z64"
fi

for rom in "${output_dir}"/*.z64; do
    [ -e "${rom}" ] || continue
    magic="$(od -An -tx1 -N4 "${rom}" | tr -d ' \n')"
    if [ "${magic}" != 80371240 ]; then
        echo "error: ${rom} is not a big-endian N64 ROM." >&2
        exit 1
    fi
done

manifest="${output_dir}/README.txt"
{
    echo "Sevenfold Nintendo 64 review ROMs"
    echo
    if [ -f "${output_dir}/sevenfold-n64-sprite.z64" ]; then
        echo "sevenfold-n64-sprite.z64"
        echo "  Playable campaign with a 1-player/2-player choice."
        if [ "${stage_picker}" -eq 1 ]; then
            echo "  Debug stage picker and live FPS display enabled."
        fi
        echo "  SHA-256: $(sha256sum "${output_dir}/sevenfold-n64-sprite.z64" | cut -d ' ' -f 1)"
        echo
    fi
    if [ -f "${output_dir}/sevenfold-n64-2-5d.z64" ]; then
        echo "sevenfold-n64-2-5d.z64"
        echo "  Fixed-camera 2.5D tactical and close-battle renderer gate."
        echo "  SHA-256: $(sha256sum "${output_dir}/sevenfold-n64-2-5d.z64" | cut -d ' ' -f 1)"
        echo
    fi
} > "${manifest}"

echo "Sevenfold N64 review set: ${output_dir}"
while IFS= read -r line; do echo "  ${line}"; done < "${manifest}"
