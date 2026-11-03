#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "${script_dir}/../../.." && pwd)"
blender_binary="${GRANDLEON_BLENDER_BINARY:-$(command -v blender || true)}"
output="${1:-${repo_root}/platform/godot/project/art/characters/relay_commander.glb}"

if [[ -z "${blender_binary}" || ! -x "${blender_binary}" ]]; then
    printf 'Blender is required to regenerate imported character assets\n' >&2
    exit 69
fi

case "${output}" in
    /*) ;;
    *) output="$(pwd -P)/${output}" ;;
esac

temporary="${output%.glb}.new.glb"
trap 'rm -f "${temporary}"' EXIT
"${blender_binary}" --background --factory-startup \
    --python "${repo_root}/platform/godot/tools/export_relay_commander.py" \
    -- "${temporary}"
mv "${temporary}" "${output}"
printf 'Imported character asset: %s\n' "${output}"
