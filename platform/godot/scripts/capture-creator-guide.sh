#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
build_dir="${repo_root}/build-godot"
output_dir="${repo_root}/docs/screenshots/native-guide"
player_shell=""

usage() {
    echo "usage: $0 [--build-dir DIR] [--output DIR] [--player-shell DIR]" >&2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            build_dir="$2"
            shift 2
            ;;
        --output)
            output_dir="$2"
            shift 2
            ;;
        --player-shell)
            player_shell="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage
            exit 64
            ;;
    esac
done

case "${build_dir}" in
    /*) ;;
    *) build_dir="${repo_root}/${build_dir}" ;;
esac
case "${output_dir}" in
    /*) ;;
    *) output_dir="${repo_root}/${output_dir}" ;;
esac
if [[ -n "${player_shell}" ]]; then
    case "${player_shell}" in
        /*) ;;
        *) player_shell="${repo_root}/${player_shell}" ;;
    esac
fi

if ! command -v xvfb-run >/dev/null 2>&1; then
    echo "xvfb-run is required to capture creator screenshots" >&2
    exit 69
fi
if [[ -n "${player_shell}" && ! -x "${player_shell}/launch-pc.sh" ]]; then
    echo "player shell is not a PC bundle: ${player_shell}" >&2
    exit 66
fi

godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
cmake -S "${repo_root}" -B "${build_dir}" \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
mkdir -p "${output_dir}"

capture() {
    local test_script="$1"
    local image_name="$2"
    shift 2
    local state_dir
    state_dir="$(mktemp -d "${build_dir}/creator-guide-state.XXXXXX")"
    mkdir -p "${state_dir}/data" "${state_dir}/config" "${state_dir}/cache"
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 60s "${godot_binary}" --editor --headless --quiet \
        --path "${stage_dir}" --quit || {
            cmake -E rm -rf "${state_dir}"
            return 1
        }
    XDG_DATA_HOME="${state_dir}/data" \
    XDG_CONFIG_HOME="${state_dir}/config" \
    XDG_CACHE_HOME="${state_dir}/cache" \
        timeout 120s xvfb-run -a -s '-screen 0 1920x1080x24' \
        "${godot_binary}" --path "${stage_dir}" \
        --script "${repo_root}/platform/godot/tests/${test_script}" -- "$@" || {
            cmake -E rm -rf "${state_dir}"
            return 1
        }
    cmake -E rm -rf "${state_dir}"
    test -s "${output_dir}/${image_name}"
}

capture creator_screen_test.gd 01-new-campaign.png \
    "--new-campaign-screenshot=${output_dir}/01-new-campaign.png"
capture campaign_editor_test.gd 02-campaign-outline.png \
    "--screenshot=${output_dir}/02-campaign-outline.png"
capture map_editor_test.gd 03-map-and-battle.png \
    "--screenshot=${output_dir}/03-map-and-battle.png"
capture story_editor_test.gd 04-story.png \
    "--screenshot=${output_dir}/04-story.png"
capture army_editor_test.gd 05-army.png \
    "--screenshot=${output_dir}/05-army.png" --role-preview
capture campaign_editor_test.gd 06-add-route.png \
    "--add-route-screenshot=${output_dir}/06-add-route.png"

if [[ -n "${player_shell}" ]]; then
    capture creator_screen_test.gd 07-export-center.png \
        "--real-shell=${player_shell}" \
        "--capture-export-center=${output_dir}/07-export-center.png"
else
    echo "skipped 07-export-center.png (pass --player-shell with a PC bundle)" >&2
fi

echo "creator guide screenshots written to ${output_dir}"
