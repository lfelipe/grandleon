#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_SEVENFOLD_3D_BUILD_DIR:-${repository_root}/build-n64-sevenfold-3d}"
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
source = root / "tools/console_3d/generate_sevenfold_roster.py"
header = root / "tools/console_3d/generated/sevenfold_roster_proxies.h"
campaign_header = root / "tools/console_3d/generated/sevenfold_campaign_proxies.h"
report = json.loads(
    (root / "tools/console_3d/generated/sevenfold_roster_proxies.json")
    .read_text(encoding="utf-8")
)
if hashlib.sha256(source.read_bytes()).hexdigest() != report["generator_sha256"]:
    raise SystemExit("Sevenfold 3D generator changed; regenerate the roster")
if hashlib.sha256(header.read_bytes()).hexdigest() != report["generated_header_sha256"]:
    raise SystemExit("Sevenfold 3D header changed; regenerate the roster")
if hashlib.sha256(campaign_header.read_bytes()).hexdigest() != report["campaign_header_sha256"]:
    raise SystemExit("Sevenfold 3D campaign header changed; regenerate the roster")
if report["styles"] != [
    "medieval", "scifi", "mythical", "nature", "sengoku", "undead", "pirates"
] or report["figures"] != ["male", "female"] or report["archetypes"] != [
    "knight", "archer", "mage", "stormcaller",
    "healer", "commander", "rogue", "beast"
]:
    raise SystemExit("Sevenfold 3D report names the wrong roster matrix")
if report["totals"]["combinations"] != 112 or len(report["models"]) != 112:
    raise SystemExit("Sevenfold 3D report does not cover 7 x 8 x 2 models")
for name, model in report["models"].items():
    for lod in ("low", "high"):
        if model[lod]["triangles"] <= 0 or model[lod]["vertices"] <= 0:
            raise SystemExit(f"{name}/{lod}: empty generated model")
print("Sevenfold 3D roster provenance passed: 112 combinations")
PYEOF

if [ "${skip_build}" -eq 0 ]; then
    GRANDLEON_N64_SEVENFOLD_3D_BUILD_DIR="${build_dir}" \
        "${repository_root}/platform/nintendo64/scripts/build-sevenfold-3d.sh" \
        "${rebuild_argument[@]}"
fi

rom="${build_dir}/platform/nintendo64/grandleon-n64-sevenfold-3d.z64"
if [ ! -f "${rom}" ]; then
    echo "error: Sevenfold 3D ROM is missing: ${rom}" >&2
    exit 1
fi
magic="$(od -An -tx1 -N4 "${rom}" | tr -d ' \n')"
if [ "${magic}" != "80371240" ]; then
    echo "error: Sevenfold output is not a big-endian .z64 ROM (header ${magic})." >&2
    exit 1
fi

GRANDLEON_N64_BUILD_DIR="${build_dir}" \
GRANDLEON_N64_ARES_SECONDS=120 \
GRANDLEON_N64_ARES_MIN_CHECKS=38 \
GRANDLEON_N64_ARES_LOG=sevenfold-3d.log \
GRANDLEON_N64_ARES_TRAIL=trail-sevenfold-3d \
GRANDLEON_N64_ARES_SHOT=screenshot-sevenfold-3d.png \
    "${repository_root}/platform/nintendo64/ares/run-ares.sh" "${rom}"

log="${build_dir}/ares/sevenfold-3d.log"
for style in medieval scifi mythical nature sengoku undead pirates; do
    for body in male female; do
        for lod in low high; do
            grep -q "^sevenfold stage=${style} body=${body} lod=${lod} models=8 .* depth=on$" "${log}"
            grep -q "^CHECKPOINT sevenfold-${style}-${body}-${lod}$" "${log}"
        done
    done
done
while read -r style terrain; do
    grep -q "^sevenfold attack stage=${style} role=.* terrain=${terrain} models=2 .* depth=on$" "${log}"
    grep -q "^CHECKPOINT sevenfold-attack-${style}-${terrain}$" "${log}"
done <<'EOF'
medieval FOREST
scifi RUINS
mythical MOUNTAIN
nature WATER
sengoku BAMBOO
undead SWAMP
pirates SAND
EOF
grep -q '^sevenfold attack-animation stage=medieval frames=96 .* depth=on$' "${log}"
grep -q '^CHECKPOINT sevenfold-attack-animation-hit$' "${log}"
grep -q '^RESULT PASS 38/38$' "${log}"
echo "Nintendo 64 Sevenfold 3D roster check passed: ${rom}"
