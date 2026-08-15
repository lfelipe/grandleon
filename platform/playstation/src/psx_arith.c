// SPDX-License-Identifier: MIT
/* 64-bit arithmetic for a MIPS-I processor.
 *
 * These are the compiler helper routines GCC emits calls to when a 32-bit
 * target has to shift or divide a 64-bit value. They normally come from
 * libgcc, and on this target they cannot.
 *
 * The pinned image's `libgcc.a` for `mipsel-linux-gnu` has exactly one
 * multilib, and it is built for the toolchain's default ISA rather than for
 * MIPS-I: `mipsel-linux-gnu-readelf -h` on its members reports `mips32r2`.
 * `__clzsi2` is a single `clz`, and `__umoddi3` uses `clz` and `mul`. The
 * R3000A implements neither, so calling into libgcc raises Reserved
 * Instruction, which PCSX-Redux reports as "Encountered reserved opcode…,
 * firing an exception". The handler returns, the result register holds
 * whatever it held before, and the caller carries on with a wrong answer.
 * That is a silent-wrong-answer failure mode, not a crash, which is why the
 * build asserts the linked executable's ISA rather than trusting the link to
 * have been clean.
 *
 * Nugget never hits this because a `-nostdlib` link does not pull libgcc in
 * automatically and Nugget's own examples need none of it. An engine that
 * shifts and divides 64-bit values does.
 *
 * So this file replaces libgcc entirely for this target: it is compiled
 * `-march=mips1` like everything else here, and `platform/playstation`'s link
 * does not pass `-lgcc`. The implementations are written from the operations'
 * definitions rather than derived from anyone's source.
 *
 * It is a short file because multiplication is not in it: GCC expands the
 * engine's 64x64 FNV multiply inline over the hardware MULTU, so what is left
 * is the shifting and the division.
 */

typedef unsigned int u32;
typedef unsigned long long u64;

/* The machine is little-endian, which is asserted on target by the
 * conformance executable before anything that depends on it runs. Halving a
 * 64-bit value through a union rather than through shifts is deliberate: a
 * shift by a variable amount is exactly what these functions implement, and
 * using one here would be a recursive call. */
union parts {
    u64 whole;
    struct {
        u32 low;
        u32 high;
    } halves;
};

/* Leading zero count. The contract libgcc documents leaves a zero argument
 * undefined; returning 32 is the useful answer and costs nothing. */
int __clzsi2(u32 value) {
    int count = 0;
    if (value == 0U) {
        return 32;
    }
    if ((value & 0xffff0000U) == 0U) {
        count += 16;
        value <<= 16;
    }
    if ((value & 0xff000000U) == 0U) {
        count += 8;
        value <<= 8;
    }
    if ((value & 0xf0000000U) == 0U) {
        count += 4;
        value <<= 4;
    }
    if ((value & 0xc0000000U) == 0U) {
        count += 2;
        value <<= 2;
    }
    if ((value & 0x80000000U) == 0U) {
        count += 1;
    }
    return count;
}

/* A shift count of zero has to be handled before the 32-minus-count form is
 * evaluated, because a 32-bit shift by 32 is undefined and MIPS-I in fact
 * shifts by count modulo 32, which would return the operand unchanged in the
 * wrong half. */
u64 __ashldi3(u64 value, int count) {
    union parts in;
    union parts out;
    in.whole = value;
    if (count == 0) {
        return value;
    }
    if (count >= 32) {
        out.halves.low = 0U;
        out.halves.high = in.halves.low << (count - 32);
    } else {
        out.halves.low = in.halves.low << count;
        out.halves.high =
            (in.halves.high << count) | (in.halves.low >> (32 - count));
    }
    return out.whole;
}

u64 __lshrdi3(u64 value, int count) {
    union parts in;
    union parts out;
    in.whole = value;
    if (count == 0) {
        return value;
    }
    if (count >= 32) {
        out.halves.high = 0U;
        out.halves.low = in.halves.high >> (count - 32);
    } else {
        out.halves.high = in.halves.high >> count;
        out.halves.low =
            (in.halves.low >> count) | (in.halves.high << (32 - count));
    }
    return out.whole;
}

long long __ashrdi3(long long value, int count) {
    union parts in;
    union parts out;
    in.whole = (u64)value;
    if (count == 0) {
        return value;
    }
    if (count >= 32) {
        const u32 sign = (u32)((int)in.halves.high >> 31);
        out.halves.high = sign;
        out.halves.low = (u32)((int)in.halves.high >> (count - 32));
    } else {
        out.halves.high = (u32)((int)in.halves.high >> count);
        out.halves.low =
            (in.halves.low >> count) | (in.halves.high << (32 - count));
    }
    return (long long)out.whole;
}

/* Restoring binary long division, with a fast path for the case that covers
 * everything this repository actually does: both operands fitting in 32 bits,
 * where the R3000A's own DIVU answers in a fixed 36 cycles.
 *
 * The slow path is 64 iterations of shift-compare-subtract. That is not fast,
 * and it does not need to be: the engine does not divide 64-bit values today
 * and should not start.
 * The one call site that reaches here is a modulo in
 * grandleon::tactics::decide, once per decision. */
static u64 divide(u64 numerator, u64 denominator, u64* remainder) {
    union parts n;
    union parts d;
    u64 quotient = 0U;
    int bit;

    n.whole = numerator;
    d.whole = denominator;
    if (denominator == 0U) {
        /* Undefined by the language. Returning zero is at least
         * deterministic, which matters more here than being clever. */
        if (remainder != 0) {
            *remainder = 0U;
        }
        return 0U;
    }
    if (n.halves.high == 0U && d.halves.high == 0U) {
        const u32 q = n.halves.low / d.halves.low;
        if (remainder != 0) {
            *remainder = n.halves.low % d.halves.low;
        }
        return q;
    }

    {
        u64 rest = 0U;
        for (bit = 63; bit >= 0; --bit) {
            rest = __ashldi3(rest, 1) | (__lshrdi3(numerator, bit) & 1U);
            if (rest >= denominator) {
                rest -= denominator;
                quotient |= __ashldi3(1U, bit);
            }
        }
        if (remainder != 0) {
            *remainder = rest;
        }
    }
    return quotient;
}

u64 __udivdi3(u64 numerator, u64 denominator) {
    return divide(numerator, denominator, 0);
}

u64 __umoddi3(u64 numerator, u64 denominator) {
    u64 remainder = 0U;
    divide(numerator, denominator, &remainder);
    return remainder;
}
