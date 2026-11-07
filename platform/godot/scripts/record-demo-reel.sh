#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_path="${2:-${build_dir}/grandleon-demo-reel.mp4}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"
duration="${GRANDLEON_DEMO_SECONDS:-20}"
graphics_profile="${GRANDLEON_DEMO_GRAPHICS:-voxel-sprites}"
demo_mode="${GRANDLEON_DEMO_MODE:-demo-reel}"

case "${demo_mode}" in
    demo-reel|movement-reel) ;;
    *)
        printf 'Unknown demo mode: %s\n' "${demo_mode}" >&2
        exit 64
        ;;
esac

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

if [[ "${GRANDLEON_DEMO_CAPTURE_CHILD:-0}" != 1 ]]; then
    cmake -S "${repo_root}" -B "${build_dir}" \
        -DGRANDLEON_BUILD_GODOT=ON \
        -DGRANDLEON_GODOT_API_VERSION=4.7 \
        -DGRANDLEON_WERROR=ON
    cmake --build "${build_dir}" --parallel --target \
        grandleon_content_compile grandleon_godot_project

    stage_dir="${build_dir}/platform/godot/project/app"
    "${build_dir}/grandleon_content_compile" \
        "${repo_root}/games/emberbound/source/project.json" \
        "${stage_dir}/game.gpk"

    state_dir="$(mktemp -d "${build_dir}/demo-reel-state.XXXXXX")"
    cleanup_state() {
        cmake -E remove_directory "${state_dir}"
    }
    trap cleanup_state EXIT
    mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        "${godot_binary}" --headless --quiet --import \
        --path "${stage_dir}"

    mkdir -p "$(dirname -- "${output_path}")"
    GRANDLEON_DEMO_CAPTURE_CHILD=1 \
    GRANDLEON_GODOT_BINARY="${godot_binary}" \
    GRANDLEON_DEMO_SECONDS="${duration}" \
    GRANDLEON_DEMO_GRAPHICS="${graphics_profile}" \
    GRANDLEON_DEMO_MODE="${demo_mode}" \
    GRANDLEON_DEMO_STATE_DIR="${state_dir}" \
        xvfb-run -a -s '-screen 0 1280x720x24' \
        "${BASH_SOURCE[0]}" "${build_dir}" "${output_path}"
    printf 'Godot gameplay capture: %s\n' "${output_path}"
    exit 0
fi

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="${GRANDLEON_DEMO_STATE_DIR:-}"
if [[ -z "${state_dir}" || ! -d "${state_dir}" ]]; then
    printf 'Capture child needs an isolated demo state directory\n' >&2
    exit 64
fi
temporary_dir="$(mktemp -d "$(dirname -- "${output_path}")/.grandleon-demo-reel.XXXXXX")"
capture_path="${temporary_dir}/capture.mp4"
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
    --campaign=emberbound
    --players=2
    "--graphics=${graphics_profile}"
    "--${demo_mode}"
)

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
    -pix_fmt yuv420p -movflags +faststart "${capture_path}"

if grep -Eq 'ERROR|SCRIPT ERROR|Invalid|Parse Error' "${temporary_dir}/game.log"; then
    sed -n '1,240p' "${temporary_dir}/game.log" >&2
    exit 1
fi
mv -f -- "${capture_path}" "${output_path}"
