#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_path="${2:-${repo_root}/out/renderer-comparison/godot.mp4}"
frame_dir="${3:-${repo_root}/out/renderer-comparison/godot-frames}"
comparison_contract="${GRANDLEON_RENDERER_COMPARISON_CONTRACT:-${repo_root}/tools/renderer_comparison/scene.json}"

for command in ffmpeg ffprobe awk jq; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Required comparison tool not found: %s\n' "${command}" >&2
        exit 69
    fi
done

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

output_path="$(absolute_path "${output_path}")"
frame_dir="$(absolute_path "${frame_dir}")"
comparison_contract="$(absolute_path "${comparison_contract}")"

if ! jq -e '
    .schema == 2 and
    .kind == "renderer_showcase" and
    .surroundings.mode == "explicit_edge_extension" and
    .camera.composition.reserved_title_pixels == 0 and
    .camera.composition.reserved_footer_pixels == 0 and
    .capture_hud.content_rect_native == [0, 0, 320, 240] and
    (.capture_hud.forbidden | index("diagnostic_text")) != null and
    .combat_backdrop.descriptor.freeze_for_exchange == true
' "${comparison_contract}" >/dev/null; then
    printf 'Renderer showcase contract is incomplete: %s\n' "${comparison_contract}" >&2
    exit 1
fi

GRANDLEON_RENDERER_COMPARISON_CONTRACT="${comparison_contract}" \
    "${script_dir}/record-combat-cinematic.sh" "${build_dir}" "${output_path}"

mkdir -p "${frame_dir}"
while IFS='|' read -r timestamp name; do
    ffmpeg -hide_banner -loglevel error -y \
        -ss "${timestamp}" -i "${output_path}" -frames:v 1 \
        "${frame_dir}/${name}.png"
done < <(jq -r '.review_frames[] | "\(.time_seconds)|\(.name)"' "${comparison_contract}")

codec="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=codec_name -of default=noprint_wrappers=1:nokey=1 \
    "${output_path}")"
dimensions="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=width,height -of csv=p=0:s=x "${output_path}")"
frame_rate="$(ffprobe -v error -select_streams v:0 \
    -show_entries stream=r_frame_rate -of default=noprint_wrappers=1:nokey=1 \
    "${output_path}")"
duration="$(ffprobe -v error -show_entries format=duration \
    -of default=noprint_wrappers=1:nokey=1 "${output_path}")"
expected_fps="$(jq -r '.video_fps' "${comparison_contract}")/1"
expected_duration="$(jq -r '.duration_seconds' "${comparison_contract}")"
if [[ "${codec}" != "h264" || "${dimensions}" != "1280x720" || "${frame_rate}" != "${expected_fps}" ]]; then
    printf 'Unexpected comparison movie: codec=%s dimensions=%s fps=%s\n' \
        "${codec}" "${dimensions}" "${frame_rate}" >&2
    exit 1
fi
if ! awk -v value="${duration}" -v expected="${expected_duration}" \
    'BEGIN { difference = value - expected; if (difference < 0) difference = -difference; exit !(difference <= 0.05) }'; then
    printf 'Unexpected comparison duration: %s seconds (contract says %s)\n' \
        "${duration}" "${expected_duration}" >&2
    exit 1
fi

printf 'Godot cross-renderer movie: %s\n' "${output_path}"
printf 'Godot comparison frames: %s\n' "${frame_dir}"
