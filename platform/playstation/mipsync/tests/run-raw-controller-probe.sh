#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 direct-deepwood-build empty-evidence-directory" >&2; exit 2; }
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
scripts=$(cd "$(dirname "$0")" && pwd)
python3 "$scripts/verify-probe-disc.py" "$build" > "$out/disc-verification.txt"
toolchain=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
emulator=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
docker run --rm --volume "$build:/disc:ro" "$toolchain" \
    mipsel-linux-gnu-nm -n /disc/grandleon_mipsync.elf > "$out/symbols.txt"
address() { awk -v symbol="$1" '$3 == symbol {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt"; }
update=$(address grandleon_runtime_update)
pad=$(address s_pad_buffers)
cursor=$(awk '$3 ~ /s_cursorE$/ {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt")
timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" \
    --env HOME=/tmp --env "GRANDLEON_UPDATE_ADDRESS=$update" \
    --env "GRANDLEON_PAD_BUFFER_ADDRESS=$pad" --env "GRANDLEON_CURSOR_ADDRESS=$cursor" \
    --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
    --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -memcard1 /out/controls.mcd -dofile /probe/raw_controller_probe.lua \
    -iso /disc/grandleon-mipsync.cue > "$out/emulator.log" 2>&1
rg -q 'MIPSYNC_SIM_READY width=14 height=9 units=11' "$out/emulator.log"
rg -q 'MIPSYNC_PHYSICAL_CONTROLLER_PASS digital=2 analog=4' "$out/emulator.log"
[[ "$(rg -c 'MIPSYNC_PHYSICAL_CONTROL_PASS ' "$out/emulator.log")" == 6 ]]
echo "PS1 physical digital/analog controller mapping and held-stick edges passed."
