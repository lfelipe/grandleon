#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot-steel-cedar-captures}"
output_dir="${2:-${repo_root}/docs/screenshots/godot/steel-and-cedar}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

case "${build_dir}" in
    /*) ;;
    *) build_dir="$(pwd -P)/${build_dir}" ;;
esac
case "${output_dir}" in
    /*) ;;
    *) output_dir="$(pwd -P)/${output_dir}" ;;
esac
if [[ -z "${godot_binary}" ]]; then
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="$(mktemp -d "${build_dir}/steel-cedar-capture-state.XXXXXX")"
cleanup() {
    cmake -E remove_directory "${state_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache" "${output_dir}"

env_args=(
    "XDG_DATA_HOME=${state_dir}/data"
    "XDG_CONFIG_HOME=${state_dir}/config"
    "XDG_CACHE_HOME=${state_dir}/cache"
)
env "${env_args[@]}" timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"

runner=("${godot_binary}")
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        printf 'DISPLAY is unset and xvfb-run is unavailable\n' >&2
        exit 1
    fi
    runner=(xvfb-run -a -s '-screen 0 1280x720x24' "${godot_binary}")
fi
env "${env_args[@]}" timeout 180s "${runner[@]}" --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/steel_cedar_board_capture_test.gd" -- \
    --source="${repo_root}/games/steel_and_cedar/source/project.json" \
    --output="${output_dir}"

printf 'Steel and Cedar board captures written to %s\n' "${output_dir}"
