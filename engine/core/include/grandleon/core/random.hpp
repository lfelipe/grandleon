// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Deterministic randomness for the authoritative simulation.
//
// `DESIGN.md` §3.2 requires "a project-owned pseudo-random number generator
// whose complete state is saved", and §9 that random decisions come from
// recorded deterministic streams. This header is that generator. It is the
// only source of dice in the engine; nothing else may roll.
//
// Three properties are load-bearing, and every choice below serves one of
// them.
//
// **Seeded from canonical state.** A `RandomState` is a seed and a draw
// position per stream. It lives inside the encounter, is hashed by
// `canonical_hash`, and travels in the snapshot, so replaying the same
// commands from the same start reproduces the same numbers on every machine.
//
// **One algorithm, specified to the bit.** Not `std::mt19937`, whose results
// this project would have to take on faith from six standard libraries, and
// not anything needing arithmetic a console's compiler cannot lower. Every
// number here comes from FNV-1a-64 (one XOR and one multiply by 0x100000001B3
// per byte), which is the single piece of 64-bit arithmetic this repository
// has already proved bit-for-bit on GCC, Clang, Emscripten, MIPS (Nintendo 64)
// and the R3000A (PlayStation). Adding dice therefore adds no arithmetic
// primitive that is not already checked on target: the conformance ROMs that
// pin the hash pin the generator with it.
//
// **A fixed consumption order.** Drawing is counter-based rather than
// chained: a draw is a pure function of (seed, stream, position), and the
// position is simply incremented. Two consequences matter. A stream's Nth
// number does not depend on how many numbers any other stream drew, so a drop
// roll cannot shift a hit roll (see `RandomStream`). And every bounded roll
// consumes exactly one draw, never a rejection loop, so "how many numbers did
// this rule take" has one answer that a reader can check against the rules.
//
// All three purposes below have a consumer, each with its own written
// consumption order. `hit` is drawn by `roll_hit` in
// `engine/simulation/src/encounter.cpp`, inside the battle and hashed with it.
// `drop` is drawn by `roll_drop` in the same file, also inside the battle and
// hashed with it: one draw per defeated unit whose type authors an uncertain
// drop, in the order the defeats resolve. `growth` is drawn by
// `campaign_runtime::derive_battle_progression`, *outside* any battle, from a
// state seeded by `campaign::derive_growth_seed` over the completed battle's
// canonical hash, because a level is a campaign's business and the simulation
// must not learn that campaigns exist.
//
// The per-purpose identity is checked rather than merely claimed: a battle
// fought against units that drop rolls exactly the hit numbers the same battle
// fought against units that do not rolls, and `tests/simulation` pins it.

namespace grandleon::core {

// FNV-1a-64, named once so the generator and the canonical hash cannot drift
// apart. Every console's conformance vectors check exactly these constants.
inline constexpr std::uint64_t fnv1a64_offset_basis = 14695981039346656037ULL;
inline constexpr std::uint64_t fnv1a64_prime = 1099511628211ULL;

[[nodiscard]] constexpr std::uint64_t fnv1a64_step(
    std::uint64_t hash,
    std::uint8_t value
) noexcept {
    hash ^= value;
    hash *= fnv1a64_prime;
    return hash;
}

// What a stream is for. Identity is per purpose rather than per encounter, so
// that adding a roll to one rule cannot move the numbers another rule sees.
// That is the failure mode a single shared stream makes almost inevitable,
// where giving enemies a drop chance silently rerolls every hit in the battle.
//
// Values are mixed into every draw and are written into saves, so this list is
// append-only and a value is never reused for a different purpose. Appending
// to it moves no hash: a stream nothing has drawn from is not encoded at all
// (see `RandomState`).
//
// The three purposes named here are the three the design already commits to.
enum class RandomStream : std::uint16_t {
    // Whether an attack connects.
    hit = 1,
    // What a defeated unit leaves behind.
    drop = 2,
    // How a unit's numbers grow when it advances.
    growth = 3,
};

// One past the largest value above. The vocabulary is closed; a caller cannot
// invent a stream, because an unnamed purpose is an unaudited consumption
// order.
inline constexpr std::size_t random_stream_count = 4;

[[nodiscard]] std::string_view random_stream_name(RandomStream stream) noexcept;

// The number a stream yields at a given position. Pure: no state, no ordering,
// no allocation, and callable from a test or a conformance ROM with nothing
// else built.
//
// The eighteen bytes hashed are, in this order and little-endian throughout:
//
//     seed      8 bytes
//     stream    2 bytes
//     position  8 bytes
//
// Little-endian because that is the byte order the rest of the engine
// serializes with, and the order is fixed here rather than left to the host so
// that every console agrees with x86 without any of them swapping.
//
// The FNV pass alone would not do. FNV-1a diffuses low bits upward and never
// downward, so consecutive positions, which differ in one low byte of the last
// field, would produce outputs that differ almost as predictably. The
// finalizer fixes that: alternating right-shift XORs fold the well-diffused
// high bits back down over the low ones, and multiplies carry them up again.
//
// It introduces no new constant. The multiplier is the FNV prime, so a target
// that has to decompose a 64-bit multiply decomposes one and not two, and the
// step that carries the hash carries the finalizer with it. That frugality
// costs rounds: the FNV prime is 0x100000001B3, a multiplier with seven set
// bits, and a sparse multiplier mixes more slowly than the dense ones a
// purpose-built finalizer would use. Four rounds is where it stops paying.
// Measured over 384000 single-bit input flips, this finalizer changes 31.996
// output bits on average against a theoretical 32: the same avalanche a widely
// trusted reference mixer shows on the identical test, and where two rounds
// still sat measurably short of it. `tests/core/random_test.cpp` keeps that
// measurement.
[[nodiscard]] constexpr std::uint64_t random_draw(
    std::uint64_t seed,
    RandomStream stream,
    std::uint64_t position
) noexcept {
    std::uint64_t hash = fnv1a64_offset_basis;
    for (std::size_t index = 0; index < 8U; ++index) {
        hash = fnv1a64_step(
            hash,
            static_cast<std::uint8_t>(seed >> (index * 8U))
        );
    }
    const auto identity = static_cast<std::uint16_t>(stream);
    for (std::size_t index = 0; index < 2U; ++index) {
        hash = fnv1a64_step(
            hash,
            static_cast<std::uint8_t>(identity >> (index * 8U))
        );
    }
    for (std::size_t index = 0; index < 8U; ++index) {
        hash = fnv1a64_step(
            hash,
            static_cast<std::uint8_t>(position >> (index * 8U))
        );
    }
    hash ^= hash >> 31U;
    hash *= fnv1a64_prime;
    hash ^= hash >> 27U;
    hash *= fnv1a64_prime;
    hash ^= hash >> 33U;
    hash *= fnv1a64_prime;
    hash ^= hash >> 29U;
    hash *= fnv1a64_prime;
    hash ^= hash >> 32U;
    return hash;
}

// The largest bound a roll may ask for. Chances this engine needs are
// percentages and per-milles, and a small closed bound keeps the reduction
// below honest without a rejection loop.
inline constexpr std::uint32_t random_maximum_bound = 65536U;

// Reduce a draw to `[0, bound)`. Fixed at one draw per roll: the modulo is
// taken over the low 32 bits rather than the whole word, which costs one
// 32-bit division instead of a 64-bit one on machines that have neither, and
// the bias this leaves is at most `bound / 2^32`, below one part in sixty-five
// thousand at the largest bound this accepts, and below one part in forty
// million for a percentage. A rejection loop would remove even that, at the
// price of a roll whose cost depends on the number it drew; this engine values
// the fixed consumption order more than the last 2^-16 of uniformity, and says
// so here rather than leaving a reader to infer it.
//
// A bound of zero or one has exactly one possible answer and returns it
// without pretending otherwise.
//
// A bound above `random_maximum_bound` is refused, not narrowed to it. The two
// differ in the only way that matters: narrowing returns a number that looks
// like an answer, is inside the range the caller asked for, and is drawn from
// a distribution that leaves every outcome above 65536 impossible: a silent
// bias no assertion downstream could see. Refusing returns zero and says so
// here, which is the same refusal `next` makes of a stream that is not one.
[[nodiscard]] constexpr std::uint32_t random_below(
    std::uint64_t value,
    std::uint32_t bound
) noexcept {
    if (bound <= 1U || bound > random_maximum_bound) {
        return 0U;
    }
    return static_cast<std::uint32_t>(value) % bound;
}

// The complete, serializable random state of one authoritative simulation: a
// seed, and how many numbers each stream has drawn.
//
// `positions` is indexed by `RandomStream`; entry zero is unused, because zero
// is not a stream. A stream at position zero has never been drawn from, which
// is why the canonical encoding below omits it entirely.
struct RandomState final {
    // Zero means no seed was chosen. `create_encounter` never leaves it zero:
    // it derives one from the encounter's own opening state, so an author who
    // says nothing still gets a definite, reproducible battle rather than an
    // undefined one. See `engine/simulation/README.md`.
    std::uint64_t seed{};
    std::array<std::uint64_t, random_stream_count> positions{};

    // Draw the next number from a stream and advance it. This is the only way
    // a rule may take a number.
    [[nodiscard]] std::uint64_t next(RandomStream stream) noexcept {
        const auto index = static_cast<std::size_t>(stream);
        if (index == 0U || index >= random_stream_count) {
            return 0U;
        }
        const std::uint64_t value =
            random_draw(seed, stream, positions[index]);
        ++positions[index];
        return value;
    }

    // One number in `[0, bound)`, consuming exactly one draw.
    //
    // A bound `random_below` refuses costs no draw either. A refused roll that
    // still advanced the stream would shift every number the rest of the
    // battle takes from it, which turns one out-of-contract call into a
    // divergence in an unrelated rule several turns later.
    [[nodiscard]] std::uint32_t roll_below(
        RandomStream stream,
        std::uint32_t bound
    ) noexcept {
        if (bound > random_maximum_bound) {
            return 0U;
        }
        return random_below(next(stream), bound);
    }

    // Whether a chance in `bound` succeeds: `chance` favourable outcomes out
    // of `bound`. Written once here so that every rule that asks "does this
    // land" asks it the same way and rounds it the same way.
    [[nodiscard]] bool roll_chance(
        RandomStream stream,
        std::uint32_t chance,
        std::uint32_t bound
    ) noexcept {
        if (chance == 0U) {
            // Still no draw: an impossible event consumes nothing, so adding a
            // rule that can never fire does not move any other stream.
            return false;
        }
        if (bound > random_maximum_bound) {
            // Neither the impossible chance above nor the certain one below:
            // a bound this cannot roll honestly is a question with no answer,
            // so the event does not happen and nothing is drawn for it.
            return false;
        }
        if (chance >= bound) {
            return true;
        }
        return roll_below(stream, bound) < chance;
    }
};

[[nodiscard]] constexpr bool operator==(
    const RandomState& lhs,
    const RandomState& rhs
) noexcept {
    if (lhs.seed != rhs.seed) {
        return false;
    }
    for (std::size_t index = 0; index < random_stream_count; ++index) {
        if (lhs.positions[index] != rhs.positions[index]) {
            return false;
        }
    }
    return true;
}

// Fold the random state into a running FNV-1a-64 hash, in the one order every
// platform must use.
//
// Only streams that have been drawn from are encoded, each as its identity
// followed by its position, in ascending identity order. That makes the
// encoding sparse on purpose: appending a new purpose to `RandomStream` moves
// no existing hash, so the vocabulary can grow without a golden regeneration
// every time. The count is written before the pairs so that no two states
// share an encoding.
[[nodiscard]] std::uint64_t hash_random_state(
    std::uint64_t hash,
    const RandomState& state
) noexcept;

// The seed to use when nothing chose one. `state_hash` is the encounter's
// canonical hash taken over its opening state, which makes the fallback a
// function of the board, the units and the rules: definite, identical on
// every platform, and different for different encounters. It never returns
// zero, because zero means "unchosen".
//
// This is deliberately not a clock or a platform source: §3.2 forbids the
// simulation reading either. Varying a battle between playthroughs is the
// caller's job (a campaign passes a seed down from its save), and the caller
// has the clock this layer must not.
[[nodiscard]] constexpr std::uint64_t derive_random_seed(
    std::uint64_t state_hash
) noexcept {
    std::uint64_t seed = state_hash;
    seed ^= seed >> 33U;
    seed *= fnv1a64_prime;
    seed ^= seed >> 29U;
    return seed == 0U ? fnv1a64_offset_basis : seed;
}

}  // namespace grandleon::core
