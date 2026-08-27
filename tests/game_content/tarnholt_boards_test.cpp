// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

// Every board the shipped campaign fights, as the compiler builds it.
//
// The browser builds the same six from the same `project.json` with no package
// compiler in between, and `editor/src/domain/tarnholt-conformance.test.ts`
// asserts these same numbers. Agreement says both roads derive the same
// identities, stats, placements and terrain; a disagreement says one of them
// has quietly grown its own idea of what the content means.
//
// **Why six and not the two that already had golden hashes.** Those two are
// pinned in `games/tarnholt/src/play_tarnholt.cpp` and are deliberately the
// campaign's plain boards, so that a hash moving there means one thing. The
// four here are the ones carrying what the engine has grown since: a character
// who can be talked to, a board won by clearing it, a deployment region with
// two waves arriving behind it, and the Marshal the last Stage is decided by.
// Nothing compared the two pipelines on any of that, and the first thing this
// test found when it was written was that the browser could not build the last
// Stage at all: it matched an objective's target on the placement's own id
// where the compiler matches on the member's, so `keep_mirea_alive` on a board
// that fields Captain Mirea as `dawn_commander_coldgate` resolved to nobody.
//
// These are opening arrangements, before a single command, which is what makes
// them the right thing to hold a second pipeline to: they pin what the content
// *is* rather than what some scripted sequence did to it.
//
// They say nothing about the weapon triangle, deliberately. See
// `tests/simulation/canonical_hash_test.cpp`: which kinds beat which is content
// a battle names rather than state it holds, so two boards differing only in
// their table are one arrangement and share a hash.

namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string hex(std::uint64_t value) {
    static const char* digits = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = digits[value & 0xF];
        value >>= 4;
    }
    return out;
}

struct Board final {
    const char* node;
    std::uint64_t hash;
    const char* carries;
};

// The six, in the order the campaign reaches them.
constexpr Board boards[] = {
    {"fordlight_battle", 0x24291ee6496e0494ULL, "four a side and nothing else"},
    {"harrow_burn_battle", 0xa69229c47344caf9ULL, "somebody who can be talked to"},
    {"sunken_mill_battle", 0xfdd6c129eeae3a75ULL, "a board won by clearing it"},
    {"emberhall_battle", 0x413e244a57ceaa5cULL, "a deployment region and two waves"},
    {"ashen_watch_battle", 0xd8ead446b269d0bbULL, "two objectives, one of them a life"},
    {"coldgate_battle", 0xf27ba4cec59d5bc5ULL, "a deployment region and the Marshal"},
};

}  // namespace

int main() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt project opens");
    if (!input) return 1;
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    if (!parsed || !compiled) return 1;
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    if (!loaded) return 1;

    const pr::CampaignLoadResult campaign =
        pr::load_campaign(loaded.package, core::stable_content_id_v1("tarnholt_line"));
    expect(static_cast<bool>(campaign), "the campaign decodes");
    if (!campaign) return 1;

    for (const Board& board : boards) {
        const std::uint64_t encounter_id = cr::encounter_of_node(
            campaign.definition,
            cr::campaign_node_ref(
                loaded.package.game_id, core::stable_content_id_v1(board.node)
            )
        );
        expect(
            encounter_id != 0,
            std::string(board.node) + " names a board"
        );
        if (encounter_id == 0) continue;
        const pr::EncounterLoadResult decoded =
            pr::load_encounter(loaded.package, encounter_id);
        expect(
            static_cast<bool>(decoded), std::string(board.node) + " decodes"
        );
        if (!decoded) continue;
        auto created = sim::create_encounter(decoded.definition);
        expect(
            static_cast<bool>(created),
            std::string(board.node) + " opens: " + board.carries
        );
        if (!created) continue;
        expect(
            created.encounter.canonical_hash() == board.hash,
            std::string(board.node) + " reaches its golden opening hash, got " +
                hex(created.encounter.canonical_hash())
        );
    }

    if (failures == 0) {
        std::cout << "tarnholt boards: six openings, as the compiler builds "
                     "them\n";
    }
    return failures == 0 ? 0 : 1;
}
