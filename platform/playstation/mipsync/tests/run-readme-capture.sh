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
env_args=(--env HOME=/tmp)
while read -r variable symbol; do env_args+=(--env "$variable=$(address "$symbol")"); done <<'SYMBOLS'
GRANDLEON_UPDATE_ADDRESS ^grandleon_runtime_update$
GRANDLEON_CAMPAIGN_MODE_ADDRESS s_campaign_modeE$
GRANDLEON_INPUT_MODE_ADDRESS s_modeE$
GRANDLEON_MOMENT_ADDRESS s_battle_momentE$
GRANDLEON_CURSOR_ADDRESS 8s_cursorE$
GRANDLEON_SELECTED_ADDRESS s_selectedE$
GRANDLEON_RESULT_FRAMES_ADDRESS s_result_framesE$
GRANDLEON_CAMERA_RETURN_ADDRESS s_camera_return_framesE$
GRANDLEON_ATTACKER_ADDRESS s_combat_attackerE$
GRANDLEON_TARGET_ADDRESS s_combat_targetE$
GRANDLEON_ATTACKER_CELL_ADDRESS s_combat_attacker_positionE$
GRANDLEON_TARGET_CELL_ADDRESS s_combat_target_positionE$
GRANDLEON_DAMAGE_ADDRESS s_last_result_damageE$
GRANDLEON_COUNTER_DAMAGE_ADDRESS s_last_result_counter_damageE$
GRANDLEON_COUNTER_ADDRESS s_combat_counterE$
SYMBOLS
timeout --kill-after=10 240 docker run --rm --user "$(id -u):$(id -g)" "${env_args[@]}" \
    --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
    --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -memcard1 /out/readme.mcd -dofile /probe/readme_capture_probe.lua \
    -iso /disc/game.cue > "$out/emulator.log" 2>&1
rg -q 'MIPSYNC_README_CAPTURE_PASS ' "$out/emulator.log"
rg -q 'MIPSYNC_README_FORECAST_CAPTURE target=5,1 expected_hit=100 expected_damage=6 expected_counter=1' "$out/emulator.log"
rg -q 'MIPSYNC_MOVE_SETTLED unit=68b15d3d1a991872 steps=4' "$out/emulator.log"
python3 - "$out" <<'PY'
import json,re,sys
from pathlib import Path
out=Path(sys.argv[1]); log=(out/'emulator.log').read_text()
match=re.search(r'MIPSYNC_README_CAPTURE_PASS frames=(\d+) fps=30 first_vblank=(\d+) last_vblank=(\d+)',log)
frames,first,last=map(int,match.groups())
with (out/'readme.bgr555le').open('wb') as destination:
    for index in range(frames):
        frame=(out/f'frame-{index:04d}.bin').read_bytes()
        assert len(frame)==320*240*2
        destination.write(frame)
forecast=int(re.search(r'MIPSYNC_README_FORECAST_FRAME index=(\d+)',log).group(1))
(out/'forecast.bgr555le').write_bytes((out/f'frame-{forecast:04d}.bin').read_bytes())
assert last-first==(frames-1)*2
proof=json.loads((out/'disc-verification.json').read_text())
proof.update({'fps':30,'width':320,'height':240,'frames':frames,'first_vblank':first,'last_vblank':last,
              'actor':'7543913367842920562','target':'7878946346193111832',
              'route':[[1,2],[4,1]],'target_cell':[5,1],'expected_hit_chance':100,'forecast_frame':'forecast.bgr555le','damage':6,'counter_damage':1,
              'capture':'every second GPU retrace, repeats retained, no game-state edits'})
(out/'readme-capture.json').write_text(json.dumps(proof,indent=2)+'\n')
PY
rg 'MIPSYNC_README_' "$out/emulator.log"
ffmpeg -v error -y -f rawvideo -pixel_format bgr555le -video_size 320x240 -i "$out/forecast.bgr555le" -frames:v 1 "$out/forecast.png"
