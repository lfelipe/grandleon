#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
bundle_dir=""
require_clean=0
require_examples=0

usage() {
    printf '%s\n' \
        "usage: $0 [--require-clean] [--require-examples] BUNDLE" \
        "Validates one packaged PC build through its launcher and bundled Godot runtime."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --require-clean) require_clean=1; shift ;;
        --require-examples) require_examples=1; shift ;;
        --help|-h) usage; exit 0 ;;
        --*) usage >&2; exit 64 ;;
        *)
            if [[ -n "${bundle_dir}" ]]; then
                usage >&2
                exit 64
            fi
            bundle_dir="$1"
            shift
            ;;
    esac
done

if [[ -z "${bundle_dir}" ]]; then
    usage >&2
    exit 64
fi
case "${bundle_dir}" in
    /*) ;;
    *) bundle_dir="$(pwd -P)/${bundle_dir}" ;;
esac

for command in cmake file timeout; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        printf 'Required PC bundle smoke tool not found: %s\n' "${command}" >&2
        exit 69
    fi
done
for required in \
    launch-pc.sh godot BUILD-INFO.txt grandleon-icon.png \
    LICENSE.txt NOTICE.txt \
    licenses/GODOT-LICENSE.txt licenses/GODOT-COPYRIGHT.txt \
    licenses/GODOT-CPP-LICENSE.txt \
    project/game.gpk project/bin/libgrandleon_godot.so; do
    if [[ ! -s "${bundle_dir}/${required}" ]]; then
        printf 'PC bundle file is missing or empty: %s\n' "${required}" >&2
        exit 66
    fi
done
if [[ ! -x "${bundle_dir}/launch-pc.sh" || ! -x "${bundle_dir}/godot" ]]; then
    printf 'PC bundle launch files are not executable\n' >&2
    exit 66
fi
for local_config in graphics.local.json controls.local.json; do
    if [[ -e "${bundle_dir}/${local_config}" || \
          -e "${bundle_dir}/project/${local_config}" ]]; then
        printf 'Private local config entered the PC bundle: %s\n' \
            "${local_config}" >&2
        exit 70
    fi
done
if [[ -e "${bundle_dir}/pc.example.json" || \
      -e "${bundle_dir}/project/pc.example.json" ]]; then
    printf 'Removed optional-model configuration entered the PC bundle\n' >&2
    exit 70
fi
if [[ -e "${bundle_dir}/runtime" ]]; then
    printf 'Test a fresh PC bundle before attaching its persistent runtime\n' >&2
    exit 73
fi
if ! file "${bundle_dir}/grandleon-icon.png" | grep -Fq '512 x 512'; then
    printf 'PC bundle icon is not a 512 by 512 PNG\n' >&2
    exit 65
fi

source_revision="$(sed -n 's/^sourceRevision=//p' "${bundle_dir}/BUILD-INFO.txt")"
source_state="$(sed -n 's/^sourceTree=//p' "${bundle_dir}/BUILD-INFO.txt")"
release_version="$(sed -n 's/^releaseVersion=//p' "${bundle_dir}/BUILD-INFO.txt")"
included_campaigns="$(sed -n 's/^includedCampaigns=//p' "${bundle_dir}/BUILD-INFO.txt")"
expected_release="$(tr -d '\r\n' <"${repo_root}/RELEASE_VERSION")"
if [[ "${release_version}" != "${expected_release}" ]]; then
    printf 'PC bundle release version is %s; expected %s\n' \
        "${release_version:-missing}" "${expected_release}" >&2
    exit 65
fi
if [[ ! "${source_revision}" =~ ^[0-9a-f]{40}$ ]]; then
    printf 'PC bundle source revision is missing or invalid\n' >&2
    exit 65
fi
if [[ "${source_state}" != clean && "${source_state}" != dirty ]]; then
    printf 'PC bundle source state is missing or invalid\n' >&2
    exit 65
fi
if [[ "${require_clean}" == 1 && "${source_state}" != clean ]]; then
    printf 'PC release bundle was built from a dirty source tree\n' >&2
    exit 65
fi
if [[ "${require_examples}" == 1 ]]; then
    if [[ "${included_campaigns}" != "steel_and_cedar:choice" ]]; then
        printf 'PC bundle does not identify the Steel and Cedar demo\n' >&2
        exit 65
    fi
    for example in \
        project/content/included/steel_and_cedar.gpk; do
        if [[ ! -s "${bundle_dir}/${example}" ]]; then
            printf 'PC bundle example is missing or empty: %s\n' "${example}" >&2
            exit 66
        fi
    done
fi
if strings "${bundle_dir}/project/bin/libgrandleon_godot.so" | \
        grep -Fq "${repo_root}"; then
    printf 'PC bundle native extension contains the private checkout path\n' >&2
    exit 70
fi

state_dir="$(mktemp -d "$(dirname -- "${bundle_dir}")/.pc-bundle-smoke.XXXXXX")"
runtime_link="${bundle_dir}/runtime"
cleanup() {
    if [[ -L "${runtime_link}" && "$(readlink "${runtime_link}")" == "${state_dir}" ]]; then
        cmake -E remove "${runtime_link}"
    fi
    cmake -E remove_directory "${state_dir}"
}
trap cleanup EXIT
mkdir -p \
    "${state_dir}/data" \
    "${state_dir}/config" \
    "${state_dir}/cache" \
    "${state_dir}/local"
cmake -E copy \
    "${repo_root}/platform/godot/tests/controls_swapped.json" \
    "${state_dir}/local/controls.local.json"
ln -s "${state_dir}" "${runtime_link}"

launcher=("${bundle_dir}/launch-pc.sh")
renderer=("${bundle_dir}/godot")
if [[ -z "${DISPLAY:-}" ]]; then
    if ! command -v xvfb-run >/dev/null 2>&1; then
        printf 'DISPLAY is unset and xvfb-run is unavailable\n' >&2
        exit 69
    fi
    launcher=(xvfb-run -a -s '-screen 0 1280x720x24' "${bundle_dir}/launch-pc.sh")
    renderer=(xvfb-run -a -s '-screen 0 1280x720x24' "${bundle_dir}/godot")
fi

GRANDLEON_WINDOWED=1 timeout 60s "${launcher[@]}" --home-smoke

GRANDLEON_WINDOWED=1 timeout 60s "${launcher[@]}" --controls-smoke

XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${bundle_dir}/godot" --headless \
    --path "${bundle_dir}/project" -- --frame-timing-smoke

capture_path="${state_dir}/concurrent-coop.png"
coop_package="res://game.gpk"
coop_campaign="$(sed -n 's/^defaultCampaign=//p' "${bundle_dir}/BUILD-INFO.txt")"
if [[ "${require_examples}" == 1 ]]; then
    coop_package="res://content/included/steel_and_cedar.gpk"
    coop_campaign="steel_and_cedar"
fi
XDG_DATA_HOME="${state_dir}/data" \
XDG_CONFIG_HOME="${state_dir}/config" \
XDG_CACHE_HOME="${state_dir}/cache" \
    timeout 60s "${renderer[@]}" --path "${bundle_dir}/project" -- \
    "--package=${coop_package}" \
    "--campaign=${coop_campaign}" \
    --players=2 \
    --graphics=voxel-sprites \
    --game-language=en \
    --coop-smoke \
    "--screenshot=${capture_path}" \
    --screenshot-frames=5
if [[ ! -s "${capture_path}" ]]; then
    printf 'Packaged PC co-op capture was not written\n' >&2
    exit 1
fi

if [[ "${require_examples}" == 1 ]]; then
    solo_output="$({
        XDG_DATA_HOME="${state_dir}/data" \
        XDG_CONFIG_HOME="${state_dir}/config" \
        XDG_CACHE_HOME="${state_dir}/cache" \
            timeout 60s "${bundle_dir}/godot" --headless \
            --path "${bundle_dir}/project" -- \
            --package=res://content/included/steel_and_cedar.gpk \
            --campaign=steel_and_cedar \
            --players=1 \
            --game-language=en \
            --included-campaign-smoke
    } 2>&1)"
    printf '%s\n' "${solo_output}"
    if ! grep -Fq \
            'included campaign integration smoke passed: steel_and_cedar (1 players)' \
            <<<"${solo_output}" || grep -Fq 'SCRIPT ERROR' <<<"${solo_output}"; then
        printf 'Packaged Steel and Cedar did not open in one-player mode\n' >&2
        exit 1
    fi
fi

printf 'PC bundle acceptance smoke passed: %s (%s)\n' \
    "${source_revision}" "${source_state}"
