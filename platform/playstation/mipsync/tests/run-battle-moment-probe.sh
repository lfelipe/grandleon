#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 renderer-build-directory empty-output-directory" >&2
    exit 2
}
test -n "${PSN00BSDK_ROOT:-}" || {
    echo "PSN00BSDK_ROOT is required" >&2
    exit 1
}
source_build=$(CDPATH= cd -- "$1" && pwd)
build=$source_build/build
cue=$build/grandleon-mipsync.cue
output=$2
test -f "$cue" || { echo "missing fixture disc: $cue" >&2; exit 1; }
test -f "$build/grandleon_mipsync.elf" || {
    echo "missing fixture ELF: $build/grandleon_mipsync.elf" >&2
    exit 1
}
if test -e "$output"; then
    test -d "$output" || { echo "output is not a directory: $output" >&2; exit 1; }
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo "output directory must be empty: $output" >&2
        exit 1
    }
else
    mkdir -p "$output"
fi
output=$(CDPATH= cd -- "$output" && pwd)

update=$(
    "$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm" -n \
        "$build/grandleon_mipsync.elf" |
    awk '$3 == "grandleon_runtime_update" {
        print "0x" substr($1, length($1) - 7); found = 1
    } END { if (!found) exit 1 }'
)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned PlayStation emulator image is missing: $image" >&2
    exit 1
}

timeout --kill-after=10 180 \
    "$docker" run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env "GRANDLEON_UPDATE_ADDRESS=$update" \
        --volume "$build:/disc:ro" \
        --volume "$script_dir:/probe:ro" \
        --volume "$output:/out" \
        --workdir /tmp \
        "$image" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter -debugger \
            -memcard1 /out/battle-moment.mcd \
            -dofile /probe/battle_moment_probe.lua \
            -iso /disc/grandleon-mipsync.cue \
        >"$output/probe.log" 2>&1

require() {
    pattern=$1
    label=$2
    grep -Eq "$pattern" "$output/probe.log" || {
        echo "battle-moment probe did not report $label" >&2
        tail -n 100 "$output/probe.log" >&2
        exit 1
    }
}
require 'MIPSYNC_BATTLE_MOMENT dialogue=2573cab1a639d692 lines=1' \
    'the authored opening line'
require 'MIPSYNC_ACTION_CHOICE kind=talk row=1' \
    'the Talk action row'
require 'MIPSYNC_BATTLE_MOMENT dialogue=64dfa433e3d3a8ce lines=1' \
    'the characterTalked line'
require 'MIPSYNC_BATTLE_MOMENT_CAPTURE 320x240 bytes=153600' \
    'the triggered conversation capture'
ffmpeg -hide_banner -loglevel error -y \
    -f rawvideo -pixel_format bgr555le -video_size 320x240 \
    -i "$output/battle-moment.bin" -frames:v 1 \
    "$output/playstation-battle-moment.png"
printf 'Mipsync Talk and triggered battle moment passed: %s\n' \
    "$output/playstation-battle-moment.png"
