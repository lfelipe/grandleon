#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

# The lower-level PlayStation builder receives its pinned revisions from the
# repository CMake target. Make this documented convenience script useful when
# invoked directly as well by entering through that target when the pins are
# not already present. The target calls back with them set, so this is one
# bounded hand-off rather than recursion.
if [ -z "${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}" ] || \
   [ -z "${GRANDLEON_NUGGET_REVISION:-}" ]; then
    cmake -S "${repository_root}" -B "${repository_root}/build" \
        -DGRANDLEON_BUILD_TESTS=ON
    exec cmake --build "${repository_root}/build" \
        --target grandleon_playstation_sevenfold_3d
fi

export GRANDLEON_PLAYSTATION_BUILD_DIR="${GRANDLEON_PSX_SEVENFOLD_3D_BUILD_DIR:-${repository_root}/build-playstation-sevenfold-3d}"
exec "${repository_root}/platform/playstation/scripts/build-playstation.sh" \
    --project "${repository_root}/games/sevenfold/source/project.json" \
    --targets grandleon_playstation_campaign_3d,grandleon_playstation_sevenfold_3d,grandleon_playstation_sevenfold_3d_check,grandleon_playstation_sevenfold_texture_experiment \
    --stage-picker \
    "$@"
