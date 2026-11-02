#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
project_source="${repo_root}/games/demo/source/project.json"
build_dir="${repo_root}/build-godot-release"
output_dir="${repo_root}/out/grandleon-arcade"
godot_binary=""

usage() {
    printf '%s\n' \
        "usage: $0 [--project FILE] [--build-dir DIR] [--output DIR] [--godot FILE]" \
        "Builds a portable SteamOS test bundle. The output directory must not exist."
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --project) project_source="$2"; shift 2 ;;
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

# Keep all editor state and import caches out of the user's home directory.
godot_state="${build_dir}/godot-state"
mkdir -p "${godot_state}/data" "${godot_state}/config" "${godot_state}/cache"
XDG_DATA_HOME="${godot_state}/data" \
XDG_CONFIG_HOME="${godot_state}/config" \
XDG_CACHE_HOME="${godot_state}/cache" \
    "${godot_binary}" --editor --headless --quiet --path "${stage_dir}" --quit
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
cmake -E copy "${script_dir}/launch-arcade.sh" "${temporary_dir}/launch-arcade.sh"
cmake -E copy \
    "${repo_root}/platform/godot/project/arcade.example.json" \
    "${temporary_dir}/arcade.example.json"
cmake -E copy \
    "${repo_root}/platform/godot/project/graphics.example.json" \
    "${temporary_dir}/graphics.example.json"
source_revision="$(git -C "${repo_root}" rev-parse HEAD)"
source_state="clean"
if [[ -n "$(git -C "${repo_root}" status --porcelain --untracked-files=normal)" ]]; then
    source_state="dirty"
fi
godot_version="$("${godot_binary}" --version | head -n 1)"
{
    printf 'Grand Leon SteamOS test bundle\n'
    printf 'sourceRevision=%s\n' "${source_revision}"
    printf 'sourceTree=%s\n' "${source_state}"
    printf 'godotVersion=%s\n' "${godot_version}"
    printf 'defaultCampaign=last_signal\n'
    printf 'defaultGraphics=voxel-sprites\n'
} >"${temporary_dir}/BUILD-INFO.txt"
chmod +x "${temporary_dir}/godot" "${temporary_dir}/launch-arcade.sh"
for local_config in arcade.local.json graphics.local.json; do
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

printf 'SteamOS bundle: %s\n' "${output_dir}"
printf 'Test locally: GRANDLEON_WINDOWED=1 %q/launch-arcade.sh\n' "${output_dir}"
