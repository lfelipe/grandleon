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

state_dir="$(mktemp -d "${build_dir}/combat-cinematic-state.XXXXXX")"
cleanup() {
    cmake -E rm -rf "${state_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"

run_godot() {
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        "$@"
}

run_godot timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"
run_godot timeout 60s "${godot_binary}" --headless --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/combat_presentation_plan_test.gd"
# Loading the production layered backdrop pack and the real 3D board is the
# expensive part of this visual test under software rendering. Keep a strict
# bound, but leave enough headroom for a cold import of the reviewed 1280x720
# panorama packs on slower CI workers.
run_godot timeout 180s "${godot_binary}" --headless --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/combat_cinematic_test.gd"

runner=("${godot_binary}")
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        printf 'DISPLAY is unset and xvfb-run is unavailable\n' >&2
        exit 69
    fi
    runner=(xvfb-run -a -s '-screen 0 1280x720x24' "${godot_binary}")
fi

set +e
output="$({
    run_godot timeout 180s "${runner[@]}" --path "${stage_dir}" -- \
        --package=res://game.gpk \
        --campaign=emberbound \
        --players=2 \
        --graphics=voxel-sprites \
        --no-progress \
        --combat-cinematic-smoke
} 2>&1)"
run_status=$?
set -e
printf '%s\n' "${output}"
if [[ "${run_status}" -ne 0 ]]; then
    printf 'combat cinematic Godot run exited %d\n' "${run_status}" >&2
    exit "${run_status}"
fi
if ! grep -Fq \
    'combat cinematic native event, snapshot, fast-forward, and skip smoke passed' \
    <<<"${output}"; then
    printf 'combat cinematic did not consume a native accepted exchange\n' >&2
    exit 1
fi

printf 'combat cinematic integration smoke passed\n'
