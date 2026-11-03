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
    "${repo_root}/games/demo/source/project.json" \
    "${stage_dir}/game.gpk"

state_dir="$(mktemp -d "${build_dir}/campaign-persistence-state.XXXXXX")"
cleanup() {
    cmake -E rm -rf "${state_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --editor --headless --quiet \
    --path "${stage_dir}" --quit

run_campaign() {
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 30s "${godot_binary}" --headless --path "${stage_dir}" -- \
        --package=res://game.gpk \
        --campaign=demo_campaign \
        --players=1 \
        --campaign-persistence-smoke
}

first="$(run_campaign)"
printf '%s\n' "${first}"
if ! grep -Fq 'campaign persistence wrote a checkpoint' <<<"${first}"; then
    printf 'fresh campaign did not report a checkpoint write\n' >&2
    exit 1
fi
mapfile -t saves < <(find "${state_dir}" -type f -name '*.gls' -print)
if [[ "${#saves[@]}" -ne 1 || ! -s "${saves[0]}" ]]; then
    printf 'fresh campaign did not leave exactly one non-empty checkpoint\n' >&2
    exit 1
fi

second="$(run_campaign)"
printf '%s\n' "${second}"
if ! grep -Fq \
    'campaign persistence reset started a fresh campaign' <<<"${second}"; then
    printf 'second process did not resume and reset the checkpoint\n' >&2
    exit 1
fi
if find "${state_dir}" -type f -name '*.gls' -print -quit | grep -q .; then
    printf 'checkpoint remained after the in-app reset\n' >&2
    exit 1
fi

printf 'campaign persistence integration smoke passed\n'
