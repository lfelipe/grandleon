#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the Nintendo 64 conformance ROM inside a pinned libdragon container.
#
# Invoked through the `grandleon_n64` CMake target. It can also be run directly
# from the repository root.
#
#   platform/nintendo64/scripts/build-n64.sh          build the ROM
#   platform/nintendo64/scripts/build-n64.sh --rebuild-image
#                                                     force the toolchain image
#   platform/nintendo64/scripts/build-n64.sh --project games/demo/source/project.json
#                                                     build a ROM of that game
#   platform/nintendo64/scripts/build-n64.sh --targets grandleon_n64_campaign
#                                                     build fewer than all of them
#   platform/nintendo64/scripts/build-n64.sh --stage-picker
#                                                     ROMs that can jump Stages
#
# `--project` is what makes this script serve an authoring surface as well as
# the gate: the ROM's content is a build input, so a ROM of an author's own game
# is this same build with one path changed rather than a second pipeline. It
# pairs with `--targets`, because a request wants one ROM and waiting for
# thirteen would be most of the wait.
#
# `--stage-picker` builds ROMs whose pause menu can leave a battle for any Stage
# of the campaign. It is for looking at one thing in a late Stage without playing
# to it, and a Stage reached that way has recorded nothing the ordinary route
# would have, so a battle there can be unwinnable. Off unless asked for, and a
# project cannot ask: it is a property of the image, not of the game.
#
# The published libdragon container carries the mips64-elf cross compiler only;
# libdragon itself is not in it. So the base image is pinned by digest and
# platform/nintendo64/Containerfile builds one pinned libdragon commit on top.
# The derived image is cached locally under a tag that names that commit, so it
# is built once and reused.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_LIBDRAGON_BASE_IMAGE:-ghcr.io/dragonminded/libdragon:trunk}"
base_digest="${GRANDLEON_LIBDRAGON_BASE_DIGEST:-}"
libdragon_commit="${GRANDLEON_LIBDRAGON_COMMIT:-}"
tiny3d_commit="${GRANDLEON_TINY3D_COMMIT:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64}"

if [ -z "${libdragon_commit}" ]; then
    echo "error: GRANDLEON_LIBDRAGON_COMMIT is not set." >&2
    exit 1
fi
if [ -z "${tiny3d_commit}" ]; then
    echo "error: GRANDLEON_TINY3D_COMMIT is not set." >&2
    exit 1
fi
# Both pins are in the tag. The image is two libraries built from source, so a
# tag naming only one of them would let a Tiny3D change reuse an image that does
# not contain it -- silently, and with a ROM to show for it.
image="grandleon/n64-toolchain:${libdragon_commit}-t3d${tiny3d_commit:0:8}"

rebuild_image=0
project="${GRANDLEON_N64_PROJECT:-}"
targets="${GRANDLEON_N64_TARGETS:-}"
stage_picker=0
scratch3d=0
usage() {
    echo "usage: $(basename "$0") [--rebuild-image] [--project PATH]" \
         "[--targets t1,t2,...] [--stage-picker] [--scratch3d]" >&2
    exit 2
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --rebuild-image) rebuild_image=1; shift ;;
        --project) [ "$#" -ge 2 ] || usage; project="$2"; shift 2 ;;
        --targets) [ "$#" -ge 2 ] || usage; targets="$2"; shift 2 ;;
        --stage-picker) stage_picker=1; shift ;;
        --scratch3d) scratch3d=1; shift ;;
        *) usage ;;
    esac
done

# Every ROM this repository checks, which is what an unqualified run builds.
default_targets="grandleon_core,grandleon_simulation,grandleon_tactics,\
grandleon_package_format,grandleon_package_runtime,grandleon_game_content,\
grandleon_client,grandleon_n64_conformance,grandleon_n64_play,\
grandleon_n64_probe,grandleon_n64_autopilot,grandleon_n64_campaign,\
grandleon_n64_campaign_autopilot"
[ -n "${targets}" ] || targets="${default_targets}"

# The container sees the repository at /src and nothing else, so a project it
# is asked to build has to be inside the repository. Said here, with the path,
# rather than discovered as a file-not-found inside the container.
#
# With no `--project`, the cache entry is *removed* rather than left alone, so
# that CMake re-establishes its own default. Leaving it alone would mean a tree
# in which somebody once built an author's game went on building that author's
# game for every later run, silently. A build directory is not supposed to
# remember which game the repository ships.
project_arg="-UGRANDLEON_N64_PROJECT"
if [ -n "${project}" ]; then
    if [ ! -f "${project}" ]; then
        echo "error: --project is not a file: ${project}" >&2
        exit 1
    fi
    project="$(cd "$(dirname "${project}")" && pwd)/$(basename "${project}")"
    case "${project}" in
        "${repository_root}"/*) ;;
        *)
            echo "error: --project must be inside the repository." >&2
            echo "  repository: ${repository_root}" >&2
            echo "  project:    ${project}" >&2
            exit 1
            ;;
    esac
    project_arg="-DGRANDLEON_N64_PROJECT=/src/${project#"${repository_root}"/}"
fi

# Written as OFF rather than removed when it was not asked for, because unlike
# the project this has a default worth stating: a tree in which somebody once
# built a picker ROM must not go on building them.
picker_arg="-DGRANDLEON_STAGE_PICKER=OFF"
[ "${stage_picker}" -eq 0 ] || picker_arg="-DGRANDLEON_STAGE_PICKER=ON"
# The measurement ROM is off unless asked for: it is in no gate, and a target
# that exists only when a flag is passed cannot be built by accident.
scratch_arg="-DGRANDLEON_N64_SCRATCH3D=OFF"
[ "${scratch3d}" -eq 0 ] || scratch_arg="-DGRANDLEON_N64_SCRATCH3D=ON"

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    echo "The Nintendo 64 build is deliberately containerised so that the" >&2
    echo "toolchain is the same everywhere. Install a container runtime or" >&2
    echo "set GRANDLEON_DOCKER to one." >&2
    exit 1
fi

if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${base_image}" >/dev/null 2>&1; then
        echo "Pulling ${base_image}…" >&2
        "${docker}" pull "${base_image}"
    fi

    # A tag can be moved. Refuse to build against a base image that is not the
    # exact one this repository pinned.
    if [ -n "${base_digest}" ]; then
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
    fi

    echo "Building ${image} from platform/nintendo64/Containerfile…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/nintendo64/Containerfile" \
        --build-arg "LIBDRAGON_COMMIT=${libdragon_commit}" \
        --build-arg "TINY3D_COMMIT=${tiny3d_commit}" \
        --tag "${image}" \
        "${repository_root}/platform/nintendo64"
fi

# The image records the libdragon commit it was built from. Verify it, so that
# a stale local tag cannot silently supply a different libdragon.
installed_commit="$(
    "${docker}" run --rm "${image}" cat /n64_toolchain/LIBDRAGON_COMMIT
)"
installed_commit="${installed_commit//[$'\r\n']/}"
if [ "${installed_commit}" != "${libdragon_commit}" ]; then
    echo "error: ${image} was built from a different libdragon commit." >&2
    echo "  expected: ${libdragon_commit}" >&2
    echo "  actual:   ${installed_commit}" >&2
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
        echo "error: GRANDLEON_N64_BUILD_DIR must be inside the repository." >&2
        echo "  repository: ${repository_root}" >&2
        echo "  build dir:  ${build_dir}" >&2
        exit 1
        ;;
esac
container_build_dir="/src/${build_dir#"${repository_root}"/}"

# Two of these configuring one build tree at once corrupt it, and that can
# happen: every check target funnels through this script, and a parallel gate
# runs several. The build therefore takes a file lock beside the tree; emulator
# runs above this script stay concurrent because the lock guards only the build.
exec 9>"${build_dir}.lock"
flock 9

# The container runs as the invoking user so that build artifacts are not
# root-owned.
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    --env "GRANDLEON_CONTAINER_BUILD_DIR=${container_build_dir}" \
    --env "GRANDLEON_CONTAINER_PROJECT_ARG=${project_arg}" \
    --env "GRANDLEON_CONTAINER_PICKER_ARG=${picker_arg}" \
    --env "GRANDLEON_CONTAINER_SCRATCH_ARG=${scratch_arg}" \
    --env "GRANDLEON_CONTAINER_TARGETS=${targets//,/ }" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}" \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/Nintendo64.cmake \
            -DCMAKE_BUILD_TYPE= \
            -DGRANDLEON_N64=ON \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON \
            ${GRANDLEON_CONTAINER_PICKER_ARG} \
            ${GRANDLEON_CONTAINER_SCRATCH_ARG} \
            ${GRANDLEON_CONTAINER_PROJECT_ARG}
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}" --parallel \
            --target ${GRANDLEON_CONTAINER_TARGETS}
    '

# Report every ROM the requested targets should have produced. Derived from the
# target list rather than written out again, so a run that built one ROM is not
# failed for the five it was not asked for.
for target in ${targets//,/ }; do
    case "${target}" in
        grandleon_n64_conformance) rom_name="grandleon_n64.z64" ;;
        grandleon_n64_*) rom_name="${target}.z64" ;;
        *) continue ;;
    esac
    rom="${build_dir}/platform/nintendo64/${rom_name}"
    if [ ! -f "${rom}" ]; then
        echo "error: expected ${rom} to exist after the build." >&2
        exit 1
    fi
    echo "ROM: ${rom} ($(wc -c < "${rom}") bytes)"
done
