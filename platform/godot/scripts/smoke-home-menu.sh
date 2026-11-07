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
included_dir="${stage_dir}/content/included"
cmake -E remove_directory "${included_dir}"
cmake -E make_directory "${included_dir}"
"${build_dir}/grandleon_content_compile" \
    "${repo_root}/games/tarnholt/source/project.json" \
    "${included_dir}/tarnholt.gpk"
"${build_dir}/grandleon_content_compile" \
    "${repo_root}/games/emberbound/source/project.json" \
    "${included_dir}/emberbound.gpk"
state_dir="$(mktemp -d "${build_dir}/home-menu-state.XXXXXX")"
cleanup() {
    cmake -E remove_directory "${state_dir}"
    cmake -E remove_directory "${included_dir}"
}
trap cleanup EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 30s "${godot_binary}" --headless --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/included_games_test.gd"

output="$({
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 30s "${godot_binary}" --headless --path "${stage_dir}" -- \
        --home-smoke
} 2>&1)"
printf '%s\n' "${output}"
if ! grep -Fq 'home menu integration smoke passed' <<<"${output}"; then
    printf 'home menu did not publish its simple game list\n' >&2
    exit 1
fi
if ! grep -Fq 'home menu selection smoke passed' <<<"${output}" || \
    grep -Fq 'SCRIPT ERROR' <<<"${output}"; then
    printf 'home menu could not safely open its selected game\n' >&2
    exit 1
fi

solo_output="$({
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 30s "${godot_binary}" --headless --path "${stage_dir}" -- \
        --package=res://content/included/tarnholt.gpk \
        --campaign=tarnholt_line \
        --players=1 \
        --included-campaign-smoke
} 2>&1)"
printf '%s\n' "${solo_output}"
if ! grep -Fq 'included campaign integration smoke passed: tarnholt_line (1 players)' \
        <<<"${solo_output}" || grep -Fq 'SCRIPT ERROR' <<<"${solo_output}"; then
    printf 'Tarnholt did not open as the one-player included campaign\n' >&2
    exit 1
fi

coop_output="$({
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 30s "${godot_binary}" --headless --path "${stage_dir}" -- \
        --package=res://content/included/emberbound.gpk \
        --campaign=emberbound \
        --players=2 \
        --included-campaign-smoke
} 2>&1)"
printf '%s\n' "${coop_output}"
if ! grep -Fq 'included campaign integration smoke passed: emberbound (2 players)' \
        <<<"${coop_output}" || grep -Fq 'SCRIPT ERROR' <<<"${coop_output}"; then
    printf 'Emberbound did not open as the two-player included campaign\n' >&2
    exit 1
fi

printf 'home menu smoke passed\n'
