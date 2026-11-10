#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 renderer-build-directory output-directory" >&2
    exit 2
}

test "$#" -eq 2 || usage
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
elf=$build/grandleon_mipsync.elf
cue=$build/grandleon-mipsync.cue
for artifact in "$elf" "$cue"; do
    test -f "$artifact" || { echo "missing renderer artifact: $artifact" >&2; exit 1; }
done
if test -e "$output"; then
    test -d "$output" || { echo "output exists and is not a directory: $output" >&2; exit 1; }
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo "output directory must be empty: $output" >&2
        exit 1
    }
else
    mkdir -p "$output"
fi
output=$(CDPATH= cd -- "$output" && pwd)

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/verify-probe-disc.py" "$build" >"$output/disc-verification.txt"
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation emulator image is missing: $image" >&2
    exit 1
}

nm=$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm
update_address=$(
    "$nm" -n "$elf" |
        awk '$3 == "grandleon_game_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
) || { echo "grandleon_game_update is missing from $elf" >&2; exit 1; }

log=$output/emulator.log
timeout --kill-after=10 180 \
    "$docker" run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env "GRANDLEON_UPDATE_ADDRESS=$update_address" \
        --env "GRANDLEON_CAPTURE_MOTION=${GRANDLEON_CAPTURE_MOTION:-0}" \
        --volume "$build:/disc:ro" \
        --volume "$script_dir:/probe:ro" \
        --volume "$output:/out" \
        --workdir /tmp \
        "$image" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter -debugger \
            -dofile /probe/visibility_probe.lua \
            -iso /disc/grandleon-mipsync.cue >"$log" 2>&1

require() {
    pattern=$1
    description=$2
    grep -Eq "$pattern" "$log" || {
        echo "visibility probe did not report $description" >&2
        tail -n 120 "$log" >&2
        exit 1
    }
}
require 'GRANDLEON_PS1_RENDERER=MIPSYNC' 'the Mipsync renderer identity'
require 'MIPSYNC_SIM_READY width=20 height=14 units=6 .*hash=8c904e6d40b072d9' 'the real one-player Twin Gate simulation'
require 'MIPSYNC_MOVE_SETTLED unit=4c0800efeb4ef2bd steps=3' 'Rowan reaching the castle approach'
require 'MIPSYNC_CAMERA_RETURN player=1 x=5 y=6' 'camera focus returned to the player after the opposing turn'
require 'MIPSYNC_VISIBILITY_REVEAL cells=18' 'the authored castle reveal'
require 'MIPSYNC_MOVE_SETTLED unit=4c0800efeb4ef2bd steps=4' 'Rowan crossing the doorway'
require 'MIPSYNC_VISIBILITY_CAPTURE name=before 320x240 bytes=153600' 'the covered castle frame'
require 'MIPSYNC_VISIBILITY_CAPTURE name=after 320x240 bytes=153600' 'the revealed castle frame'

approach_line=$(grep -n 'MIPSYNC_MOVE_SETTLED unit=4c0800efeb4ef2bd steps=3' "$log" | head -n 1 | cut -d: -f1)
covered_line=$(grep -n 'MIPSYNC_VISIBILITY_CAPTURE name=before ' "$log" | head -n 1 | cut -d: -f1)
reveal_line=$(grep -n 'MIPSYNC_VISIBILITY_REVEAL cells=18' "$log" | head -n 1 | cut -d: -f1)
cross_line=$(grep -n 'MIPSYNC_MOVE_SETTLED unit=4c0800efeb4ef2bd steps=4' "$log" | head -n 1 | cut -d: -f1)
test "$approach_line" -lt "$covered_line" && test "$covered_line" -lt "$reveal_line" && test "$reveal_line" -lt "$cross_line" || {
    echo "castle must stay covered during cursor inspection and reveal during Rowan's doorway crossing" >&2
    exit 1
}

printf 'Mipsync Twin Gate visibility passed: approach=%s reveal=%s crossing=%s\n' \
    "$approach_line" "$reveal_line" "$cross_line"
printf 'Log: %s\nCovered capture: %s\nRevealed capture: %s\n' \
    "$log" "$output/mipsync-visibility-before.bin" \
    "$output/mipsync-visibility-after.bin"
