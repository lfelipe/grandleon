#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 production-build empty-evidence-directory" >&2; exit 2; }
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
scripts=$(cd "$(dirname "$0")" && pwd)
python3 "$scripts/verify-campaign-disc.py" "$build" > "$out/disc-verification.json"
toolchain=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
emulator=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
docker run --rm --volume "$build:/disc:ro" "$toolchain" mipsel-linux-gnu-nm -n /disc/stages/01/build/grandleon_mipsync.elf > "$out/symbols.txt"
address() { awk -v symbol="$1" '$3 ~ symbol {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt"; }
language=$(python3 -c 'import json,sys; p=json.load(open(sys.argv[1])); print(next(i for i,v in enumerate(p["languages"]) if v["code"]=="pt-BR"))' "$build/stages/01/two.json")
env_args=(--env HOME=/tmp --env "GRANDLEON_STORY_LANGUAGE_INDEX=$language")
while read -r variable symbol; do env_args+=(--env "$variable=$(address "$symbol")"); done <<'SYMBOLS'
GRANDLEON_UPDATE_ADDRESS ^grandleon_runtime_update$
GRANDLEON_CAMPAIGN_MODE_ADDRESS s_campaign_modeE$
GRANDLEON_CAMPAIGN_MENU_ADDRESS s_campaign_menu_indexE$
GRANDLEON_LANGUAGE_INDEX_ADDRESS s_language_indexE$
GRANDLEON_STORY_LINE_ADDRESS s_story_line_indexE$
GRANDLEON_STORY_PAGE_ADDRESS s_story_page_offsetE$
GRANDLEON_SPEAKER_IDS_ADDRESS ^g_grandleon_speaker_unit_type_ids$
GRANDLEON_SPEAKER_TEXTURES_ADDRESS ^g_grandleon_speaker_texture_indices$
GRANDLEON_SPEAKER_COUNT_ADDRESS ^g_grandleon_speaker_count$
GRANDLEON_TEXTURE_ENSURE_ADDRESS ^ps1_texture_ensure_resident$
SYMBOLS
timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" "${env_args[@]}" \
    --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
    --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -memcard1 /out/story.mcd -dofile /probe/story_portrait_probe.lua \
    -iso /disc/game.cue > "$out/emulator.log" 2>&1
rg -q 'MIPSYNC_PRODUCTION_STORY_PASS lines=3 portraits=3' "$out/emulator.log"
for capture in title-ptbr story-0 story-1 story-2; do
    ffmpeg -v error -y -f rawvideo -pixel_format bgr555le -video_size 320x240 \
        -i "$out/$capture.bin" -frames:v 1 "$out/$capture.png"
done
rg 'MIPSYNC_STORY_PORTRAIT|MIPSYNC_PRODUCTION_STORY_PASS' "$out/emulator.log"
