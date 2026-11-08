#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <grandleon-repo> [output-dir]" >&2
    exit 2
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
package_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
repo_root=$(CDPATH= cd -- "$1" && pwd)
output_dir=${2:-/tmp/grandleon-pyrite-build}
mkdir -p "$output_dir" "$output_dir/cache"
output_dir=$(CDPATH= cd -- "$output_dir" && pwd)

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
    "$package_root" "$repo_root" "$output_dir"

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
        grandleon_simulation \
        grandleon_package_format \
        grandleon_tactics \
        grandleon_package_runtime

export N64_INST="$n64_inst"
export GRANDLEON_REPO="$repo_root"
export GRANDLEON_N64_BUILD="$n64_build"
"$pyrite_cli" --cli --cmd build "$output_dir/project/project.p64proj"

rom="$output_dir/project/grandleon_pyrite_bell_orchard.z64"
test -s "$rom"
printf '%s  %s\n' "$(sha256sum "$rom" | cut -d ' ' -f 1)" "$rom" \
    > "$output_dir/rom.sha256"
wc -c "$rom"
printf '%s\n' "built $rom"
