#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the Sevenfold console renderer ROM with pinned libdragon
# and Tiny3D revisions. The removed polygon-proxy campaign/viewer is not part
# of this build.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_LIBDRAGON_BASE_IMAGE:-ghcr.io/dragonminded/libdragon:trunk}"
base_digest="${GRANDLEON_LIBDRAGON_BASE_DIGEST:-sha256:cf979424d0c2d3e281fda0820e8244def9b2debea2ab140c6513e41c8b66cdd0}"
libdragon_commit="${GRANDLEON_N64_RENDERER_LIBDRAGON_COMMIT:-c79a52b42ac790e06e797aede43914dd8754cd5f}"
tiny3d_commit="${GRANDLEON_N64_RENDERER_TINY3D_COMMIT:-517156d9464aaab95bf9531336d9a57624405ca7}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_N64_SEVENFOLD_RENDERER_BUILD_DIR:-${repository_root}/build-n64-sevenfold-renderer}"
image="grandleon/n64-renderer-toolchain:${libdragon_commit:0:12}-t3d${tiny3d_commit:0:12}"
rebuild_image=0

usage() {
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --rebuild-image) rebuild_image=1; shift ;;
        *) usage ;;
    esac
done

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${base_image}" >/dev/null 2>&1; then
        "${docker}" pull "${base_image}"
    fi
    actual_digests="$(
        "${docker}" image inspect --format '{{range .RepoDigests}}{{.}} {{end}}' "${base_image}"
    )"
    case "${actual_digests}" in
        *"${base_digest}"*) ;;
        *)
            echo "error: ${base_image} does not match the pinned digest." >&2
            echo "  expected: ${base_digest}" >&2
            echo "  actual:   ${actual_digests}" >&2
            exit 1
            ;;
    esac
    "${docker}" build \
        --file "${repository_root}/platform/nintendo64/Containerfile.renderer" \
        --build-arg "LIBDRAGON_COMMIT=${libdragon_commit}" \
        --build-arg "TINY3D_COMMIT=${tiny3d_commit}" \
        --tag "${image}" \
        "${repository_root}/platform/nintendo64"
fi

installed_libdragon="$("${docker}" run --rm "${image}" cat /n64_toolchain/LIBDRAGON_COMMIT)"
installed_tiny3d="$("${docker}" run --rm "${image}" cat /n64_toolchain/TINY3D_COMMIT)"
installed_libdragon="${installed_libdragon//[$'\r\n']/}"
installed_tiny3d="${installed_tiny3d//[$'\r\n']/}"
if [ "${installed_libdragon}" != "${libdragon_commit}" ] || \
   [ "${installed_tiny3d}" != "${tiny3d_commit}" ]; then
    echo "error: renderer toolchain image contains the wrong pinned revisions." >&2
    exit 1
fi

mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"
case "${build_dir}/" in
    "${repository_root}/"*) ;;
    *)
        echo "error: GRANDLEON_N64_SEVENFOLD_RENDERER_BUILD_DIR must be inside the repository." >&2
        exit 1
        ;;
esac
container_build_dir="/src/${build_dir#"${repository_root}"/}"

exec 9>"${build_dir}.lock"
flock 9
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env "GRANDLEON_CONTAINER_BUILD_DIR=${container_build_dir}" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}" \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/Nintendo64.cmake \
            -DCMAKE_BUILD_TYPE= \
            -DGRANDLEON_N64=ON \
            -DGRANDLEON_N64_SEVENFOLD_RENDERER=ON \
            -DGRANDLEON_N64_PROJECT=/src/games/sevenfold/source/project.json \
            -DGRANDLEON_STAGE_PICKER=ON \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}" --parallel \
            --target grandleon_n64_sevenfold_renderer
    '

rom="${build_dir}/platform/nintendo64/grandleon-n64-sevenfold-renderer.z64"
if [ ! -f "${rom}" ]; then
    echo "error: expected renderer ROM was not produced: ${rom}" >&2
    exit 1
fi
echo "Sevenfold renderer ROM: ${rom} ($(wc -c < "${rom}") bytes)"
echo "SHA-256: $(sha256sum "${rom}" | cut -d ' ' -f 1)"
