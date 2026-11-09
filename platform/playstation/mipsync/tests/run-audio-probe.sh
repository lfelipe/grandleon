#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 production-build empty-evidence-directory" >&2; exit 2; }
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
test -s "$build/game.cue"
scripts=$(cd "$(dirname "$0")" && pwd)
toolchain=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
emulator=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
docker run --rm --volume "$build:/disc:ro" "$toolchain" \
    mipsel-linux-gnu-nm -n /disc/stages/01/build/grandleon_mipsync.elf > "$out/symbols.txt"
address() { awk -v symbol="$1" '$3 == symbol {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt"; }
update=$(address grandleon_runtime_update)
ready=$(address s_cd_ready)
tracks=$(address s_track_count)
playing=$(address s_playing)
for mode in write read; do
    timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp --env "GRANDLEON_UPDATE_ADDRESS=$update" \
        --env "GRANDLEON_AUDIO_MODE=$mode" --env "GRANDLEON_CD_READY_ADDRESS=$ready" \
        --env "GRANDLEON_TRACK_COUNT_ADDRESS=$tracks" --env "GRANDLEON_PLAYING_ADDRESS=$playing" \
        --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
        --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
        -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
        -memcard1 /out/audio.mcd -dofile /probe/audio_probe.lua -iso /disc/game.cue \
        > "$out/$mode.log" 2>&1
    rg -q 'MIPSYNC_AUDIO_TRANSPORT_READY tracks=7 theme=0' "$out/$mode.log"
    rg -q "MIPSYNC_AUDIO_PROBE_DONE mode=$mode" "$out/$mode.log"
done
rg -q 'MIPSYNC_SETTING name=music value=35 saved=1' "$out/write.log"
rg -q 'MIPSYNC_SETTING name=effects value=80 saved=1' "$out/write.log"
rg -q 'MIPSYNC_BATTLE_MENU screen=settings' "$out/write.log"
rg -q 'MIPSYNC_SETTING name=music value=40 saved=1' "$out/write.log"
rg -q 'MIPSYNC_SETTING name=effects value=75 saved=1' "$out/write.log"
rg -q 'MIPSYNC_SETTINGS_LOAD .*music=40 effects=75' "$out/read.log"
for screen in title battle; do
    ffmpeg -v error -y -f rawvideo -pixel_format bgr555le -video_size 320x240 \
        -i "$out/audio-$screen.bin" -frames:v 1 "$out/audio-$screen.png"
done
sha256sum "$build/game.bin" > "$out/tested-bin.sha256"
echo "PS1 CD transport and independent title/battle audio settings passed."
