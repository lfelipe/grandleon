#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 nameable-token-fixture-build empty-output-directory" >&2
    exit 2
}
# The fixture marks Rowan as player-nameable and gives the opening story the
# portable text `Hold, {{member:rowan}}.`. The probe therefore checks entry,
# persistence, Continue behavior, and a visible substitution in one run.
test -n "${PSN00BSDK_ROOT:-}" || {
    echo "PSN00BSDK_ROOT is required" >&2
    exit 1
}
source_build=$(CDPATH= cd -- "$1" && pwd)
build=$source_build/bell-build
cue=$source_build/grandleon-mipsync-campaign.cue
output=$2
test -f "$cue" || { echo "missing campaign disc: $cue" >&2; exit 1; }
test -f "$build/grandleon_mipsync.elf" || {
    echo "missing campaign ELF: $build/grandleon_mipsync.elf" >&2
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

run_mode() {
    mode=$1
    timeout --kill-after=10 180 \
        "$docker" run --rm --user "$(id -u):$(id -g)" \
            --env HOME=/tmp \
            --env "GRANDLEON_UPDATE_ADDRESS=$update" \
            --env "GRANDLEON_NAME_MODE=$mode" \
            --volume "$source_build:/disc:ro" \
            --volume "$script_dir:/probe:ro" \
            --volume "$output:/out" \
            --workdir /tmp \
            "$image" \
            stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
                -bios /usr/local/share/pcsx-redux/openbios.bin \
                -testmode -interpreter -debugger \
                -memcard1 /out/name.mcd \
                -dofile /probe/name_probe.lua \
                -iso "/disc/$(basename -- "$cue")" \
            >"$output/$mode.log" 2>&1
}

run_mode write
grep -Eq 'MIPSYNC_NAME_PROMPT member=.* limit=18' "$output/write.log" || {
    echo "name probe did not present the controller keyboard" >&2
    tail -n 80 "$output/write.log" >&2
    exit 1
}
grep -Eq 'MIPSYNC_NAME_SAVED member=.* default=0 saved=1' "$output/write.log" || {
    echo "name probe did not save the chosen name" >&2
    tail -n 80 "$output/write.log" >&2
    exit 1
}
grep -Eq 'MIPSYNC_MEMBER_TEXT node=.* value=Hold, A\.' "$output/write.log" || {
    echo "name probe did not apply the saved name to campaign dialogue" >&2
    tail -n 80 "$output/write.log" >&2
    exit 1
}
run_mode read
grep -Eq 'MIPSYNC_CAMPAIGN_RESUME node=.* players=1 names=1' "$output/read.log" || {
    echo "name probe did not restore the portable campaign name" >&2
    tail -n 80 "$output/read.log" >&2
    exit 1
}
if grep -q 'MIPSYNC_NAME_PROMPT' "$output/read.log"; then
    echo "Continue incorrectly repeated the founding name prompt" >&2
    tail -n 80 "$output/read.log" >&2
    exit 1
fi
printf 'Mipsync controller name persistence passed: %s\n' "$output/name.mcd"
