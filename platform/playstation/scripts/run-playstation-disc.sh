#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Boots the disc image and plays the campaign off it.
#
# Invoked through the `grandleon_playstation_disc_check` CMake target. It can
# also be run directly from the repository root, after the expectations have
# been derived and the disc built:
#
#   cmake --build build --target grandleon_playstation_campaign_expectations
#   platform/playstation/scripts/build-playstation.sh
#   platform/playstation/scripts/build-disc.sh
#   platform/playstation/scripts/run-playstation-disc.sh
#
# ---------------------------------------------------------------------------
# Why this check exists and what it is not
#
# Every other PlayStation run injects the executable with `-loadexe`, which
# hijacks the BIOS shell at 0x80030000 and drops the image straight into RAM. A
# disc goes the other way round: the shell runs, reads SYSTEM.CNF off the
# filesystem, finds MAIN.EXE, loads it sector by sector and jumps to it. No
# other check on this console exercises one byte of that, so a disc that builds
# and does not boot would look finished.
#
# So this is a real boot, and it is held to exactly the standard the injected
# run is held to: the same emulator image, the same Lua observer, the same
# joiner, the same host-derived expectations, the same assertion floors. It
# plays the campaign the disc carries: founding it, then picking it up in a
# second process. Each boot must reach the same transcript line for line and
# the same claimed pixel at every checkpoint. That is a stronger statement
# than "the first frame matches", and it is the same statement
# `grandleon_playstation_campaign_check` makes about the executable, so the two
# answers are comparable by construction.
#
# It is not a second campaign check. It says nothing new about the rules or the
# renderer; what it says is that the boot path in front of them works, and that
# a disc and a memory card do not interfere with each other.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
disc_name="${GRANDLEON_PLAYSTATION_DISC_NAME:-grandleon}"
# Which campaign the disc carries, and therefore which host derivation this run
# is compared against. It has to agree with the executable
# `platform/playstation/scripts/build-disc.sh` put on the image.
campaign="${GRANDLEON_PLAYSTATION_DISC_CAMPAIGN:-tarnholt_line}"
# A whole campaign under the interpreter, plus the BIOS shell reading a
# three-quarter-megabyte executable off an emulated drive. A hang detector
# rather than a budget.
timeout_seconds="${GRANDLEON_PLAYSTATION_DISC_TIMEOUT:-3000}"
# The assertion floor, and it is deliberately the number
# `run-playstation-campaign.sh` holds the *injected* founding pass to rather
# than a second number derived here. The two runs are the same choreography:
# the same executable, the same empty card, the same script. They therefore
# reach the same count, and holding them to different floors would make two
# answers about one campaign incomparable for no gain.
#
# The floor is only the guard against `HARNESS RESULT PASS 0/0`. What actually
# pins this run is the joiner below, which requires every line of the host's
# derivation to have been reported and every claimed pixel to match; a disc that
# booted and stopped a third of the way through fails there, on the missing
# lines, long before a count could let it through.
min_checks="${GRANDLEON_PLAYSTATION_DISC_MIN_CHECKS:-270}"
resume_min_checks="${GRANDLEON_PLAYSTATION_DISC_RESUME_MIN_CHECKS:-27}"

for name_of in revision base_digest; do
    if [ -z "${!name_of}" ]; then
        echo "error: ${name_of} is not set." >&2
        exit 1
    fi
done
image="grandleon/playstation-emulator:${revision}"

rebuild_image=0
if [ "${1:-}" = "--rebuild-image" ]; then
    rebuild_image=1
elif [ "$#" -gt 0 ]; then
    echo "usage: $(basename "$0") [--rebuild-image]" >&2
    exit 2
fi

if ! command -v "${docker}" >/dev/null 2>&1; then
    echo "error: '${docker}' is not on PATH." >&2
    exit 1
fi

pinned_base="${base_image}@${base_digest}"
if [ "${rebuild_image}" -eq 1 ] || ! "${docker}" image inspect "${image}" >/dev/null 2>&1; then
    if ! "${docker}" image inspect "${pinned_base}" >/dev/null 2>&1; then
        echo "Pulling ${pinned_base}…" >&2
        "${docker}" pull "${pinned_base}"
    fi
    echo "Building ${image} from platform/playstation/emulator/Containerfile…" >&2
    "${docker}" build \
        --file "${repository_root}/platform/playstation/emulator/Containerfile" \
        --build-arg "PCSX_REDUX_REVISION=${revision}" \
        --tag "${image}" \
        "${repository_root}/platform/playstation/emulator"
fi

installed_revision="$("${docker}" run --rm "${image}" cat /usr/local/PCSX_REDUX_REVISION)"
installed_revision="${installed_revision//[$'\r\n']/}"
if [ "${installed_revision}" != "${revision}" ]; then
    echo "error: ${image} was built from a different PCSX-Redux revision." >&2
    echo "  expected: ${revision}" >&2
    echo "  actual:   ${installed_revision}" >&2
    echo "Re-run with --rebuild-image." >&2
    exit 1
fi

# The negative control over the thing that decides this run, before the
# emulator, as every other entry point on this console carries it.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test

disc_dir="${build_dir}/disc"
bin="${disc_dir}/${disc_name}.bin"
cue="${disc_dir}/${disc_name}.cue"
for artifact in "${bin}" "${cue}"; do
    if [ ! -f "${artifact}" ]; then
        echo "error: ${artifact} does not exist." >&2
        echo "Build it first: platform/playstation/scripts/build-disc.sh" >&2
        exit 1
    fi
done

for mode in found resume; do
    if [ ! -f "${build_dir}/${campaign}_${mode}.txt" ]; then
        echo "error: ${build_dir}/${campaign}_${mode}.txt does not exist." >&2
        echo "Derive it first:" >&2
        echo "    cmake --build build --target grandleon_playstation_campaign_expectations" >&2
        exit 1
    fi
done

# The same joiner the injected campaign run uses, built the same way and for the
# same reason: it links nothing from this repository, so it cannot agree with
# the executable by sharing a mistake.
joiner="${build_dir}/playstation_turn_probe"
if [ ! -x "${joiner}" ] || \
   [ "${repository_root}/platform/playstation/harness/playstation_turn_probe.c" -nt "${joiner}" ]; then
    echo "Building the joiner…" >&2
    cc -std=c11 -O2 -Wall -Wextra -Werror \
        -o "${joiner}" \
        "${repository_root}/platform/playstation/harness/playstation_turn_probe.c"
fi

# One card, empty to start with, and two boots over it: the arrangement
# `run-playstation-campaign.sh` uses, for the reason it gives and for one more
# that belongs to this check.
#
# A disc and a memory card have never met before this target existed. Nothing
# about a disc should touch the save path: the card is SIO0 and the drive is
# not. But "should" is what a check is for, and founding a campaign off the
# disc and then picking it up off the disc in a second emulator process is what
# turns that into an answer. Which script the executable plays is decided by
# what the card holds, so the resuming boot reaches screens the founding boot's
# expectations do not contain: a card the disc boot failed to write would replay
# the founding script and fail on the first line.
cards="${build_dir}/disc-cards"
rm -rf "${cards}"
mkdir -p "${cards}"

run_pass() {
    local mode="$1"
    local frame_dir="${build_dir}/disc-frames-${mode}"
    local log="${build_dir}/disc-${campaign}-${mode}.log"
    local floor="$2"
    local status=0

    rm -rf "${frame_dir}"
    mkdir -p "${frame_dir}"

    # The invocation, and the one difference from every other run on this
    # console: `-iso` in place of `-loadexe`. The BIOS still boots, and `-bios`
    # and `-run` are as mandatory here as there. This time it boots the way a
    # console does, off the disc in the drive.
    #
    # The cue sheet is handed over rather than the bin, because the cue is the
    # disc and the bin is one track of it. It is also the file a burning program
    # is given, so pointing the emulator at it means the emulator read the thing
    # that would be burned.
    set +e
    timeout --kill-after=10 "${timeout_seconds}" \
        "${docker}" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --env GRANDLEON_PLAYSTATION_FRAME_DIR=/out \
        --volume "${disc_dir}:/disc:ro" \
        --volume "${repository_root}/platform/playstation/harness:/harness:ro" \
        --volume "${frame_dir}:/out" \
        --volume "${cards}:/cards" \
        --workdir /tmp \
        "${image}" \
        stdbuf -oL -eL pcsx-redux -no-ui -run -softgpu \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter \
            -memcard1 "/cards/${campaign}.mcd" \
            -dofile /harness/playstation_turn_probe.lua \
            -iso "/disc/${disc_name}.cue" \
        > "${log}" 2>&1
    status=$?
    set -e

    if [ "${status}" -eq 124 ] || [ "${status}" -eq 137 ]; then
        echo "error: the ${mode} boot did not finish in ${timeout_seconds}s." >&2
        tail -n 40 "${log}" >&2 || true
        exit 1
    fi

    local verdict
    if ! verdict="$(
            "${repository_root}/scripts/assert-harness-verdict.sh" \
                --log "${log}" --require "^HARNESS RESULT PASS " \
                --min-checks "${floor}" \
                --what "the observer on the ${campaign} ${mode} disc boot"
        )"; then
        echo "The emulator exited ${status}. Log: ${log}" >&2
        grep "^HARNESS CHECK .* FAIL" "${log}" >&2 \
            || echo "  (the observer never ran; did the disc boot at all?)" >&2
        exit 1
    fi

    if [ "${status}" -ne 0 ]; then
        echo "error: the observer says pass and the exit code is ${status}." >&2
        echo "The two channels disagree; log: ${log}" >&2
        exit 1
    fi

    # The executable chooses its script from what the card holds, so which one
    # it played is checked rather than inferred from the comparison passing.
    if ! grep -q "^SCRIPT ${mode} " "${log}"; then
        echo "error: the ${mode} boot did not play the ${mode} script." >&2
        grep -m1 "^SCRIPT " "${log}" >&2 || echo "  (it named no script)" >&2
        exit 1
    fi

    if ! "${joiner}" "${log}" "${build_dir}/${campaign}_${mode}.txt" "${frame_dir}"; then
        echo "error: what the ${mode} boot played is not what the rules say." >&2
        echo "Log: ${log}" >&2
        exit 1
    fi

    printf '%s\n' "${verdict}"
    echo "   frames: $(find "${frame_dir}" -name 'frame-*.bin' | wc -l)"
}

started="${SECONDS}"

echo "== ${campaign}: booting the disc onto an empty card"
run_pass found "${min_checks}"
if [ ! -s "${cards}/${campaign}.mcd" ]; then
    echo "error: the disc boot wrote no card image." >&2
    exit 1
fi
written="$(md5sum < "${cards}/${campaign}.mcd")"

echo "== ${campaign}: booting the same disc again, over the card it wrote"
run_pass resume "${resume_min_checks}"
if [ "${written}" != "$(md5sum < "${cards}/${campaign}.mcd")" ]; then
    echo "error: a boot that only resumed rewrote the card." >&2
    exit 1
fi

echo "The campaign booted off ${disc_name}.cue twice and picked itself up again."
echo "   the card is byte for byte what the founding boot wrote"
echo "   wall clock: $(( SECONDS - started ))s"
