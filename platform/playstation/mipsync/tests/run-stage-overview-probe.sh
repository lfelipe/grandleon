#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
[ "$#" -eq 2 ] || { echo 'usage: PSN00BSDK_ROOT=/path/to/sdk run-stage-overview-probe.sh build-directory empty-output-directory' >&2; exit 2; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
[ ! -e "$output" ] || { echo 'output must not exist' >&2; exit 2; }
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)
script=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$script/verify-probe-disc.py" "$build" > "$output/disc-verification.txt"
address=$("${PSN00BSDK_ROOT:?SDK is required}/bin/mipsel-none-elf-nm" -n "$build/grandleon_mipsync.elf" | awk '$3 == "grandleon_game_update" {print "0x" substr($1,length($1)-7); found=1} END {if (!found) exit 1}')
for mode in full skip; do
    mkdir "$output/$mode"
    skip=0
    [ "$mode" != skip ] || skip=40
    timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" --env HOME=/tmp \
        --env "GRANDLEON_UPDATE_ADDRESS=$address" --env "GRANDLEON_OVERVIEW_SKIP_AT=$skip" \
        --volume "$build:/disc:ro" --volume "$script:/probe:ro" --volume "$output/$mode:/out" \
        --workdir /tmp grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687 \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu -bios /usr/local/share/pcsx-redux/openbios.bin \
        -testmode -interpreter -debugger -dofile /probe/stage_overview_probe.lua \
        -iso /disc/grandleon-mipsync.cue > "$output/$mode/emulator.log" 2>&1
    python3 - "$output/$mode/emulator.log" "$skip" <<'PY'
import re,sys
s=open(sys.argv[1]).read()
a=re.search(r'MIPSYNC_SIM_READY .*hash=([0-9a-f]+)',s)
b=re.search(r'MIPSYNC_STAGE_OVERVIEW_END skipped=(\d) hash=([0-9a-f]+)',s)
assert a and b and a[1]==b[2], 'overview must preserve the real encounter hash'
assert int(b[1])==bool(int(sys.argv[2])), 'skip must use normal input'
assert 'MIPSYNC_OVERVIEW_CAPTURE' in s, 'real rendered overview frames required'
assert 'MIPSYNC_MOVE_SETTLED' not in s, 'skip must not issue a move'
intro=s[s.index('MIPSYNC_STAGE_OVERVIEW_BEGIN'):s.index('MIPSYNC_STAGE_OVERVIEW_END')]
assert 'MIPSYNC_VISIBILITY_REVEAL' not in intro, 'moving the camera must not uncover a room'
PY
done
printf 'PS1 full stage overview and controller skip preserve the encounter\n'
