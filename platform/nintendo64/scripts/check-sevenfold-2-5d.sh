#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_SEVENFOLD_2_5D_BUILD_DIR:-${repository_root}/build-n64-sevenfold-2-5d}"
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

def sha(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

cutout_dir = root / "platform/nintendo64/prototype/assets/sevenfold_cutouts"
cutouts = json.loads((cutout_dir / "manifest.json").read_text())
if cutouts["format"] != "Grandleon N64 fixed-camera 2.5D cutouts v3":
    raise SystemExit("unexpected 2.5D cutout format")
if sha(root / cutouts["generated_by"]) != cutouts["generator_sha256"]:
    raise SystemExit("cutout generator changed; regenerate the cutouts")
if len(cutouts["assets"]) != 40:
    raise SystemExit("2.5D pack must contain thirty-eight cutouts and two effects")
for asset in cutouts["assets"]:
    output = cutout_dir / asset["path"]
    if not output.is_file() or output.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"missing 2.5D cutout: {output}")
    if "source" in asset:
        if sha(root / asset["source"]) != asset["source_sha256"]:
            raise SystemExit(f"cutout source changed: {asset['source']}")
        if sha(root / asset["mask"]) != asset["mask_sha256"]:
            raise SystemExit(f"cutout team mask changed: {asset['mask']}")

terrain_dir = root / "platform/nintendo64/prototype/assets/sevenfold_terrain"
terrain = json.loads((terrain_dir / "manifest.json").read_text())
if sha(root / terrain["generated_by"]) != terrain["generator_sha256"]:
    raise SystemExit("terrain generator changed; regenerate the atlases")
if len(terrain["assets"]) != 3:
    raise SystemExit("terrain pack must contain three atlases")
for asset in terrain["assets"]:
    output = terrain_dir / asset["path"]
    if sha(output) != asset["sha256"]:
        raise SystemExit(f"terrain atlas changed: {output}")

backdrop_dir = root / "platform/nintendo64/prototype/assets/sevenfold_backdrops"
backdrops = json.loads((backdrop_dir / "manifest.json").read_text())
if sha(root / backdrops["generated_by"]) != backdrops["generator_sha256"]:
    raise SystemExit("backdrop generator changed; regenerate the tiles")
if backdrops["variants"] != 3 or backdrops["selected_variant"] != 2:
    raise SystemExit("backdrop selector contract changed")
if len(backdrops["assets"]) != 72 or len(backdrops["composites"]) != 3:
    raise SystemExit("backdrop pack must hold three twenty-four-tile variants")
for asset in backdrops["assets"]:
    output = backdrop_dir / asset["path"]
    if sha(output) != asset["sha256"]:
        raise SystemExit(f"backdrop tile changed: {output}")

comparison = json.loads(
    (root / "tools/renderer_comparison/scene.json").read_text()
)
surroundings = comparison["surroundings"]
values = [
    surroundings["width"], surroundings["height"],
    *surroundings["active_origin"],
    *(value for row in surroundings["rows"] for value in row),
]
layout_hash = 0x811C9DC5
for value in values:
    layout_hash = ((layout_hash ^ value) * 0x01000193) & 0xFFFFFFFF
if (
    comparison["schema"] != 2
    or surroundings["width"] != 12
    or surroundings["height"] != 10
    or surroundings["active_origin"] != [3, 3]
    or layout_hash != 0x66909BD6
    or comparison["combat_backdrop"]["foreground_contract"]["live_floor_strip"]
):
    raise SystemExit("shared renderer-comparison scene is out of sync")
print("Nintendo 64 2.5D asset provenance passed")
PYEOF

if [ "${skip_build}" -eq 0 ]; then
    GRANDLEON_N64_SEVENFOLD_2_5D_BUILD_DIR="${build_dir}" \
        "${repository_root}/platform/nintendo64/scripts/build-sevenfold-2-5d.sh" \
        "${rebuild_argument[@]}"
fi

rom="${build_dir}/platform/nintendo64/grandleon-n64-sevenfold-2-5d.z64"
if [ ! -f "${rom}" ]; then
    echo "error: Sevenfold 2.5D ROM is missing: ${rom}" >&2
    exit 1
fi
magic="$(od -An -tx1 -N4 "${rom}" | tr -d ' \n')"
if [ "${magic}" != "80371240" ]; then
    echo "error: output is not a big-endian .z64 ROM (header ${magic})." >&2
    exit 1
fi

GRANDLEON_N64_BUILD_DIR="${build_dir}" \
GRANDLEON_N64_ARES_SECONDS=40 \
GRANDLEON_N64_ARES_MIN_CHECKS=11 \
GRANDLEON_N64_ARES_POLL_SECONDS=0.05 \
GRANDLEON_N64_ARES_LOG=sevenfold-2-5d.log \
GRANDLEON_N64_ARES_TRAIL=trail-sevenfold-2-5d \
GRANDLEON_N64_ARES_SHOT=screenshot-sevenfold-2-5d.png \
    "${repository_root}/platform/nintendo64/ares/run-ares.sh" "${rom}"

log="${build_dir}/ares/sevenfold-2-5d.log"
grep -q '^grandleon n64 2.5d renderer$' "${log}"
grep -q '^sevenfold 2.5d terrain=64x64-ci4 backdrop=320x240-ci4 tiles=24 cutouts=godot-derived floor=pre-rendered-full-frame ' "${log}"
grep -q '^sevenfold tactical-cutout stage=medieval models=8 roles=8 bodies=mixed teams=blue-red .* highlights=blue-and-gold$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-cutout-medieval$' "${log}"
grep -q '^sevenfold tactical-cutout-animation stage=medieval frames=72 .* easing=smoothstep .* ordering=foot-depth$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-cutout-move-start$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-cutout-move-end$' "${log}"
grep -q '^sevenfold readme-sequence id=medieval_forest_renderer_showcase_v2 frames=245 fps=30 seconds=8.15 board=6x4 surroundings=12x10 .* anchors=locked .* fps_throughput=.*$' "${log}"
for checkpoint in sequence-start board focus attack counter restored; do
    grep -q "^CHECKPOINT sevenfold-readme-${checkpoint}$" "${log}"
done
grep -q '^RESULT PASS 11/11$' "${log}"
echo "Nintendo 64 Sevenfold 2.5D check passed: ${rom}"
