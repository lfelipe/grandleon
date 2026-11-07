#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

if [ -z "${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}" ] || \
   [ -z "${GRANDLEON_NUGGET_REVISION:-}" ]; then
    cmake -S "${repository_root}" -B "${repository_root}/build" \
        -DGRANDLEON_BUILD_TESTS=ON
    exec cmake --build "${repository_root}/build" \
        --target grandleon_playstation_sevenfold_renderer
fi

export GRANDLEON_PLAYSTATION_BUILD_DIR="${GRANDLEON_PSX_SEVENFOLD_RENDERER_BUILD_DIR:-${repository_root}/build-playstation-sevenfold-renderer}"
exec "${repository_root}/platform/playstation/scripts/build-playstation.sh" \
    --project "${repository_root}/games/sevenfold/source/project.json" \
    --targets grandleon_playstation_sevenfold_renderer \
    --stage-picker \
    "$@"
