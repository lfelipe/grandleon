#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds Sevenfold's playable Nintendo 64 campaign. The campaign itself is the
# renderer test: there is no standalone viewer or second graphics path.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
stage_picker=0
output_dir="${GRANDLEON_N64_SEVENFOLD_OUTPUT_DIR:-${repository_root}/build-n64-sevenfold-roms}"
rebuild_argument=()

usage() {
    echo "usage: $(basename "$0") [--stage-picker] [--output DIR] [--rebuild-image]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
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

case "${output_dir}" in
    /*) ;;
    *) output_dir="$(pwd -P)/${output_dir}" ;;
esac
mkdir -p "${output_dir}"

campaign_build="${GRANDLEON_N64_SEVENFOLD_CAMPAIGN_BUILD_DIR:-${repository_root}/build-n64-sevenfold-campaign}"
picker_argument=()
[ "${stage_picker}" -eq 0 ] || picker_argument=(--stage-picker)

GRANDLEON_N64_BUILD_DIR="${campaign_build}" \
    "${repository_root}/platform/nintendo64/scripts/build-n64.sh" \
    --project "${repository_root}/games/sevenfold/source/project.json" \
    --targets grandleon_n64_campaign \
    "${picker_argument[@]}" "${rebuild_argument[@]}"
campaign_rom="${campaign_build}/platform/nintendo64/grandleon_n64_campaign.z64"
cp "${campaign_rom}" "${output_dir}/sevenfold-n64.z64"

for rom in "${output_dir}"/*.z64; do
    magic="$(od -An -tx1 -N4 "${rom}" | tr -d ' \n')"
    if [ "${magic}" != 80371240 ]; then
        echo "error: ${rom} is not a big-endian N64 ROM." >&2
        exit 1
    fi
done

manifest="${output_dir}/README.txt"
{
    echo "Sevenfold Nintendo 64"
    echo
    echo "sevenfold-n64.z64"
    echo "  The playable campaign with a 1-player/2-player choice."
    if [ "${stage_picker}" -eq 1 ]; then
        echo "  Debug stage picker and live FPS display enabled."
    fi
    echo "  SHA-256: $(sha256sum "${output_dir}/sevenfold-n64.z64" | cut -d ' ' -f 1)"
} > "${manifest}"

echo "Sevenfold N64 artifacts: ${output_dir}"
while IFS= read -r line; do echo "  ${line}"; done < "${manifest}"
