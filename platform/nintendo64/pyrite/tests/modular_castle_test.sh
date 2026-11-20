#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Exercise the normal 32x24 castle ROM with real controller input.
set -euo pipefail
[[ $# == 3 ]] || { echo "usage: $0 normal.z64 empty-output players" >&2; exit 2; }
rom=$(realpath "$1")
mkdir -p "$2"
output=$(realpath "$2")
players=$3
[[ -z $(find "$output" -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo "output must be empty" >&2; exit 2; }
repo=$(cd "$(dirname "$0")/../../../.." && pwd)
case "$players" in
    1) choose='advance:Return/PYRITE_SAVE'; steps=2; second=0 ;;
    2) choose='advance:Return/PYRITE_TITLE,wait:2,press:Down/0.40,wait:0.8,press:Return/0.40,await:PYRITE_PLAYERS,wait:1,advance:Return/PYRITE_SAVE'; steps=4; second=1 ;;
    *) echo "players must be 1 or 2" >&2; exit 2 ;;
esac
sequence="wait:2,$choose,await:PYRITE_BATTLE_MOMENT,wait:1,advance:Return/PYRITE_BATTLE_MOMENT_DONE,wait:3,view-shot:castle-closed.png,press:Return/0.25,wait:0.4"
for ((i=0;i<steps;i++)); do sequence+=',press:Right/0.25,wait:0.4'; done
sequence+=',press:Return/0.25,await:PYRITE_ROOM_REVEAL,wait:3,press:space/0.25,wait:1,view-shot:castle-entry.png'
# Rowan carries a pack. Skip Pack and confirm Wait before scrolling the board.
sequence+=',press:Down/0.25,wait:0.4,press:Return/0.25,wait:0.4'
# The current digital pad advances on presses; holding it is not analog repeat.
for ((i=0;i<32;i++)); do sequence+=',press:Right/0.25,wait:0.15'; done
for ((i=0;i<24;i++)); do sequence+=',press:Down/0.25,wait:0.15'; done
sequence+=',wait:2,view-shot:castle-far-corner.png'
GRANDLEON_N64_BUILD_DIR="$output" GRANDLEON_N64_ARES_LOG=castle.log \
GRANDLEON_N64_ARES_REQUIRE='^CHECKPOINT PYRITE_ROOM_REVEAL new=9 explored=9 total=170' \
GRANDLEON_N64_ARES_ALLOW_TIMEOUT=1 GRANDLEON_N64_ARES_SECONDS=100 \
GRANDLEON_N64_ARES_SECOND_CONTROLLER="$second" GRANDLEON_N64_ARES_INPUT_SEQUENCE="$sequence" \
    "$repo/platform/nintendo64/ares/run-ares.sh" "$rom" > "$output/runner.log" 2>&1
log="$output/ares/castle.log"
rg -q 'review_driver=0' "$log"
rg -q "size=32x24 players=$players" "$log"
rg -q 'GRANDLEON_N64_BOARD_MEMORY cells=768 .*rdram=4194304' "$log"
rg -q 'CHECKPOINT PYRITE_CAMERA_HORIZONTAL' "$log"
rg -q 'CHECKPOINT PYRITE_CAMERA_VERTICAL' "$log"
[[ $(rg -c '^CHECKPOINT PYRITE_ROOM_REVEAL ' "$log") == 1 ]]
for frame in castle-closed castle-entry castle-far-corner; do [[ -s "$output/ares/$frame.png" ]]; done
echo "Normal N64 ${players}P castle entry, local reveal and both scrolling axes pass in base 4 MiB."
