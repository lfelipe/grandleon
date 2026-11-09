#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk MIPSYNC_SOURCE=/path/to/mipsync $0 project.json empty-output-directory" >&2
    exit 2
}

test "$#" -eq 2 || usage
project=$1
output=$2
test -f "$project" || { echo "project source is not readable: $project" >&2; exit 1; }
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
test -d "$PSN00BSDK_ROOT/include/libpsn00b" \
        && test -f "$PSN00BSDK_ROOT/lib/libpsn00b/release/libpsxgpu_exe_nogprel.a" || {
    echo "PSN00BSDK_ROOT is not a PSn00bSDK installation: $PSN00BSDK_ROOT" >&2
    exit 1
}
if test -e "$output"; then
    test -d "$output" || { echo "output is not a directory: $output" >&2; exit 1; }
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo "output directory must be empty: $output" >&2
        exit 1
    }
else
    mkdir -p "$output"
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mipsync_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$mipsync_dir/../../.." && pwd)
patch_dir=$repo_root/tools/upstream_patches/engines/mipsync
# shellcheck disable=SC1090
. "$patch_dir/upstream.env"
source=${MIPSYNC_SOURCE:-$UPSTREAM_URL}
output=$(CDPATH= cd -- "$output" && pwd)
project=$(CDPATH= cd -- "$(dirname -- "$project")" && pwd)/$(basename -- "$project")
host=$output/host
package=$output/game.gpk
plan=$output/campaign-plan.tsv
upstream=$output/upstream
normalized=$output/dependencies/psn00b
stages=$output/stages
disc=$output/disc

cmake -S "$repo_root" -B "$host" \
    -DGRANDLEON_BUILD_TESTS=OFF -DGRANDLEON_DESKTOP_SDL=OFF \
    -DGRANDLEON_CCACHE=OFF
cmake --build "$host" --parallel \
    --target grandleon_content_compile grandleon_ps1_mipsync_scene_export
"$host/grandleon_content_compile" "$project" "$package"
python3 "$mipsync_dir/tools/plan_project.py" "$project" > "$plan"
mode=$(awk -F '\t' '$1 == "campaign" { print $3 }' "$plan")
case "$mode" in one|two|choice) ;; *) echo "invalid campaign plan" >&2; exit 1 ;; esac

git clone --quiet --no-checkout "$source" "$upstream"
git -C "$upstream" checkout --quiet --detach "$UPSTREAM_REVISION"
git -C "$upstream" config user.name "Grandleon Builder"
git -C "$upstream" config user.email "builder@grandleon.invalid"
"$repo_root/tools/upstream_patches/manage.sh" apply mipsync "$upstream"
patched_revision=$(git -C "$upstream" rev-parse HEAD)

mkdir -p "$normalized" "$stages" "$disc"
psn_ar=$(command -v "${GRANDLEON_PSN00B_AR:-mipsel-linux-gnu-ar}")
psn_size=$(command -v "${GRANDLEON_PSN00B_SIZE:-mipsel-linux-gnu-size}")
psn_nm=$(command -v "${GRANDLEON_PSN00B_NM:-mipsel-linux-gnu-nm}")
disc_builder=$(command -v "${GRANDLEON_MKPSXISO:-mkpsxiso}")
for library in psxgpu psxetc psxapi; do
    python3 "$mipsync_dir/tools/normalize_psn00b_archive.py" \
        --ar "$psn_ar" \
        "$PSN00BSDK_ROOT/lib/libpsn00b/release/lib${library}_exe_nogprel.a" \
        "$normalized/lib${library}_exe_nogprel.a"
done

stage_runtime() {
    stage=$1
    primary=$2
    one_player=${3:-}
    source_dir=$stage/source
    mkdir -p "$source_dir"
    cp -R "$upstream/templates/ps1/starter/." "$source_dir/"
    cp "$mipsync_dir/runtime/CMakeLists.txt" "$source_dir/CMakeLists.txt"
    cp "$mipsync_dir/runtime/main.c" "$source_dir/main.c"
    cp "$mipsync_dir/runtime/scene_adapter.c" "$source_dir/scene_adapter.c"
    cp "$mipsync_dir/runtime/game_bridge.cpp" "$source_dir/game_bridge.cpp"
    cp "$mipsync_dir/runtime/game_bridge.h" "$source_dir/game_bridge.h"
    cp "$mipsync_dir/runtime/iso.xml" "$source_dir/iso.xml"
    cp "$mipsync_dir/runtime/system.cnf" "$source_dir/system.cnf"
    mkdir -p "$source_dir/generated"
    python3 "$mipsync_dir/tools/generate_scene.py" \
        "$primary" "$repo_root" "$source_dir/generated"
    if test -n "$one_player"; then
        python3 "$mipsync_dir/tools/generate_simulation.py" \
            "$primary" "$source_dir/generated" \
            --one-player-manifest "$one_player"
    else
        python3 "$mipsync_dir/tools/generate_simulation.py" \
            "$primary" "$source_dir/generated"
    fi
}

tab=$(printf '\t')
while IFS="$tab" read -r kind index encounter; do
    test "$kind" = stage || continue
    stage=$stages/$index
    mkdir -p "$stage"
    case "$mode" in
        one)
            "$host/grandleon_ps1_mipsync_scene_export" \
                "$package" "$encounter" 1 "$stage/one.json"
            stage_runtime "$stage" "$stage/one.json"
            ;;
        two)
            "$host/grandleon_ps1_mipsync_scene_export" \
                "$package" "$encounter" 2 "$stage/two.json"
            stage_runtime "$stage" "$stage/two.json"
            ;;
        choice)
            "$host/grandleon_ps1_mipsync_scene_export" \
                "$package" "$encounter" 1 "$stage/one.json"
            "$host/grandleon_ps1_mipsync_scene_export" \
                "$package" "$encounter" 2 "$stage/two.json"
            stage_runtime "$stage" "$stage/two.json" "$stage/one.json"
            ;;
    esac
done < "$plan"

nugget=${PLAYSTATION_NUGGET:-/nugget}
libstdcxx=${GRANDLEON_LIBSTDCXX_TREE:-/libstdcxx/tree.cc}
test -d "$nugget" || { echo "Nugget source is not available: $nugget" >&2; exit 1; }
test -f "$libstdcxx" || { echo "libstdc++ compatibility source is not available: $libstdcxx" >&2; exit 1; }
while IFS="$tab" read -r kind index encounter; do
    test "$kind" = stage || continue
    stage=$stages/$index
    cmake -S "$stage/source" -B "$stage/build" \
        -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/toolchains/PlayStation.cmake" \
        -DPLAYSTATION_NUGGET="$nugget" \
        -DGRANDLEON_SOURCE_ROOT="$repo_root" \
        -DGRANDLEON_LIBSTDCXX_TREE="$libstdcxx" \
        -DPSN00BSDK_ROOT="$PSN00BSDK_ROOT" \
        -DGRANDLEON_PSN00B_ARCHIVE_ROOT="$normalized" \
        -DGRANDLEON_MIPSYNC_CAMPAIGN=ON
    cmake --build "$stage/build" --target grandleon_mipsync --parallel
done < "$plan"

boot_file=
stage_files=$output/stage-files.tsv
: > "$stage_files"
while IFS="$tab" read -r kind index encounter; do
    test "$kind" = stage || continue
    stage=$stages/$index
    filename=$(tr -d '\r\n' < "$stage/source/generated/stage-filename.txt")
    case "$filename" in STG[0-9][0-9].EXE) ;; *) echo "invalid stage filename: $filename" >&2; exit 1 ;; esac
    test ! -e "$disc/$filename" || { echo "duplicate stage filename: $filename" >&2; exit 1; }
    cp "$stage/build/grandleon_mipsync.exe" "$disc/$filename"
    printf '%s\t%s\t%s\n' "$index" "$filename" "$encounter" >> "$stage_files"
    if test "$(tr -d '\r\n' < "$stage/source/generated/stage-is-boot.txt")" = 1; then
        test -z "$boot_file" || { echo "campaign has more than one boot stage" >&2; exit 1; }
        boot_file=$filename
    fi
done < "$plan"
test -n "$boot_file" || { echo "campaign has no boot stage" >&2; exit 1; }

printf 'BOOT = cdrom:\\%s;1\nTCB = 4\nEVENT = 16\nSTACK = 801FFFF0\n' \
    "$boot_file" > "$disc/system.cnf"
{
    printf '%s\n' '<?xml version="1.0" encoding="utf-8"?>' \
        '<iso_project><track type="data">' \
        '<identifiers system="PLAYSTATION" volume="GRANDLEON" volume_set="GRANDLEON" publisher="GRANDLEON" data_preparer="GRANDLEON" application="PLAYSTATION" copyright="SYSTEM.CNF;1" />' \
        '<directory_tree>' \
        '<file name="SYSTEM.CNF" type="data" source="system.cnf" />'
    while IFS="$tab" read -r index filename encounter; do
        printf '<file name="%s" type="data" source="%s" />\n' \
            "$filename" "$filename"
    done < "$stage_files"
    printf '%s\n' '<dummy sectors="1024" />' \
        '</directory_tree></track></iso_project>'
} > "$disc/disc.xml"
(cd "$disc" && "$disc_builder" -y \
    -o "$output/game.bin" -c "$output/game.cue" disc.xml)

while IFS="$tab" read -r kind index encounter; do
    test "$kind" = stage || continue
    stage=$stages/$index
    python3 "$mipsync_dir/tools/write_build_manifest.py" \
        --elf "$stage/build/grandleon_mipsync.elf" \
        --exe "$stage/build/grandleon_mipsync.exe" \
        --cue "$output/game.cue" --bin "$output/game.bin" \
        --scene-metrics "$stage/source/generated/scene-metrics.json" \
        --size-tool "$psn_size" \
        --nm-tool "$psn_nm" \
        --upstream-base "$UPSTREAM_REVISION" \
        --upstream-patched "$patched_revision" \
        --output "$stage/build-manifest.json"
done < "$plan"

python3 - "$stages" <<'PY'
import json
from pathlib import Path
import sys

for path in sorted(Path(sys.argv[1]).glob("*/build-manifest.json")):
    document = json.loads(path.read_text(encoding="utf-8"))
    memory = document["main_ram"]
    vram = document["scene"]["vram"]
    if document.get("identity") != "GRANDLEON_PS1_RENDERER=MIPSYNC":
        raise SystemExit(f"{path}: wrong renderer identity")
    if not memory.get("passes_playable_slice_budget"):
        raise SystemExit(f"{path}: exceeds PlayStation main RAM")
    if memory["platform_audio_reserve_bytes"] < 65536:
        raise SystemExit(f"{path}: audio reserve is below 64 KiB")
    if vram["texture_bytes"] + vram["vram_reserved_bytes"] != 384 * 256 * 2:
        raise SystemExit(f"{path}: texture atlas exceeds PlayStation VRAM")
    print(
        f"MIPSYNC_PROJECT_BUDGET {path.parent.name} "
        f"ram={memory['playable_slice_committed_bytes']}/{memory['main_ram_bytes']} "
        f"vram={vram['texture_bytes']}/{384 * 256 * 2}"
    )
PY

sha256sum "$output/game.cue" "$output/game.bin" > "$output/SHA256SUMS"
printf 'GRANDLEON_PS1_RENDERER=MIPSYNC fallback=none stages=%s mode=%s\n' \
    "$(wc -l < "$stage_files")" "$mode"
printf 'CUE=%s\nBIN=%s\nHASHES=%s\n' \
    "$output/game.cue" "$output/game.bin" "$output/SHA256SUMS"
