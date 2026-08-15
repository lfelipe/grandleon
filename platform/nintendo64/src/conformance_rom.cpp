// SPDX-License-Identifier: MIT
// Nintendo 64 conformance ROM.
//
// This is not a game and not a renderer. It exists to answer one question that
// a successful compile cannot: does the portable engine produce the same
// canonical state on a big-endian MIPS console that it produces on the host?
//
// It replays the shared reference vector from tests/simulation/encounter_test.cpp
// and compares canonical_hash() against the same two pinned values, then
// exercises the package format and package runtime enough to prove they run
// rather than merely link.
//
// Results go to plain stdout. libdragon fans standard output out to every
// channel it has: the on-screen console for a person, and the emulator log for
// a script.

#include <libdragon.h>

#include <grandleon/core/content_identity.hpp>
#include <grandleon/core/random.hpp>
#include <grandleon/core/version.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    if (condition) {
        std::printf("ok   %s\n", message);
    } else {
        ++failures;
        std::printf("FAIL %s\n", message);
    }
}

void expect_hash(
    std::uint64_t actual,
    std::uint64_t expected,
    const char* message
) {
    ++checks;
    if (actual == expected) {
        std::printf("ok   %s (%016llx)\n", message,
                    static_cast<unsigned long long>(actual));
    } else {
        ++failures;
        std::printf(
            "FAIL %s: expected %016llx, got %016llx\n",
            message,
            static_cast<unsigned long long>(expected),
            static_cast<unsigned long long>(actual)
        );
    }
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

// The word sizes the determinism contract is built on. A silent change here
// would move every hash below, so it is checked before anything depends on it.
void reports_target_word_sizes() {
    expect(sizeof(std::uint64_t) == 8, "uint64_t is eight bytes");
    expect(sizeof(sim::UnitId) == 8, "UnitId is eight bytes");
    expect(sizeof(void*) == 4 || sizeof(void*) == 8, "pointer size is sane");
    const std::uint32_t probe = 0x01020304U;
    const auto* first = reinterpret_cast<const std::uint8_t*>(&probe);
    expect(*first == 0x01U, "target is big-endian");
    std::printf(
        "info sizeof: UnitSnapshot=%u Encounter=%u Command=%u Event=%u ptr=%u\n",
        static_cast<unsigned>(sizeof(sim::UnitSnapshot)),
        static_cast<unsigned>(sizeof(sim::Encounter)),
        static_cast<unsigned>(sizeof(sim::Command)),
        static_cast<unsigned>(sizeof(sim::Event)),
        static_cast<unsigned>(sizeof(void*))
    );
}

// The constant-folded identity mapping, evaluated on the target rather than by
// the host compiler, because it feeds the canonical hash.
void computes_stable_content_identity() {
    const auto id = core::stable_content_id_v1("grandleon/n64");
    expect(id == core::stable_content_id_v1("grandleon/n64"),
           "stable content identity is stable");
    expect(id != core::stable_content_id_v1("grandleon/n65"),
           "stable content identity separates distinct keys");
    const auto version = core::engine_version();
    std::printf(
        "info engine version %u.%u.%u\n",
        static_cast<unsigned>(version.major),
        static_cast<unsigned>(version.minor),
        static_cast<unsigned>(version.patch)
    );
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
// places the engine allocates per command. Running one proves the vector paths
// work against libdragon's heap, the movement field's bucket queue (a vector of
// vectors) included.
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
    const auto plan =
        tac::decide(snapshot, 10, tac::Behavior::pursue, {});
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
    source.target = pf::TargetProfile::nintendo64;
    source.sections = {
        {pf::SectionType::classes, 1, 0, pf::section_flag_required,
         {{100, {0x01U, 0x02U, 0x03U}}, {101, {0x04U}}}},
        {pf::SectionType::unit_types, 1, 0, pf::section_flag_required,
         {{200, {0x05U, 0x06U}}}},
    };
    return source;
}

// The container is written and read back byte by byte with shifts, so it should
// be endian-independent by construction. "Should be" is what this checks.
void round_trips_a_package() {
    const auto bytes = pf::write_mock_package(package_source());
    expect(!bytes.empty(), "package writer produces bytes");
    std::printf("info package is %u bytes\n",
                static_cast<unsigned>(bytes.size()));

    const pf::LoadOptions options{
        {0, 1, 0}, pf::TargetProfile::nintendo64, 0U, 1024, 10000
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

    // And the runtime must decline a package that has no encounters at all,
    // rather than decoding whatever happens to be at that offset.
    const auto missing = pr::load_encounter(loaded.package, 1);
    expect(
        missing.error == pr::EncounterLoadError::missing_section,
        "encounter loader reports a missing section"
    );
    const auto campaign = pr::load_campaign(loaded.package, 1);
    expect(
        campaign.error == pr::CampaignError::missing_section,
        "campaign loader reports a missing section"
    );
    expect(
        pf::error_name(pf::Error::checksum_mismatch) == "checksum_mismatch",
        "package error names are readable on target"
    );
}

}  // namespace

int main() {
    // The emulator log channel first, so that a failure during display setup is
    // still visible to a harness.
    debug_init_emulog();
    console_init();
    console_set_render_mode(RENDER_MANUAL);

    std::printf("grandleon n64 conformance\n");

    reports_target_word_sizes();
    computes_stable_content_identity();
    reproduces_the_random_substrate();
    matches_shared_reference_vector();
    rejects_commands_atomically();
    runs_the_tactics_policy();
    round_trips_a_package();

    // One machine-checkable line. A harness looks for exactly this.
    if (failures == 0) {
        std::printf("RESULT PASS %d/%d\n", checks, checks);
    } else {
        std::printf("RESULT FAIL %d/%d\n", checks - failures, checks);
    }
    console_render();

    // A ROM has nowhere to return to. The console stays on the rendered frame
    // for a person; the result keeps going out on the log channel so a capture
    // that attached late still sees it.
    while (true) {
        wait_ms(1000);
        std::printf(
            failures == 0 ? "RESULT PASS %d/%d\n" : "RESULT FAIL %d/%d\n",
            checks - failures,
            checks
        );
    }
}
