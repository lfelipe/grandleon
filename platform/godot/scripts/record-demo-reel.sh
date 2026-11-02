#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_path="${2:-${build_dir}/grandleon-demo-reel.mp4}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"
duration="${GRANDLEON_DEMO_SECONDS:-20}"
config_path="${GRANDLEON_DEMO_CONFIG:-}"
graphics_profile="${GRANDLEON_DEMO_GRAPHICS:-voxel-sprites}"

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

build_dir="$(absolute_path "${build_dir}")"
output_path="$(absolute_path "${output_path}")"
if [[ -n "${config_path}" ]]; then
    config_path="$(absolute_path "${config_path}")"
    if [[ ! -f "${config_path}" ]]; then
        printf 'Demo model config not found: %s\n' "${config_path}" >&2
        exit 66
    fi
fi
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

if [[ "${GRANDLEON_DEMO_CAPTURE_CHILD:-0}" != 1 ]]; then
    cmake -S "${repo_root}" -B "${build_dir}" \
        -DGRANDLEON_BUILD_GODOT=ON \
        -DGRANDLEON_GODOT_API_VERSION=4.7 \
        -DGRANDLEON_WERROR=ON
    cmake --build "${build_dir}" --parallel --target \
        grandleon_content_compile grandleon_godot_project

    stage_dir="${build_dir}/platform/godot/project/app"
    "${build_dir}/grandleon_content_compile" \
        "${repo_root}/games/demo/source/project.json" \
        "${stage_dir}/game.gpk"

    state_dir="${build_dir}/demo-reel-state"
    mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        "${godot_binary}" --editor --headless --quiet \
        --path "${stage_dir}" --quit

    mkdir -p "$(dirname -- "${output_path}")"
    GRANDLEON_DEMO_CAPTURE_CHILD=1 \
    GRANDLEON_GODOT_BINARY="${godot_binary}" \
    GRANDLEON_DEMO_SECONDS="${duration}" \
    GRANDLEON_DEMO_CONFIG="${config_path}" \
    GRANDLEON_DEMO_GRAPHICS="${graphics_profile}" \
        xvfb-run -a -s '-screen 0 1280x720x24' \
        "${BASH_SOURCE[0]}" "${build_dir}" "${output_path}"
    printf 'Godot gameplay capture: %s\n' "${output_path}"
    exit 0
fi

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="${build_dir}/demo-reel-state"
temporary_dir="$(mktemp -d)"
game_pid=''
cleanup() {
    if [[ -n "${game_pid}" ]]; then
        kill "${game_pid}" 2>/dev/null || true
        wait "${game_pid}" 2>/dev/null || true
    fi
    rm -r "${temporary_dir}"
}
trap cleanup EXIT

game_arguments=(
    --package=res://game.gpk
    --campaign=last_signal
    --players=2
    "--graphics=${graphics_profile}"
    --demo-reel
)
if [[ -n "${config_path}" ]]; then
    game_arguments+=("--config=${config_path}")
fi

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    "${godot_binary}" --path "${stage_dir}" -- \
    "${game_arguments[@]}" >"${temporary_dir}/game.log" 2>&1 &
game_pid=$!
sleep 1

capture_display="${DISPLAY%%.*}.0"
ffmpeg -hide_banner -loglevel warning -y \
    -f x11grab -draw_mouse 0 -framerate 30 \
    -video_size 1280x720 -i "${capture_display}" \
    -t "${duration}" -an -c:v libx264 -preset medium -crf 21 \
    -pix_fmt yuv420p -movflags +faststart "${output_path}"

if grep -Eq 'ERROR|SCRIPT ERROR|Invalid|Parse Error' "${temporary_dir}/game.log"; then
    sed -n '1,240p' "${temporary_dir}/game.log" >&2
    exit 1
fi
