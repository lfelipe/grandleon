#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

case "${build_dir}" in
    /*) ;;
    *) build_dir="$(pwd -P)/${build_dir}" ;;
esac
if [[ -n "${godot_binary}" ]]; then
    case "${godot_binary}" in
        /*) ;;
        *) godot_binary="$(pwd -P)/${godot_binary}" ;;
    esac
else
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi
if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi

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

state_dir="${build_dir}/motion-path-smoke-state"
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --editor --headless --quiet \
    --path "${stage_dir}" --quit
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/motion_path_test.gd"

runner=("${godot_binary}")
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        printf 'DISPLAY is unset and xvfb-run is unavailable\n' >&2
        exit 69
    fi
    runner=(xvfb-run -a "${godot_binary}")
fi

run_profile() {
    local profile="$1"
    local capture_path="${state_dir}/concurrent-routes-${profile}.png"
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 60s "${runner[@]}" --path "${stage_dir}" -- \
        --package=res://game.gpk \
        --campaign=emberbound \
        --players=2 \
        "--graphics=${profile}" \
        --coop-smoke \
        "--screenshot=${capture_path}" \
        --screenshot-frames=5
    if [[ ! -s "${capture_path}" ]]; then
        printf 'Concurrent route capture was not written: %s\n' \
            "${capture_path}" >&2
        exit 1
    fi
    printf 'motion presentation %s smoke passed: %s\n' \
        "${profile}" "${capture_path}"
}

run_profile pixel
run_profile voxel
