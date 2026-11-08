#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 3 ]; then
    echo "usage: $0 <grandleon-repo> [output-dir] [project.json]" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
package_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$1" && pwd)
output_dir=${2:-/tmp/grandleon-pyrite-build}
project_source=${3:-"$repo_root/games/steel_and_cedar/source/project.json"}
if [ ! -f "$project_source" ]; then
    echo "Nintendo 64 Pyrite export refused: project is not readable: $project_source" >&2
    exit 2
fi
mkdir -p "$output_dir" "$output_dir/cache"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

host_cxx=${CXX:-c++}
"$host_cxx" -std=c++20 -Wall -Wextra -Werror -pedantic \
    "$package_root/tests/board_camera_test.cpp" \
    -o "$output_dir/board_camera_test"
"$output_dir/board_camera_test"

. "$package_root/versions.env"
pyrite_cli=$($package_root/scripts/fetch-pyrite.sh \
    "$package_root" "$output_dir/cache")

if [ -n "${GRANDLEON_PYRITE_N64_INST:-}" ]; then
    n64_inst=$(CDPATH= cd -- "$GRANDLEON_PYRITE_N64_INST" && pwd)
    "$package_root/scripts/check-pins.sh" "$package_root" "$n64_inst"
else
    n64_inst=$($package_root/scripts/export-toolchain.sh \
        "$package_root" "$output_dir/cache")
fi

"$package_root/scripts/stage-inputs.sh" \
    "$package_root" "$repo_root" "$output_dir" "$project_source"

"$host_cxx" -std=c++20 -Wall -Wextra -Werror -pedantic \
    -I"$repo_root/engine/core/include" \
    -I"$repo_root/engine/simulation/include" \
    "$package_root/tests/gameplay_slice_test.cpp" \
    "$package_root/project/src/user/GameplaySlice.cpp" \
    "$output_dir/host-build/libgrandleon_simulation.a" \
    "$output_dir/host-build/libgrandleon_core.a" \
    -o "$output_dir/gameplay_slice_test"
"$output_dir/gameplay_slice_test"

n64_build="$output_dir/grandleon-n64"
N64_INST="$n64_inst" cmake \
    -S "$repo_root" \
    -B "$n64_build" \
    -DCMAKE_TOOLCHAIN_FILE="$repo_root/cmake/toolchains/Nintendo64.cmake" \
    -DN64_INST="$n64_inst" \
    -DGRANDLEON_BUILD_TESTS=OFF \
    -DGRANDLEON_N64=OFF \
    -DGRANDLEON_CCACHE=OFF
N64_INST="$n64_inst" cmake --build "$n64_build" --parallel \
    --target \
        grandleon_core \
        grandleon_campaign \
        grandleon_simulation \
        grandleon_package_format \
        grandleon_tactics \
        grandleon_package_runtime \
        grandleon_campaign_runtime \
        grandleon_storage \
        grandleon_client \
        grandleon_client_campaign

export N64_INST="$n64_inst"
export GRANDLEON_REPO="$repo_root"
export GRANDLEON_N64_BUILD="$n64_build"
"$pyrite_cli" --cli --cmd build "$output_dir/project/project.p64proj"

rom="$output_dir/project/grandleon_pyrite_game.z64"
test -s "$rom"
# A release/export cutover must distinguish this artifact without trusting a
# filename. The canonical big-endian ROM header is stable even though scanning
# arbitrary N64 code strings is not (the linker may word-swap string data).
rom_identity=$(dd if="$rom" bs=1 skip=32 count=20 status=none | tr -d '\000')
if [ "$rom_identity" != 'Grandleon Pyrite Gam' ]; then
    echo "unexpected renderer identity in N64 ROM header: $rom_identity" >&2
    exit 1
fi
printf '%s  %s\n' "$(sha256sum "$rom" | cut -d ' ' -f 1)" "$rom" \
    > "$output_dir/rom.sha256"
wc -c "$rom"
printf '%s\n' "built $rom"
