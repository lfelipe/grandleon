#!/bin/sh
# SPDX-License-Identifier: MIT
# Run the normal authored fixture with a fresh card, then two fresh-process resumes.
set -eu
test "$#" -eq 2 || { echo "usage: $0 fixture-build-directory empty-output-directory" >&2; exit 2; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
if test -e "$output"; then
    test -d "$output" && test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo 'output directory must be empty' >&2; exit 1;
    }
else
    mkdir -p "$output"
fi
output=$(CDPATH= cd -- "$output" && pwd)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/verify-campaign-disc.py" "$build" > "$output/disc-identity.json"
python3 "$script_dir/personal-character-bindings.py" "$build" "$output/personal-character-bindings.lua" \
    --nm "${GRANDLEON_PSN00B_NM:-nm}" > "$output/bindings.log"
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null
for players in ${GRANDLEON_PERSONAL_PLAYERS_LIST:-1 2}; do
    for phase in new loss reload; do
        log=$output/$players-$phase.log
        timeout --kill-after=10 240 "$docker" run --rm --user "$(id -u):$(id -g)" \
            --env HOME=/tmp --env "GRANDLEON_PERSONAL_PHASE=$phase" \
            --env "GRANDLEON_PERSONAL_PLAYERS=$players" \
            --volume "$build:/disc:ro" --volume "$script_dir:/probe:ro" \
            --volume "$output:/out" --workdir /tmp "$image" \
            stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
                -bios /usr/local/share/pcsx-redux/openbios.bin \
                -testmode -interpreter -debugger \
                -memcard1 "/out/personal-$players.mcd" \
                -dofile /probe/personal_character_probe.lua -iso /disc/game.cue > "$log" 2>&1 || {
            tail -n 100 "$log" >&2; exit 1;
        }
        if grep -Eq 'PERSONAL_CHARACTER_FAIL|MIPSYNC_SIM_HASH_FAIL|MIPSYNC_CAMPAIGN_.*FAIL' "$log"; then
            tail -n 100 "$log" >&2; exit 1
        fi
        grep -q "PERSONAL_CHARACTER_PASS phase=$phase players=$players" "$log"
        if test "$phase" = new; then
            grep -q 'assertion=permanent_and_withdraw_defeated_by_real_combat' "$log"
            grep -q 'MIPSYNC_CAMPAIGN_READY card=1 continue=0' "$log"
        elif test "$phase" = loss; then
            grep -q 'assertion=required_defeat_overrides_live_allies_and_fails_goal' "$log"
        fi
        if test "$phase" != new; then
            grep -q 'MIPSYNC_CAMPAIGN_READY card=1 continue=1' "$log"
        fi
        if test "$phase" != reload; then
            grep -q 'MIPSYNC_CAMPAIGN_SAVE bytes=[1-9][0-9]* status=0' "$log"
        fi
        if test "$players" = 2 && test "$phase" != reload; then
            grep -q 'MIPSYNC_LOCAL_INPUT player=2 unit=' "$log"
        fi
        sha256sum "$output/personal-$players.mcd" >> "$output/card-digests.txt"
    done
done
printf 'Personal character rules passed through normal controllers, authored routes and fresh-process card reloads: %s\n' "$output"
