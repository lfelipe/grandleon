// SPDX-License-Identifier: MIT
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

struct Stage final {
    const char* key;
    std::size_t allies;
    std::size_t solo_enemies;
    std::size_t coop_enemies;
    std::size_t player_two_allies;
};

constexpr std::array<Stage, 5> stages{{
    {"rowanfield", 2, 2, 3, 1},
    {"thorn_road", 4, 4, 6, 2},
    {"glasswater", 6, 6, 8, 3},
    {"ember_vault", 6, 6, 7, 3},
    {"hollow_throne", 6, 4, 5, 3},
}};

}  // namespace

int main() {
    const std::string filename = std::string(GRANDLEON_SOURCE_DIR) +
                                 "/games/emberbound/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Emberbound project opens");
    if (!input) return 1;
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    if (!parsed) return 1;
    expect(
        parsed.source.campaigns.size() == 1U,
        "the player-facing project contains exactly one campaign"
    );
    expect(
        parsed.source.encounters.size() == stages.size(),
        "that campaign contains exactly five battles"
    );
    if (!parsed.source.campaigns.empty()) {
        const gc::Campaign& campaign = parsed.source.campaigns.front();
        expect(
            campaign.roster.size() == 6U,
            "the two heroes recruit two characters for each co-op army"
        );
        const auto joining_at = [&campaign](std::string_view node) {
            const std::uint64_t id = core::stable_content_id_v1(node);
            return static_cast<std::size_t>(std::count_if(
                campaign.roster.begin(), campaign.roster.end(),
                [id](const gc::CampaignMember& member) {
                    return member.join_node_id == id;
                }
            ));
        };
        expect(
            joining_at("rowanfield") == 2U &&
                joining_at("thorn_road") == 2U,
            "the army grows after each of the first two battles"
        );
        expect(
            std::count_if(
                campaign.roster.begin(), campaign.roster.end(),
                [](const gc::CampaignMember& member) {
                    return member.join_node_id == 0U;
                }
            ) == 2,
            "Auren and Senna are the founding company"
        );

        const std::uint64_t defeat_node =
            core::stable_content_id_v1("campaign_defeat");
        std::size_t protected_battles = 0;
        for (const gc::CampaignNode& node : campaign.nodes) {
            if (node.kind != gc::CampaignNodeKind::encounter) continue;
            bool has_victory = false;
            bool has_defeat = false;
            for (const gc::CampaignConditionalTarget& target :
                 node.conditional_targets) {
                if (target.predicates.size() != 1U) continue;
                const gc::CampaignPredicate& predicate =
                    target.predicates.front();
                if (predicate.kind !=
                    gc::CampaignPredicateKind::objective_result) {
                    continue;
                }
                has_victory = has_victory ||
                    predicate.result == gc::ObjectiveOutcome::satisfied;
                has_defeat = has_defeat ||
                    (predicate.result == gc::ObjectiveOutcome::failed &&
                     target.target_id == defeat_node);
            }
            if (node.unconditional_targets.empty() && has_victory &&
                has_defeat) {
                ++protected_battles;
            }
        }
        expect(
            protected_battles == stages.size(),
            "every battle separates victory progression from the defeat ending"
        );
        const auto has_node = [&campaign](std::string_view key,
                                          gc::CampaignNodeKind kind) {
            const std::uint64_t id = core::stable_content_id_v1(key);
            return std::any_of(
                campaign.nodes.begin(), campaign.nodes.end(),
                [id, kind](const gc::CampaignNode& node) {
                    return node.id == id && node.kind == kind;
                }
            );
        };
        expect(
            has_node("campaign_complete", gc::CampaignNodeKind::terminal) &&
                has_node("campaign_defeat", gc::CampaignNodeKind::story) &&
                has_node("campaign_failed", gc::CampaignNodeKind::terminal),
            "victory and defeat have distinct authored endings"
        );
    }

    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    if (!compiled) return 1;
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 20'000}
    );
    expect(static_cast<bool>(loaded), "and its portable package loads");
    if (!loaded) return 1;

    for (const Stage& stage : stages) {
        const std::string key = std::string("emberbound/") + stage.key;
        const std::uint64_t id = core::stable_content_id_v1(key);
        const pr::EncounterLoadResult solo =
            pr::load_encounter(loaded.package, id, {1});
        const pr::EncounterLoadResult coop =
            pr::load_encounter(loaded.package, id, {2});
        expect(solo && coop, std::string(stage.key) + " loads for 1P and 2P");
        if (!solo || !coop) continue;

        const auto count_side = [](const pr::EncounterLoadResult& board,
                                   sim::Side side) {
            return static_cast<std::size_t>(std::count_if(
                board.definition.units.begin(), board.definition.units.end(),
                [side](const sim::UnitDefinition& unit) {
                    return unit.side == side;
                }
            ));
        };
        expect(
            count_side(solo, sim::Side::first) == stage.allies &&
                count_side(coop, sim::Side::first) == stage.allies,
            std::string(stage.key) + " fields the growing allied army"
        );
        expect(
            count_side(solo, sim::Side::second) == stage.solo_enemies &&
                count_side(coop, sim::Side::second) == stage.coop_enemies,
            std::string(stage.key) + " scales its opposition for co-op"
        );
        const std::size_t solo_p2 = static_cast<std::size_t>(std::count_if(
            solo.definition.units.begin(), solo.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.side == sim::Side::first && unit.local_player == 2U;
            }
        ));
        const std::size_t coop_p2 = static_cast<std::size_t>(std::count_if(
            coop.definition.units.begin(), coop.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.side == sim::Side::first && unit.local_player == 2U;
            }
        ));
        expect(
            solo_p2 == 0U && coop_p2 == stage.player_two_allies,
            std::string(stage.key) +
                " is one complete army in solo and two owned armies in co-op"
        );
        expect(
            static_cast<bool>(sim::create_encounter(solo.definition)) &&
                static_cast<bool>(sim::create_encounter(coop.definition)),
            std::string(stage.key) + " opens under both party sizes"
        );
    }

    const pr::EncounterLoadResult finale = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("emberbound/hollow_throne"), {2}
    );
    const std::uint64_t regent = core::stable_content_id_v1("regent_veyr");
    expect(
        finale && std::any_of(
            finale.definition.units.begin(), finale.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.side == sim::Side::second &&
                       unit.unit_type_id == regent;
            }
        ),
        "the fifth battle contains Regent Veyr as its boss"
    );

    if (failures == 0) {
        std::cout << "emberbound campaign: five growing 1P/2P battles and a boss\n";
    }
    return failures == 0 ? 0 : 1;
}
