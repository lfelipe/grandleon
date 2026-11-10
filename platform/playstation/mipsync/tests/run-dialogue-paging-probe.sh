#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 direct-ptbr-twin-gate-build empty-evidence-directory" >&2; exit 2; }
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
moment=$(awk '$3 ~ /s_battle_momentE$/ {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt")
timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" \
    --env HOME=/tmp --env "GRANDLEON_UPDATE_ADDRESS=$update" \
    --env "GRANDLEON_MOMENT_ADDRESS=$moment" \
    --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
    --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -memcard1 /out/controls.mcd -dofile /probe/dialogue_paging_probe.lua \
    -iso /disc/grandleon-mipsync.cue > "$out/emulator.log" 2>&1
rg -q 'MIPSYNC_SIM_READY .*players=2' "$out/emulator.log"
rg -q 'MIPSYNC_DIALOGUE_PAGING_PASS same_speaker_offset=[1-9].*next_line=1' "$out/emulator.log"
echo "PS1 authored translated dialogue keeps its speaker until all pages are shown."
