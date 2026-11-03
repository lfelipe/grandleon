#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
blender_binary="${GRANDLEON_BLENDER_BINARY:-$(command -v blender || true)}"
output_dir="${1:-${repo_root}/platform/godot/project/art/characters}"

if [[ -z "${blender_binary}" || ! -x "${blender_binary}" ]]; then
    printf 'Blender is required to regenerate imported character assets\n' >&2
    exit 69
fi

case "${output_dir}" in
    /*) ;;
    *) output_dir="$(pwd -P)/${output_dir}" ;;
esac

temporary_dir="$(mktemp -d)"
trap 'rm -r "${temporary_dir}"' EXIT
"${blender_binary}" --background --factory-startup \
    --python "${repo_root}/platform/godot/tools/export_relay_commander.py" \
    -- "${temporary_dir}/relay_commander.glb"
"${blender_binary}" --background --factory-startup \
    --python "${repo_root}/platform/godot/tools/export_relay_drone.py" \
    -- "${temporary_dir}/relay_drone.glb"
"${blender_binary}" --background --factory-startup \
    --python "${repo_root}/platform/godot/tools/export_relay_rogue.py" \
    -- "${temporary_dir}/relay_rogue.glb"
"${blender_binary}" --background --factory-startup \
    --python "${repo_root}/platform/godot/tools/export_relay_stormcaller.py" \
    -- "${temporary_dir}/relay_stormcaller.glb"
mkdir -p "${output_dir}"
mv "${temporary_dir}/relay_commander.glb" "${output_dir}/relay_commander.glb"
mv "${temporary_dir}/relay_drone.glb" "${output_dir}/relay_drone.glb"
mv "${temporary_dir}/relay_rogue.glb" "${output_dir}/relay_rogue.glb"
mv "${temporary_dir}/relay_stormcaller.glb" "${output_dir}/relay_stormcaller.glb"
printf 'Imported character assets: %s\n' "${output_dir}"
