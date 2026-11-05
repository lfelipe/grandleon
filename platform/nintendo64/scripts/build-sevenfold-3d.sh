#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
export GRANDLEON_N64_PROTOTYPE_BUILD_DIR="${GRANDLEON_N64_SEVENFOLD_3D_BUILD_DIR:-${repository_root}/build-n64-sevenfold-3d}"
exec "${repository_root}/platform/nintendo64/scripts/build-character-prototype.sh" \
    --sevenfold "$@"
