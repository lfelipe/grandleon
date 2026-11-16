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
if [[ -z "${godot_binary}" ]]; then
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
state_dir="$(mktemp -d "${build_dir}/lan-smoke-state.XXXXXX")"
cleanup() {
    cmake -E remove_directory "${state_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"

export XDG_DATA_HOME="${state_dir}/data"
export XDG_CONFIG_HOME="${state_dir}/config"
export XDG_CACHE_HOME="${state_dir}/cache"
timeout 120s "${godot_binary}" --editor --headless --quiet --path "${stage_dir}" --quit
"${build_dir}/grandleon_content_compile" "${repo_root}/games/steel_and_cedar/source/project.json" "${state_dir}/steel.gpk"
mkdir -p "${stage_dir}/content/included"
cp "${state_dir}/steel.gpk" "${stage_dir}/content/included/steel_and_cedar.gpk"
for test in lan_session lan_player lan_lobby personal_inventory_lan personal_inventory_weapon_lan bell_traversal motion_path; do
    log="${state_dir}/${test}.log"
    arguments=()
    if [[ "${test}" == bell_traversal ]]; then arguments+=("--package=${state_dir}/steel.gpk"); fi
    if ! timeout 90s "${godot_binary}" --headless --path "${stage_dir}" \
        --script "${repo_root}/platform/godot/tests/${test}_test.gd" -- \
        "${arguments[@]}" >"${log}" 2>&1; then
        cat "${log}"
        exit 1
    fi
    cat "${log}"
    if grep -Eq 'SCRIPT ERROR:|^ERROR:|Parse Error:' "${log}"; then exit 1; fi
done
