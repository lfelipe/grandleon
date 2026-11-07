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

hybrid_dir = root / "platform/nintendo64/prototype/assets/sevenfold_hybrid"
hybrid_report = json.loads((hybrid_dir / "manifest.json").read_text())
hybrid_generator = root / hybrid_report["generated_by"]
if (
    hashlib.sha256(hybrid_generator.read_bytes()).hexdigest()
    != hybrid_report["generator_sha256"]
):
    raise SystemExit("N64 hybrid generator changed; regenerate its cutouts")
if len(hybrid_report["assets"]) != 40:
    raise SystemExit(
        "N64 hybrid manifest must contain thirty-eight cutouts and two effects"
    )
for asset in hybrid_report["assets"]:
    output = hybrid_dir / asset["path"]
    if not output.is_file() or output.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"N64 hybrid asset is missing or not PNG: {output}")
    if "source" not in asset:
        continue
    source_path = root / asset["source"]
    mask_path = root / asset["mask"]
    if (
        hashlib.sha256(source_path.read_bytes()).hexdigest()
        != asset["source_sha256"]
    ):
        raise SystemExit(f"N64 hybrid source changed: {source_path}")
    if (
        hashlib.sha256(mask_path.read_bytes()).hexdigest()
        != asset["mask_sha256"]
    ):
        raise SystemExit(f"N64 hybrid mask changed: {mask_path}")

terrain_dir = root / "platform/nintendo64/prototype/assets/sevenfold_terrain"
terrain_report = json.loads((terrain_dir / "manifest.json").read_text())
terrain_generator = root / terrain_report["generated_by"]
if (
    hashlib.sha256(terrain_generator.read_bytes()).hexdigest()
    != terrain_report["generator_sha256"]
):
    raise SystemExit("N64 terrain generator changed; regenerate its atlases")
if len(terrain_report["assets"]) != 3:
    raise SystemExit("N64 terrain manifest must contain three original atlases")
for asset in terrain_report["assets"]:
    output = terrain_dir / asset["path"]
    if not output.is_file() or output.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"N64 terrain asset is missing or not PNG: {output}")
    if hashlib.sha256(output.read_bytes()).hexdigest() != asset["sha256"]:
        raise SystemExit(f"N64 terrain asset changed: {output}")
    expected = (64, 32, "CI4") if asset["path"] == "floor_atlas.png" \
        else (64, 64, "CI4")
    if (asset["width"], asset["height"], asset["format"]) != expected:
        raise SystemExit(f"N64 terrain atlas contract changed: {output}")

backdrop_dir = root / "platform/nintendo64/prototype/assets/sevenfold_backdrops"
backdrop_report = json.loads((backdrop_dir / "manifest.json").read_text())
backdrop_generator = root / backdrop_report["generated_by"]
if (
    hashlib.sha256(backdrop_generator.read_bytes()).hexdigest()
    != backdrop_report["generator_sha256"]
):
    raise SystemExit("N64 backdrop generator changed; regenerate its tiles")
source_manifest = root / backdrop_report["source_manifest"]
if (
    hashlib.sha256(source_manifest.read_bytes()).hexdigest()
    != backdrop_report["source_manifest_sha256"]
):
    raise SystemExit("Shared Godot backdrop manifest changed; regenerate N64 tiles")
if (
    backdrop_report["terrain_ids"]
    != {"dominant": 2, "secondary": 9, "transition": 12}
    or backdrop_report["transition_orientation"] != "north_south"
    or backdrop_report["variants"] != 3
    or backdrop_report["selected_variant"] != 2
    or not backdrop_report["freeze_for_exchange"]
):
    raise SystemExit("N64 backdrop selector contract changed")
if len(backdrop_report["assets"]) != 72 or len(backdrop_report["composites"]) != 3:
    raise SystemExit("N64 backdrop must contain three twenty-four-tile variants")
for variant in backdrop_report["source_variants"]:
    for layer in variant["layers"]:
        path = root / layer["path"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != layer["sha256"]:
            raise SystemExit(f"Shared Godot backdrop layer changed: {path}")
for asset in backdrop_report["assets"]:
    output = backdrop_dir / asset["path"]
    if hashlib.sha256(output.read_bytes()).hexdigest() != asset["sha256"]:
        raise SystemExit(f"N64 backdrop tile changed: {output}")
    if (asset["width"], asset["height"], asset["format"]) != (80, 40, "CI4"):
        raise SystemExit(f"N64 backdrop tile contract changed: {output}")
for composite in backdrop_report["composites"]:
    output = backdrop_dir / composite["path"]
    if hashlib.sha256(output.read_bytes()).hexdigest() != composite["sha256"]:
        raise SystemExit(f"N64 backdrop composite changed: {output}")

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
    layout_hash ^= value
    layout_hash = (layout_hash * 0x01000193) & 0xFFFFFFFF
if (
    comparison["schema"] != 2
    or comparison["id"] != "medieval_forest_renderer_showcase_v2"
    or surroundings["width"] != 12
    or surroundings["height"] != 10
    or surroundings["active_origin"] != [3, 3]
    or len(surroundings["rows"]) != 10
    or any(len(row) != 12 for row in surroundings["rows"])
    or layout_hash != 0x66909BD6
    or comparison["combat_backdrop"]["foreground_contract"]["live_floor_strip"]
    or comparison["combat_backdrop"]["foreground_contract"]["floor_composition"]
       != "pre_rendered_full_frame"
):
    raise SystemExit("N64 schema-two showcase layout is out of sync")
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
GRANDLEON_N64_ARES_MIN_CHECKS=63 \
GRANDLEON_N64_ARES_POLL_SECONDS=0.05 \
GRANDLEON_N64_ARES_LOG=sevenfold-3d.log \
GRANDLEON_N64_ARES_TRAIL=trail-sevenfold-3d \
GRANDLEON_N64_ARES_SHOT=screenshot-sevenfold-3d.png \
    "${repository_root}/platform/nintendo64/ares/run-ares.sh" "${rom}"

log="${build_dir}/ares/sevenfold-3d.log"
for style in medieval scifi mythical nature sengoku undead pirates; do
    for body in male female; do
        for lod in low high; do
            grep -q "^sevenfold stage=${style} body=${body} lod=${lod} models=8 .* heading=three-quarter depth=on$" "${log}"
            grep -q "^CHECKPOINT sevenfold-${style}-${body}-${lod}$" "${log}"
        done
    done
done
while read -r team direction; do
    grep -q "^sevenfold heading=${direction} team=${team} models=8 .* heading=three-quarter depth=on$" "${log}"
    grep -q "^CHECKPOINT sevenfold-heading-${team}$" "${log}"
done <<'EOF'
blue right
red left
EOF
for style in medieval pirates; do
    for mode in flat ci4; do
        grep -q "^sevenfold tactical stage=${style} mode=${mode} models=8 bodies=mixed .* highlights=blue-and-gold depth=on$" "${log}"
        checkpoint=before
        [ "${mode}" = ci4 ] && checkpoint=after
        grep -q "^CHECKPOINT sevenfold-tactical-${style}-${checkpoint}$" "${log}"
    done
done
grep -q '^sevenfold tactical-hybrid stage=medieval models=8 roles=8 bodies=mixed teams=blue-red .* tmem_peak=3648 embedded=.* ordering=foot-depth highlights=blue-and-gold$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-hybrid-medieval$' "${log}"
grep -q '^sevenfold terrain-quality material=64x64-ci4 regions=4 prop=64x64-ci4 cells=4 tmem_peak=2080 embedded=.* variation=deterministic transitions=cluster-tone provenance=repo-generated$' "${log}"
grep -q '^sevenfold backdrop pack=forest-grass-paved orientation=north-south variants=3 selected=2 tiles=24 tile=80x40-ci4 composite=320x240 tmem_peak=1632 embedded=.* rdram=.* frozen=yes floor=pre-rendered-full-frame source=godot-plates$' "${log}"
grep -q '^sevenfold showcase-contract schema=2 surroundings=12x10 active-origin=3,3 layout_hash=66909bd6 diagnostics=none$' "${log}"
grep -q '^sevenfold tactical-hybrid-animation stage=medieval frames=72 .* motion=tile-move poses=idle2-walk2 easing=smoothstep beats=anticipation-move-settle ordering=foot-depth$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-hybrid-move-start$' "${log}"
grep -q '^CHECKPOINT sevenfold-tactical-hybrid-move-end$' "${log}"
while read -r style role terrain; do
    for mode in flat ci4; do
        grep -q "^sevenfold texture stage=${style} role=${role} terrain=${terrain} mode=${mode} models=2 .* depth=on$" "${log}"
        checkpoint=before
        [ "${mode}" = ci4 ] && checkpoint=after
        grep -q "^CHECKPOINT sevenfold-texture-${style}-${checkpoint}$" "${log}"
    done
done <<'EOF'
medieval knight FOREST
mythical mage MOUNTAIN
EOF
for style in medieval mythical; do
    for pose in idle impact; do
        grep -q "^sevenfold hybrid stage=${style} pose=${pose} terrain=.* sprites=2 scene_triangles=.* tmem_peak=4064 embedded=.* team=blue-red$" "${log}"
        grep -q "^CHECKPOINT sevenfold-hybrid-${style}-${pose}$" "${log}"
    done
done
while read -r style terrain; do
    grep -q "^sevenfold attack stage=${style} role=.* terrain=${terrain} models=2 .* heading=three-quarter depth=on$" "${log}"
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
grep -q '^sevenfold attack-animation stage=medieval frames=96 .* anchors=locked counter=right-to-left heading=three-quarter depth=on$' "${log}"
grep -q '^CHECKPOINT sevenfold-attack-animation-hit$' "${log}"
grep -q '^sevenfold hybrid-animation stage=medieval frames=246 .* anchors=locked counter=right-to-left poses=5 easing=smoothstep motion=anticipation-impact-recoil-settle camera=frontal$' "${log}"
grep -q '^CHECKPOINT sevenfold-hybrid-animation-start$' "${log}"
grep -q '^CHECKPOINT sevenfold-hybrid-animation-impact$' "${log}"
grep -q '^CHECKPOINT sevenfold-hybrid-animation-counter$' "${log}"
grep -q '^sevenfold readme-sequence id=medieval_forest_renderer_showcase_v2 frames=245 fps=30 seconds=8.15 board=6x4 surroundings=12x10 terrain=2-9-12 hud=compact diagnostics=none left=medieval-knight-male-blue-hp12-to8 right=medieval-knight-female-red-hp11-to8 damage=3-4 impacts=3.45-5.50 anchors=locked .* fps_throughput=.*$' "${log}"
grep -q '^CHECKPOINT sevenfold-readme-sequence-start$' "${log}"
for checkpoint in board focus attack counter restored; do
    grep -q "^CHECKPOINT sevenfold-readme-${checkpoint}$" "${log}"
done
grep -q '^RESULT PASS 63/63$' "${log}"
echo "Nintendo 64 Sevenfold 3D roster check passed: ${rom}"
