#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${1:-${repo_root}/build-godot}"
# This captures the deterministic low-detail terrain relay used by renderer
# diagnostics. It must not overwrite the reviewed production artwork in the
# project. Pass an explicit second argument when another diagnostic location is
# useful.
output_dir="${2:-${build_dir}/generated-combat-backdrops/medieval-forest-gate-diagnostic}"
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
else
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi
if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi
for command in cmake ffprobe xvfb-run; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Required backdrop tool not found: %s\n' "${command}" >&2
        exit 69
    fi
done

cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project
stage_dir="${build_dir}/platform/godot/project/app"
mkdir -p "${output_dir}"
state_dir="$(mktemp -d)"
trap 'rm -rf -- "${state_dir}"' EXIT
mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    "${godot_binary}" --headless --quiet --import \
    --path "${stage_dir}"
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    xvfb-run -a -s '-screen 0 1280x720x24' \
    "${godot_binary}" --path "${stage_dir}" \
    --script "${repo_root}/platform/godot/tests/combat_backdrop_capture.gd" -- \
    --output-dir="${output_dir}"

for variant in 0 1 2; do
    for layer in panorama transition_grass_paved midground_forest props; do
        path="${output_dir}/${layer}_${variant}.png"
        dimensions="$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0:s=x "${path}")"
        if [[ "${dimensions}" != "1280x720" ]]; then
            printf 'Unexpected backdrop layer dimensions: %s is %s\n' "${path}" "${dimensions}" >&2
            exit 1
        fi
    done
done

printf 'Godot diagnostic combat backdrop layers: %s\n' "${output_dir}"
