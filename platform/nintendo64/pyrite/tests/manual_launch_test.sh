#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Exercise a production ROM through ordinary controller input, with no driver.
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 production.z64 empty-evidence-directory" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
rom=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || {
    echo "evidence directory must be empty" >&2; exit 2;
}
runner="$repo/platform/nintendo64/ares/run-ares.sh"
export GRANDLEON_N64_BUILD_DIR="$out"
export GRANDLEON_N64_ARES_ALLOW_TIMEOUT=1
export GRANDLEON_N64_ARES_REQUIRE='review_driver=0'
GRANDLEON_N64_ARES_SECONDS=18 GRANDLEON_N64_ARES_LOG=idle.log \
    GRANDLEON_N64_ARES_SHOT=idle.png GRANDLEON_N64_ARES_TRAIL=idle-trail \
    "$runner" "$rom"
if rg -q 'CHECKPOINT PYRITE_TITLE|GRANDLEON_PYRITE_CAMPAIGN_BEGIN' "$out/ares/idle.log"; then
    echo "production ROM advanced without controller input" >&2; exit 1
fi
# Allow the real save and scene load to complete before advancing story lines.
sequence='wait:5,press:Return,wait:2,press:Return,wait:2,press:Return,wait:6,press:Return,wait:2,press:Return,wait:2,press:Return,wait:6,press:Return,wait:2,press:Return,wait:2,press:Return,wait:3,shot:board.png,wait:5,shot:board-idle.png'
GRANDLEON_N64_ARES_SECONDS=44 GRANDLEON_N64_ARES_LOG=manual.log \
    GRANDLEON_N64_ARES_SHOT=manual.png GRANDLEON_N64_ARES_TRAIL=manual-trail \
    GRANDLEON_N64_ARES_INPUT_SEQUENCE="$sequence" "$runner" "$rom"
rg -q 'CHECKPOINT PYRITE_CAMPAIGN_STAGE index=1' "$out/ares/manual.log"
if rg -q 'CHECKPOINT PYRITE_(SELECTED|MOVED|ENEMY)|GRANDLEON_PYRITE_COMBAT ' "$out/ares/manual.log"; then
    echo "production battle used automated actions" >&2; exit 1
fi
echo "Production ROM waits at title and battle; inspect board.png and board-idle.png."
