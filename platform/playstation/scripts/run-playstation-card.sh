#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Proves the PlayStation save path against a memory card, across processes.
#
# Invoked through the `grandleon_playstation_card_check` CMake target and by the
# `grandleon.playstation_card` test. It can also be run directly from the
# repository root, after platform/playstation/scripts/build-playstation.sh has
# produced an executable.
#
#   platform/playstation/scripts/run-playstation-card.sh
#   platform/playstation/scripts/run-playstation-card.sh --rebuild-image
#
# ---------------------------------------------------------------------------
# Why five passes, and why two of them are separate processes over one image
#
# A save is the one claim a console cannot make from inside a single run: an
# executable that writes bytes and reads them back has proved a buffer, and a
# buffer is not a memory card. So the emulator is started five times, and the
# card image on disk is the only thing that survives between them.
#
#   1. an unformatted card    the game must refuse it and write *nothing*. The
#                             image's digest is taken before and after and must
#                             be the same byte for byte. Formatting somebody's
#                             card because this game did not recognise it is
#                             the worst thing this code could do, and this is
#                             the check that says it does not.
#   2. a formatted blank card the game writes its file (a real card file, with
#                             a directory entry, a title and an icon), fills
#                             every slot, and then reads the region back off
#                             the card over the top of the copy it wrote from.
#   3. the same card again    a *different process*, with nothing of the second
#                             one's memory left. It must find its own file in
#                             the card's directory, write not one frame to find
#                             it, and read back every byte.
#   4. a card whose chain     a formatted card holding a file under this game's
#      runs into another      name whose block chain runs on into a block
#      game's file            another game's file starts in. Adopting it and
#                             committing would erase somebody else's save, so
#                             the file is disowned and the card is left alone,
#                             checked on the digest exactly as pass one is.
#   5. a card whose only      a full card whose last entry says it is free and
#      free entry is          does not match its own checksum. An entry the
#      damaged                card cannot vouch for may still describe a file,
#                             so it is not a block to hand out, and the answer
#                             is that the card is full. Digested the same way.
#
# `docs/ARES_VALIDATION.md` imposes exactly this obligation on the Nintendo 64's
# cartridge and it is not weaker here.
#
# Every pass is read on two channels that must agree: the process exit code and
# the `RESULT PASS n/n` line on the teletype. The reason is the one
# run-playstation.sh gives: an exit code with no report is a channel that has
# stopped working rather than a pass. And each pass is read on the count in that
# line as well as the word in it, against a floor of its own: the three assert
# very different amounts, and `RESULT PASS 0/0` is what an emptied assertion
# table prints.

set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
revision="${GRANDLEON_PCSX_REDUX_REVISION:-}"
base_image="${GRANDLEON_PCSX_REDUX_BUILD_IMAGE:-ghcr.io/grumpycoders/pcsx-redux-build}"
base_digest="${GRANDLEON_PCSX_REDUX_BUILD_DIGEST:-}"
docker="${GRANDLEON_DOCKER:-docker}"
build_dir="${GRANDLEON_PLAYSTATION_BUILD_DIR:-${repository_root}/build-playstation}"
executable_name="grandleon_psx_card.ps-exe"
# Three passes of a few seconds each under the interpreter. This is a hang
# detector, not a budget.
timeout_seconds="${GRANDLEON_PLAYSTATION_TIMEOUT:-600}"

for name in revision base_digest; do
    if [ -z "${!name}" ]; then
        echo "error: ${name} is not set." >&2
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

executable="${build_dir}/target/platform/playstation/${executable_name}"
if [ ! -f "${executable}" ]; then
    echo "error: ${executable} does not exist." >&2
    echo "Build it first: cmake --build build --target grandleon_playstation" >&2
    exit 1
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

# The negative control over the thing that decides all three passes, and it runs
# before the emulator rather than being a note somebody could stop believing.
# Each pass is judged on a line in a log and the arithmetic in it, so the script
# that reads that line is first pointed at a battery of logs that measure
# nothing (absent, silent, `0/0`, partial, under the floor, countless, and a
# diagnostic that merely echoes the verdict) and required to refuse every one.
# It costs no emulator and about a hundredth of a second.
echo "== Proving the verdict check can still fail =="
"${repository_root}/scripts/assert-harness-verdict.sh" --self-test

cards="${build_dir}/cards"
rm -rf "${cards}"
mkdir -p "${cards}"

# An unformatted card is 128 KiB of nothing. PCSX-Redux writes a formatted one
# when the file does not exist, which is why this pass supplies its own: what is
# being checked is what the game does with a card it cannot read, and a card the
# emulator formatted for it is not that.
: > "${cards}/unformatted.mcd"
head -c 131072 /dev/zero >> "${cards}/unformatted.mcd"
before_digest="$(md5sum < "${cards}/unformatted.mcd")"

# One pass. `$1` is the card image inside the container, `$2` the log's name,
# `$3` a sentence the log must contain besides the verdict, and `$4` the number
# of assertions that pass has to reach.
#
# The floor is a parameter rather than one number for the three because the
# three passes assert wildly different amounts: a pass that refuses a card it
# cannot read has almost nothing to say, and the pass that writes one says the
# most. One floor for all three would be the smallest of them, and would let the
# two larger passes lose most of their table and still be green.
run_pass() {
    local card="$1"
    local label="$2"
    local must_say="$3"
    local min_checks="$4"
    local log="${build_dir}/emulator-card-${label}.log"
    local status=0

    set +e
    timeout --kill-after=10 "${timeout_seconds}" \
        "${docker}" run --rm \
        --user "$(id -u):$(id -g)" \
        --env HOME=/tmp \
        --volume "${build_dir}/target/platform/playstation:/exe:ro" \
        --volume "${cards}:/cards" \
        --workdir /tmp \
        "${image}" \
        stdbuf -oL -eL pcsx-redux -no-ui -run \
            -bios /usr/local/share/pcsx-redux/openbios.bin \
            -testmode -interpreter \
            -memcard1 "/cards/${card}" \
            -loadexe "/exe/${executable_name}" \
        > "${log}" 2>&1
    status=$?
    set -e

    if [ "${status}" -eq 124 ] || [ "${status}" -eq 137 ]; then
        echo "error: the ${label} pass did not finish in ${timeout_seconds}s." >&2
        tail -n 40 "${log}" >&2 || true
        exit 1
    fi
    # The verdict, the count behind it, and an anchor. `RESULT PASS 0/0` carries
    # the word and asserts nothing, and an unanchored match is also satisfied by
    # a diagnostic line that merely names the verdict. The decision is made by
    # the one script every console here shares rather than by a `grep` written
    # again per caller.
    local verdict
    if ! verdict="$(
            "${repository_root}/scripts/assert-harness-verdict.sh" \
                --log "${log}" --require "^RESULT PASS " \
                --min-checks "${min_checks}" --what "the ${label} pass"
        )"; then
        echo "PCSX-Redux exited ${status}. Log: ${log}" >&2
        tail -n 40 "${log}" >&2 || echo "  (no output at all)" >&2
        exit 1
    fi
    if [ "${status}" -ne 0 ]; then
        echo "error: the ${label} pass says pass and the exit code is ${status}." >&2
        echo "The two channels disagree; log: ${log}" >&2
        exit 1
    fi
    if ! grep -q "${must_say}" "${log}"; then
        echo "error: the ${label} pass passed without doing what it is for." >&2
        echo "  expected the log to say: ${must_say}" >&2
        echo "Log: ${log}" >&2
        tail -n 40 "${log}" >&2 || true
        exit 1
    fi
    printf '%s\n' "${verdict}"
}

echo "== a card this game cannot read"
# Three: the control port answered, the card was refused without a frame being
# written to it, and it was refused on its header alone. There is nothing else
# to ask of a card the game will not touch, which is why this is the smallest
# floor here and why it is still worth stating: a pass that asserted two of
# those three refused a card for the wrong reason or wrote to it on the way.
run_pass unformatted.mcd unformatted "CARD REFUSED unformatted" 3
after_digest="$(md5sum < "${cards}/unformatted.mcd")"
if [ "${before_digest}" != "${after_digest}" ]; then
    echo "error: the game changed a card it said it had refused." >&2
    echo "  before: ${before_digest}" >&2
    echo "  after:  ${after_digest}" >&2
    exit 1
fi
echo "   the card is byte for byte what it was"

echo "== a blank card"
# Thirty, and the pass that says the most: the card answers and offers its whole
# region, the budget holds every slot the save menu offers, the card had no file
# of this game's on it, each of the four slots is taken, each reads back byte for
# byte, the card answers again after a reload, and the region read off the card
# parses and holds every byte written into it. Most of that total scales with
# the four slots, so a run that wrote one slot and called it a save cannot reach
# it.
run_pass campaign.mcd fresh "CARD wrote a fresh card" 30
if [ ! -s "${cards}/campaign.mcd" ]; then
    echo "error: no card image was written." >&2
    exit 1
fi
written_digest="$(md5sum < "${cards}/campaign.mcd")"

echo "== the same card, a new process"
# Seventeen: everything the writing pass asserts about the device and the four
# slots' contents, less the writing. The file is found in the card's own
# directory, nothing is written to find it, every byte survived the power cycle,
# and each of the four slots reads back byte for byte.
run_pass campaign.mcd resumed "CARD read a card written before this process started" 17
resumed_digest="$(md5sum < "${cards}/campaign.mcd")"
if [ "${written_digest}" != "${resumed_digest}" ]; then
    echo "error: a run that only read the card changed it." >&2
    echo "  after writing: ${written_digest}" >&2
    echo "  after reading: ${resumed_digest}" >&2
    exit 1
fi
echo "   the card is byte for byte what the run before wrote"

# A card the game must read, refuse, and leave alone. `$1` is which one
# write-foreign-card.py builds, `$2` the fault the log has to name.
#
# The images are built here rather than checked in: a hand-made 128 KiB binary
# is a fixture nobody can read, and the generator says in words what each of
# its frames is for.
#
# Four assertions each: the control port answered, not one frame was written,
# the refusal came from the card's directory rather than from the wire, and the
# whole directory was read to decide it. Which of the two directory refusals it
# was is pinned by the sentence, not by the count.
refuse_pass() {
    local kind="$1"
    local fault="$2"
    "${repository_root}/platform/playstation/scripts/write-foreign-card.py" \
        "${kind}" "${cards}/${kind}.mcd"
    local before after
    before="$(md5sum < "${cards}/${kind}.mcd")"
    run_pass "${kind}.mcd" "${kind}" "CARD REFUSED ${fault}" 4
    after="$(md5sum < "${cards}/${kind}.mcd")"
    if [ "${before}" != "${after}" ]; then
        echo "error: the game wrote to a card it said it had refused." >&2
        echo "  before: ${before}" >&2
        echo "  after:  ${after}" >&2
        exit 1
    fi
    echo "   the other game's blocks are byte for byte what they were"
}

echo "== a card whose chain runs into another game's file"
refuse_pass chained foreign-file

echo "== a card whose only free block has a damaged entry"
refuse_pass damaged card-full

echo "PlayStation memory card passed. Card images: ${cards}"
