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
cp "$mipsync_dir/runtime/game_bridge.cpp" "$stage/game_bridge.cpp"
cp "$mipsync_dir/runtime/game_bridge.h" "$stage/game_bridge.h"
cp "$mipsync_dir/runtime/grandleon_audio.c" "$stage/grandleon_audio.c"
cp "$mipsync_dir/runtime/grandleon_audio.h" "$stage/grandleon_audio.h"
cp "$mipsync_dir/runtime/iso.xml" "$stage/iso.xml"
cp "$mipsync_dir/runtime/system.cnf" "$stage/system.cnf"
mkdir -p "$stage/generated"
python3 "$mipsync_dir/tools/generate_audio.py" "$repo_root" "$stage/generated" --music "$stage/music"
python3 "$mipsync_dir/tools/generate_scene.py" \
    "$manifest" "$repo_root" "$stage/generated"
python3 "$mipsync_dir/tools/generate_simulation.py" \
    "$manifest" "$stage/generated"
normalized=$output/dependencies/psn00b
mkdir -p "$normalized"
for library in psxgpu psxetc psxspu psxcd psxapi; do
    python3 "$mipsync_dir/tools/normalize_psn00b_archive.py" \
        --ar "$PSN00BSDK_ROOT/bin/mipsel-none-elf-ar" \
        "$PSN00BSDK_ROOT/lib/libpsn00b/release/lib${library}_exe_nogprel.a" \
        "$normalized/lib${library}_exe_nogprel.a"
done

output=$(CDPATH= cd -- "$output" && pwd)
stage=$output/source
build=$output/build
psn00b_root=$(CDPATH= cd -- "$PSN00BSDK_ROOT" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
image=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
command -v "$docker" >/dev/null 2>&1 || { echo "container runtime is required: $docker" >&2; exit 1; }
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
        cmake -S /work/source -B /work/build \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/PlayStation.cmake \
            -DPLAYSTATION_NUGGET=/nugget \
            -DGRANDLEON_SOURCE_ROOT=/src \
            -DGRANDLEON_LIBSTDCXX_TREE=/libstdcxx/tree.cc \
            -DPSN00BSDK_ROOT=/psn \
            -DGRANDLEON_PSN00B_ARCHIVE_ROOT=/work/dependencies/psn00b
        cmake --build /work/build --target iso --parallel
    '

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

