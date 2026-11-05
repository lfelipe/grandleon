#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_PROTOTYPE_BUILD_DIR:-${repository_root}/build-n64-character-prototype}"
skip_build=0
rebuild_argument=()

usage() {
    echo "usage: $(basename "$0") [--skip-build] [--rebuild-image]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-build) skip_build=1; shift ;;
        --rebuild-image) rebuild_argument=(--rebuild-image); shift ;;
        *) usage ;;
    esac
done

python3 - "${repository_root}" <<'PYEOF'
import hashlib
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
report = json.loads(
    (root / "tools/console_3d/generated/character_proxies_v2.json")
    .read_text(encoding="utf-8")
)
helper = root / "platform/godot/tools/blender_character_tools.py"
for name, model in report["models"].items():
    source = root / model["source"]
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    helper_hash = hashlib.sha256(helper.read_bytes()).hexdigest()
    if source_hash != model["source_sha256"]:
        raise SystemExit(f"{name}: Godot source changed; regenerate console LODs")
    if helper_hash != model["blender_helper_sha256"]:
        raise SystemExit(f"{name}: Blender helper changed; regenerate console LODs")
    for lod in ("low", "high"):
        data = model["lods"][lod]
        if data["triangles"] <= 0 or data["vertices"] <= 0:
            raise SystemExit(f"{name}/{lod}: empty generated LOD")
print("console proxy provenance passed")
PYEOF

grep -q '^RESULT PASS 4/4$' \
    "${repository_root}/tools/console_3d/evidence/v2/n64/ares.log"
grep -q '^RESULT FAIL 144/147 style scifi$' \
    "${repository_root}/tools/console_3d/evidence/v2/ps1/pcsx-redux.log"

if [ "${skip_build}" -eq 0 ]; then
    GRANDLEON_N64_PROTOTYPE_BUILD_DIR="${build_dir}" \
        "${repository_root}/platform/nintendo64/scripts/build-character-prototype.sh" \
        "${rebuild_argument[@]}"
fi

rom="${build_dir}/platform/nintendo64/grandleon-n64-3d-character-prototype.z64"
if [ ! -f "${rom}" ]; then
    echo "error: prototype ROM is missing: ${rom}" >&2
    exit 1
fi
magic="$(od -An -tx1 -N4 "${rom}" | tr -d ' \n')"
if [ "${magic}" != "80371240" ]; then
    echo "error: prototype is not a big-endian .z64 ROM (header ${magic})." >&2
    exit 1
fi

GRANDLEON_N64_BUILD_DIR="${build_dir}" \
GRANDLEON_N64_ARES_SECONDS=60 \
GRANDLEON_N64_ARES_MIN_CHECKS=4 \
GRANDLEON_N64_ARES_LOG=character-prototype.log \
GRANDLEON_N64_ARES_TRAIL=trail-character-prototype \
GRANDLEON_N64_ARES_SHOT=screenshot-character-prototype.png \
    "${repository_root}/platform/nintendo64/ares/run-ares.sh" "${rom}"

log="${build_dir}/ares/character-prototype.log"
for scene in \
    'v2 strategic sixteen low-LOD' \
    'v2 tactical six high-LOD' \
    'v2 action two high-LOD'; do
    grep -q "^${scene} .* depth=on$" "${log}"
done
grep -q '^RESULT PASS 4/4$' "${log}"
echo "Nintendo 64 3D character prototype check passed: ${rom}"
