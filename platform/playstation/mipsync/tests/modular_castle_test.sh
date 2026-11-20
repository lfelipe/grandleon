#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
[[ $# == 3 ]] || { echo "usage: $0 scene.json renderer-build empty-output" >&2; exit 2; }
scene=$(realpath "$1")
build=$(realpath "$2")
mkdir -p "$3"
output=$(realpath "$3")
[[ -z $(find "$output" -mindepth 1 -maxdepth 1 -print -quit) ]] || { echo "output must be empty" >&2; exit 2; }
script_dir=$(cd "$(dirname "$0")" && pwd)
if [[ -f "$build/game.cue" ]]; then
    python3 "$script_dir/verify-campaign-disc.py" "$build" > "$output/disc-verification.txt"
    elf="$build/stages/01/build/grandleon_mipsync.elf"
    cue=game.cue
else
    python3 "$script_dir/verify-probe-disc.py" "$build" > "$output/disc-verification.txt"
    elf="$build/grandleon_mipsync.elf"
    cue=grandleon-mipsync.cue
fi
players=$(python3 -c 'import json,sys; s=json.load(open(sys.argv[1])); assert (s["width"],s["height"])==(32,24); print(s["local_players"])' "$scene")
if [[ "$cue" == game.cue ]]; then
    variant=one
    if [[ "$players" == 2 ]]; then variant=two; fi
    cmp "$scene" "$build/stages/01/$variant.json"
fi
read -r initial_x initial_y < <(python3 - "$scene" <<'PYINITIAL'
import json,sys
scene=json.load(open(sys.argv[1]))
units=sorted((u for u in scene['simulation']['units'] if u['side']==0 and u.get('local_player',1)<=1 and not u.get('arrival_round',0)),key=lambda u:int(u['id']))
print(*units[0]['position'])
PYINITIAL
)
nm=${PSN00BSDK_ROOT:?}/bin/mipsel-none-elf-nm
"$nm" -n "$elf" > "$output/symbols.txt"
symbol() { awk -v p="$1" '$3 ~ p { print "0x" substr($1,length($1)-7); found=1; exit } END {if (!found) exit 1}' "$output/symbols.txt"; }
# LTO can fold the count and policy constants. The linked array gives the
# actual number of uint16_t cell indices, and the replay tests local reveals.
cover_bytes=$("$nm" -S "$elf" | awk '$4 == "g_grandleon_visibility_cell_indices" { print "0x" $2 }')
[[ -n $cover_bytes && $((cover_bytes % 2)) == 0 ]]
display_address=
if [[ ${GRANDLEON_MEASURE_PS1_SWAPS:-0} == 1 ]]; then
    display_address=$(symbol '^PutDispEnv$')
fi
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
timeout --kill-after=10 180 docker run --rm --user "$(id -u):$(id -g)" --env HOME=/tmp \
    --env "GRANDLEON_DISPLAY_ADDRESS=$display_address" \
    --env "GRANDLEON_UPDATE_ADDRESS=$(symbol '^grandleon_game_update$')" \
    --env "GRANDLEON_CAMPAIGN_MODE_ADDRESS=$(symbol 's_campaign_modeE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_MENU_ADDRESS=$(symbol 's_campaign_menu_indexE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_MOMENT_ADDRESS=$(symbol 's_battle_momentE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_CURSOR_ADDRESS=$(symbol 's_cursorE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_REVEALED_ADDRESS=$(symbol 's_visibility_revealedE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_COVER_CELLS_ADDRESS=$(symbol '^g_grandleon_visibility_cell_indices$')" \
    --env "GRANDLEON_COVER_COUNT=$((cover_bytes / 2))" \
    --env "GRANDLEON_CAMERA_MIN_X=$(symbol 's_camera_min_xE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_CAMERA_MAX_X=$(symbol 's_camera_max_xE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_CAMERA_MIN_Z=$(symbol 's_camera_min_zE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_CAMERA_MAX_Z=$(symbol 's_camera_max_zE([.]lto_priv[.][0-9]+)?$')" \
    --env "GRANDLEON_ROOM_PLAYERS=$players" \
    --env "GRANDLEON_INITIAL_X=$initial_x" --env "GRANDLEON_INITIAL_Y=$initial_y" \
    --volume "$build:/disc:ro" --volume "$script_dir:/probe:ro" --volume "$output:/out" \
    --workdir /tmp "$image" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -dofile /probe/modular_castle_probe.lua -iso "/disc/$cue" > "$output/emulator.log" 2>&1
rg -q "MIPSYNC_MODULAR_CASTLE_PASS explored=9 total=170 players=$players" "$output/emulator.log"
[[ $(wc -c < "$output/castle-closed.bin") == 153600 ]]
[[ $(wc -c < "$output/castle-entry.bin") == 153600 ]]
python3 - "$scene" "$output/emulator.log" "$cue" <<'PYIDENTITY'
import json,sys
scene=json.load(open(sys.argv[1]));log=open(sys.argv[2]).read()
units = len(scene['simulation']['units'])
players = scene['local_players']
assert f"MIPSYNC_SIM_READY width=32 height=24 units={units} " in log
assert 'MIPSYNC_SIM_HASH_FAIL' not in log
if sys.argv[3] == 'game.cue':
    # The engine validates the authored hash before applying persistent kits.
    # The live hash includes those kits, so match mode and layout here after
    # verifying the disc and the selected build manifest above.
    assert any(f" players={players} hash=" in line
               for line in log.splitlines() if line.startswith('MIPSYNC_SIM_READY '))
else:
    assert f"players={players} hash={int(scene['canonical_hash']):016x}" in log
PYIDENTITY
rg -q "MIPSYNC_MODULAR_SCROLL_PASS cursor=31,23 explored=9 players=$players" "$output/emulator.log"
[[ $(wc -c < "$output/castle-far-corner.bin") == 153600 ]]
if [[ -n $display_address ]]; then
    for phase in room horizontal vertical; do
        [[ $(rg -c "^$phase," "$output/scroll-swaps.csv") -ge 2 ]]
    done
fi
echo "Actual PS1 $players-player larger castle entry, local room reveal and both scrolling axes pass."
