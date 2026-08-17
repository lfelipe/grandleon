// SPDX-License-Identifier: MIT
// PlayStation conformance executable.
//
// This is not a game and not a renderer. It is the console's entry gate,
// built to answer six questions and nothing else:
//
//   1. do the five portable engine libraries compile for mipsel-o32 with no
//      source change and the repository's full warning discipline;
//   2. do 0e41227fef2c075f and 9090072b2c0a69c5 reproduce on a 32-bit
//      little-endian target;
//   3. does libstdc++'s container mix link against a -nostdlib R3000A runtime,
//      and does operator new reach a real allocator;
//   4. does the report reach a script through the BIOS teletype, headlessly,
//      under the MIT OpenBIOS;
//   5. does pcsx_exit() set the emulator's process exit code;
//   6. what does it cost in code, in heap, and in cycles.
//
// It answers one question the evaluation did not ask, because the answer
// turned out to
// change what a port would look like: whether this machine can run the
// *on-console content path*: parsing and compiling the checked-in source
// project the way the Nintendo 64 play ROM does. The evaluation predicted it
// could, on a RAM budget. The budget was never the binding
// constraint. See platform/playstation/README.md; the short version is that
// the content path's JSON parser throws, and the pinned toolchain's
// exception-handling archives cannot be linked into a freestanding R3000A
// executable at all. So this executable takes the escape hatch the evaluation
// describes instead: the demo campaign is compiled on the host and
// shipped as a package, and the executable replays it from those bytes to the
// golden hash games/demo/src/play_demo.cpp pins.
//
// Results go out over the BIOS teletype, which PCSX-Redux intercepts and
// writes to its own stdout. There is no video output at all: the GPU is never
// touched, because a renderer is not part of this gate.

#include "psx_runtime.h"

#include <grandleon/core/content_identity.hpp>
#include <grandleon/core/random.hpp>
#include <grandleon/core/version.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>

#include "generated/demo_package.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace psx = grandleon::playstation;
namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;

// PCSX-Redux's control port answers with the ASCII of its own name. Reading it
// is how an executable can tell whether the machine it woke up on is the one
// that will listen to its verdict. It is the same probe Nugget's
// `pcsx_present()` makes, written out here rather than pulled in with a C
// header full of inline assembly that the warning discipline would have to be
// relaxed for.
namespace {

bool emulator_present() {
    return *reinterpret_cast<volatile std::uint32_t*>(0x1f802080) == 0x58534350U;
}

}  // namespace

// Nugget's assembly thunk to the kernel's own printf (A0 table entry 0x3f).
// It exists here for exactly one check: whether printf reaches stdout
// headlessly under OpenBIOS, and the honest way to answer that is to call it
// rather than to reason about it. Everything else this executable
// prints goes through the putchar path in psx_runtime.cpp, so a defect in the
// kernel's varargs formatter cannot corrupt the report.
extern "C" int ramsyscall_printf(const char* format, ...);

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    psx::Line line;
    if (condition) {
        line.text("ok   ").text(message).flush();
    } else {
        ++failures;
        line.text("FAIL ").text(message).flush();
    }
}

void expect_hash(
    std::uint64_t actual,
    std::uint64_t expected,
    const char* message
) {
    ++checks;
    psx::Line line;
    if (actual == expected) {
        line.text("ok   ").text(message).text(" (").hex64(actual).text(")");
    } else {
        ++failures;
        line.text("FAIL ")
            .text(message)
            .text(": expected ")
            .hex64(expected)
            .text(", got ")
            .hex64(actual);
    }
    line.flush();
}

// Built member by member rather than as a braced aggregate. The engine's
// definition structs end in vectors that these cases leave empty, and
// -Wextra's -Wmissing-field-initializers objects to eliding them.
sim::UnitDefinition unit(
    sim::UnitId id,
    sim::ContentId unit_type_id,
    sim::Side side,
    sim::Position position,
    std::int16_t health,
    std::int16_t strength,
    std::int16_t defense,
    std::uint8_t movement = 1
) {
    sim::UnitDefinition value;
    value.id = id;
    value.unit_type_id = unit_type_id;
    value.side = side;
    value.position = position;
    value.health = health;
    value.strength = strength;
    value.defense = defense;
    value.movement = movement;
    return value;
}

sim::EncounterDefinition board(
    std::uint16_t width,
    std::uint16_t height,
    std::vector<sim::UnitDefinition> units
) {
    sim::EncounterDefinition value;
    value.width = width;
    value.height = height;
    value.units = std::move(units);
    return value;
}

// The word sizes the determinism contract is built on, checked before anything
// depends on them.
//
// The interesting line is the alignment one. MIPS-I has no LD and no SD, so a
// 64-bit value is always two lw/sw pairs and four-byte alignment is all the
// hardware needs. That is the direct answer to the open question in
// engine/package_format/README.md for this target. GCC still reports
// __alignof__(long long) as 8 on o32 and pads structs accordingly, so both
// facts are reported rather than one being asserted over the other.
void reports_target_word_sizes() {
    expect(sizeof(std::uint64_t) == 8, "uint64_t is eight bytes");
    expect(sizeof(sim::UnitId) == 8, "UnitId is eight bytes");
    expect(sizeof(void*) == 4, "pointer is four bytes");
    expect(sizeof(long) == 4, "long is four bytes on o32");
    expect(alignof(void*) == 4, "pointer aligns to four");
    const std::uint32_t probe = 0x01020304U;
    const auto* first = reinterpret_cast<const std::uint8_t*>(&probe);
    expect(*first == 0x04U, "target is little-endian");

    // Unaligned-by-eight 64-bit access, which on a machine with LD would be
    // the hazard and here is not one. The value is written and read back
    // through a four-byte-aligned address; if MIPS-I had a 64-bit load this
    // would take an address error exception.
    alignas(8) std::uint8_t storage[16] = {};
    auto* misaligned = reinterpret_cast<std::uint64_t*>(storage + 4);
    *misaligned = 0x0123456789abcdefULL;
    expect(
        *misaligned == 0x0123456789abcdefULL,
        "a four-byte-aligned 64-bit value round trips"
    );

    psx::Line line;
    line.text("info alignof: uint64_t=")
        .decimal(static_cast<std::uint32_t>(alignof(std::uint64_t)))
        .text(" ptr=")
        .decimal(static_cast<std::uint32_t>(alignof(void*)))
        .text(" sizeof: UnitSnapshot=")
        .decimal(static_cast<std::uint32_t>(sizeof(sim::UnitSnapshot)))
        .text(" Encounter=")
        .decimal(static_cast<std::uint32_t>(sizeof(sim::Encounter)))
        .text(" Command=")
        .decimal(static_cast<std::uint32_t>(sizeof(sim::Command)))
        .text(" Event=")
        .decimal(static_cast<std::uint32_t>(sizeof(sim::Event)))
        .flush();
}

// The constant-folded identity mapping, evaluated on the target rather than by
// the host compiler, because it feeds the canonical hash.
void computes_stable_content_identity() {
    const auto id = core::stable_content_id_v1("grandleon/playstation");
    expect(id == core::stable_content_id_v1("grandleon/playstation"),
           "stable content identity is stable");
    expect(id != core::stable_content_id_v1("grandleon/playstatoin"),
           "stable content identity separates distinct keys");
    const auto version = core::engine_version();
    psx::Line line;
    line.text("info engine version ")
        .decimal(version.major)
        .text(".")
        .decimal(version.minor)
        .text(".")
        .decimal(version.patch)
        .flush();
}

// The deterministic random substrate. Vectors are copied from
// tests/core/random_test.cpp and must not diverge from it.
//
// engine/core/include/grandleon/core/random.hpp builds every number out of the
// same FNV-1a-64 step the canonical hash is built from, and out of nothing
// else, so a target that reproduces the hashes below the way it already
// reproduces the reference vector has proved the dice as well. That is the
// whole reason the generator was written this way rather than around a
// standard-library engine whose results six toolchains would each define for
// themselves.
void reproduces_the_random_substrate() {
    expect_hash(
        core::random_draw(0, core::RandomStream::hit, 0),
        0xce18b86f70a6d6baULL,
        "random draw, seed 0, hit stream, position 0"
    );
    expect_hash(
        core::random_draw(0, core::RandomStream::drop, 0),
        0x086fb47af38e6898ULL,
        "random draw, seed 0, drop stream, position 0"
    );
    expect_hash(
        core::random_draw(1, core::RandomStream::hit, 0),
        0x22041e7398479ad6ULL,
        "random draw, seed 1, hit stream, position 0"
    );
    // Both 64-bit fields at their maximum, which is where a 32-bit target that
    // borrowed or truncated somewhere in the generator's shifts would show it.
    expect_hash(
        core::random_draw(
            0xFFFFFFFFFFFFFFFFULL,
            core::RandomStream::growth,
            0xFFFFFFFFFFFFFFFFULL
        ),
        0x12e537d1165220f4ULL,
        "random draw, both fields at their maximum"
    );

    // A sequence rather than a point: sixty-four consecutive draws of one
    // stream, folded back through the same hash. One wrong number anywhere in
    // the run changes this, so matching it is matching the sequence and not a
    // lucky value.
    core::RandomState state;
    state.seed = core::fnv1a64_offset_basis;
    std::uint64_t summary = core::fnv1a64_offset_basis;
    for (int index = 0; index < 64; ++index) {
        const std::uint64_t value = state.next(core::RandomStream::hit);
        for (int byte = 0; byte < 8; ++byte) {
            summary = core::fnv1a64_step(
                summary,
                static_cast<std::uint8_t>(value >> (byte * 8))
            );
        }
    }
    expect_hash(
        summary,
        0x829e2bbcd92773c4ULL,
        "sixty-four consecutive draws of one stream"
    );

    // Streams are independent by construction: a drop roll cannot shift a hit
    // roll, whatever order the two happen in.
    core::RandomState quiet;
    quiet.seed = 4242;
    core::RandomState noisy;
    noisy.seed = 4242;
    bool independent = true;
    for (int index = 0; index < 8; ++index) {
        (void)noisy.next(core::RandomStream::drop);
        if (quiet.next(core::RandomStream::hit) !=
            noisy.next(core::RandomStream::hit)) {
            independent = false;
        }
    }
    expect(independent, "one stream's draws do not shift another's");

    // The canonical encoding of the state, which is the part that moves a
    // golden. An untouched state encodes as its seed and a zero count, so
    // naming a new stream purpose moves no hash.
    core::RandomState encoded;
    encoded.seed = 0x0102030405060708ULL;
    expect_hash(
        core::hash_random_state(core::fnv1a64_offset_basis, encoded),
        0x185dadca3e3c4da5ULL,
        "canonical encoding of an undrawn random state"
    );
    (void)encoded.next(core::RandomStream::drop);
    expect_hash(
        core::hash_random_state(core::fnv1a64_offset_basis, encoded),
        0x83bb6fb2acf39567ULL,
        "canonical encoding of a drawn-from random state"
    );
}

// The shared reference vector. Definition, command sequence, and both hashes
// are copied from matches_browser_conformance_vector() in
// tests/simulation/encounter_test.cpp and must not diverge from it.
void matches_shared_reference_vector() {
    auto created = sim::create_encounter(board(
        4,
        3,
        {
            unit(20, 200, sim::Side::second, {2, 1}, 5, 2, 1),
            unit(10, 100, sim::Side::first, {0, 1}, 8, 4, 0),
        }
    ));
    expect(static_cast<bool>(created), "reference encounter is created");
    expect_hash(
        created.encounter.canonical_hash(),
        0x0e41227fef2c075fULL,
        "initial state matches the shared conformance hash"
    );

    const sim::Command commands[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(created.encounter.apply(command)),
            "reference command succeeds"
        );
    }
    expect_hash(
        created.encounter.canonical_hash(),
        0x9090072b2c0a69c5ULL,
        "completed state matches the shared conformance hash"
    );

    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.outcome == sim::Outcome::first_side_won,
        "reference vector ends with the first side winning"
    );
}

// A rejected command must leave canonical state untouched. This is the one
// engine property most likely to break under an unfamiliar allocator or a
// different struct layout, because it depends on the whole state round-tripping
// through the hash unchanged.
void rejects_commands_atomically() {
    auto created = sim::create_encounter(board(
        4,
        3,
        {
            unit(20, 200, sim::Side::second, {2, 1}, 4, 3, 1),
            unit(10, 100, sim::Side::first, {0, 1}, 6, 4, 1),
        }
    ));
    expect(static_cast<bool>(created), "atomicity encounter is created");
    const auto before = created.encounter.canonical_hash();
    const auto rejected =
        created.encounter.apply({sim::CommandType::move, 10, {1, 2}, 0});
    expect(
        rejected.error == sim::CommandError::invalid_destination,
        "diagonal move is rejected on target"
    );
    expect_hash(
        created.encounter.canonical_hash(),
        before,
        "rejection leaves canonical state unchanged"
    );
}

// The board searches in engine/simulation and engine/tactics are the only
// places the engine allocates per command, and the evaluation named
// node-based containers on a machine with no data cache as this target's one
// real risk. Running them is what turns that from a worry into a number. The
// movement field's bucket queue, a vector of vectors, is the newest thing on
// that heap.
void runs_the_tactics_policy() {
    auto created = sim::create_encounter(board(
        8,
        6,
        {
            unit(20, 200, sim::Side::second, {6, 4}, 9, 3, 1, 3),
            unit(10, 100, sim::Side::first, {1, 1}, 9, 4, 1, 3),
        }
    ));
    expect(static_cast<bool>(created), "policy encounter is created");
    const auto snapshot = created.encounter.snapshot();
    const auto plan = tac::decide(snapshot, 10, tac::Behavior::pursue, {});
    expect(plan.actionable, "pursue policy proposes a command");
    expect(
        static_cast<bool>(created.encounter.apply(plan.command)),
        "the simulation accepts the policy's proposal"
    );
}

pf::PackageSource package_source() {
    pf::PackageSource source;
    source.game_id[0] = 0x47U;
    source.content_revision = 3;
    source.required_engine = {{0, 1, 0}, {0, 9, 99}};
    // Deliberately the portable profile. package.hpp's TargetProfile has no
    // PlayStation entry, its values are serialized and therefore append-only,
    // and adding one would be an engine change this gate is not entitled to
    // make. See platform/playstation/README.md.
    source.target = pf::TargetProfile::portable;
    source.sections = {
        {pf::SectionType::classes, 1, 0, pf::section_flag_required,
         {{100, {0x01U, 0x02U, 0x03U}}, {101, {0x04U}}}},
        {pf::SectionType::unit_types, 1, 0, pf::section_flag_required,
         {{200, {0x05U, 0x06U}}}},
    };
    return source;
}

// The container is written and read back byte by byte with shifts, so it should
// be endian-independent by construction. This target is where "should be" gets
// its third data point: the hashes were proved on a little-endian host and a
// big-endian console, and this is a little-endian console.
void round_trips_a_package() {
    const auto bytes = pf::write_mock_package(package_source());
    expect(!bytes.empty(), "package writer produces bytes");
    psx::Line line;
    line.text("info package is ")
        .decimal(static_cast<std::uint32_t>(bytes.size()))
        .text(" bytes")
        .flush();

    const pf::LoadOptions options{
        {0, 1, 0}, pf::TargetProfile::portable, 0U, 1024, 10000
    };
    const auto loaded = pf::load_mock_package(bytes, options);
    expect(static_cast<bool>(loaded), "package loads on target");
    expect(loaded.package.sections.size() == 2, "both sections load");
    expect(
        loaded.package.content_revision == 3,
        "content revision survives the round trip"
    );
    const auto* record = loaded.package.find(pf::SectionType::classes, 101);
    expect(record != nullptr, "stable identifier lookup works");
    expect(
        record != nullptr && record->payload_size == 1,
        "record payload size survives the round trip"
    );

    // Corruption must be detected rather than tolerated: the checksum covers
    // the payload, so flipping one byte has to fail the load.
    auto corrupted = bytes;
    corrupted[corrupted.size() - 1] ^= 0xffU;
    const auto rejected = pf::load_mock_package(corrupted, options);
    expect(!rejected, "a corrupted package is rejected");

    const auto missing = pr::load_encounter(loaded.package, 1);
    expect(
        missing.error == pr::EncounterLoadError::missing_section,
        "encounter loader reports a missing section"
    );
    expect(
        pf::error_name(pf::Error::checksum_mismatch) == "checksum_mismatch",
        "package error names are readable on target"
    );
}

// Real content, end to end, from a package rather than from source.
//
// This is the escape hatch the evaluation describes: compile on the
// host, ship the package. On this target it is not an optimisation but the
// only route available, because the content path cannot be linked here at
// all. The bytes below are `games/demo/source/project.json` as
// `grandleon_content_compile` produced them during this build, so nothing
// hand-made is checked in and the package cannot drift from the source
// project.
//
// It matters more than the synthetic package round trip above, because it is
// the first thing on this machine to exercise the package format, the
// encounter loader, the campaign loader and the simulation against content a
// person authored, and because it ends at a golden hash pinned long before
// this console was a candidate.
void replays_the_demo_package() {
    const auto before = psx::heap_census();
    const std::vector<std::uint8_t> bytes(
        demo_package_bytes, demo_package_bytes + demo_package_size
    );
    const auto loaded = pf::load_mock_package(
        bytes, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10000}
    );
    expect(static_cast<bool>(loaded), "the host-compiled demo package loads");
    if (!loaded) {
        return;
    }

    const auto encounter_id =
        core::stable_content_id_v1("demo_campaign/bridge_encounter");
    auto decoded = pr::load_encounter(loaded.package, encounter_id);
    expect(static_cast<bool>(decoded), "the demo encounter decodes");
    auto campaign = pr::load_campaign(
        loaded.package, core::stable_content_id_v1("demo_campaign")
    );
    expect(static_cast<bool>(campaign), "the demo campaign decodes");
    if (!decoded || !campaign) {
        return;
    }
    pr::CampaignCursor cursor(std::move(campaign.definition));
    expect(
        cursor.current().encounter_id == encounter_id,
        "the demo campaign begins at the bridge encounter"
    );

    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "the demo encounter is created");
    if (!created) {
        return;
    }
    const auto first_id = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/dawn_guard_leader"
    );
    const auto second_id = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/river_watch_leader"
    );
    // The reference stream games/demo/src/play_demo.cpp applies, command for
    // command. The first two are one turn: both classes carry two action
    // points, so the walk leaves the rider a point to strike with. The console
    // has to agree about that as much as about the value below.
    const sim::Command commands[] = {
        {sim::CommandType::move, first_id, {1, 1}, 0},
        {sim::CommandType::attack, first_id, {}, second_id},
        {sim::CommandType::attack, second_id, {}, first_id},
        {sim::CommandType::attack, first_id, {}, second_id},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(created.encounter.apply(command)),
            "demo playthrough command succeeds"
        );
    }
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::first_side_won,
        "the demo playthrough ends with the first side winning"
    );
    // The golden value games/demo/src/play_demo.cpp pins, reached on a 32-bit
    // little-endian console from the same package bytes the host reads.
    expect_hash(
        created.encounter.canonical_hash(),
        0x673e5a59765c94c5ULL,
        "the demo playthrough reaches its golden canonical hash"
    );

    const auto after = psx::heap_census();
    psx::Line line;
    line.text("info demo package is ")
        .decimal(static_cast<std::uint32_t>(demo_package_size))
        .text(" bytes, playthrough took ")
        .decimal(after.allocations - before.allocations)
        .text(" allocations, peak ")
        .decimal(after.peak_allocated_bytes)
        .text(" bytes")
        .flush();
}

// Whether printf reaches stdout headlessly under the MIT OpenBIOS is a
// separate question from whether putchar does, because the kernel's
// printf is a reimplemented varargs formatter rather than a byte channel, and
// OpenBIOS is a reimplementation of a reimplementation.
void reaches_the_teletype_through_printf() {
    const int written = ramsyscall_printf(
        "info kernel printf: %s %d %s\n", "reached", 1, "stdout"
    );
    expect(
        written >= 0, "the kernel's printf returns without faulting"
    );
}

// Cost, measured rather than modelled. The evaluation estimated 25 to
// 30 cycles per FNV step on this CPU against 13 measured on the VR4300; this
// is the measurement that replaces the estimate.
//
// These are reported and never asserted on. A timing that moves with the
// emulator's scheduling is not a conformance property, and a gate that failed
// on one would be a gate nobody trusts.
void report_cost(const char* label, std::uint32_t ticks, std::uint32_t runs) {
    psx::Line line;
    line.text("info cost ")
        .text(label)
        .text(": ")
        .decimal(runs)
        .text(" runs in ")
        .decimal(ticks)
        .text(" ticks, ")
        .decimal((ticks * 1000U) / (psx::ticks_per_1000_microseconds * runs))
        .text(" us each, ~")
        .decimal(
            (ticks * psx::cpu_cycles_per_1000_ticks) / (runs * 1000U)
        )
        .text(" cycles each")
        .flush();
}

// FNV-1a-64 as engine/simulation writes it, isolated so the per-step cost can
// be attributed. This is a copy of the step, not a substitute for it: this
// target needs no hand-written arithmetic, because GCC expands the 64x64
// multiply inline over the hardware MULTU rather than calling __muldi3.
std::uint64_t fnv_step(std::uint64_t hash, std::uint8_t byte) {
    hash ^= static_cast<std::uint64_t>(byte);
    hash *= 0x00000100000001b3ULL;
    return hash;
}

void measures_the_cost() {
    auto created = sim::create_encounter(board(
        4,
        3,
        {
            unit(20, 200, sim::Side::second, {2, 1}, 5, 2, 1),
            unit(10, 100, sim::Side::first, {0, 1}, 8, 4, 0),
        }
    ));
    if (!created) {
        return;
    }

    volatile std::uint64_t sink = 0;
    constexpr std::uint32_t hash_runs = 64;
    std::uint32_t start = psx::clock_ticks();
    for (std::uint32_t run = 0; run < hash_runs; ++run) {
        sink = created.encounter.canonical_hash();
    }
    report_cost("canonical_hash", psx::clock_ticks() - start, hash_runs);

    constexpr std::uint32_t step_runs = 20000;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    start = psx::clock_ticks();
    for (std::uint32_t step = 0; step < step_runs; ++step) {
        hash = fnv_step(hash, static_cast<std::uint8_t>(step));
    }
    sink = hash;
    report_cost("fnv step", psx::clock_ticks() - start, step_runs);

    const auto bytes = pf::write_mock_package(package_source());
    const pf::LoadOptions options{
        {0, 1, 0}, pf::TargetProfile::portable, 0U, 1024, 10000
    };
    constexpr std::uint32_t load_runs = 16;
    start = psx::clock_ticks();
    for (std::uint32_t run = 0; run < load_runs; ++run) {
        const auto loaded = pf::load_mock_package(bytes, options);
        sink = loaded.package.content_revision;
    }
    report_cost("load_mock_package", psx::clock_ticks() - start, load_runs);

    constexpr std::uint32_t create_runs = 32;
    start = psx::clock_ticks();
    for (std::uint32_t run = 0; run < create_runs; ++run) {
        auto again = sim::create_encounter(board(
            4,
            3,
            {
                unit(20, 200, sim::Side::second, {2, 1}, 5, 2, 1),
                unit(10, 100, sim::Side::first, {0, 1}, 8, 4, 0),
            }
        ));
        sink = again.encounter.canonical_hash();
    }
    report_cost("create_encounter", psx::clock_ticks() - start, create_runs);
    (void)sink;
}

void reports_heap(const char* label) {
    const auto census = psx::heap_census();
    psx::Line line;
    line.text("info heap ")
        .text(label)
        .text(": capacity=")
        .decimal(psx::heap_capacity())
        .text(" free=")
        .decimal(census.free_bytes)
        .text(" live=")
        .decimal(census.allocated_bytes)
        .text(" peak=")
        .decimal(census.peak_allocated_bytes)
        .text(" largest_free=")
        .decimal(census.largest_free_block)
        .text(" allocations=")
        .decimal(census.allocations)
        .flush();
}

// A size this machine cannot have is refused, not wrapped into one it can.
//
// This is the only place the question can honestly be asked. `std::size_t` is
// thirty-two bits wide here and sixty-four on every host that builds this
// repository, and the defect lives exactly in that difference: the allocator
// adds a header and an alignment to the request before it looks for a block,
// and on a 32-bit machine a request near the top of the address space comes
// out of that sum *smaller than the minimum block*. It is then raised to the
// minimum block and answered: sixteen bytes handed to a caller that asked for
// four gigabytes, which proceeds to write them starting inside the heap.
//
// A host cannot reach the case at all, so nothing on a host can pin it. It is
// checked on the R3000A, over the real allocator, through the real
// `operator new`.
void refuses_a_size_this_machine_cannot_have() {
    const auto before = psx::heap_census();

    // The whole span the sum can wrap on, and the case just past the heap.
    // `std::nothrow`, because the throwing form of this runtime's
    // `operator new` prints and halts, which is the right answer to running
    // out of memory and the wrong one to being asked a question.
    const std::size_t impossible[] = {
        static_cast<std::size_t>(0xFFFFFFFFU),
        static_cast<std::size_t>(0xFFFFFFF8U),
        static_cast<std::size_t>(0xFFFFFFF1U),
        static_cast<std::size_t>(0x80000000U),
        static_cast<std::size_t>(psx::heap_capacity()) + 1U,
    };
    bool all_refused = true;
    for (const std::size_t size : impossible) {
        void* const block = ::operator new(size, std::nothrow);
        if (block != nullptr) {
            all_refused = false;
            ::operator delete(block, std::nothrow);
        }
    }
    expect(
        all_refused,
        "a request larger than the heap is refused rather than wrapped"
    );

    const auto after = psx::heap_census();
    expect(
        after.allocations == before.allocations &&
            after.live_allocations == before.live_allocations &&
            after.free_bytes == before.free_bytes,
        "and a refused request costs the heap nothing at all"
    );

    // The bound is a refusal, not a ceiling on the allocator: a request for
    // nearly the whole heap is still served. Nearly, because the block's own
    // header and the rounding up to eight come out of the block the payload
    // sits in, and the census reports blocks whole.
    const std::size_t nearly_everything =
        static_cast<std::size_t>(before.largest_free_block) - 16U;
    void* const largest = ::operator new(nearly_everything, std::nothrow);
    expect(
        largest != nullptr,
        "and a request for nearly the whole heap is still served"
    );
    ::operator delete(largest, std::nothrow);
}

// The heap must come back to where it started. A leak matters less on a
// machine with 2 MiB than on one with 64 KiB, but a leak is still a defect,
// and the whole-heap coalescing this runtime does is only correct if it is.
void returns_the_heap(std::uint32_t baseline_free) {
    const auto census = psx::heap_census();
    expect(
        census.live_allocations == 0,
        "every allocation the run made was returned"
    );
    expect(
        census.free_bytes == baseline_free,
        "the heap coalesced back to one free block"
    );
    expect(
        census.largest_free_block == census.free_bytes,
        "the heap is not fragmented at exit"
    );
}

}  // namespace

int main() {
    psx::start_clock();
    psx::print("grandleon playstation conformance");

    expect(emulator_present(), "PCSX-Redux's control port is present");

    // Ahead of the census, deliberately. It asks the allocator for nearly the
    // whole heap and gives it straight back, and the numbers this run ends by
    // reporting are meant to be the cost of the work rather than of the
    // question. `reset_heap_peak` below is what makes that true.
    refuses_a_size_this_machine_cannot_have();

    const auto baseline = psx::heap_census().free_bytes;
    psx::reset_heap_peak();
    reports_heap("start");

    reports_target_word_sizes();
    computes_stable_content_identity();
    reaches_the_teletype_through_printf();
    reproduces_the_random_substrate();
    matches_shared_reference_vector();
    rejects_commands_atomically();
    runs_the_tactics_policy();
    round_trips_a_package();
    replays_the_demo_package();
    measures_the_cost();

    reports_heap("end");
    returns_the_heap(baseline);

    // One machine-checkable line. A harness looks for exactly this.
    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .signed_decimal(checks - failures)
        .text("/")
        .signed_decimal(checks)
        .flush();

    // And the verdict again as a process exit code, so CI need not scrape a
    // log at all. An executable on this machine has nowhere to return to, so
    // this return is not the ordinary kind: Nugget's crt0 enters through
    // `cxxmain`, which walks the init array and then calls
    // `pcsx_exit(main(argc, argv))`. Under `-testmode` that ends the emulator
    // process with this value. Under anything else it pauses, which is why the
    // run script also imposes a timeout.
    return failures == 0 ? 0 : 1;
}
