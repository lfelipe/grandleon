// SPDX-License-Identifier: MIT
// The deterministic random substrate.
//
// Three things are checked here and nowhere else: the numbers themselves, the
// properties the rules will lean on once they start drawing, and the quality
// claim the header makes about its finalizer.
//
// The vectors below are the same ones the Nintendo 64 and
// PlayStation conformance ROMs assert on target. Six toolchains agreeing on
// them is the whole reason the generator is built from FNV-1a-64 and nothing
// else; if this file and a console ever disagree, the substrate is broken and
// every golden, replay and autopilot downstream of it is untrustworthy.

#include <grandleon/core/random.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>

namespace {

using grandleon::core::derive_random_seed;
using grandleon::core::fnv1a64_offset_basis;
using grandleon::core::fnv1a64_prime;
using grandleon::core::fnv1a64_step;
using grandleon::core::hash_random_state;
using grandleon::core::random_below;
using grandleon::core::random_draw;
using grandleon::core::random_maximum_bound;
using grandleon::core::random_stream_count;
using grandleon::core::random_stream_name;
using grandleon::core::RandomState;
using grandleon::core::RandomStream;

// The hash primitive the generator is built from is the one the engine already
// hashes state with, and the one three consoles already reproduce. Pinning the
// published FNV-1a-64 vectors here says so in the same file as the rest.
void reproduces_the_hash_primitive() {
    const auto fnv = [](const char* text) {
        std::uint64_t hash = fnv1a64_offset_basis;
        for (const char* cursor = text; *cursor != '\0'; ++cursor) {
            hash = fnv1a64_step(hash, static_cast<std::uint8_t>(*cursor));
        }
        return hash;
    };
    assert(fnv1a64_offset_basis == 0xcbf29ce484222325ULL);
    assert(fnv1a64_prime == 0x00000100000001b3ULL);
    assert(fnv("") == 0xcbf29ce484222325ULL);
    assert(fnv("a") == 0xaf63dc4c8601ec8cULL);
    assert(fnv("foobar") == 0x85944171f73967e8ULL);
}

// The generator, to the bit. Every platform must produce exactly these.
void produces_the_canonical_vectors() {
    static_assert(
        random_draw(0, RandomStream::hit, 0) == 0xce18b86f70a6d6baULL,
        "draw(0, hit, 0)"
    );
    static_assert(
        random_draw(0, RandomStream::hit, 1) == 0x015b235d0f87661cULL,
        "draw(0, hit, 1)"
    );
    static_assert(
        random_draw(0, RandomStream::drop, 0) == 0x086fb47af38e6898ULL,
        "draw(0, drop, 0)"
    );
    static_assert(
        random_draw(0, RandomStream::growth, 0) == 0xe3340374cae06546ULL,
        "draw(0, growth, 0)"
    );
    static_assert(
        random_draw(1, RandomStream::hit, 0) == 0x22041e7398479ad6ULL,
        "draw(1, hit, 0)"
    );
    static_assert(
        random_draw(fnv1a64_offset_basis, RandomStream::hit, 7) ==
            0x90f6bd00d789fa03ULL,
        "draw(basis, hit, 7)"
    );
    // Both extremes of both 64-bit fields, which is where a platform that
    // sign-extends or truncates somewhere would show it.
    static_assert(
        random_draw(
            0xFFFFFFFFFFFFFFFFULL,
            RandomStream::growth,
            0xFFFFFFFFFFFFFFFFULL
        ) == 0x12e537d1165220f4ULL,
        "draw(max, growth, max)"
    );

    // A run rather than a point: the first sixty-four numbers of one stream,
    // folded back through the same hash. One wrong draw anywhere in the run
    // changes this, so a console reproducing it has reproduced the sequence and
    // not merely a lucky value.
    RandomState state;
    state.seed = fnv1a64_offset_basis;
    std::uint64_t summary = fnv1a64_offset_basis;
    for (int index = 0; index < 64; ++index) {
        const std::uint64_t value = state.next(RandomStream::hit);
        for (int byte = 0; byte < 8; ++byte) {
            summary = fnv1a64_step(
                summary,
                static_cast<std::uint8_t>(value >> (byte * 8))
            );
        }
    }
    assert(summary == 0x829e2bbcd92773c4ULL);
    assert(state.positions[static_cast<std::size_t>(RandomStream::hit)] == 64U);
}

// Drawing is a pure function of (seed, stream, position): no hidden chaining,
// no dependence on what else was drawn. This is what makes replay from a saved
// snapshot exact and what lets one stream be added without disturbing another.
void draws_are_positional_and_repeatable() {
    for (std::uint64_t position = 0; position < 256; ++position) {
        assert(
            random_draw(1234, RandomStream::hit, position) ==
            random_draw(1234, RandomStream::hit, position)
        );
    }

    RandomState first;
    first.seed = 99;
    RandomState second;
    second.seed = 99;
    for (int index = 0; index < 32; ++index) {
        assert(first.next(RandomStream::hit) == second.next(RandomStream::hit));
    }

    // The tenth hit is the tenth hit whether or not anything drew from the
    // other streams in between. A drop roll cannot shift a hit roll.
    RandomState quiet;
    quiet.seed = 4242;
    RandomState noisy;
    noisy.seed = 4242;
    for (int index = 0; index < 10; ++index) {
        (void)noisy.next(RandomStream::drop);
        (void)noisy.next(RandomStream::growth);
        assert(
            quiet.next(RandomStream::hit) == noisy.next(RandomStream::hit)
        );
    }

    // Different streams at the same position are different numbers.
    for (std::uint64_t position = 0; position < 512; ++position) {
        const std::uint64_t hit = random_draw(7, RandomStream::hit, position);
        const std::uint64_t drop = random_draw(7, RandomStream::drop, position);
        const std::uint64_t growth =
            random_draw(7, RandomStream::growth, position);
        assert(hit != drop);
        assert(hit != growth);
        assert(drop != growth);
    }

    // A different seed is a different battle.
    for (std::uint64_t position = 0; position < 256; ++position) {
        assert(
            random_draw(1, RandomStream::hit, position) !=
            random_draw(2, RandomStream::hit, position)
        );
    }
}

// A stream outside the vocabulary draws nothing and advances nothing, so a
// caller that invents one gets a refusal rather than a silent second copy of
// stream zero.
void refuses_an_unnamed_stream() {
    RandomState state;
    state.seed = 5;
    const auto invalid = static_cast<RandomStream>(random_stream_count + 3);
    assert(state.next(invalid) == 0U);
    assert(state.next(static_cast<RandomStream>(0)) == 0U);
    for (std::size_t index = 0; index < random_stream_count; ++index) {
        assert(state.positions[index] == 0U);
    }
}

// Bounding: one draw per roll, always in range, and the degenerate bounds have
// the one answer they can have.
void bounds_every_roll() {
    assert(random_below(0xFFFFFFFFFFFFFFFFULL, 0) == 0U);
    assert(random_below(0xFFFFFFFFFFFFFFFFULL, 1) == 0U);

    // The largest bound the reduction answers honestly is answered.
    assert(random_below(12345, random_maximum_bound) < random_maximum_bound);
    assert(random_below(12345, random_maximum_bound) == 12345U);

    // One past it is refused rather than quietly answered with a smaller
    // bound. Capping returns a number below 65536 for every draw, so a rule
    // asking for a bound of 100000 never sees 70000 and nothing anywhere says
    // so: a wrong distribution that looks exactly like a right one, in the
    // module the determinism story rests on.
    assert(random_below(12345, random_maximum_bound + 1U) == 0U);
    assert(random_below(12345, random_maximum_bound + 1000U) == 0U);
    assert(random_below(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFU) == 0U);

    // And a refused roll consumes nothing, exactly as an invalid stream does.
    // A roll that was not answered must not move the position a later roll
    // reads from, or one out-of-contract call would shift every number the
    // rest of the battle draws.
    RandomState refusing;
    refusing.seed = 0x99;
    const std::size_t hit = static_cast<std::size_t>(RandomStream::hit);
    assert(
        refusing.roll_below(RandomStream::hit, random_maximum_bound + 1U) == 0U
    );
    assert(refusing.positions[hit] == 0U);

    // The same refusal one level up. An out-of-contract bound is neither the
    // impossible chance nor the certain one, so `roll_chance` reports that the
    // event did not happen and draws nothing for it, rather than reporting
    // that it always does.
    assert(
        !refusing.roll_chance(RandomStream::hit, 1U, random_maximum_bound + 1U)
    );
    assert(refusing.positions[hit] == 0U);

    // The contract's own edge still rolls, and still costs its one draw.
    assert(refusing.roll_below(RandomStream::hit, random_maximum_bound) <
           random_maximum_bound);
    assert(refusing.positions[hit] == 1U);

    RandomState state;
    state.seed = 0xABCDEF;
    for (std::uint32_t bound = 1; bound <= 100; ++bound) {
        const std::uint64_t before =
            state.positions[static_cast<std::size_t>(RandomStream::hit)];
        const std::uint32_t value = state.roll_below(RandomStream::hit, bound);
        assert(value < bound);
        assert(
            state.positions[static_cast<std::size_t>(RandomStream::hit)] ==
            before + 1U
        );
    }

    // A chance that cannot happen and a chance that always happens are decided
    // without drawing, so adding a rule that never fires moves nothing.
    RandomState chances;
    chances.seed = 77;
    assert(!chances.roll_chance(RandomStream::drop, 0, 100));
    assert(chances.roll_chance(RandomStream::drop, 100, 100));
    assert(chances.roll_chance(RandomStream::drop, 200, 100));
    assert(chances.positions[static_cast<std::size_t>(RandomStream::drop)] ==
           0U);
    (void)chances.roll_chance(RandomStream::drop, 50, 100);
    assert(chances.positions[static_cast<std::size_t>(RandomStream::drop)] ==
           1U);
}

// The quality claim, measured rather than asserted by adjective.
//
// A hit chance a player is shown has to be the chance they get, so a roll whose
// consecutive draws correlate would make the displayed number a lie. FNV-1a
// alone would do exactly that: it carries differences upward only, and
// consecutive positions differ in one low byte. That is why the finalizer
// exists and why its effect is checked here instead of trusted.
void mixes_well_enough_to_price_a_chance() {
    // Avalanche: flipping one input bit should change about half the output
    // bits. The header's four rounds were chosen against this measurement.
    long long total = 0;
    long long samples = 0;
    for (std::uint64_t position = 0; position < 500; ++position) {
        const std::uint64_t value =
            random_draw(0x1234567890abcdefULL, RandomStream::hit, position);
        for (int bit = 0; bit < 64; ++bit) {
            const std::uint64_t flipped = random_draw(
                0x1234567890abcdefULL,
                RandomStream::hit,
                position ^ (1ULL << bit)
            );
            std::uint64_t difference = value ^ flipped;
            int changed = 0;
            while (difference != 0U) {
                changed += static_cast<int>(difference & 1U);
                difference >>= 1U;
            }
            total += changed;
            ++samples;
        }
    }
    const double mean = static_cast<double>(total) / static_cast<double>(samples);
    if (mean < 31.7 || mean > 32.3) {
        std::printf("avalanche mean %f, expected close to 32\n", mean);
        assert(false);
    }

    // Uniformity of a percentage roll over consecutive positions, which is
    // exactly how a rule will consume it. Chi-square with 99 degrees of
    // freedom; the 99.9th percentile is about 148.
    int bins[100] = {};
    const int rolls = 200000;
    for (int index = 0; index < rolls; ++index) {
        ++bins[random_below(
            random_draw(99, RandomStream::hit, static_cast<std::uint64_t>(index)),
            100
        )];
    }
    const double expected = static_cast<double>(rolls) / 100.0;
    double chi_square = 0.0;
    for (const int count : bins) {
        const double deviation = static_cast<double>(count) - expected;
        chi_square += deviation * deviation / expected;
    }
    if (chi_square > 148.0) {
        std::printf("chi-square %f over %d rolls\n", chi_square, rolls);
        assert(false);
    }

    // No short cycle in the range a battle could reach.
    std::set<std::uint64_t> seen;
    for (std::uint64_t position = 0; position < 20000; ++position) {
        seen.insert(random_draw(3, RandomStream::growth, position));
    }
    assert(seen.size() == 20000U);
}

// The canonical encoding of the state, and the property that pays for its
// sparseness: naming a new purpose moves no hash.
void hashes_the_state_canonically() {
    RandomState state;
    state.seed = 0x0102030405060708ULL;
    assert(hash_random_state(fnv1a64_offset_basis, state) ==
           0x185dadca3e3c4da5ULL);

    RandomState drawn = state;
    (void)drawn.next(RandomStream::drop);
    assert(hash_random_state(fnv1a64_offset_basis, drawn) ==
           0x83bb6fb2acf39567ULL);
    assert(!(state == drawn));

    // A state nothing has drawn from hashes as though its streams did not
    // exist. That is what makes `RandomStream` extensible without a golden
    // regeneration: the encoding of an untouched encounter is the seed and a
    // zero count, whatever the vocabulary happens to contain.
    RandomState fresh;
    fresh.seed = state.seed;
    assert(hash_random_state(fnv1a64_offset_basis, fresh) ==
           hash_random_state(fnv1a64_offset_basis, state));
    assert(fresh == state);

    // Distinct states hash distinctly: the seed, which stream drew, and how
    // often each are all covered.
    std::set<std::uint64_t> hashes;
    for (std::uint64_t seed = 1; seed <= 3; ++seed) {
        for (std::size_t stream = 1; stream < random_stream_count; ++stream) {
            for (std::uint64_t count = 0; count <= 3; ++count) {
                RandomState candidate;
                candidate.seed = seed;
                candidate.positions[stream] = count;
                hashes.insert(
                    hash_random_state(fnv1a64_offset_basis, candidate)
                );
            }
        }
    }
    // Three seeds x three streams x four counts, minus the two duplicate
    // all-zero states each seed contributes: 36 - 6 = 30 distinct encodings.
    assert(hashes.size() == 30U);
}

// The fallback seed: definite, derived from the encounter, and never zero,
// because zero is what "nobody chose one" means.
void derives_a_seed_when_nobody_chose_one() {
    assert(derive_random_seed(0) != 0U);
    assert(derive_random_seed(0) == 0xcbf29ce484222325ULL);
    // Deliberately a made-up number rather than a real encounter hash: this
    // vector must not have to be regenerated every time a golden moves.
    assert(derive_random_seed(0x0123456789abcdefULL) == 0x295e4aef64b96e25ULL);

    std::set<std::uint64_t> seeds;
    for (std::uint64_t hash = 0; hash < 4096; ++hash) {
        const std::uint64_t seed = derive_random_seed(hash);
        assert(seed != 0U);
        seeds.insert(seed);
    }
    assert(seeds.size() == 4096U);
}

void names_every_stream() {
    assert(random_stream_name(RandomStream::hit) == "hit");
    assert(random_stream_name(RandomStream::drop) == "drop");
    assert(random_stream_name(RandomStream::growth) == "growth");
    assert(
        random_stream_name(static_cast<RandomStream>(random_stream_count)) ==
        "unknown"
    );
}

}  // namespace

int main() {
    reproduces_the_hash_primitive();
    produces_the_canonical_vectors();
    draws_are_positional_and_repeatable();
    refuses_an_unnamed_stream();
    bounds_every_roll();
    mixes_well_enough_to_price_a_chance();
    hashes_the_state_canonically();
    derives_a_seed_when_nobody_chose_one();
    names_every_stream();
    return 0;
}
