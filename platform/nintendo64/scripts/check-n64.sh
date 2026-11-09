#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Build and boot two distinct authored games through the normal Nintendo 64
# export path. Both must identify the Pyrite renderer and pass on an exact 4 MB
# accurate-ares machine; a missing renderer or toolchain is an export failure.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
root_build="${GRANDLEON_N64_BUILD_DIR:-${repository_root}/build-n64-check}"
ares="${repository_root}/platform/nintendo64/ares/run-ares.sh"

check_project() {
    local name=$1 project=$2 build_dir="${root_build}/${name}"
    GRANDLEON_N64_BUILD_DIR="${build_dir}" \
        "${repository_root}/platform/nintendo64/scripts/build-n64.sh" \
        --project "${project}"

    local rom="${build_dir}/platform/nintendo64/grandleon_n64.z64"
    grep -Fx 'renderer=pyrite-tiny3d' \
        "${build_dir}/platform/nintendo64/renderer-identity.txt" >/dev/null
    grep -Fx 'fallback=none' \
        "${build_dir}/platform/nintendo64/renderer-identity.txt" >/dev/null
    GRANDLEON_N64_BUILD_DIR="${build_dir}" \
        GRANDLEON_N64_ARES_SECONDS=300 \
        GRANDLEON_N64_ARES_LOG="${name}.log" \
        GRANDLEON_N64_ARES_SHOT="${name}.png" \
        GRANDLEON_N64_ARES_TRAIL="${name}-trail" \
        GRANDLEON_N64_ARES_REQUIRE='^RESULT PASS PYRITE_GAMEPLAY' \
        "${ares}" "${rom}"
}

"${repository_root}/scripts/assert-harness-verdict.sh" --self-test
check_project demo "${repository_root}/games/demo/source/project.json"
check_project steel "${repository_root}/games/steel_and_cedar/source/project.json"
printf '%s\n' 'Nintendo 64 Pyrite export gate passed for demo and Steel and Cedar.'
