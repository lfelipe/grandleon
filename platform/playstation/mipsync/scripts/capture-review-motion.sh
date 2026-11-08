#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 campaign-build-directory empty-output-directory" >&2
    exit 2
}
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
test -f "$build/grandleon-mipsync-campaign.cue" || {
    echo "missing campaign disc: $build/grandleon-mipsync-campaign.cue" >&2
    exit 1
}
test -f "$build/bell-build/grandleon_mipsync.elf" || {
    echo "missing Bell Orchard ELF" >&2
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
        "$build/bell-build/grandleon_mipsync.elf" |
    awk '$3 == "grandleon_runtime_update" {
        print "0x" substr($1, length($1) - 7); found = 1
    } END { if (!found) exit 1 }'
)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe_dir=$(CDPATH= cd -- "$script_dir/../tests" && pwd)
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
        --volume "$probe_dir:/probe:ro" \
        --volume "$output:/out" \
        --workdir /tmp \
        "$image" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter -debugger \
            -memcard1 /out/review.mcd \
            -dofile /probe/review_motion.lua \
            -iso /disc/grandleon-mipsync-campaign.cue \
        >"$output/capture.log" 2>&1

grep -Eq 'MIPSYNC_REVIEW_MOTION frames=1[0-9][0-9]' "$output/capture.log" || {
    echo "review capture did not finish" >&2
    tail -n 80 "$output/capture.log" >&2
    exit 1
}
ffmpeg -hide_banner -loglevel error -y \
    -f image2 -vcodec rawvideo -pixel_format bgr555le \
    -video_size 320x240 -framerate 30 -start_number 0 \
    -i "$output/frame-%04d.bin" \
    -vf 'scale=640:480:flags=neighbor' -an \
    -c:v libx264 -crf 20 -pix_fmt yuv420p -movflags +faststart \
    "$output/playstation-movement.mp4"
printf 'PlayStation movement capture: %s\n' "$output/playstation-movement.mp4"
