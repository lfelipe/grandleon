#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Fails unless the linked executable leaves the console a heap to run in, and
# says how much it left.
#
# This machine has no virtual memory and no loader that can say no. The BIOS
# shell copies the image to its load address, jumps into it, and whatever is
# above the last byte of `.bss` is the heap: `psx_runtime.cpp` takes
# `__heap_base` to `__sp` less the stack reserve and hands out blocks from it.
# There is nowhere for that arithmetic to report a problem. An image whose
# `.bss` ends *above* the stack computes a negative extent in unsigned
# arithmetic, gets a heap of nearly four gigabytes, and hands out pointers into
# the stack it is standing on. Nothing fails; things stop being true.
#
# So the size an image may be is refused here rather than written down
# anywhere, and it is derived rather than declared: every one of the three
# numbers comes out of the image itself. `__heap_base` is where the linker put
# the end of `.bss`, `__sp` is the stack the linker script places, and the
# reserve is the one number this file states, because `psx_runtime.cpp` states
# it and the two must agree.
#
# What this does *not* claim is that an image which passes will run. A heap of
# one kilobyte is a heap, and a campaign that wants a hundred will fail at the
# first allocation with the runtime's own refusal. There is no measured floor
# to compare against and inventing one would be worse than reporting the
# number: the remaining heap is printed on every build, so an image with an
# uncomfortable margin says so out loud long before it stops fitting.
#
# Invoked from platform/playstation/CMakeLists.txt as a post-build step, so it
# runs inside the toolchain container where readelf lives.
#
#   check-heap-room.sh <readelf> <executable>
#   check-heap-room.sh --self-test
#
# The self-test is the negative control, and it is why this check is worth
# having at all: a check that has never been seen to fail says nothing, and
# this one passes on every executable in the repository. It stands in a
# readelf that reports an image ending above the stack and requires the refusal,
# then one that reports an image ending below it and requires the pass. It needs
# no toolchain and no executable, and `build-playstation.sh` runs it before it
# builds anything.

set -euo pipefail

if [ "${1:-}" = "--self-test" ]; then
    scratch="$(mktemp -d)"
    trap 'rm -rf "${scratch}"' EXIT
    stub_readelf() {
        printf '#!/bin/sh\n' > "${scratch}/readelf"
        printf 'cat <<TABLE\n' >> "${scratch}/readelf"
        printf '   810: %s     0 NOTYPE  GLOBAL DEFAULT  ABS __heap_base\n' \
            "$1" >> "${scratch}/readelf"
        printf '  1194: 801fff00     0 NOTYPE  GLOBAL DEFAULT  ABS __sp\n' \
            >> "${scratch}/readelf"
        printf 'TABLE\n' >> "${scratch}/readelf"
        chmod +x "${scratch}/readelf"
    }

    # An image whose .bss ends one byte above the stack reserve. This is the
    # failure the check exists for, and it has to be caught.
    stub_readelf 801f7f01
    if "$0" "${scratch}/readelf" "${scratch}/image.elf" >/dev/null 2>&1; then
        echo "error: the heap check accepted an image that overruns the stack." >&2
        exit 1
    fi
    # And one byte below it, which has to be accepted, so that the check above
    # is measuring the boundary rather than refusing everything.
    stub_readelf 801f7eff
    if ! "$0" "${scratch}/readelf" "${scratch}/image.elf" >/dev/null 2>&1; then
        echo "error: the heap check refused an image that fits." >&2
        exit 1
    fi
    # And an image the linker never placed, which is not a small heap but an
    # unanswerable question.
    printf '#!/bin/sh\nexit 0\n' > "${scratch}/readelf"
    chmod +x "${scratch}/readelf"
    if "$0" "${scratch}/readelf" "${scratch}/image.elf" >/dev/null 2>&1; then
        echo "error: the heap check accepted an image with no __heap_base." >&2
        exit 1
    fi
    echo "Heap check self-test: refuses an overrun, accepts the byte below it."
    exit 0
fi

if [ "$#" -ne 2 ]; then
    echo "usage: $(basename "$0") <readelf> <executable>" >&2
    exit 2
fi
readelf="$1"
executable="$2"

# The stack reserve `psx_runtime.cpp` keeps below `__sp`, in bytes. Stated in
# both places because neither can read the other: that file cannot run a shell
# and this one cannot include a header. It is asserted rather than trusted —
# a mismatch is caught by `grandleon.playstation` conformance, which reports
# the heap the runtime actually computed.
stack_reserve=$(( 32 * 1024 ))

address_of() {
    local symbol="$1" value
    value="$(
        "${readelf}" -sW "${executable}" \
            | awk -v want="${symbol}" '$8 == want { print $2; exit }'
    )"
    if [ -z "${value}" ]; then
        echo "error: ${executable} declares no ${symbol}." >&2
        echo "The linker script is supposed to place it; without it there is" >&2
        echo "no way to say where this image ends or where the stack is." >&2
        exit 1
    fi
    printf '%s\n' "$(( 0x${value} ))"
}

heap_base="$(address_of __heap_base)"
stack_top="$(address_of __sp)"
heap_limit=$(( stack_top - stack_reserve ))

if [ "${heap_base}" -ge "${heap_limit}" ]; then
    echo "error: ${executable##*/} leaves no heap." >&2
    printf '  image ends at   0x%08x\n' "${heap_base}" >&2
    printf '  heap must end by 0x%08x (stack 0x%08x less %d reserved)\n' \
        "${heap_limit}" "${stack_top}" "${stack_reserve}" >&2
    echo >&2
    echo "This image is larger than the console's main RAM holds. The" >&2
    echo "PlayStation compiles nothing on the machine: everything the game" >&2
    echo "is, package bytes and art included, is in this file, so what has" >&2
    echo "to shrink is the content." >&2
    exit 1
fi

printf 'Heap check: %s leaves %d bytes of heap (image ends 0x%08x)\n' \
    "${executable##*/}" "$(( heap_limit - heap_base ))" "${heap_base}"
