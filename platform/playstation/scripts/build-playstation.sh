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
#   platform/playstation/scripts/build-playstation.sh --project games/demo/source/project.json
#                                                              build that game
#   platform/playstation/scripts/build-playstation.sh --targets grandleon_playstation_campaign
#                                                              build fewer than all of them
#
# `--project` is what makes this script serve an authoring surface as well as
# the gate: the executable's content is a build input, so a disc of an author's
# own game is this same build with one path changed rather than a second
# pipeline. It pairs with `--targets`, because a request wants one executable
# and waiting for ten would be most of the wait — and because the autopilot
# builds carry scripts recorded against the shipped boards, so they are the
# gate's and not an author's.
#
# The base image is upstream PCSX-Redux's own build image, which carries the
# Debian mipsel cross compiler and a hosted libstdc++ for that ABI. It is
# pinned by digest, and platform/playstation/Containerfile adds CMake and
# Nugget on top. The derived image is cached locally under a tag naming the
# pinned digest, so it is built once and reused.
#
# Two builds happen in here, in this order and in the same container:
#
#   1. a *host* build of grandleon_content_compile, used to compile the demo
#      project and the project of `--project` into packages;
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
# edit to the project below and a revert afterwards. Passed to cmake either way,
# for the same cache reason the option above is.
scratch3d_style="${GRANDLEON_PLAYSTATION_SCRATCH3D_STYLE:-}"
# The project the play, turn and campaign executables carry, and the one this
# script compiles into their package. The default lives here and nowhere else:
# platform/playstation/CMakeLists.txt refuses without it rather than keeping a
# second copy that could name a different game.
project="${GRANDLEON_PLAYSTATION_PROJECT:-${repository_root}/games/tarnholt/source/project.json}"
targets="${GRANDLEON_PLAYSTATION_TARGETS:-}"

for name in base_digest nugget_revision; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-toolchain:${base_digest#sha256:}"

rebuild_image=0
usage() {
    echo "usage: $(basename "$0") [--rebuild-image] [--project PATH]" \
         "[--targets t1,t2,...]" >&2
    exit 2
}
while [ "$#" -gt 0 ]; do
    case "$1" in
        --rebuild-image) rebuild_image=1; shift ;;
        --project) [ "$#" -ge 2 ] || usage; project="$2"; shift 2 ;;
        --targets) [ "$#" -ge 2 ] || usage; targets="$2"; shift 2 ;;
        *) usage ;;
    esac
done

# Every executable this repository checks, which is what an unqualified run
# builds. The engine libraries come first because the cross build needs them,
# and naming them is how a partial run still gets them. The last three carry a
# controller script; the played builds above them wait on the pad, which under
# a headless emulator means for ever. See platform/playstation/README.md.
default_targets="grandleon_core,grandleon_simulation,grandleon_tactics,\
grandleon_package_format,grandleon_package_runtime,\
grandleon_playstation_conformance,grandleon_playstation_card,\
grandleon_playstation_play,grandleon_playstation_play_raised,\
grandleon_playstation_turn,grandleon_playstation_campaign,\
grandleon_playstation_campaign_demo,grandleon_playstation_turn_autopilot,\
grandleon_playstation_campaign_autopilot,\
grandleon_playstation_campaign_demo_autopilot"
[ -n "${targets}" ] || targets="${default_targets}"
if [ "${scratch3d}" = "ON" ]; then
    targets="${targets},grandleon_playstation_scratch3d"
fi

# What each requested target is called once it is an executable, derived from
# the target list rather than written out again, so a run asked for one
# executable neither reports nor demands the nine it was not asked for. The two
# names differ by one prefix and the conformance executable is the exception,
# which is why this is a case and not a substitution. Derived once, here,
# because both the size report inside the container and the check after it are
# about the same list.
executables=""
for target in ${targets//,/ }; do
    case "${target}" in
        grandleon_playstation_conformance) name="grandleon_psx" ;;
        grandleon_playstation_*)
            name="grandleon_psx_${target#grandleon_playstation_}" ;;
        *) continue ;;
    esac
    executables="${executables} ${name}"
done

# The container sees the repository at /src and nothing else, so a project it
# is asked to build has to be inside the repository. Said here, with the path,
# rather than discovered as a file-not-found inside the container.
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
container_project="/src/${project#"${repository_root}"/}"

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

# The negative control over the one post-build check that would otherwise
# never have been seen to fail. Before the container, because a check that
# measures nothing should be found out in a second rather than after a build.
"${repository_root}/platform/playstation/scripts/check-heap-room.sh" --self-test

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
    --env "GRANDLEON_CONTAINER_PROJECT=${container_project}" \
    --env "GRANDLEON_CONTAINER_TARGETS=${targets//,/ }" \
    --env "GRANDLEON_CONTAINER_EXECUTABLES=${executables}" \
    --env "GRANDLEON_SCRATCH3D=${scratch3d}" \
    --env "GRANDLEON_SCRATCH3D_STYLE=${scratch3d_style}" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        # 1. The host content compiler, and the two packages it produces: the
        #    demo campaign the conformance executable replays to a golden hash,
        #    and the project the play, turn and campaign executables draw.
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}/host" \
            -DCMAKE_BUILD_TYPE=Release \
            -DGRANDLEON_BUILD_TESTS=OFF
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}/host" --parallel \
            --target grandleon_content_compile
        "${GRANDLEON_CONTAINER_BUILD_DIR}/host/grandleon_content_compile" \
            /src/games/demo/source/project.json \
            "${GRANDLEON_CONTAINER_BUILD_DIR}/demo.gpk"
        "${GRANDLEON_CONTAINER_BUILD_DIR}/host/grandleon_content_compile" \
            "${GRANDLEON_CONTAINER_PROJECT}" \
            "${GRANDLEON_CONTAINER_BUILD_DIR}/board.gpk"

        # 2. The cross build.
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}/target" \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/PlayStation.cmake \
            -DCMAKE_BUILD_TYPE= \
            -DGRANDLEON_PLAYSTATION=ON \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON \
            -DGRANDLEON_PLAYSTATION_SCRATCH3D="${GRANDLEON_SCRATCH3D}" \
            -DGRANDLEON_PLAYSTATION_SCRATCH3D_STYLE="${GRANDLEON_SCRATCH3D_STYLE}" \
            -DGRANDLEON_PLAYSTATION_PROJECT="${GRANDLEON_CONTAINER_PROJECT}" \
            -DPLAYSTATION_DEMO_PACKAGE="${GRANDLEON_CONTAINER_BUILD_DIR}/demo.gpk" \
            -DPLAYSTATION_BOARD_PACKAGE="${GRANDLEON_CONTAINER_BUILD_DIR}/board.gpk"
        # shellcheck disable=SC2086
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}/target" --parallel \
            --target ${GRANDLEON_CONTAINER_TARGETS}
        for elf in ${GRANDLEON_CONTAINER_EXECUTABLES}; do
            mipsel-linux-gnu-size \
                "${GRANDLEON_CONTAINER_BUILD_DIR}/target/platform/playstation/${elf}.elf"
        done
    '

# And require every one of them to be there afterwards.
for name in ${executables}; do
    executable="${build_dir}/target/platform/playstation/${name}.ps-exe"
    if [ ! -f "${executable}" ]; then
        echo "error: expected ${executable} to exist after the build." >&2
        exit 1
    fi
    echo "PS-EXE: ${executable} ($(wc -c < "${executable}") bytes)"
done
