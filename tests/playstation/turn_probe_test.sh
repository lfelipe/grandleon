#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# The turn probe's own memory safety, and the one refusal that provides it.
#
# `playstation_turn_probe` reads three files a machine other than this one
# produced, and the largest of them is an emulator's stdout. It allocates one
# frame buffer, sized from the frame dimensions the log reports, and every
# checkpoint's pixels are read into it. Those dimensions therefore have to be
# settled before the buffer exists and fixed for as long as it does. So `main`
# reads the whole log for them before comparing anything, and a later line
# naming a different size is refused rather than adopted. A probe
# that adopted it would read the new size into the old allocation: two frame
# sizes one octave apart in a log is a heap write of several hundred kilobytes,
# chosen by the emulator's output.
#
# This runs against the same host compiler and the same source the run scripts
# build, and it needs no emulator, no container and no ROM, so it belongs in
# the native gate rather than behind the PlayStation flag.
set -euo pipefail

source_file="$1"
work="$(mktemp -d)"
trap 'rm -rf "${work}"' EXIT

compiler="${CC:-cc}"
flags=(-std=c11 -O1 -g -Wall -Wextra -Werror)

# The sanitizer is the belt to the refusal's braces: the refusal is what makes
# the overflow unreachable, and this is what would notice if it stopped being.
# Not every host has it, so the behavioural half below runs either way and the
# run says which halves it did.
probe="${work}/probe"
"${compiler}" "${flags[@]}" -o "${probe}" "${source_file}"
sanitized="${probe}"
sanitizer="no"
if "${compiler}" "${flags[@]}" -fsanitize=address \
        -o "${work}/probe-asan" "${source_file}" 2>/dev/null; then
    sanitized="${work}/probe-asan"
    sanitizer="yes"
fi
echo "compiler ${compiler}, address sanitizer ${sanitizer}"

failures=0
check() {
    if [ "$1" != "0" ]; then
        echo "FAIL: $2"
        failures=$((failures + 1))
    fi
}

# ---------------------------------------------------------------------------
# A log that contradicts itself about the frame size.
#
# The first size is small enough to allocate a buffer no full frame fits in;
# the second is the one `main` settles on. An implementation that let `handle`
# reassign the dimensions reads 320x240 into an 8x8 allocation.
# ---------------------------------------------------------------------------
hostile="${work}/hostile"
mkdir -p "${hostile}/frames"
cat >"${hostile}/log.txt" <<'LOG'
CHECKPOINT one
PROBE-COUNT 0
HARNESS FRAMES 2 8x8
CHECKPOINT two
PROBE-COUNT 0
HARNESS FRAMES 2 320x240
LOG
cat >"${hostile}/expect.txt" <<'EXPECT'
SCRIPT
PRESS a
TRANSCRIPT
CHECKPOINT one
CHECKPOINT two
EXPECT
head -c 128 /dev/zero >"${hostile}/frames/frame-0000.bin"
head -c 153600 /dev/zero >"${hostile}/frames/frame-0001.bin"

set +e
hostile_out="$("${sanitized}" "${hostile}/log.txt" "${hostile}/expect.txt" \
    "${hostile}/frames" 2>&1)"
hostile_status=$?
set -e

case "${hostile_out}" in
    *AddressSanitizer*)
        echo "${hostile_out}"
        check 1 "the probe reads a contradictory log without corrupting its heap"
        ;;
    *)
        check 0 "the probe reads a contradictory log without corrupting its heap"
        ;;
esac
case "${hostile_out}" in
    *"reports one frame size"*)
        check 0 "and says the harness contradicted itself about the frame size"
        ;;
    *)
        echo "${hostile_out}"
        check 1 "and says the harness contradicted itself about the frame size"
        ;;
esac
if [ "${hostile_status}" -ne 0 ]; then
    check 0 "and fails the run rather than reporting a verdict over it"
else
    check 1 "and fails the run rather than reporting a verdict over it"
fi

# ---------------------------------------------------------------------------
# The shape a real run has: one checkpoint, one probe, one frame, one
# `HARNESS FRAMES` line. It has to still pass, or the refusal above has been
# bought by breaking the gate it protects.
#
# The pixel is red 1, green 2, blue 3 in the console's five bits per channel:
# 1 | (2 << 5) | (3 << 10) = 0x0C41, little-endian.
# ---------------------------------------------------------------------------
good="${work}/good"
mkdir -p "${good}/frames"
cat >"${good}/log.txt" <<'LOG'
LAYOUT script 1
SESSION presses 1
CHECKPOINT one
FACT the console settled
PROBE corner 0 0 1 2 3
READBACK corner 1 2 3
PROBE-COUNT 1
HARNESS FRAMES 1 2x2
HARNESS RESULT PASS
RESULT PASS
LOG
cat >"${good}/expect.txt" <<'EXPECT'
SCRIPT
PRESS a
TRANSCRIPT
CHECKPOINT one
FACT the console settled
EXPECT
printf '\x41\x0c\x00\x00\x00\x00\x00\x00' >"${good}/frames/frame-0000.bin"

set +e
good_out="$("${sanitized}" "${good}/log.txt" "${good}/expect.txt" \
    "${good}/frames" 2>&1)"
good_status=$?
set -e

[ "${good_status}" -eq 0 ]
check $? "a well-formed run still passes"
case "${good_out}" in
    *"RESULT PASS"*)
        check 0 "and reports the verdict it always did"
        ;;
    *)
        echo "${good_out}"
        check 1 "and reports the verdict it always did"
        ;;
esac
# The one `HARNESS FRAMES` line a real harness prints must add no check to the
# tally, or every count this probe reports would have moved.
case "${good_out}" in
    *"reports one frame size"*)
        echo "${good_out}"
        check 1 "and does not complain about the single frame size it was given"
        ;;
    *)
        check 0 "and does not complain about the single frame size it was given"
        ;;
esac

if [ "${failures}" -ne 0 ]; then
    echo "${failures} failed"
    exit 1
fi
echo "the turn probe bounds its frame buffer"
