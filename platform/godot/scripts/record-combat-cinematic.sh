#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_path="${2:-${build_dir}/grandleon-combat-cinematic.mp4}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

build_dir="$(absolute_path "${build_dir}")"
output_path="$(absolute_path "${output_path}")"
if [[ -n "${godot_binary}" ]]; then
    godot_binary="$(absolute_path "${godot_binary}")"
else
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi

if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi
for command in cmake ffmpeg xvfb-run; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Required capture tool not found: %s\n' "${command}" >&2
        exit 69
    fi
done

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="$(mktemp -d "${build_dir}/combat-capture-state.XXXXXX")"
temporary_dir="$(mktemp -d "${build_dir}/combat-capture-video.XXXXXX")"
raw_path="${temporary_dir}/capture.avi"
capture_path="${temporary_dir}/capture.mp4"
cleanup() {
    cmake -E remove_directory "${state_dir}"
    cmake -E remove_directory "${temporary_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    "${godot_binary}" --editor --headless --quiet \
    --path "${stage_dir}" --quit

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    xvfb-run -a -s '-screen 0 1280x720x24' \
    "${godot_binary}" --path "${stage_dir}" \
    --resolution 1280x720 --write-movie "${raw_path}" --fixed-fps 30 \
    --script "${repo_root}/platform/godot/tests/combat_cinematic_capture.gd"

ffmpeg -hide_banner -loglevel warning -y -i "${raw_path}" -an \
    -c:v libx264 -preset medium -crf 21 \
    -pix_fmt yuv420p -movflags +faststart "${capture_path}"
mkdir -p "$(dirname -- "${output_path}")"
mv -f -- "${capture_path}" "${output_path}"
printf 'Godot combat cinematic capture: %s\n' "${output_path}"
