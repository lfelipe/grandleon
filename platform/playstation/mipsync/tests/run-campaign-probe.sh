#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

test "$#" -eq 2 || {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 campaign-build-directory empty-output-directory" >&2
    exit 2
}
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
for artifact in "$build/grandleon-mipsync-campaign.cue" \
        "$build/grandleon-mipsync-campaign.bin"; do
    test -f "$artifact" || { echo "missing campaign artifact: $artifact" >&2; exit 1; }
done
for stage in bell deep watch village gate; do
    for artifact in "$build/$stage-build/grandleon_mipsync.elf" \
            "$build/$stage-build-manifest.json"; do
        test -f "$artifact" || { echo "missing campaign artifact: $artifact" >&2; exit 1; }
    done
done

# The campaign proof is only useful if the two executable stages leave the
# reserves the production player still needs. Keep these checks beside the
# runtime hand-off so a visually green disc cannot hide an over-budget build.
python3 - \
    "$build/bell-build-manifest.json" \
    "$build/deep-build-manifest.json" \
    "$build/watch-build-manifest.json" \
    "$build/village-build-manifest.json" \
    "$build/gate-build-manifest.json" <<'PY'
import json
from pathlib import Path
import sys

for filename in sys.argv[1:]:
    path = Path(filename)
    document = json.loads(path.read_text(encoding="utf-8"))
    memory = document["main_ram"]
    scene = document["scene"]
    vram = scene["vram"]
    if document.get("identity") != "GRANDLEON_PS1_RENDERER=MIPSYNC":
        raise SystemExit(f"{path}: wrong renderer identity")
    if scene.get("terrain_relief") != "smooth":
        raise SystemExit(f"{path}: production terrain relief is not enabled")
    if scene.get("terrain_material") != "relay":
        raise SystemExit(f"{path}: Godot-derived terrain material is not enabled")
    if scene.get("terrain_forms", 0) < 1:
        raise SystemExit(f"{path}: native terrain geometry is missing")
    visibility = scene.get("visibility", {})
    if path.name == "gate-build-manifest.json":
        if visibility.get("cells") != 18 or visibility.get("entities") != 1:
            raise SystemExit(
                f"{path}: Twin Gate castle cover is not one batched 18-cell region"
            )
        if visibility.get("reveal") != "whole_authored_region_on_entry":
            raise SystemExit(f"{path}: Twin Gate reveal policy is not recorded")
    elif visibility.get("cells") != 0 or visibility.get("entities") != 0:
        raise SystemExit(f"{path}: unexpected hidden-area presentation")
    if not memory.get("passes_playable_slice_budget"):
        raise SystemExit(f"{path}: playable slice exceeds main RAM")
    if memory["playable_slice_committed_bytes"] > memory["main_ram_bytes"]:
        raise SystemExit(f"{path}: committed bytes exceed main RAM")
    if memory["platform_audio_reserve_bytes"] < 65536:
        raise SystemExit(f"{path}: platform audio reserve is below 64 KiB")
    if memory["stack_reserve_bytes"] < 32768:
        raise SystemExit(f"{path}: stack reserve is below 32 KiB")
    if memory["minimum_gameplay_heap_bytes"] < 524288:
        raise SystemExit(f"{path}: gameplay heap reserve is below 512 KiB")
    if vram["texture_bytes"] + vram["vram_reserved_bytes"] != 704 * 512 * 2:
        raise SystemExit(f"{path}: generated texture atlas escaped its VRAM budget")
    print(
        f"MIPSYNC_CAMPAIGN_BUDGET {path.name} "
        f"ram={memory['playable_slice_committed_bytes']}/{memory['main_ram_bytes']} "
        f"vram={vram['texture_bytes']}/{704 * 512 * 2} "
        f"audio_reserve={memory['platform_audio_reserve_bytes']}"
    )
PY
if test -e "$output"; then
    test -d "$output" || { echo "output exists and is not a directory: $output" >&2; exit 1; }
    test -z "$(find "$output" -mindepth 1 -maxdepth 1 -print -quit)" || {
        echo "output directory must be empty: $output" >&2
        exit 1
    }
else
    mkdir -p "$output"
fi
output=$(CDPATH= cd -- "$output" && pwd)

if test -n "${GRANDLEON_UPDATE_ADDRESS:-}"; then
    bell_update=$GRANDLEON_UPDATE_ADDRESS
else
    nm=$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm
    address_of() {
        "$nm" -n "$1" | awk '$3 == "grandleon_runtime_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
    }
    bell_update=$(address_of "$build/bell-build/grandleon_mipsync.elf")
    for stage in deep watch village gate; do
        stage_update=$(address_of "$build/$stage-build/grandleon_mipsync.elf")
        test "$bell_update" = "$stage_update" || {
            echo "campaign probe update seam differs: $bell_update vs $stage_update ($stage)" >&2
            exit 1
        }
    done
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation emulator image is missing: $image" >&2
    exit 1
}

run_mode() {
    mode=$1
    log=$output/$mode.log
    card=campaign.mcd
    test "$mode" != solo || card=solo.mcd
    timeout --kill-after=10 180 \
        "$docker" run --rm --user "$(id -u):$(id -g)" \
            --env HOME=/tmp \
            --env "GRANDLEON_UPDATE_ADDRESS=$bell_update" \
            --env "GRANDLEON_CAMPAIGN_MODE=$mode" \
            --volume "$build:/disc:ro" \
            --volume "$script_dir:/probe:ro" \
            --volume "$output:/out" \
            --workdir /tmp \
            "$image" \
            stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
                -bios /usr/local/share/pcsx-redux/openbios.bin \
                -testmode -interpreter -debugger \
                -memcard1 "/out/$card" \
                -dofile /probe/campaign_probe.lua \
                -iso /disc/grandleon-mipsync-campaign.cue >"$log" 2>&1
}

run_mode new
before=$(sha256sum "$output/campaign.mcd" | awk '{print $1}')
run_mode continue
after=$(sha256sum "$output/campaign.mcd" | awk '{print $1}')
test "$before" != "$after" || {
    echo "full campaign traversal did not advance the memory card" >&2
    exit 1
}

require() {
    log=$1
    pattern=$2
    label=$3
    grep -Eq "$pattern" "$log" || {
        echo "campaign probe did not report $label" >&2
        tail -n 100 "$log" >&2
        exit 1
    }
}
require "$output/new.log" 'MIPSYNC_CAMPAIGN_READY card=1 continue=0' 'a fresh card'
require "$output/new.log" 'MIPSYNC_CAMPAIGN_SAVE bytes=[1-9][0-9]* status=0' 'successful card saves'
require "$output/new.log" 'MIPSYNC_LOCAL_INPUT player=2 unit=[0-9a-f]+' 'controller-two unit ownership'
require "$output/new.log" 'MIPSYNC_CAMPAIGN_RESUME node=a6b11e9ea987638a players=2' 'the Deepwood hand-off'
require "$output/new.log" 'MIPSYNC_SIM_READY width=14 height=9 units=11 .* players=2 hash=72216135b6c8e9d3' 'the real two-player Deepwood simulation'
require "$output/new.log" 'MIPSYNC_CAMPAIGN_CAPTURE name=bell 320x240' 'the Bell capture'
require "$output/new.log" 'MIPSYNC_CAMPAIGN_CAPTURE name=bell-dialogue 320x240' 'the Bell in-battle conversation capture'
require "$output/new.log" 'MIPSYNC_BATTLE_MOMENT dialogue=fe2d78238cc49c48 lines=2' 'the Bell March opening conversation'
require "$output/new.log" 'MIPSYNC_BATTLE_MOMENT dialogue=830ac62c3f992c4c lines=2' 'the Bell Cedar opening conversation'
require "$output/new.log" 'MIPSYNC_CAMPAIGN_CAPTURE name=deepwood 320x240' 'the Deepwood capture'
require "$output/new.log" 'MIPSYNC_BATTLE_MOMENT dialogue=b46de5c083313842 lines=2' 'the Deepwood March opening conversation'
require "$output/new.log" 'MIPSYNC_BATTLE_MOMENT dialogue=35a3ea52846d6bf2 lines=2' 'the Deepwood Cedar opening conversation'
require "$output/new.log" 'MIPSYNC_GRANDLEON_METRICS frames=600 rendered_frames=300 scheduled_frames=300 deferred_frames=0 missed_vblanks=0 max_elapsed_vblanks=[12]$' 'the Deepwood 30 fps result'
require "$output/continue.log" 'MIPSYNC_CAMPAIGN_READY card=1 continue=1' 'Continue from a later process'
require "$output/continue.log" 'MIPSYNC_CAMPAIGN_RESUME node=0079abb4797ff5b4 players=2' 'the Cedar Watch hand-off'
require "$output/continue.log" 'MIPSYNC_CAMPAIGN_RESUME node=07cb9d097af3a9a1 players=2' 'the Gate Village hand-off'
require "$output/continue.log" 'MIPSYNC_CAMPAIGN_RESUME node=e5c0ef3904d2b0a3 players=2' 'the Twin Gate hand-off'
require "$output/continue.log" 'MIPSYNC_SIM_READY width=20 height=14 units=11 .* players=2 hash=25b92e2ee75b62e2' 'the real two-player Twin Gate simulation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=8c940045110ed85c lines=2' 'the Watch March opening conversation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=31c58f2e783ee068 lines=2' 'the Watch Cedar opening conversation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=65cde04c4bb6ec2d lines=2' 'the Village March opening conversation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=56eb4fb87152b839 lines=2' 'the Village Cedar opening conversation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=f5eec13d4bc815ae lines=2' 'the Twin Gate March opening conversation'
require "$output/continue.log" 'MIPSYNC_BATTLE_MOMENT dialogue=d312ee0bdab15c06 lines=2' 'the Twin Gate Cedar opening conversation'
for capture in deepwood watch village gate; do
    require "$output/continue.log" "MIPSYNC_CAMPAIGN_CAPTURE name=$capture 320x240" "$capture capture"
done
require "$output/continue.log" 'MIPSYNC_GRANDLEON_METRICS frames=600 rendered_frames=300 scheduled_frames=300 deferred_frames=0 missed_vblanks=0 max_elapsed_vblanks=[12]$' 'the Twin Gate 30 fps result'
require "$output/continue.log" 'MIPSYNC_MISS_RANGE first=0 last=0$' 'an empty missed-frame range'

run_mode solo
require "$output/solo.log" 'MIPSYNC_SIM_READY width=8 height=6 units=5 .* players=1 hash=d900322b622a711e' 'the authored solo Bell Orchard simulation'
require "$output/solo.log" 'MIPSYNC_SIM_READY width=14 height=9 units=7 .* players=1 hash=c6543d2796b7fe5c' 'the authored solo Deepwood simulation'
require "$output/solo.log" 'MIPSYNC_SIM_READY width=16 height=12 units=16 .* players=1 hash=7fcea7aa25ff75ca' 'the authored solo Cedar Watch simulation'
require "$output/solo.log" 'MIPSYNC_SIM_READY width=18 height=12 units=8 .* players=1 hash=88a9732d74e5ee06' 'the authored solo Gate Village simulation'
require "$output/solo.log" 'MIPSYNC_SIM_READY width=20 height=14 units=6 .* players=1 hash=8c904e6d40b072d9' 'the authored solo Twin Gate simulation'

printf 'Mipsync full campaign/card passed: card_sha256=%s update=%s\n' "$after" "$bell_update"
printf 'New log: %s\nContinue log: %s\nSolo log: %s\n' \
    "$output/new.log" "$output/continue.log" "$output/solo.log"
