// SPDX-License-Identifier: MIT
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: grandleon_demo_playthrough <demo.gpk>\n";
        return 64;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open demo package\n";
        return 66;
    }
    const std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const auto loaded = pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    if (!loaded) {
        std::cerr << "package rejected: "
                  << pf::error_name(loaded.error) << '\n';
        return 65;
    }

    const auto encounter_id =
        core::stable_content_id_v1("demo_campaign/bridge_encounter");
    auto decoded = pr::load_encounter(loaded.package, encounter_id);
    if (!decoded) {
        std::cerr << "encounter rejected: "
                  << pr::error_name(decoded.error) << '\n';
        return 65;
    }
    auto created = sim::create_encounter(decoded.definition);
    if (!created) {
        std::cerr << "encounter invalid: "
                  << sim::error_name(created.error) << '\n';
        return 65;
    }
    auto campaign = pr::load_campaign(
        loaded.package,
        core::stable_content_id_v1("demo_campaign")
    );
    if (!campaign) {
        std::cerr << "campaign rejected: "
                  << pr::error_name(campaign.error) << '\n';
        return 65;
    }
    pr::CampaignCursor cursor(std::move(campaign.definition));
    if (cursor.current().encounter_id != encounter_id) {
        std::cerr << "campaign did not begin at demo encounter\n";
        return 65;
    }

    const auto first_id = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/dawn_guard_leader"
    );
    const auto second_id = core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/river_watch_leader"
    );
    // Four commands, three turns, and nobody stands still.
    //
    // The first two are one turn: the rider's class carries two action points,
    // so the walk onto the bridge leaves it a point to strike with and the turn
    // does not pass until the strike closes it. That is the whole of what this
    // board is for, and it is why no `wait` separates them. The second side is
    // not owed the board after a walk, and a script that handed it one would be
    // demonstrating a rule this board does not have.
    //
    // The rest is the arithmetic the two classes make. Seven health against a
    // blow of three (four strength less one defence, the same for both) is
    // three exchanges, and a counter is free: the rider's opening strike draws
    // one, the picket's own swing draws another that leaves it on one, and the
    // rider's last blow lands on a picket that has already spent everything it
    // had. The reference stream is a fight rather than a replay against a
    // statue, which is what makes the value below worth agreeing about.
    const sim::Command commands[] = {
        {sim::CommandType::move, first_id, {1, 1}, 0},
        {sim::CommandType::attack, first_id, {}, second_id},
        {sim::CommandType::attack, second_id, {}, first_id},
        {sim::CommandType::attack, first_id, {}, second_id},
    };
    for (const sim::Command& command : commands) {
        const auto applied = created.encounter.apply(command);
        if (!applied) {
            std::cerr << "reference command rejected: "
                      << sim::error_name(applied.error) << '\n';
            return 65;
        }
    }
    const auto snapshot = created.encounter.snapshot();
    if (snapshot.outcome != sim::Outcome::first_side_won) {
        std::cerr << "reference playthrough did not reach first-side victory\n";
        return 1;
    }
    // Golden value for the demo encounter, played through this exact command
    // sequence. The browser build reaches the same value from the same source
    // project, which is what makes "play the demo and read the hash" a real
    // native/browser conformance check rather than a smoke test.
    //
    // A class stat is the widest thing that can move it. `canonical_hash`
    // folds every unit's own numbers in, and a class stands under every
    // character placed from it, so editing one line of `baseStats` moves this
    // value, the PlayStation's copy of it, and every count and health any test
    // of this board reads. Both classes on this board carry two action points
    // and seven health; changing either is not a tuning pass but a golden move,
    // and the protocol for one is to re-derive rather than to adjust.
    constexpr std::uint64_t demo_completed_hash = 0x673e5a59765c94c5ULL;
    if (created.encounter.canonical_hash() != demo_completed_hash) {
        std::cerr << "demo canonical hash changed: expected "
                  << demo_completed_hash << " got "
                  << created.encounter.canonical_hash() << '\n';
        return 1;
    }
    if (cursor.advance_after(snapshot.outcome) != pr::CampaignError::none ||
        !cursor.complete() ||
        cursor.current().id !=
            core::stable_content_id_v1("demo_complete")) {
        std::cerr << "campaign did not advance to demo terminal\n";
        return 1;
    }
    std::cout << "first_side_won activations="
              << snapshot.activation_count << " hash="
              << created.encounter.canonical_hash()
              << " campaign=demo_complete\n";
    return 0;
}
