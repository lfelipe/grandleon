#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
output_dir="${2:-${build_dir}/graphics-prototypes}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

build_dir="$(absolute_path "${build_dir}")"
output_dir="$(absolute_path "${output_dir}")"
if [[ -n "${godot_binary}" ]]; then
    godot_binary="$(absolute_path "${godot_binary}")"
fi

if [[ -z "${godot_binary}" ]]; then
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi
if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7
cmake --build "${build_dir}" --parallel --target \
    grandleon_content_compile grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
"${build_dir}/grandleon_content_compile" \
    "${repo_root}/games/emberbound/source/project.json" \
    "${stage_dir}/game.gpk"

godot_state="${build_dir}/graphics-prototype-state"
mkdir -p "${godot_state}/data" "${godot_state}/config" \
    "${godot_state}/cache" "${output_dir}"
XDG_DATA_HOME="${godot_state}/data" \
XDG_CONFIG_HOME="${godot_state}/config" \
XDG_CACHE_HOME="${godot_state}/cache" \
    "${godot_binary}" --editor --headless --quiet \
    --path "${stage_dir}" --quit

runner=("${godot_binary}")
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        printf 'DISPLAY is unset and xvfb-run is unavailable\n' >&2
        exit 69
    fi
    runner=(xvfb-run -a "${godot_binary}")
fi

capture() {
    local profile="$1"
    local filename="$2"
    XDG_DATA_HOME="${godot_state}/data" \
    XDG_CONFIG_HOME="${godot_state}/config" \
    XDG_CACHE_HOME="${godot_state}/cache" \
        "${runner[@]}" --path "${stage_dir}" -- \
        --package=res://game.gpk \
        --campaign=emberbound \
        --players=2 \
        "--graphics=${profile}" \
        --board-preview \
        "--screenshot=${output_dir}/${filename}" \
        --screenshot-frames=12
}

capture pixel pixel-2d.png
capture diorama diorama-sprites.png
capture voxel-sprites voxel-sprites.png
capture voxel full-voxel.png

XDG_DATA_HOME="${godot_state}/data" \
XDG_CONFIG_HOME="${godot_state}/config" \
XDG_CACHE_HOME="${godot_state}/cache" \
    timeout 60s "${runner[@]}" --path "${stage_dir}" -- \
    --package=res://game.gpk \
    --campaign=emberbound \
    --players=2 \
    --graphics=voxel-sprites \
    --graphics-cycle-smoke

printf 'Godot-rendered prototype captures: %s\n' "${output_dir}"
