#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
case "${build_dir}" in /*) ;; *) build_dir="$(pwd -P)/${build_dir}" ;; esac
godot_binary="${GRANDLEON_GODOT_BINARY:-}"
if [[ -z "${godot_binary}" ]]; then
    godot_binary="$("${script_dir}/setup-godot.sh" | tail -n 1)"
fi
cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON -DGRANDLEON_GODOT_API_VERSION=4.7 -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel 2 --target grandleon_godot_project
stage_dir="${build_dir}/platform/godot/project/app"
state_dir="$(mktemp -d "${build_dir}/local-recovery-smoke.XXXXXX")"
trap 'cmake -E remove_directory "${state_dir}"' EXIT
export XDG_DATA_HOME="${state_dir}/data"
export XDG_CONFIG_HOME="${state_dir}/config"
export XDG_CACHE_HOME="${state_dir}/cache"
mkdir -p "${XDG_DATA_HOME}" "${XDG_CONFIG_HOME}" "${XDG_CACHE_HOME}"
timeout 120s "${godot_binary}" --editor --headless --quiet --path "${stage_dir}" --quit
mkdir -p "${stage_dir}/content/included"
"${build_dir}/grandleon_content_compile" "${repo_root}/games/steel_and_cedar/source/project.json" \
    "${stage_dir}/content/included/steel_and_cedar.gpk"
for test in local_recovery local_recovery_player personal_inventory_player personal_inventory_weapon shop_workflow; do
    log="${state_dir}/${test}.log"
    if ! timeout 90s "${godot_binary}" --headless --path "${stage_dir}" \
        --script "${repo_root}/platform/godot/tests/${test}_test.gd" >"${log}" 2>&1; then
        cat "${log}"
        exit 1
    fi
    cat "${log}"
    if grep -Eq 'SCRIPT ERROR:|^ERROR:|Parse Error:' "${log}"; then exit 1; fi
done
python3 "${script_dir}/test-local-recovery.py" --godot "${godot_binary}" \
    --project "${stage_dir}" --compiler "${build_dir}/grandleon_content_compile" \
    --output "${state_dir}/processes"
python3 "${repo_root}/platform/godot/tests/run_inventory_checkpoint_compatibility_test.py" \
    --godot "${godot_binary}" --project "${stage_dir}" \
    --output "${state_dir}/inventory-compatibility"
