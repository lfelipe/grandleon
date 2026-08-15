#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the WebAssembly simulation module inside the pinned Emscripten SDK
# container and packs it into the editor's checked-in generated module.
#
# Invoked through the `grandleon_wasm` and `grandleon_wasm_check` CMake targets.
# It can also be run directly from the repository root.
#
#   platform/web/scripts/build-wasm.sh            build and update the module
#   platform/web/scripts/build-wasm.sh --check    fail if the module is stale

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
image="${GRANDLEON_EMSDK_IMAGE:-emscripten/emsdk:4.0.23}"
expected_digest="${GRANDLEON_EMSDK_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_WASM_BUILD_DIR:-${repository_root}/build-wasm}"

check_only=0
if [ "${1:-}" = "--check" ]; then
    check_only=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--check]" >&2
    exit 2
fi

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    echo "The WebAssembly build is deliberately containerised so that the" >&2
    echo "checked-in module is reproducible. Install a container runtime or" >&2
    echo "set GRANDLEON_DOCKER to one." >&2
    exit 1
fi

if ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    echo "Pulling ${image}…" >&2
    "${docker}" pull "${image}"
fi

# A tag can be moved. Refuse to build against an image that is not the exact
# one this repository pinned, because the output is checked in.
if [ -n "${expected_digest}" ]; then
    actual_digests="$(
        "${docker}" image inspect --format '{{range .RepoDigests}}{{.}} {{end}}' "${image}"
    )"
    case "${actual_digests}" in
        *"${expected_digest}"*) ;;
        *)
            echo "error: ${image} does not match the pinned digest." >&2
            echo "  expected: ${expected_digest}" >&2
            echo "  actual:   ${actual_digests}" >&2
            exit 1
            ;;
    esac
fi

mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"

# Only the repository is mounted into the container, so the build directory has
# to live inside it for the container to see the same path.
case "${build_dir}/" in
    "${repository_root}/"*) ;;
    *)
        echo "error: GRANDLEON_WASM_BUILD_DIR must be inside the repository." >&2
        echo "  repository: ${repository_root}" >&2
        echo "  build dir:  ${build_dir}" >&2
        exit 1
        ;;
esac
container_build_dir="/src/${build_dir#"${repository_root}"/}"

# The container runs as the invoking user so that build artifacts are not
# root-owned. Emscripten then needs a writable cache and home outside the
# read-only SDK installation.
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env EM_CACHE=/tmp/emscripten-cache \
    --env "GRANDLEON_CONTAINER_BUILD_DIR=${container_build_dir}" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        emcmake cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}" \
            -DCMAKE_BUILD_TYPE=MinSizeRel \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}" --parallel \
            --target grandleon_core \
                     grandleon_simulation \
                     grandleon_package_format \
                     grandleon_package_runtime \
                     grandleon_web_simulation
    '

module="${build_dir}/platform/web/grandleon_simulation.wasm"
if [ ! -f "${module}" ]; then
    echo "error: expected ${module} to exist after the build." >&2
    exit 1
fi

pack_args=("${module}")
if [ "${check_only}" -eq 1 ]; then
    pack_args+=(--check)
fi
node "${repository_root}/editor/scripts/generate-wasm-module.mjs" "${pack_args[@]}"
