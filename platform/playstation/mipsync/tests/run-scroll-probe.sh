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
command -v "$docker" >/dev/null 2>&1 || { echo "container runtime is required: $docker" >&2; exit 1; }
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation emulator image is missing: $image" >&2
    exit 1
}
installed_revision=$("$docker" run --rm "$image" cat /usr/local/PCSX_REDUX_REVISION)
test "$installed_revision" = "$revision" || {
    echo "emulator revision mismatch: expected $revision, got $installed_revision" >&2
    exit 1
}

nm=$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm
test -x "$nm" || { echo "missing nm tool: $nm" >&2; exit 1; }
update_address=$(
    "$nm" -n "$elf" |
        awk '$3 == "grandleon_game_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
) || { echo "grandleon_game_update is missing from $elf" >&2; exit 1; }

log=$output/emulator.log
set +e
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
            -dofile /probe/scroll_probe.lua \
            -iso /disc/grandleon-mipsync.cue \
    >"$log" 2>&1
status=$?
set -e
test "$status" -eq 0 || { tail -n 80 "$log" >&2; exit "$status"; }

require() {
    pattern=$1
    description=$2
    grep -Eq "$pattern" "$log" || {
        echo "scroll probe did not report $description" >&2
        tail -n 80 "$log" >&2
        exit 1
    }
}
require 'GRANDLEON_PS1_RENDERER=MIPSYNC' 'the Mipsync renderer identity'
require 'MIPSYNC_SIM_READY width=14 height=9 units=11 .*hash=72216135b6c8e9d3' 'the real Cedar Deepwood simulation'
require 'MIPSYNC_GRANDLEON_METRICS frames=600 rendered_frames=300 scheduled_frames=300 deferred_frames=0 missed_vblanks=0 max_elapsed_vblanks=[12]$' 'the steady 30 fps scrolling frame result'
require 'MIPSYNC_MISS_RANGE first=0 last=0$' 'an empty missed-frame range'
require 'MIPSYNC_CAMERA_PROOF min_x=-6 max_x=6 min_z=11 max_z=17 current_x=6 current_z=11 chunks=1' 'the viewport clamp and bounded terrain page'
require 'MIPSYNC_SCROLL_CAPTURE 320x240 bytes=153600' 'the final framebuffer capture'

metrics=$(grep 'MIPSYNC_GRANDLEON_METRICS frames=600 ' "$log" | tail -n 1)
printf 'Mipsync Cedar Deepwood scrolling passed: %s\n' "$metrics"
printf 'Log: %s\nCapture: %s\n' "$log" "$output/mipsync-scroll-final.bin"
