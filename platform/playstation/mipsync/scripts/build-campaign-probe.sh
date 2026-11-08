#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 bell-1p.json bell-2p.json deepwood-1p.json deepwood-2p.json watch-1p.json watch-2p.json village-1p.json village-2p.json gate-1p.json gate-2p.json empty-output-directory" >&2
    exit 2
}

test "$#" -eq 11 || usage
bell_one=$1
bell_two=$2
deep_one=$3
deep_two=$4
watch_one=$5
watch_two=$6
village_one=$7
village_two=$8
gate_one=$9
gate_two=${10}
output=${11}
for manifest in "$bell_one" "$bell_two" "$deep_one" "$deep_two" \
        "$watch_one" "$watch_two" "$village_one" "$village_two" \
        "$gate_one" "$gate_two"; do
    test -f "$manifest" || { echo "scene manifest does not exist: $manifest" >&2; exit 1; }
done
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
if test -e "$output"; then
    test -d "$output" || { echo "output exists and is not a directory: $output" >&2; exit 1; }
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
output=$(CDPATH= cd -- "$output" && pwd)
upstream=$output/upstream
source=${MIPSYNC_SOURCE:-$UPSTREAM_URL}

git clone --quiet --no-checkout "$source" "$upstream"
git -C "$upstream" checkout --quiet --detach "$UPSTREAM_REVISION"
"$repo_root/tools/upstream_patches/manage.sh" apply mipsync "$upstream"
patched_revision=$(git -C "$upstream" rev-parse HEAD)

normalized=$output/dependencies/psn00b
mkdir -p "$normalized"
for library in psxgpu psxetc psxapi; do
    python3 "$mipsync_dir/tools/normalize_psn00b_archive.py" \
        --ar "$PSN00BSDK_ROOT/bin/mipsel-none-elf-ar" \
        "$PSN00BSDK_ROOT/lib/libpsn00b/release/lib${library}_exe_nogprel.a" \
        "$normalized/lib${library}_exe_nogprel.a"
done

stage_scene() {
    stage=$1
    two=$2
    one=$3
    mkdir -p "$stage"
    cp -R "$upstream/templates/ps1/starter/." "$stage/"
    cp "$mipsync_dir/runtime/CMakeLists.txt" "$stage/CMakeLists.txt"
    cp "$mipsync_dir/runtime/main.c" "$stage/main.c"
    cp "$mipsync_dir/runtime/scene_adapter.c" "$stage/scene_adapter.c"
    cp "$mipsync_dir/runtime/game_bridge.cpp" "$stage/game_bridge.cpp"
    cp "$mipsync_dir/runtime/game_bridge.h" "$stage/game_bridge.h"
    cp "$mipsync_dir/runtime/iso.xml" "$stage/iso.xml"
    cp "$mipsync_dir/runtime/system.cnf" "$stage/system.cnf"
    mkdir -p "$stage/generated"
    python3 "$mipsync_dir/tools/generate_scene.py" "$two" "$repo_root" "$stage/generated"
    python3 "$mipsync_dir/tools/generate_simulation.py" \
        "$two" "$stage/generated" --one-player-manifest "$one"
}

stage_scene "$output/bell-source" "$bell_two" "$bell_one"
stage_scene "$output/deep-source" "$deep_two" "$deep_one"
stage_scene "$output/watch-source" "$watch_two" "$watch_one"
stage_scene "$output/village-source" "$village_two" "$village_one"
stage_scene "$output/gate-source" "$gate_two" "$gate_one"

psn00b_root=$(CDPATH= cd -- "$PSN00BSDK_ROOT" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
image=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation toolchain image is missing: $image" >&2
    exit 1
}
"$docker" run --rm --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --volume "$repo_root:/src:ro" \
    --volume "$output:/work" \
    --volume "$psn00b_root:/psn:ro" \
    --workdir /work \
    "$image" /bin/bash -euo pipefail -c '
        configure() {
            source_dir=$1
            build_dir=$2
            cmake -S "$source_dir" -B "$build_dir" \
                -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/PlayStation.cmake \
                -DPLAYSTATION_NUGGET=/nugget \
                -DGRANDLEON_SOURCE_ROOT=/src \
                -DGRANDLEON_LIBSTDCXX_TREE=/libstdcxx/tree.cc \
                -DPSN00BSDK_ROOT=/psn \
                -DGRANDLEON_PSN00B_ARCHIVE_ROOT=/work/dependencies/psn00b \
                -DGRANDLEON_MIPSYNC_CAMPAIGN=ON \
                -DGRANDLEON_MIPSYNC_CAMPAIGN_PROBE=ON
            cmake --build "$build_dir" --target grandleon_mipsync --parallel
        }
        configure /work/bell-source /work/bell-build
        configure /work/deep-source /work/deep-build
        configure /work/watch-source /work/watch-build
        configure /work/village-source /work/village-build
        configure /work/gate-source /work/gate-build
    '

disc=$output/disc
mkdir -p "$disc"
boot_file=
for stage in bell deep watch village gate; do
    disc_file=$(tr -d '\r\n' < "$output/${stage}-source/generated/stage-filename.txt")
    case "$disc_file" in
        STG[0-9][0-9].EXE) ;;
        *) echo "invalid generated stage filename: $disc_file" >&2; exit 1 ;;
    esac
    cp "$output/${stage}-build/grandleon_mipsync.exe" "$disc/$disc_file"
    if test "$(tr -d '\r\n' < "$output/${stage}-source/generated/stage-is-boot.txt")" = 1; then
        test -z "$boot_file" || { echo "campaign has more than one boot stage" >&2; exit 1; }
        boot_file=$disc_file
    fi
done
test -n "$boot_file" || { echo "campaign has no boot stage" >&2; exit 1; }
printf 'BOOT = cdrom:\\%s;1\nTCB = 4\nEVENT = 16\nSTACK = 801FFFF0\n' "$boot_file" > "$disc/system.cnf"
printf '%s\n' \
    '<?xml version="1.0" encoding="utf-8"?>' \
    '<iso_project><track type="data">' \
    '<identifiers system="PLAYSTATION" volume="GRANDLEON_CAMPAIGN" volume_set="GRANDLEON_CAMPAIGN" publisher="GRANDLEON" data_preparer="GRANDLEON" application="PLAYSTATION" copyright="SYSTEM.CNF;1" />' \
    '<directory_tree>' \
    '<file name="SYSTEM.CNF" type="data" source="system.cnf" />' \
    '<file name="STG01.EXE" type="data" source="STG01.EXE" />' \
    '<file name="STG02.EXE" type="data" source="STG02.EXE" />' \
    '<file name="STG03.EXE" type="data" source="STG03.EXE" />' \
    '<file name="STG04.EXE" type="data" source="STG04.EXE" />' \
    '<file name="STG05.EXE" type="data" source="STG05.EXE" />' \
    '<dummy sectors="1024" />' \
    '</directory_tree></track></iso_project>' > "$disc/disc.xml"
(cd "$disc" && "$PSN00BSDK_ROOT/bin/mkpsxiso" -y \
    -o "$output/grandleon-mipsync-campaign.bin" \
    -c "$output/grandleon-mipsync-campaign.cue" disc.xml)

for stage in bell deep watch village gate; do
    python3 "$mipsync_dir/tools/write_build_manifest.py" \
        --elf "$output/${stage}-build/grandleon_mipsync.elf" \
        --exe "$output/${stage}-build/grandleon_mipsync.exe" \
        --cue "$output/grandleon-mipsync-campaign.cue" \
        --bin "$output/grandleon-mipsync-campaign.bin" \
        --scene-metrics "$output/${stage}-source/generated/scene-metrics.json" \
        --size-tool "$PSN00BSDK_ROOT/bin/mipsel-none-elf-size" \
        --nm-tool "$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm" \
        --upstream-base "$UPSTREAM_REVISION" \
        --upstream-patched "$patched_revision" \
        --output "$output/${stage}-build-manifest.json"
done

sha256sum "$output/grandleon-mipsync-campaign.cue" \
    "$output/grandleon-mipsync-campaign.bin" \
    "$output/bell-build/grandleon_mipsync.elf" \
    "$output/deep-build/grandleon_mipsync.elf" \
    "$output/watch-build/grandleon_mipsync.elf" \
    "$output/village-build/grandleon_mipsync.elf" \
    "$output/gate-build/grandleon_mipsync.elf" > "$output/SHA256SUMS"
printf 'CUE=%s\nBIN=%s\nHASHES=%s\n' \
    "$output/grandleon-mipsync-campaign.cue" \
    "$output/grandleon-mipsync-campaign.bin" "$output/SHA256SUMS"
