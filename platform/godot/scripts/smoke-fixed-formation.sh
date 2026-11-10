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

state_dir="$(mktemp -d "${build_dir}/fixed-formation-state.XXXXXX")"
cleanup() {
    cmake -E rm -rf "${state_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"

set +e
output="$({
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 30s "${godot_binary}" --headless --path "${stage_dir}" -- \
        --package=res://game.gpk \
        --campaign=emberbound \
        --players=2 \
        --fixed-formation-smoke
} 2>&1)"
run_status=$?
set -e
printf '%s\n' "${output}"
if [[ "${run_status}" -ne 0 ]]; then
    printf 'fixed-formation Godot run exited %d\n' "${run_status}" >&2
    exit "${run_status}"
fi
if ! grep -Fq \
    'fixed formation started without an empty company screen' \
    <<<"${output}"; then
    printf 'fixed formation did not proceed directly to its battle\n' >&2
    exit 1
fi

printf 'fixed-formation integration smoke passed\n'
