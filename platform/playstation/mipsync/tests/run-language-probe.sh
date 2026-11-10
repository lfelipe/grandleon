#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 translated-build-directory empty-output-directory" >&2
    exit 2
}
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
source_build=$(CDPATH= cd -- "$1" && pwd)
context=battle
build=$source_build/build
cue=$build/grandleon-mipsync.cue
disc_dir=$build
language_index=1
if test -f "$source_build/game.cue"; then
    context=title
    build=$source_build/stages/01/build
    cue=$source_build/game.cue
    disc_dir=$source_build
    language_index=$(python3 - "$source_build/stages/01/two.json" <<'PY'
import json, sys
codes = [value['code'] for value in json.load(open(sys.argv[1]))['languages']]
if 'pt-BR' not in codes: raise SystemExit('production campaign does not provide pt-BR')
print(codes.index('pt-BR'))
PY
    )
elif test -f "$source_build/grandleon-mipsync-campaign.cue"; then
    context=title
    build=$source_build/bell-build
    cue=$source_build/grandleon-mipsync-campaign.cue
    disc_dir=$source_build
fi
output=$2
test -f "$cue" || {
    echo "missing translated disc: $cue" >&2
    exit 1
}
test -f "$build/grandleon_mipsync.elf" || {
    echo "missing translated renderer ELF: $build/grandleon_mipsync.elf" >&2
    exit 1
}
if test -e "$output"; then
    test -d "$output" || { echo "output is not a directory: $output" >&2; exit 1; }
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo "output directory must be empty: $output" >&2
        exit 1
    }
else
    mkdir -p "$output"
fi
output=$(CDPATH= cd -- "$output" && pwd)

update=$(
    "$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm" -n \
        "$build/grandleon_mipsync.elf" |
    awk '$3 == "grandleon_runtime_update" {
        print "0x" substr($1, length($1) - 7); found = 1
    } END { if (!found) exit 1 }'
)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned PlayStation emulator image is missing: $image" >&2
    exit 1
}

run_mode() {
    mode=$1
    timeout --kill-after=10 180 \
        "$docker" run --rm --user "$(id -u):$(id -g)" \
            --env HOME=/tmp \
            --env "GRANDLEON_UPDATE_ADDRESS=$update" \
            --env "GRANDLEON_LANGUAGE_MODE=$mode" \
            --env "GRANDLEON_LANGUAGE_CONTEXT=$context" \
            --env "GRANDLEON_LANGUAGE_STEPS=$language_index" \
            --volume "$disc_dir:/disc:ro" \
            --volume "$script_dir:/probe:ro" \
            --volume "$output:/out" \
            --workdir /tmp \
            "$image" \
            stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
                -bios /usr/local/share/pcsx-redux/openbios.bin \
                -testmode -interpreter -debugger \
                -memcard1 /out/language.mcd \
                -dofile /probe/language_probe.lua \
                -iso "/disc/$(basename -- "$cue")" \
            >"$output/$mode.log" 2>&1
}

run_mode write
grep -Eq 'MIPSYNC_SETTING name=language value=1 saved=1 language=pt-BR' \
        "$output/write.log" || {
    echo "language probe did not switch the live game to pt-BR" >&2
    tail -n 80 "$output/write.log" >&2
    exit 1
}
run_mode read
grep -Eq 'MIPSYNC_SETTINGS_LOAD close=1 reduced=0 fps=0 language=pt-BR' \
        "$output/read.log" || {
    echo "language probe did not restore the saved language" >&2
    tail -n 80 "$output/read.log" >&2
    exit 1
}
printf 'Mipsync live language persistence passed: %s\n' "$output/language.mcd"
