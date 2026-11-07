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
if ! command -v convert >/dev/null 2>&1; then
    echo "ImageMagick's convert command is required to build creator GIFs" >&2
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
frames_dir="$(mktemp -d "${build_dir}/creator-guide-frames.XXXXXX")"
trap 'cmake -E rm -rf "${frames_dir}"' EXIT

capture() {
    local test_script="$1"
    local expected_output="$2"
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
    test -s "${expected_output}"
}

make_gif() {
    local output="$1"
    shift
    convert "$@" -resize '960x540>' -set delay 90 -loop 0 \
        -layers Optimize "${output}"
    if command -v gifsicle >/dev/null 2>&1; then
        gifsicle --batch --optimize=3 "${output}"
    fi
}

mkdir -p \
    "${frames_dir}/start" \
    "${frames_dir}/terrain" \
    "${frames_dir}/story" \
    "${frames_dir}/campaign" \
    "${frames_dir}/army" \
    "${frames_dir}/test"

capture creator_screen_test.gd "${frames_dir}/start/01-new-campaign.png" \
    "--new-campaign-screenshot=${frames_dir}/start/01-new-campaign.png"
capture campaign_editor_test.gd "${frames_dir}/start/02-outline.png" \
    "--screenshot=${frames_dir}/start/02-outline.png"
capture map_editor_test.gd "${frames_dir}/start/03-first-map.png" \
    "--screenshot=${frames_dir}/start/03-first-map.png"
make_gif "${output_dir}/01-start-a-campaign.gif" \
    "${frames_dir}/start/01-new-campaign.png" \
    "${frames_dir}/start/02-outline.png" \
    "${frames_dir}/start/03-first-map.png"

capture map_editor_test.gd "${frames_dir}/terrain/06-redo.png" \
    "--terrain-guide-frames=${frames_dir}/terrain"
capture map_editor_test.gd "${frames_dir}/terrain/07-setup.png" \
    "--setup-screenshot=${frames_dir}/terrain/07-setup.png"
capture map_editor_test.gd "${frames_dir}/terrain/08-look.png" \
    "--look-screenshot=${frames_dir}/terrain/08-look.png"
make_gif "${output_dir}/02-shape-a-battle.gif" \
    "${frames_dir}/terrain/01-map.png" \
    "${frames_dir}/terrain/02-brush.png" \
    "${frames_dir}/terrain/03-fill.png" \
    "${frames_dir}/terrain/04-rectangle.png" \
    "${frames_dir}/terrain/05-undo.png" \
    "${frames_dir}/terrain/06-redo.png" \
    "${frames_dir}/terrain/07-setup.png" \
    "${frames_dir}/terrain/08-look.png"

capture story_editor_test.gd "${frames_dir}/story/01-story.png" \
    "--screenshot=${frames_dir}/story/01-story.png"
capture story_editor_test.gd "${frames_dir}/story/02-new-scene.png" \
    "--screenshot=${frames_dir}/story/02-new-scene.png" --new-scene-preview
capture story_editor_test.gd "${frames_dir}/story/03-more.png" \
    "--screenshot=${frames_dir}/story/03-more.png" --more-preview
make_gif "${output_dir}/03-write-the-story.gif" \
    "${frames_dir}/story/01-story.png" \
    "${frames_dir}/story/02-new-scene.png" \
    "${frames_dir}/story/03-more.png"

capture campaign_editor_test.gd "${frames_dir}/campaign/01-outline.png" \
    "--screenshot=${frames_dir}/campaign/01-outline.png"
capture campaign_editor_test.gd "${frames_dir}/campaign/02-outcomes.png" \
    "--outcomes-screenshot=${frames_dir}/campaign/02-outcomes.png"
capture campaign_editor_test.gd "${frames_dir}/campaign/03-add-route.png" \
    "--add-route-screenshot=${frames_dir}/campaign/03-add-route.png"
capture campaign_editor_test.gd "${frames_dir}/campaign/04-branch.png" \
    "--branch-screenshot=${frames_dir}/campaign/04-branch.png"
make_gif "${output_dir}/04-build-the-campaign.gif" \
    "${frames_dir}/campaign/01-outline.png" \
    "${frames_dir}/campaign/02-outcomes.png" \
    "${frames_dir}/campaign/03-add-route.png" \
    "${frames_dir}/campaign/04-branch.png"

capture army_editor_test.gd "${frames_dir}/army/01-new-role.png" \
    "--screenshot=${frames_dir}/army/01-new-role.png" --role-preview
capture army_editor_test.gd "${frames_dir}/army/02-tune-role.png" \
    "--screenshot=${frames_dir}/army/02-tune-role.png" --role-tuning-preview
capture army_editor_test.gd "${frames_dir}/army/03-growth.png" \
    "--screenshot=${frames_dir}/army/03-growth.png" --growth-preview
capture army_editor_test.gd "${frames_dir}/army/04-supplies.png" \
    "--screenshot=${frames_dir}/army/04-supplies.png" --supplies-preview
make_gif "${output_dir}/05-grow-the-army.gif" \
    "${frames_dir}/army/01-new-role.png" \
    "${frames_dir}/army/02-tune-role.png" \
    "${frames_dir}/army/03-growth.png" \
    "${frames_dir}/army/04-supplies.png"

capture map_editor_test.gd "${frames_dir}/test/01-play.png" \
    "--surroundings-play-screenshot=${frames_dir}/test/01-play.png"

if [[ -n "${player_shell}" ]]; then
    capture creator_screen_test.gd "${frames_dir}/test/02-export.png" \
        "--real-shell=${player_shell}" \
        "--capture-export-center=${frames_dir}/test/02-export.png"
else
    capture creator_screen_test.gd "${frames_dir}/test/02-export.png" \
        "--capture-export-center=${frames_dir}/test/02-export.png"
fi
make_gif "${output_dir}/06-test-and-export.gif" \
    "${frames_dir}/test/01-play.png" \
    "${frames_dir}/test/02-export.png"

echo "creator guide GIFs written to ${output_dir}"
