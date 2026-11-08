#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <package-root> <cache-dir>" >&2
    exit 2
fi

package_root=$1
cache_dir=$2
. "$package_root/versions.env"

output="$cache_dir/n64-toolchain-${LIBDRAGON_REVISION}-${TINY3D_REVISION}"
if [ -d "$output" ]; then
    "$package_root/scripts/check-pins.sh" "$package_root" "$output" >/dev/null
    printf '%s\n' "$output"
    exit 0
fi

if command -v docker >/dev/null 2>&1; then
    container_tool=docker
elif command -v podman >/dev/null 2>&1; then
    container_tool=podman
else
    echo "Docker or Podman is required to create the pinned N64 toolchain" >&2
    exit 1
fi

tag="grandleon-pyrite-toolchain:${LIBDRAGON_REVISION%????????????????????????????????????}-t3d-${TINY3D_REVISION%????????????????????????????????????}"
$container_tool build \
    --file "$package_root/toolchain/Containerfile" \
    --build-arg "LIBDRAGON_REVISION=$LIBDRAGON_REVISION" \
    --build-arg "TINY3D_REVISION=$TINY3D_REVISION" \
    --tag "$tag" \
    "$package_root/toolchain" >&2

mkdir -p "$output"
container_id=$($container_tool create "$tag")
cleanup() {
    $container_tool rm "$container_id" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM
$container_tool cp "$container_id:/n64_toolchain/." "$output/" >&2
$container_tool cp "$container_id:/GRANDLEON_PYRITE_TOOLCHAIN_LICENSES.tar.xz" "$output/" >&2
$container_tool cp "$container_id:/GRANDLEON_PYRITE_TOOLCHAIN_COMPONENTS.tsv" "$output/" >&2
"$package_root/scripts/check-pins.sh" "$package_root" "$output" >/dev/null
printf '%s\n' "$output"
