#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
project_source="${repo_root}/games/emberbound/source/project.json"
build_dir="${repo_root}/build-godot-release"
output_dir="${repo_root}/out/grandleon-pc"
godot_binary=""
campaign_key="emberbound"
player_only=0
include_examples=1
release_version="$(tr -d '\r\n' <"${repo_root}/RELEASE_VERSION")"

usage() {
    printf '%s\n' \
        "usage: $0 [--project FILE] [--campaign KEY] [--player-only] [--build-dir DIR] [--output DIR] [--godot FILE]" \
        "Builds a portable Linux PC bundle. The output directory must not exist."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --project) project_source="$2"; include_examples=0; shift 2 ;;
        --campaign) campaign_key="$2"; shift 2 ;;
        --player-only) player_only=1; shift ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --output) output_dir="$2"; shift 2 ;;
        --godot) godot_binary="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 64 ;;
    esac
done

absolute_path() {
    case "$1" in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$(pwd -P)" "$1" ;;
    esac
}

project_source="$(absolute_path "${project_source}")"
build_dir="$(absolute_path "${build_dir}")"
output_dir="$(absolute_path "${output_dir}")"
if [[ -n "${godot_binary}" ]]; then
    godot_binary="$(absolute_path "${godot_binary}")"
fi

if [[ ! -f "${project_source}" ]]; then
    printf 'project not found: %s\n' "${project_source}" >&2
    exit 66
fi
if [[ ! "${campaign_key}" =~ ^[a-z][a-z0-9_]*$ ]]; then
    printf 'campaign must be a portable source key: %s\n' "${campaign_key}" >&2
    exit 64
fi
if [[ ! "${release_version}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    printf 'invalid product release version: %s\n' "${release_version}" >&2
    exit 65
fi
if [[ -e "${output_dir}" ]]; then
    printf 'output already exists; choose a new directory: %s\n' "${output_dir}" >&2
    exit 73
fi
if [[ -z "${godot_binary}" ]]; then
    godot_binary="$(${script_dir}/setup-godot.sh | tail -n 1)"
fi
if [[ ! -x "${godot_binary}" ]]; then
    printf 'Godot executable not found: %s\n' "${godot_binary}" >&2
    exit 69
fi

cmake -S "${repo_root}" -B "${build_dir}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGRANDLEON_BUILD_TESTS=OFF \
    -DGRANDLEON_BUILD_GODOT=ON \
    -DGRANDLEON_GODOT_API_VERSION=4.7 \
    -DGRANDLEON_WERROR=ON
cmake --build "${build_dir}" --parallel --target \
    grandleon_content_compile grandleon_godot_project

stage_dir="${build_dir}/platform/godot/project/app"
"${build_dir}/grandleon_content_compile" "${project_source}" "${stage_dir}/game.gpk"
if [[ "${include_examples}" == "1" ]]; then
    cmake -E make_directory "${stage_dir}/content/included"
    "${build_dir}/grandleon_content_compile" \
        "${repo_root}/games/tarnholt/source/project.json" \
        "${stage_dir}/content/included/tarnholt.gpk"
    "${build_dir}/grandleon_content_compile" \
        "${repo_root}/games/emberbound/source/project.json" \
        "${stage_dir}/content/included/emberbound.gpk"
else
    cmake -E remove_directory "${stage_dir}/content/included"
fi

# Keep all editor state and import caches out of the user's home directory.
godot_state="${build_dir}/godot-state"
mkdir -p "${godot_state}/data" "${godot_state}/config" "${godot_state}/cache"
XDG_DATA_HOME="${godot_state}/data" \
XDG_CONFIG_HOME="${godot_state}/config" \
XDG_CACHE_HOME="${godot_state}/cache" \
    "${godot_binary}" --headless --quiet --import --path "${stage_dir}"
cmake -E rm -rf "${stage_dir}/.godot/editor"

extension="${stage_dir}/bin/libgrandleon_godot.so"
if strings "${extension}" | grep -Fq "${repo_root}"; then
    printf 'refusing bundle: native extension contains the private checkout path\n' >&2
    exit 70
fi

output_parent="$(dirname -- "${output_dir}")"
mkdir -p "${output_parent}"
temporary_dir="$(mktemp -d "${output_dir}.tmp.XXXXXX")"
cleanup() {
    if [[ -d "${temporary_dir}" ]]; then
        cmake -E rm -rf "${temporary_dir}"
    fi
}
trap cleanup EXIT

cmake -E copy_directory "${stage_dir}" "${temporary_dir}/project"
cmake -E copy "${godot_binary}" "${temporary_dir}/godot"
cmake -E copy "${script_dir}/launch-pc.sh" "${temporary_dir}/launch-pc.sh"
cmake -E copy "${script_dir}/control-pc.sh" "${temporary_dir}/control-pc.sh"
cmake -E copy "${stage_dir}/icon.png" "${temporary_dir}/grandleon-icon.png"
cmake -E make_directory "${temporary_dir}/licenses"
cmake -E copy "${repo_root}/LICENSE" "${temporary_dir}/LICENSE.txt"
cmake -E copy "${repo_root}/NOTICE" "${temporary_dir}/NOTICE.txt"
cmake -E copy "${repo_root}/assets/audio/VSCO-2-CE-LICENSE.txt" \
    "${temporary_dir}/licenses/VSCO-2-CE-LICENSE.txt"
cmake -E copy "${repo_root}/assets/fonts/spleen/LICENSE" \
    "${temporary_dir}/licenses/SPLEEN-LICENSE.txt"
cmake -E copy \
    "${repo_root}/third_party/godot/4.7.2/LICENSE.txt" \
    "${temporary_dir}/licenses/GODOT-LICENSE.txt"
cmake -E copy \
    "${repo_root}/third_party/godot/4.7.2/COPYRIGHT.txt" \
    "${temporary_dir}/licenses/GODOT-COPYRIGHT.txt"
cmake -E copy \
    "${repo_root}/third_party/godot-cpp/10.0.0-stable/LICENSE.md" \
    "${temporary_dir}/licenses/GODOT-CPP-LICENSE.txt"
cmake -E copy \
    "${repo_root}/platform/godot/project/graphics.example.json" \
    "${temporary_dir}/graphics.example.json"
cmake -E copy \
    "${repo_root}/platform/godot/project/controls.example.json" \
    "${temporary_dir}/controls.example.json"
if [[ "${player_only}" == "1" ]]; then
    cmake -E rm -rf \
        "${temporary_dir}/project/creator.tscn" \
        "${temporary_dir}/project/map_editor.tscn" \
        "${temporary_dir}/project/story_editor.tscn" \
        "${temporary_dir}/project/army_editor.tscn" \
        "${temporary_dir}/project/gear_editor.tscn" \
        "${temporary_dir}/project/content/templates" \
        "${temporary_dir}/project/scripts/creator/creator_screen.gd" \
        "${temporary_dir}/project/scripts/creator/map_editor.gd" \
        "${temporary_dir}/project/scripts/creator/story_editor.gd" \
        "${temporary_dir}/project/scripts/creator/army_editor.gd" \
        "${temporary_dir}/project/scripts/creator/gear_editor.gd" \
        "${temporary_dir}/project/scripts/creator/standalone_exporter.gd"
    printf 'Player-only Grandleon game bundle.\n' >"${temporary_dir}/PLAYER-ONLY"
fi
printf '%s\n' "${campaign_key}" >"${temporary_dir}/DEFAULT-CAMPAIGN"
source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
source_state="clean"
if [[ -n "$(git -C "${repo_root}" status --porcelain --untracked-files=normal)" ]]; then
    source_state="dirty"
fi
godot_version="$("${godot_binary}" --version | head -n 1)"
creator_included="yes"
if [[ "${player_only}" == "1" ]]; then
    creator_included="no"
fi
{
    printf 'Grandleon Linux PC bundle\n'
    printf 'releaseVersion=%s\n' "${release_version}"
    printf 'sourceRevision=%s\n' "${source_revision}"
    printf 'sourceTree=%s\n' "${source_state}"
    printf 'godotVersion=%s\n' "${godot_version}"
    printf 'defaultCampaign=%s\n' "${campaign_key}"
    if [[ "${include_examples}" == "1" ]]; then
        printf 'includedCampaigns=tarnholt:1,emberbound:2\n'
    else
        printf 'includedCampaigns=none\n'
    fi
    printf 'defaultGraphics=voxel-sprites\n'
    printf 'creatorIncluded=%s\n' "${creator_included}"
} >"${temporary_dir}/BUILD-INFO.txt"
chmod +x \
    "${temporary_dir}/godot" \
    "${temporary_dir}/launch-pc.sh" \
    "${temporary_dir}/control-pc.sh"
for local_config in graphics.local.json controls.local.json; do
    if [[ -e "${temporary_dir}/${local_config}" || \
          -e "${temporary_dir}/project/${local_config}" ]]; then
        printf 'refusing bundle: private local config was staged: %s\n' \
            "${local_config}" >&2
        exit 70
    fi
done
if rg --hidden --no-ignore --text --fixed-strings --files-with-matches \
    "${repo_root}" "${temporary_dir}" >/dev/null; then
    printf 'refusing bundle: output contains the private checkout path\n' >&2
    exit 70
fi
mv "${temporary_dir}" "${output_dir}"
trap - EXIT

printf 'Linux PC bundle: %s\n' "${output_dir}"
printf 'Test locally: GRANDLEON_WINDOWED=1 %q/launch-pc.sh\n' "${output_dir}"
