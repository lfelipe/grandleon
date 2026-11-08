#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot-conversation-backdrops}"
godot_binary="${GRANDLEON_GODOT_BINARY:-}"

case "${build_dir}" in
    /*) ;;
    *) build_dir="$(pwd -P)/${build_dir}" ;;
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
state_dir="$(mktemp -d "${build_dir}/conversation-backdrop-state.XXXXXX")"
cleanup() {
    cmake -E remove_directory "${state_dir}"
}
trap cleanup EXIT
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
    --script "${repo_root}/platform/godot/tests/conversation_backdrop_test.gd"
