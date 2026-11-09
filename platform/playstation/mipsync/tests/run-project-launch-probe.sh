#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 project-build-directory empty-output-directory" >&2
    exit 2
}
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
for artifact in "$build/game.cue" "$build/game.bin" \
        "$build/stages/01/build/grandleon_mipsync.elf" \
        "$build/stages/01/build-manifest.json" "$build/campaign-plan.tsv"; do
    test -f "$artifact" || { echo "missing project artifact: $artifact" >&2; exit 1; }
done
mode=$(awk -F '	' '$1 == "campaign" { print $3 }' "$build/campaign-plan.tsv")
campaign=$(awk -F '	' '$1 == "campaign" { print $2 }' "$build/campaign-plan.tsv")
case "$mode:$campaign" in
    one:demo_campaign)
        expected='MIPSYNC_SIM_READY width=6 height=4 units=2 .* players=1'
        label='the direct-entry one-player battle after only New Game and Play'
        ;;
    choice:steel_and_cedar)
        expected='MIPSYNC_SIM_READY width=8 height=6 units=8 .* players=2'
        label='the authored two-player battle reached through the player chooser'
        ;;
    *)
        echo "project launch probe supports the demo or Steel and Cedar campaign" >&2
        exit 1
        ;;
esac
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

nm=$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm
update=$(
    "$nm" -n "$build/stages/01/build/grandleon_mipsync.elf" |
        awk '$3 == "grandleon_runtime_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation emulator image is missing: $image" >&2
    exit 1
}

log=$output/project.log
timeout --kill-after=10 90 \
    "$docker" run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env "GRANDLEON_UPDATE_ADDRESS=$update" \
        --env "GRANDLEON_PROJECT_MODE=$mode" \
        --volume "$build:/disc:ro" \
        --volume "$script_dir:/probe:ro" \
        --volume "$output:/out" \
        --workdir /tmp \
        "$image" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter -debugger \
            -memcard1 /out/project.mcd \
            -dofile /probe/project_launch_probe.lua \
            -iso /disc/game.cue >"$log" 2>&1

require() {
    pattern=$1
    label=$2
    grep -Eq "$pattern" "$log" || {
        echo "project launch probe did not report $label" >&2
        tail -n 100 "$log" >&2
        exit 1
    }
}
require "MIPSYNC_PROJECT_PROBE_READY mode=$mode" 'the update breakpoint'
require 'MIPSYNC_CAMPAIGN_READY card=1 continue=0' 'a fresh campaign card'
require "$expected" "$label"
if test "$mode" = one && grep -Eq 'MIPSYNC_SIM_READY .* players=2' "$log"; then
    echo "one-player project started a two-player simulation" >&2
    exit 1
fi
printf 'Mipsync %s project launch passed: update=%s log=%s\n' \
    "$mode" "$update" "$log"
