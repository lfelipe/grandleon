#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu

usage() {
    echo "usage: PSN00BSDK_ROOT=/path/to/sdk $0 scene.json renderer-build-directory output-directory" >&2
    exit 2
}

test "$#" -eq 3 || usage
manifest=$1
shift
test -f "$manifest" || { echo "missing scene manifest: $manifest" >&2; exit 2; }
test -n "${PSN00BSDK_ROOT:-}" || { echo "PSN00BSDK_ROOT is required" >&2; exit 1; }
build=$(CDPATH= cd -- "$1" && pwd)
output=$2
elf=$build/grandleon_mipsync.elf
cue=$build/grandleon-mipsync.cue
for artifact in "$elf" "$cue"; do
    test -f "$artifact" || { echo "missing renderer artifact: $artifact" >&2; exit 1; }
done
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

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../tests" && pwd)
python3 "$script_dir/verify-probe-disc.py" "$build" >"$output/disc-verification.txt"
docker=${GRANDLEON_DOCKER:-docker}
revision=a415a98e6c347b41ca5a1d3f23bbc4eb26059687
image=${GRANDLEON_PLAYSTATION_EMULATOR_IMAGE:-grandleon/playstation-emulator:$revision}
command -v "$docker" >/dev/null 2>&1 || { echo "container runtime is required: $docker" >&2; exit 1; }
"$docker" image inspect "$image" >/dev/null 2>&1 || {
    echo "pinned Grandleon PlayStation emulator image is missing: $image" >&2
    exit 1
}
installed_revision=$("$docker" run --rm "$image" cat /usr/local/PCSX_REDUX_REVISION)
test "$installed_revision" = "$revision" || {
    echo "emulator revision mismatch: expected $revision, got $installed_revision" >&2
    exit 1
}

nm=$PSN00BSDK_ROOT/bin/mipsel-none-elf-nm
test -x "$nm" || { echo "missing nm tool: $nm" >&2; exit 1; }
update_address=$(
    "$nm" -n "$elf" |
        awk '$3 == "grandleon_game_update" {
            print "0x" substr($1, length($1) - 7); found = 1
        } END { if (!found) exit 1 }'
) || { echo "grandleon_game_update is missing from $elf" >&2; exit 1; }

log=$output/emulator.log
set +e
timeout --kill-after=10 180 \
    "$docker" run --rm --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env GRANDLEON_SCENE_REVIEW=1 \
        --env "GRANDLEON_REVIEW_RIGHT_COUNT=${GRANDLEON_REVIEW_RIGHT_COUNT:-13}" \
        --env "GRANDLEON_REVIEW_DOWN_COUNT=${GRANDLEON_REVIEW_DOWN_COUNT:-4}" \
        --env "GRANDLEON_UPDATE_ADDRESS=$update_address" \
        --env "GRANDLEON_CAPTURE_MOTION=${GRANDLEON_CAPTURE_MOTION:-0}" \
        --volume "$build:/disc:ro" \
        --volume "$script_dir:/probe:ro" \
        --volume "$output:/out" \
        --workdir /tmp \
        "$image" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter -debugger \
            -dofile /probe/scroll_probe.lua \
            -iso /disc/grandleon-mipsync.cue \
    >"$log" 2>&1
status=$?
set -e
test "$status" -eq 0 || { tail -n 80 "$log" >&2; exit "$status"; }

# A review capture validates the actual selected scene and records pacing;
# the strict scrolling performance regression remains a separate test.
python3 - "$manifest" "$output" <<'PYVERIFY'
import json
from pathlib import Path
import re
import sys
scene = json.loads(Path(sys.argv[1]).read_text())
output = Path(sys.argv[2])
log = (output / 'emulator.log').read_text()
expected = f"MIPSYNC_SIM_READY width={scene['width']} height={scene['height']} units={len(scene['units'])}"
identity = f"players={scene['local_players']} hash={int(scene['canonical_hash']):016x}"
if expected not in log or identity not in log or 'GRANDLEON_PS1_RENDERER=MIPSYNC' not in log:
    raise SystemExit('capture does not match the selected real campaign scene')
if 'MIPSYNC_SCROLL_CAPTURE 320x240 bytes=153600' not in log or (output / 'mipsync-scroll-final.bin').stat().st_size != 153600:
    raise SystemExit('capture did not produce a complete framebuffer')
metrics = {k: int(v) for k, v in re.findall(r'(frames|rendered_frames|scheduled_frames|deferred_frames|missed_vblanks|max_elapsed_vblanks|render_hbl_avg|render_hbl_max)=(\d+)', log)}
if metrics.get('frames') != 600:
    raise SystemExit('capture did not finish its measurement interval')
report = {'schema':'grandleon.scene-review.v1','scene':str(Path(sys.argv[1]).resolve()),'canonical_hash':scene['canonical_hash'],'capture_verified':True,'metrics':metrics,'scope':'Emulator visual review. Frame-budget misses are recorded, not treated as a passed performance test.'}
(output / 'review.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
PYVERIFY
