#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the PlayStation executables inside a pinned container.
#
# Invoked through the `grandleon_playstation` CMake target. It can also be run
# directly from the repository root.
#
#   platform/playstation/scripts/build-playstation.sh          build it
#   platform/playstation/scripts/build-playstation.sh --rebuild-image
#                                                              force the image
#
# The base image is upstream PCSX-Redux's own build image, which carries the
# Debian mipsel cross compiler and a hosted libstdc++ for that ABI. It is
# pinned by digest, and platform/playstation/Containerfile adds CMake and
# Nugget on top. The derived image is cached locally under a tag naming the
# pinned digest, so it is built once and reused.
#
# Two builds happen in here, in this order and in the same container:
#
#   1. a *host* build of grandleon_content_compile, used once to compile
#      games/demo/source/project.json into a package;
#   2. the cross build, which embeds those package bytes.
#
# The host step exists because this target cannot run the content path itself.
# tools/game_content's JSON parser reports a malformed document by throwing,
# and the pinned toolchain's exception-handling archives are compiled
# -mabicalls against glibc, so they cannot be linked into a freestanding
# R3000A executable. See platform/playstation/README.md.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
nugget_revision="${GRANDLEON_NUGGET_REVISION:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
# The throwaway GTE measurement program of platform/playstation/scratch. Off
# unless asked for, and passed to cmake either way rather than only when on: a
# cache remembers an option, so a tree configured once with it would go on
# building it for every gate afterwards.
scratch3d="${GRANDLEON_PLAYSTATION_SCRATCH3D:-OFF}"
# Which character style the scratch draws. Empty is the board project's own, and
# that is the only value any shipped executable ever sees: this names a style for
# the measurement program alone, so that measuring the other six costs nobody an
# edit to games/tarnholt/source/project.json and a revert afterwards. Passed to
# cmake either way, for the same cache reason the option above is.
scratch3d_style="${GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE:-}"

for name in base_digest nugget_revision; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-toolchain:${base_digest#sha256:}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
fi

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    echo "The PlayStation build is deliberately containerised so that the" >&2
    echo "toolchain is the same everywhere. Install a container runtime or" >&2
    echo "set GRANDLEON_DOCKER to one." >&2
    exit 1
fi

# The base is addressed by digest rather than by tag, so there is no tag to
# have moved and no separate verification step: a digest reference either
# resolves to those exact bytes or fails.
pinned_base="${base_image}@${base_digest}"
if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${pinned_base}" >/dev/null 2>&1; then
        echo "Pulling ${pinned_base}…" >&2
        "${docker}" pull "${pinned_base}"
    fi

    echo "Building ${image} from platform/playstation/Containerfile…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/playstation/Containerfile" \
        --build-arg "NUGGET_REVISION=${nugget_revision}" \
        --tag "${image}" \
        "${repository_root}/platform/playstation"
fi

# The image records what it was built from. Verify both, so that a stale local
# tag cannot silently supply a different toolchain or a different Nugget.
installed_digest="$("${docker}" run --rm "${image}" cat /PCSX_REDUX_BUILD_DIGEST)"
installed_digest="${installed_digest//[$'\r\n']/}"
if [ "${installed_digest}" != "${base_digest}" ]; then
    echo "error: ${image} was built from a different base image." >&2
    echo "  expected: ${base_digest}" >&2
    echo "  actual:   ${installed_digest}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi
installed_nugget="$("${docker}" run --rm "${image}" cat /nugget/NUGGET_REVISION)"
installed_nugget="${installed_nugget//[$'\r\n']/}"
if [ "${installed_nugget}" != "${nugget_revision}" ]; then
    echo "error: ${image} was built with a different Nugget revision." >&2
    echo "  expected: ${nugget_revision}" >&2
    echo "  actual:   ${installed_nugget}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi

mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"

# Only the repository is mounted into the container, so the build directory has
# to live inside it for the container to see the same path.
case "${build_dir}/" in
    "${repository_root}/"*) ;;
    *)
        echo "error: GRANDLEON_PLAYSTATION_BUILD_DIR must be inside the repository." >&2
        echo "  repository: ${repository_root}" >&2
        echo "  build dir:  ${build_dir}" >&2
        exit 1
        ;;
esac
container_build_dir="/src/${build_dir#"${repository_root}"/}"

# Two of these configuring one build tree at once corrupt it. Every check
# target funnels through this script, and a parallel gate runs several. The
# build therefore takes a file lock beside the tree; emulator runs above this
# script stay concurrent because the lock guards only the build.
exec 9>"${build_dir}.lock"
flock 9

# The container runs as the invoking user so that build artifacts are not
# root-owned.
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env "GRANDLEON_CONTAINER_BUILD_DIR=${container_build_dir}" \
    --env "GRANDLEON_SCRATCH3D=${scratch3d}" \
    --env "GRANDLEON_SCRATCH3D_STYLE=${scratch3d_style}" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        # 1. The host content compiler, and the two packages it produces: the
        #    demo campaign the conformance executable replays to a golden hash,
        #    and the Tarnholt project the play executables draw.
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}/host" \
            -DCMAKE_BUILD_TYPE=Release \
            -DGRANDLEON_BUILD_TESTS=OFF
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}/host" --parallel \
            --target grandleon_content_compile
        "${GRANDLEON_CONTAINER_BUILD_DIR}/host/grandleon_content_compile" \
            /src/games/demo/source/project.json \
            "${GRANDLEON_CONTAINER_BUILD_DIR}/demo.gpk"
        "${GRANDLEON_CONTAINER_BUILD_DIR}/host/grandleon_content_compile" \
            /src/games/tarnholt/source/project.json \
            "${GRANDLEON_CONTAINER_BUILD_DIR}/tarnholt.gpk"

        # 2. The cross build.
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}/target" \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/PlayStation.cmake \
            -DCMAKE_BUILD_TYPE= \
            -DGRANDLEON_PLAYSTATION=ON \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON \
            -DGRANDLEON_PLAYSTATION_SCRATCH3D="${GRANDLEON_SCRATCH3D}" \
            -DGRANDLEON_PLAYSTATION_SCRATCH3D_STYLE="${GRANDLEON_SCRATCH3D_STYLE}" \
            -DPLAYSTATION_DEMO_PACKAGE="${GRANDLEON_CONTAINER_BUILD_DIR}/demo.gpk" \
            -DPLAYSTATION_BOARD_PACKAGE="${GRANDLEON_CONTAINER_BUILD_DIR}/tarnholt.gpk"
        targets="grandleon_core grandleon_simulation grandleon_tactics"
        targets="${targets} grandleon_package_format grandleon_package_runtime"
        targets="${targets} grandleon_playstation_conformance"
        targets="${targets} grandleon_playstation_card"
        targets="${targets} grandleon_playstation_play"
        targets="${targets} grandleon_playstation_play_raised"
        targets="${targets} grandleon_playstation_turn"
        targets="${targets} grandleon_playstation_campaign"
        targets="${targets} grandleon_playstation_campaign_demo"
        reported="grandleon_psx grandleon_psx_card grandleon_psx_play"
        reported="${reported} grandleon_psx_play_raised grandleon_psx_turn"
        reported="${reported} grandleon_psx_campaign"
        reported="${reported} grandleon_psx_campaign_demo"
        if [ "${GRANDLEON_SCRATCH3D}" = "ON" ]; then
            targets="${targets} grandleon_playstation_scratch3d"
            reported="${reported} grandleon_psx_scratch3d"
        fi
        # shellcheck disable=SC2086
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}/target" --parallel \
            --target ${targets}
        for elf in ${reported}; do
            mipsel-linux-gnu-size \
                "${GRANDLEON_CONTAINER_BUILD_DIR}/target/platform/playstation/${elf}.elf"
        done
    '

expected_executables="grandleon_psx grandleon_psx_card grandleon_psx_play"
expected_executables="${expected_executables} grandleon_psx_play_raised"
expected_executables="${expected_executables} grandleon_psx_turn"
expected_executables="${expected_executables} grandleon_psx_campaign"
expected_executables="${expected_executables} grandleon_psx_campaign_demo"
if [ "${scratch3d}" = "ON" ]; then
    expected_executables="${expected_executables} grandleon_psx_scratch3d"
fi
for name in ${expected_executables}; do
    executable="${build_dir}/target/platform/playstation/${name}.ps-exe"
    if [ ! -f "${executable}" ]; then
        echo "error: expected ${executable} to exist after the build." >&2
        exit 1
    fi
    echo "PS-EXE: ${executable} ($(wc -c < "${executable}") bytes)"
done
