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
#include <set>
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

constexpr std::array<std::string_view, 7> styles{
    "medieval", "scifi", "mythical", "nature",
    "sengoku", "undead", "pirates",
};

}  // namespace

int main() {
    const std::string filename = std::string(GRANDLEON_SOURCE_DIR) +
        "/games/sevenfold/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Sevenfold project opens");
    if (!input) return 1;
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    if (!parsed) return 1;

    const gc::GameSource& source = parsed.source;
    expect(source.campaigns.size() == 1U, "there is one showcase campaign");
    expect(source.encounters.size() == styles.size(), "there are seven gates");
    expect(source.maps.size() == styles.size(), "every gate has its own map");
    expect(source.unit_types.size() == 56U, "seven styles cover eight roles");
    if (!source.campaigns.empty()) {
        expect(
            source.campaigns.front().roster.size() == 28U,
            "all four allies from every gate are available to Stage jumps"
        );
    }

    std::array<std::array<int, 8>, 7> role_counts{};
    std::array<std::array<int, 2>, 7> body_counts{};
    for (const gc::UnitType& unit : source.unit_types) {
        const std::uint8_t style = gc::resolved_character_style(source, unit);
        const std::uint8_t role = gc::resolved_archetype(source, unit);
        const std::uint8_t body = gc::resolved_character_figure(source, unit);
        if (style < role_counts.size() && role < role_counts.front().size()) {
            ++role_counts[style][role];
        }
        if (style < body_counts.size() && body < body_counts.front().size()) {
            ++body_counts[style][body];
        }
    }
    for (std::size_t style = 0; style < styles.size(); ++style) {
        expect(
            std::all_of(
                role_counts[style].begin(), role_counts[style].end(),
                [](int count) { return count == 1; }
            ),
            std::string(styles[style]) + " fields each archetype exactly once"
        );
        expect(
            body_counts[style][0] == 4 && body_counts[style][1] == 4,
            std::string(styles[style]) + " mixes four male and four female units"
        );
    }

    std::set<std::uint8_t> terrain_kinds;
    std::set<std::vector<std::uint8_t>> layouts;
    for (const gc::Map& map : source.maps) {
        terrain_kinds.insert(map.terrain_kinds.begin(), map.terrain_kinds.end());
        layouts.insert(map.terrain_kinds);
        expect(
            std::set<std::uint8_t>(
                map.terrain_kinds.begin(), map.terrain_kinds.end()
            ).size() >= 4U,
            "each gate mixes at least four terrain kinds"
        );
    }
    expect(terrain_kinds.size() == 13U, "the maps cover all thirteen terrains");
    expect(layouts.size() == styles.size(), "all seven map layouts are distinct");

    const gc::CompileResult compiled = gc::compile(source);
    expect(static_cast<bool>(compiled), "and compiles");
    if (!compiled) return 1;
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 20'000}
    );
    expect(static_cast<bool>(loaded), "and its portable package loads");
    if (!loaded) return 1;

    for (std::string_view style : styles) {
        const std::string key = std::string("sevenfold_gate/") +
            std::string(style);
        const std::uint64_t id = core::stable_content_id_v1(key);
        const pr::EncounterLoadResult solo =
            pr::load_encounter(loaded.package, id, {1});
        const pr::EncounterLoadResult coop =
            pr::load_encounter(loaded.package, id, {2});
        expect(solo && coop, std::string(style) + " loads for 1P and 2P");
        if (!solo || !coop) continue;

        const auto count = [](const pr::EncounterLoadResult& board,
                              sim::Side side, std::uint8_t player) {
            return static_cast<std::size_t>(std::count_if(
                board.definition.units.begin(), board.definition.units.end(),
                [side, player](const sim::UnitDefinition& unit) {
                    return unit.side == side &&
                        (player == 0U || unit.local_player == player);
                }
            ));
        };
        expect(
            count(solo, sim::Side::first, 0) == 4U &&
                count(solo, sim::Side::second, 0) == 4U &&
                count(solo, sim::Side::first, 2) == 0U,
            std::string(style) + " is a small four-versus-four solo battle"
        );
        expect(
            count(coop, sim::Side::first, 0) == 4U &&
                count(coop, sim::Side::second, 0) == 5U &&
                count(coop, sim::Side::first, 2) == 2U,
            std::string(style) + " splits allies and adds one co-op opponent"
        );
        expect(
            static_cast<bool>(sim::create_encounter(solo.definition)) &&
                static_cast<bool>(sim::create_encounter(coop.definition)),
            std::string(style) + " opens under both player counts"
        );
    }

    if (failures == 0) {
        std::cout << "sevenfold campaign: complete art and terrain tour in 1P/2P\n";
    }
    return failures == 0 ? 0 : 1;
}
