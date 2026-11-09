#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Build the one Nintendo 64 release artifact. The normal author/export path
# and the repository gate deliberately call the same generic Pyrite pipeline;
# there is no renderer selector or legacy fallback here.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
build_dir="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64}"
project="${GRANDLEON_N64_PROJECT:-${repository_root}/games/steel_and_cedar/source/project.json}"

usage() {
    echo "usage: $(basename "$0") [--project PROJECT.JSON]" >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --project) [[ $# -ge 2 ]] || usage; project=$2; shift 2 ;;
        *) usage ;;
    esac
done

[[ -f "${project}" ]] || {
    echo "Nintendo 64 export refused: project is not readable: ${project}" >&2
    exit 1
}
project="$(cd "$(dirname "${project}")" && pwd)/$(basename "${project}")"

mkdir -p "${build_dir}"
build_dir="$(cd "${build_dir}" && pwd)"
work="${build_dir}/pyrite"
canonical_dir="${build_dir}/platform/nintendo64"
canonical_rom="${canonical_dir}/grandleon_n64.z64"

mkdir -p "${canonical_dir}"
"${repository_root}/platform/nintendo64/pyrite/scripts/build.sh" \
    "${repository_root}" "${work}" "${project}"

source_rom="${work}/project/grandleon_pyrite_game.z64"
[[ -s "${source_rom}" ]] || {
    echo "Nintendo 64 export refused: Pyrite produced no ROM" >&2
    exit 1
}
identity="$(dd if="${source_rom}" bs=1 skip=32 count=20 status=none | tr -d '\000')"
[[ "${identity}" == "Grandleon Pyrite Gam" ]] || {
    echo "Nintendo 64 export refused: unexpected renderer identity: ${identity}" >&2
    exit 1
}
"${repository_root}/tools/upstream_patches/cutover-audit.sh" \
    "${repository_root}/platform/nintendo64/pyrite/cutover-contract.tsv" \
    "${repository_root}" "${source_rom}"

install -m 0644 "${source_rom}" "${canonical_rom}.new"
mv -f "${canonical_rom}.new" "${canonical_rom}"
sha256sum "${canonical_rom}" > "${canonical_rom}.sha256"
printf '%s\n' 'renderer=pyrite-tiny3d' 'fallback=none' \
    "project=${project}" "rom=${canonical_rom}" \
    > "${canonical_dir}/renderer-identity.txt"
printf 'ROM: %s (%s bytes)\n' "${canonical_rom}" "$(wc -c < "${canonical_rom}")"
