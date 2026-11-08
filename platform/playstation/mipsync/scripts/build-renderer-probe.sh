#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk [MIPSYNC_SOURCE=/path/or/url] $0 scene.json empty-output-directory" >&2
    exit 2
}

test "$#" -eq 2 || usage
manifest=$1
output=$2
test -f "$manifest" || { echo "scene manifest does not exist: $manifest" >&2; exit 1; }
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
test -f "$PSN00BSDK_ROOT/lib/libpsn00b/cmake/sdk.cmake" || {
    echo "PSN00BSDK_ROOT is not a PSn00bSDK installation: $PSN00BSDK_ROOT" >&2
    exit 1
}
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
source=${MIPSYNC_SOURCE:-$UPSTREAM_URL}
upstream=$output/upstream
stage=$output/source
build=$output/build

git clone --quiet --no-checkout "$source" "$upstream"
git -C "$upstream" checkout --quiet --detach "$UPSTREAM_REVISION"
"$repo_root/tools/upstream_patches/manage.sh" apply mipsync "$upstream"
patched_revision=$(git -C "$upstream" rev-parse HEAD)

mkdir -p "$stage"
cp -R "$upstream/templates/ps1/starter/." "$stage/"
cp "$mipsync_dir/runtime/CMakeLists.txt" "$stage/CMakeLists.txt"
cp "$mipsync_dir/runtime/main.c" "$stage/main.c"
cp "$mipsync_dir/runtime/scene_adapter.c" "$stage/scene_adapter.c"
cp "$mipsync_dir/runtime/iso.xml" "$stage/iso.xml"
cp "$mipsync_dir/runtime/system.cnf" "$stage/system.cnf"
mkdir -p "$stage/generated"
python3 "$mipsync_dir/tools/generate_scene.py" \
    "$manifest" "$repo_root" "$stage/generated"

PATH="$PSN00BSDK_ROOT/bin:$PATH" cmake \
    -S "$stage" -B "$build" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$PSN00BSDK_ROOT/lib/libpsn00b/cmake/sdk.cmake"
PATH="$PSN00BSDK_ROOT/bin:$PATH" cmake --build "$build" --target iso

elf=$build/grandleon_mipsync.elf
exe=$build/grandleon_mipsync.exe
cue=$build/grandleon-mipsync.cue
bin=$build/grandleon-mipsync.bin
python3 "$mipsync_dir/tools/write_build_manifest.py" \
    --elf "$elf" --exe "$exe" --cue "$cue" --bin "$bin" \
    --scene-metrics "$stage/generated/scene-metrics.json" \
    --size-tool "$PSN00BSDK_ROOT/bin/mipsel-none-elf-size" \
    --nm-tool "$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm" \
    --upstream-base "$UPSTREAM_REVISION" \
    --upstream-patched "$patched_revision" \
    --output "$output/build-manifest.json"

printf 'CUE=%s\nBIN=%s\nMANIFEST=%s\n' \
    "$cue" "$bin" "$output/build-manifest.json"

