#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 production.z64 empty-evidence-directory" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
rom=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
mkdir -p "$out/saves"
export GRANDLEON_N64_BUILD_DIR="$out"
export GRANDLEON_N64_ARES_SAVES="$out/saves"
export GRANDLEON_N64_ARES_ALLOW_TIMEOUT=1
sequence='wait:8,press:Up,wait:1,press:Return,wait:1,press:Left,wait:0.6,press:Down,wait:0.6,press:Right,wait:1,shot:audio-settings.png'
GRANDLEON_N64_ARES_SECONDS=22 GRANDLEON_N64_ARES_LOG=write.log \
    GRANDLEON_N64_ARES_LINGER=35 \
    GRANDLEON_N64_ARES_REQUIRE='PYRITE_AUDIO_SETTINGS music=40 effects=80' \
    GRANDLEON_N64_ARES_INPUT_SEQUENCE="$sequence" \
    "$repo/platform/nintendo64/ares/run-ares.sh" "$rom"
GRANDLEON_N64_ARES_SECONDS=10 GRANDLEON_N64_ARES_LOG=read.log \
    GRANDLEON_N64_ARES_REQUIRE='PYRITE_AUDIO_LOADED music=40 effects=80' \
    "$repo/platform/nintendo64/ares/run-ares.sh" "$rom"
echo "N64 title audio controls persist independent volumes across restart."
