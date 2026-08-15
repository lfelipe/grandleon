#!/usr/bin/env bash
# SPDX-License-Identifier: MIT

# Fails unless every instruction in the linked executable is one an R3000A can
# execute.
#
# This exists because the failure it catches is silent. The pinned toolchain
# image's prebuilt libraries (`libgcc.a`, and `libstdc++.a`'s `tree.o`) have
# exactly one multilib each, and it is built for the toolchain's default ISA
# rather than for MIPS-I. They link without a word of complaint and then
# execute `clz`, `mul` and `movn`, which the R3000A does not implement. The CPU
# takes a Reserved Instruction exception, the BIOS handler returns, the result
# register still holds whatever it held before, and the caller proceeds with a
# wrong answer. Under PCSX-Redux the only sign is one line among hundreds of
# lines of report: "Encountered reserved opcode …, firing an exception". The run
# can still print `RESULT PASS`.
#
# The check is the ELF header rather than a scan of the disassembly, because
# the header is authoritative and a denylist of mnemonics never is. GNU ld
# merges the ISA level of every input object into `e_flags`, taking the
# highest, so a single mips32r2 object anywhere in the link raises the whole
# executable and this notices.
#
# Invoked from platform/playstation/CMakeLists.txt as a post-build step, so it
# runs inside the toolchain container where readelf lives.
#
#   check-mips1.sh <readelf> <executable>

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $(basename "$0") <readelf> <executable>" >&2
    exit 2
fi
readelf="$1"
executable="$2"

flags="$("${readelf}" -h "${executable}" | sed -n 's/^  Flags: *//p')"
if [ -z "${flags}" ]; then
    echo "error: could not read ELF flags from ${executable}." >&2
    exit 1
fi

case ",${flags// /}," in
    *,mips1,*) ;;
    *)
        echo "error: ${executable} is not a MIPS-I image." >&2
        echo "  flags: ${flags}" >&2
        echo >&2
        echo "Something in the link was built for a later MIPS ISA than the" >&2
        echo "R3000A implements. It will raise Reserved Instruction on this" >&2
        echo "console, the BIOS handler will return, and the caller will read" >&2
        echo "a stale register as the result." >&2
        echo >&2
        echo "To find it, run readelf -h over the link's inputs and look for" >&2
        echo "one that is not 'mips1'. The usual culprits are prebuilt" >&2
        echo "archives from the toolchain image rather than anything this" >&2
        echo "repository compiles." >&2
        exit 1
        ;;
esac

echo "ISA check: ${executable##*/} is mips1 (${flags})"
