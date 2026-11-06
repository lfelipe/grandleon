#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Builds the focused N64 3D diagnostic or the Sevenfold 3D build. This remains
# separate from build-n64.sh because it uses Tiny3D and a preview libdragon
# pin. Sevenfold includes both its authored campaign renderer and its complete
# roster viewer; the focused commander/rogue build remains diagnostic only.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
base_image="${GRANDLEON_LIBDRAGON_BASE_IMAGE:-ghcr.io/dragonminded/libdragon:trunk}"
base_digest="${GRANDLEON_LIBDRAGON_BASE_DIGEST:-sha256:cf979424d0c2d3e281fda0820e8244def9b2debea2ab140c6513e41c8b66cdd0}"
libdragon_commit="${GRANDLEON_N64_PROTOTYPE_LIBDRAGON_COMMIT:-c79a52b42ac790e06e797aede43914dd8754cd5f}"
tiny3d_commit="${GRANDLEON_N64_PROTOTYPE_TINY3D_COMMIT:-517156d9464aaab95bf9531336d9a57624405ca7}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_N64_PROTOTYPE_BUILD_DIR:-${repository_root}/build-n64-character-prototype}"
image="grandleon/n64-character-prototype-toolchain:${libdragon_commit:0:12}-t3d${tiny3d_commit:0:12}"
bundle_dir=""
rebuild_image=0
sevenfold=0

usage() {
    echo "usage: $(basename "$0") [--rebuild-image] [--bundle DIR] [--sevenfold]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --rebuild-image) rebuild_image=1; shift ;;
        --bundle) [ "$#" -ge 2 ] || usage; bundle_dir="$2"; shift 2 ;;
        --sevenfold) sevenfold=1; shift ;;
        *) usage ;;
    esac
done

if [ "${sevenfold}" -eq 1 ] && [ -n "${bundle_dir}" ]; then
    echo "error: --bundle currently describes the fixed two-role comparison; use --sevenfold without it." >&2
    exit 2
fi

prototype_option=ON
sevenfold_option=OFF
target=grandleon_n64_character_prototype
rom_name=grandleon-n64-3d-character-prototype.z64
label="Experimental ROM"
if [ "${sevenfold}" -eq 1 ]; then
    prototype_option=OFF
    sevenfold_option=ON
    target=grandleon_n64_sevenfold_3d
    rom_name=grandleon-n64-sevenfold-3d.z64
    label="Sevenfold 3D roster ROM"
fi

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${base_image}" >/dev/null 2>&1; then
        echo "Pulling ${base_image}…" >&2
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
    echo "Building ${image} from Containerfile.character-prototype…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/nintendo64/Containerfile.character-prototype" \
        --build-arg "LIBDRAGON_COMMIT=${libdragon_commit}" \
        --build-arg "TINY3D_COMMIT=${tiny3d_commit}" \
        --tag "${image}" \
        "${repository_root}/platform/nintendo64"
fi

installed_libdragon="$("${docker}" run --rm "${image}" cat /n64_toolchain/LIBDRAGON_COMMIT)"
installed_tiny3d="$("${docker}" run --rm "${image}" cat /n64_toolchain/TINY3D_COMMIT)"
installed_libdragon="${installed_libdragon//[$'\r\n']/}"
installed_tiny3d="${installed_tiny3d//[$'\r\n']/}"
if [ "${installed_libdragon}" != "${libdragon_commit}" ] || [ "${installed_tiny3d}" != "${tiny3d_commit}" ]; then
    echo "error: ${image} does not contain the requested prototype pins." >&2
    echo "  libdragon expected ${libdragon_commit}, found ${installed_libdragon}" >&2
    echo "  Tiny3D expected ${tiny3d_commit}, found ${installed_tiny3d}" >&2
    exit 1
fi

mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"
case "${build_dir}/" in
    "${repository_root}/"*) ;;
    *)
        echo "error: GRANDLEON_N64_PROTOTYPE_BUILD_DIR must be inside the repository." >&2
        exit 1
        ;;
esac
container_build_dir="/src/${build_dir#"${repository_root}"/}"

exec 9>"${build_dir}.lock"
flock 9
"${docker}" run --rm \
    --user "$(id -u):$(id -g)" \
    --env "GRANDLEON_CONTAINER_BUILD_DIR=${container_build_dir}" \
    --env "GRANDLEON_CONTAINER_PROTOTYPE_OPTION=${prototype_option}" \
    --env "GRANDLEON_CONTAINER_SEVENFOLD_OPTION=${sevenfold_option}" \
    --env "GRANDLEON_CONTAINER_TARGET=${target}" \
    --volume "${repository_root}:/src" \
    --workdir /src \
    "${image}" \
    /bin/bash -euo pipefail -c '
        project_argument=(-UGRANDLEON_N64_PROJECT)
        if [ "${GRANDLEON_CONTAINER_SEVENFOLD_OPTION}" = ON ]; then
            project_argument=(-DGRANDLEON_N64_PROJECT=/src/games/sevenfold/source/project.json)
        fi
        cmake -S /src -B "${GRANDLEON_CONTAINER_BUILD_DIR}" \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/toolchains/Nintendo64.cmake \
            -DCMAKE_BUILD_TYPE= \
            -DGRANDLEON_N64=ON \
            -DGRANDLEON_N64_CHARACTER_PROTOTYPE=${GRANDLEON_CONTAINER_PROTOTYPE_OPTION} \
            -DGRANDLEON_N64_SEVENFOLD_3D=${GRANDLEON_CONTAINER_SEVENFOLD_OPTION} \
            -DGRANDLEON_STAGE_PICKER=${GRANDLEON_CONTAINER_SEVENFOLD_OPTION} \
            -DGRANDLEON_BUILD_TESTS=OFF \
            -DGRANDLEON_WERROR=ON \
            "${project_argument[@]}"
        cmake --build "${GRANDLEON_CONTAINER_BUILD_DIR}" --parallel \
            --target "${GRANDLEON_CONTAINER_TARGET}"
    '

rom="${build_dir}/platform/nintendo64/${rom_name}"
if [ ! -f "${rom}" ]; then
    echo "error: expected prototype ROM was not produced: ${rom}" >&2
    exit 1
fi
rom_sha256="$(sha256sum "${rom}" | cut -d ' ' -f 1)"
echo "${label}: ${rom} ($(wc -c < "${rom}") bytes)"
echo "SHA-256: ${rom_sha256}"

if [ -n "${bundle_dir}" ]; then
    case "${bundle_dir}" in
        /*) ;;
        *) bundle_dir="$(pwd -P)/${bundle_dir}" ;;
    esac
    if [ -e "${bundle_dir}" ] && [ -n "$(find "${bundle_dir}" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
        echo "error: --bundle directory is not empty: ${bundle_dir}" >&2
        exit 1
    fi
    mkdir -p "${bundle_dir}/licenses" "${bundle_dir}/generated" \
        "${bundle_dir}/evidence"
    cp "${rom}" "${bundle_dir}/grandleon-n64-3d-character-prototype.z64"
    cp "${repository_root}/LICENSE" "${bundle_dir}/LICENSE.txt"
    cp "${repository_root}/NOTICE" "${bundle_dir}/NOTICE.txt"
    cp "${repository_root}/third_party/tiny3d/${tiny3d_commit}/LICENSE" \
        "${bundle_dir}/licenses/Tiny3D-LICENSE.txt"
    cp "${repository_root}/docs/CONSOLE_3D_EXPERIMENT.md" \
        "${bundle_dir}/README.md"
    cp "${repository_root}/tools/console_3d/README.md" \
        "${bundle_dir}/METHOD.md"
    cp "${repository_root}/tools/console_3d/generated/character_proxies_v2.json" \
        "${bundle_dir}/generated/character_proxies_v2.json"
    cp -a "${repository_root}/tools/console_3d/evidence/v2" \
        "${bundle_dir}/evidence/v2"
    sed -i \
        -e 's#../tools/console_3d/README.md#METHOD.md#g' \
        -e 's#../tools/console_3d/evidence/v2/#evidence/v2/#g' \
        "${bundle_dir}/README.md"
    cat > "${bundle_dir}/BUILD-INFO.txt" <<EOF
artifact=grandleon-n64-3d-character-prototype.z64
artifactSha256=${rom_sha256}
libdragon=${libdragon_commit}
tiny3d=${tiny3d_commit}
ares=0aafd85789215e84e1e43415c07d4c88461b7899
scope=fixed commander-and-rogue comparison; not a campaign renderer
EOF
    echo "Prototype review bundle: ${bundle_dir}"
fi
