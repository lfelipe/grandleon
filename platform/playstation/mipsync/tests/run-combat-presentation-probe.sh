#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
test "$#" -eq 2 || { echo "usage: $0 direct-bell-build empty-evidence-directory" >&2; exit 2; }
build=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
test -z "$(find "$out" -mindepth 1 -maxdepth 1 -print -quit)" || exit 2
scripts=$(cd "$(dirname "$0")" && pwd)
python3 "$scripts/verify-probe-disc.py" "$build" > "$out/disc-verification.txt"
toolchain=${GRANDLEON_PLAYSTATION_TOOLCHAIN_IMAGE:-grandleon/playstation-toolchain:e8db332e2b2d035cb0bbc5aeec2c134b9daac60c6f787e91c94cf03194eb7a5b}
emulator=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:a415a98e6c347b41ca5a1d3f23bbc4eb26059687}
docker run --rm --volume "$build:/disc:ro" "$toolchain" mipsel-linux-gnu-nm -n /disc/grandleon_mipsync.elf > "$out/symbols.txt"
address() { awk -v symbol="$1" '$3 ~ symbol {print "0x" $1; found=1} END {if (!found) exit 1}' "$out/symbols.txt"; }
controlled=${GRANDLEON_CONTROLLED_LETHAL_FIXTURE:-0}
env_args=(--env HOME=/tmp --env "GRANDLEON_CONTROLLED_LETHAL_FIXTURE=$controlled")
while read -r variable symbol; do env_args+=(--env "$variable=$(address "$symbol")"); done <<'SYMBOLS'
GRANDLEON_UPDATE_ADDRESS ^grandleon_game_update$
GRANDLEON_AUDIO_CUE_ADDRESS ^grandleon_audio_cue$
GRANDLEON_RESULT_DURATION_ADDRESS s_result_durationE$
GRANDLEON_RESULT_FRAMES_ADDRESS s_result_framesE$
GRANDLEON_CURSOR_ADDRESS 8s_cursorE$
GRANDLEON_PLAYER_CURSORS_ADDRESS s_player_cursorsE$
GRANDLEON_AI_ACTIVE_ADDRESS s_ai_activeE$
GRANDLEON_ATTACKER_ADDRESS s_combat_attackerE$
GRANDLEON_TARGET_ADDRESS s_combat_targetE$
GRANDLEON_CLOSE_ADDRESS s_result_close_combatE$
GRANDLEON_ATTACKER_CELL_ADDRESS s_combat_attacker_positionE$
GRANDLEON_TARGET_CELL_ADDRESS s_combat_target_positionE$
GRANDLEON_CAMERA_RETURN_ADDRESS s_camera_return_framesE$
GRANDLEON_ON_BOARD_ADDRESS s_unit_on_boardE$
GRANDLEON_IDS_ADDRESS ^g_grandleon_unit_ids$
GRANDLEON_BINDINGS_ADDRESS ^g_grandleon_unit_entity_indices$
GRANDLEON_POSES_ADDRESS ^g_grandleon_unit_pose_texture_indices$
GRANDLEON_SCENE_ADDRESS ^g_ps1_scene$
GRANDLEON_UNIT_COUNT_ADDRESS ^g_grandleon_unit_count$
SYMBOLS
bytes=$(docker run --rm --volume "$build:/disc:ro" "$toolchain" mipsel-linux-gnu-nm -S /disc/grandleon_mipsync.elf | awk '$4 == "k_ps1_entities" {print "0x" $2; found=1} END {if (!found) exit 1}')
env_args+=(--env "GRANDLEON_ENTITY_ARRAY_BYTES=$bytes")
timeout --kill-after=10 240 docker run --rm --user "$(id -u):$(id -g)" "${env_args[@]}" \
    --volume "$build:/disc:ro" --volume "$scripts:/probe:ro" --volume "$out:/out" \
    --workdir /tmp "$emulator" stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
    -bios /usr/local/share/pcsx-redux/openbios.bin -testmode -interpreter -debugger \
    -memcard1 /out/presentation.mcd -dofile /probe/combat_presentation_probe.lua \
    -iso /disc/grandleon-mipsync.cue > "$out/emulator.log" 2>&1
if [[ "$controlled" == 1 ]]; then
    test -s "$build/../source/generated/lethal-fixture.json"
    cp "$build/../source/generated/lethal-fixture.json" "$out/lethal-fixture.json"
    expected=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["canonical_hashes"]["two_player"])' "$out/lethal-fixture.json")
    rg -q "MIPSYNC_SIM_READY width=8 height=6 units=8 .*hash=$expected" "$out/emulator.log"
    rg -q 'MIPSYNC_COMBAT_PRESENTATION_PASS .*fatal_samples=[1-9]' "$out/emulator.log"
else
    rg -q 'MIPSYNC_SIM_READY width=8 height=6 units=8 .*hash=ab6ddf2e9a634fa0' "$out/emulator.log"
    rg -q 'MIPSYNC_COMBAT_PRESENTATION_PASS enemy=[1-9].*side_samples=[1-9].*restores=[1-9]' "$out/emulator.log"
    rg -q 'MIPSYNC_COMBAT_AUDIO_PASS launch=28 impact=37 counter_launch=68 counter_impact=77' "$out/emulator.log"
    rg -q 'MIPSYNC_PLAYER_CAMERA_PASS player=2 ' "$out/emulator.log"
fi
rg 'MIPSYNC_COMBAT_PRESENTATION_PASS' "$out/emulator.log"
