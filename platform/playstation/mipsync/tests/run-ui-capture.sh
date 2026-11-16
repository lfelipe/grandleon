#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 production-build empty-evidence-directory" >&2; exit 2; }
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
scripts=$(cd "$(dirname "$0")" && pwd)
python3 "$scripts/verify-campaign-disc.py" "$build" > "$out/artifact.json"
docker run --rm --volume "$build:/disc:ro" grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b mipsel-linux-gnu-nm -n /disc/stages/01/build/grandleon_mipsync.elf > "$out/symbols.txt"
address(){ awk -v pattern="$1" '$3~pattern {print "0x"$1;found=1} END{if(!found)exit 1}' "$out/symbols.txt"; }
envargs=(--env HOME=/tmp)
while read -r key pattern; do envargs+=(--env "$key=$(address "$pattern")");done <<'SYMS'
UPDATE ^grandleon_runtime_update$
CAMP s_campaign_modeE([.]lto_priv[.][0-9]+)?$
MODE s_modeE([.]lto_priv[.][0-9]+)?$
MOMENT s_battle_momentE([.]lto_priv[.][0-9]+)?$
MENU s_campaign_menu_indexE([.]lto_priv[.][0-9]+)?$
SETTINGS s_settings_menu_indexE([.]lto_priv[.][0-9]+)?$
CURSOR 8s_cursorE([.]lto_priv[.][0-9]+)?$
SELECTED s_selectedE([.]lto_priv[.][0-9]+)?$
SYMS
timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" "${envargs[@]}" --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" --workdir /tmp grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687 stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger -memcard1 /out/ui.mcd -dofile /probe/ui_capture_probe.lua -iso /disc/game.cue > "$out/emulator.log" 2>&1
rg -q 'PS1_UI_CAPTURE_PASS states=6' "$out/emulator.log"
for state in main settings pause movement actions targeting;do
 ffmpeg -v error -y -f rawvideo -pixel_format bgr555le -video_size 320x240 -i "$out/$state.bin" -frames:v 1 "$out/$state.png"
done
python3 - "$out" <<'PYTHON'
import hashlib,json,sys
from pathlib import Path
out=Path(sys.argv[1])
proof=json.loads((out/'artifact.json').read_text())
proof.update(width=320,height=240,language='en',players=1,
             actor='7543913367842920562',target='7878946346193111832',
             route=[[1,2],[4,1]],target_cell=[5,1],target_hp=15,damage=6,
             flow='normal title/settings/story/pause/move/action/forecast; input seam only')
proof['screenshots']={}
for name in ['main','settings','pause','movement','actions','targeting']:
    assert (out/(name+'.bin')).stat().st_size==320*240*2
    proof['screenshots'][name]={'file':name+'.png','sha256':hashlib.sha256((out/(name+'.png')).read_bytes()).hexdigest()}
(out/'ui-capture.json').write_text(json.dumps(proof,indent=2)+'\n')
PYTHON
