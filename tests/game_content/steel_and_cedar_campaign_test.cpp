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
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

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
    std::uint16_t width;
    std::uint16_t height;
    std::size_t solo_enemies;
    std::size_t coop_enemies;
    std::size_t coop_moments;
};

constexpr std::array<Stage, 5> stages{{
    {"bell_orchard", 8, 6, 3, 4, 2},
    {"cedar_deepwood", 14, 9, 5, 7, 2},
    {"cedar_watch", 16, 12, 6, 9, 2},
    {"gate_village", 18, 12, 6, 9, 2},
    {"twin_gate", 20, 14, 9, 11, 3},
}};

std::size_t count_units(
    const pr::EncounterLoadResult& board,
    sim::Side side,
    std::uint8_t player = 0
) {
    return static_cast<std::size_t>(std::count_if(
        board.definition.units.begin(), board.definition.units.end(),
        [side, player](const sim::UnitDefinition& unit) {
            return unit.side == side &&
                (player == 0U || unit.local_player == player);
        }
    ));
}

const sim::UnitDefinition* unit_from_source_key(
    const pr::EncounterLoadResult& board,
    std::uint64_t source_key
) {
    const auto identity = std::find_if(
        board.placements.begin(), board.placements.end(),
        [source_key](const pr::PlacementIdentity& placement) {
            return placement.source_key_id == source_key;
        }
    );
    if (identity == board.placements.end()) return nullptr;
    const auto unit = std::find_if(
        board.definition.units.begin(), board.definition.units.end(),
        [identity](const sim::UnitDefinition& candidate) {
            return candidate.id == identity->unit_id;
        }
    );
    return unit == board.definition.units.end() ? nullptr : &*unit;
}

std::set<unsigned> enemy_quadrants(const pr::EncounterLoadResult& board) {
    std::set<unsigned> quadrants;
    for (const sim::UnitDefinition& unit : board.definition.units) {
        if (unit.side != sim::Side::second) continue;
        const unsigned right =
            unit.position.x >= static_cast<std::int16_t>(board.definition.width / 2U)
                ? 1U : 0U;
        const unsigned lower =
            unit.position.y >= static_cast<std::int16_t>(board.definition.height / 2U)
                ? 2U : 0U;
        quadrants.insert(right + lower);
    }
    return quadrants;
}

}  // namespace

int main() {
    const std::string filename = std::string(GRANDLEON_SOURCE_DIR) +
        "/games/steel_and_cedar/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Steel and Cedar project opens");
    if (!input) return 1;
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    if (!parsed) return 1;

    const gc::GameSource& source = parsed.source;
    expect(source.campaigns.size() == 1U, "there is one focused campaign");
    expect(source.encounters.size() == stages.size(), "it has five battles");
    expect(source.maps.size() == stages.size(), "every battle has its own map");
    expect(source.unit_types.size() == 12U, "the cast stays purposefully small");
    if (!source.campaigns.empty()) {
        const gc::Campaign& campaign = source.campaigns.front();
        expect(
            campaign.player_mode == gc::LocalPlayerMode::choice,
            "the campaign explicitly offers one or two local players"
        );
        expect(campaign.roster.size() == 4U, "both armies have stable identities");
        expect(
            std::all_of(
                campaign.roster.begin(), campaign.roster.end(),
                [](const gc::CampaignMember& member) {
                    return member.join_node_id == 0U;
                }
            ),
            "the four identities are founded once rather than duplicated by mode"
        );
        std::set<std::uint64_t> members;
        for (const gc::CampaignMember& member : campaign.roster) {
            members.insert(member.id);
        }
        expect(members.size() == campaign.roster.size(), "every roster identity is unique");
    }

    std::array<int, 2> styles{};
    const std::uint64_t rowan_id = core::stable_content_id_v1("rowan_vale");
    const std::uint64_t boss_id = core::stable_content_id_v1("lord_masatora_unit");
    for (const gc::UnitType& unit : source.unit_types) {
        const std::uint8_t style = gc::resolved_character_style(source, unit);
        if (style == 0U) ++styles[0];
        if (style == 4U) ++styles[1];
        if (unit.id == rowan_id || unit.id == boss_id) {
            expect(
                gc::resolved_archetype(source, unit) == 5U,
                "commanders resolve to the shared Commander visual treatment"
            );
        }
    }
    expect(
        styles[0] == 6 && styles[1] == 6,
        "the cast balances Medieval and Sengoku presentation"
    );

    std::map<std::uint64_t, const gc::Map*> maps;
    std::set<std::vector<std::uint8_t>> layouts;
    for (const gc::Map& map : source.maps) {
        maps.emplace(map.id, &map);
        layouts.insert(map.terrain_kinds);
        expect(
            std::set<std::uint8_t>(
                map.terrain_kinds.begin(), map.terrain_kinds.end()
            ).size() >= 5U,
            "each map has an intentional mixed-terrain vocabulary"
        );
    }
    expect(layouts.size() == stages.size(), "no battle repeats a terrain layout");
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const Stage& stage = stages[index];
        const auto found = maps.find(core::stable_content_id_v1(stage.key));
        expect(found != maps.end(), std::string(stage.key) + " has a map");
        if (found == maps.end()) continue;
        expect(
            found->second->width == stage.width &&
                found->second->height == stage.height,
            std::string(stage.key) + " has its designed dimensions"
        );
        if (index > 0U) {
            expect(
                found->second->width >= 14U && found->second->height >= 9U,
                std::string(stage.key) + " extends beyond one console view"
            );
        }
    }

    const auto landmark_cells = [&maps](
        std::string_view map_key, std::string_view terrain_key
    ) {
        const auto found = maps.find(core::stable_content_id_v1(map_key));
        if (found == maps.end()) return std::size_t{};
        const std::uint64_t terrain_id = core::stable_content_id_v1(terrain_key);
        return static_cast<std::size_t>(std::count(
            found->second->terrain.begin(), found->second->terrain.end(),
            terrain_id
        ));
    };
    expect(
        landmark_cells("cedar_deepwood", "mountain_ancient_grove") == 8U,
        "Deepwood authors two explicit four-cell ancient groves"
    );
    expect(
        landmark_cells("gate_village", "mountain_village_house") == 24U,
        "Gate Village authors six explicit four-cell houses"
    );
    for (const std::string_view map_key : {"cedar_deepwood", "gate_village"}) {
        const gc::Map& map = *maps.at(core::stable_content_id_v1(map_key));
        for (std::size_t cell = 0; cell < map.terrain.size(); ++cell) {
            const bool landmark =
                map.terrain[cell] == core::stable_content_id_v1(
                    "mountain_ancient_grove"
                ) ||
                map.terrain[cell] == core::stable_content_id_v1(
                    "mountain_village_house"
                );
            if (landmark) {
                expect(
                    map.terrain_kinds[cell] == gc::terrain_kind_index("mountain"),
                    "every landmark footprint keeps impassable mountain rules"
                );
            }
        }
    }

    const auto objective_count = [&source](gc::ObjectiveKind kind) {
        return std::count_if(
            source.objectives.begin(), source.objectives.end(),
            [kind](const gc::Objective& objective) {
                return objective.kind == kind;
            }
        );
    };
    expect(
        objective_count(gc::ObjectiveKind::defeat_all_opponents) == 1 &&
        objective_count(gc::ObjectiveKind::survive_rounds) == 1 &&
        objective_count(gc::ObjectiveKind::defeat_target) == 2 &&
        objective_count(gc::ObjectiveKind::protect_target) == 1,
        "the campaign introduces four objective shapes deliberately"
    );
    const std::uint64_t hold_id = core::stable_content_id_v1("hold_watch");
    const std::uint64_t protect_id = core::stable_content_id_v1("protect_rowan");
    const auto hold = std::find_if(
        source.objectives.begin(), source.objectives.end(),
        [hold_id](const gc::Objective& objective) { return objective.id == hold_id; }
    );
    const auto protect = std::find_if(
        source.objectives.begin(), source.objectives.end(),
        [protect_id](const gc::Objective& objective) {
            return objective.id == protect_id;
        }
    );
    expect(
        hold != source.objectives.end() && hold->rounds == 20U,
        "Cedar Watch is a substantial twenty-round defense"
    );
    expect(
        protect != source.objectives.end() &&
            protect->target_placement_id == core::stable_content_id_v1("rowan"),
        "the defense and later battles clearly protect Commander Rowan"
    );

    const gc::CompileResult compiled = gc::compile(source);
    expect(static_cast<bool>(compiled), "and compiles");
    if (!compiled) return 1;
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 20'000}
    );
    expect(static_cast<bool>(loaded), "and its portable package loads");
    if (!loaded) return 1;
    expect(
        static_cast<bool>(pf::load_mock_package(
            compiled.package,
            {{0, 1, 0}, pf::TargetProfile::nintendo64, 0, 48, 20'000}
        )),
        "the complete package stays within the console encounter budget"
    );

    const std::uint64_t akari_type = core::stable_content_id_v1("akari_mori");
    const std::uint64_t ren_type = core::stable_content_id_v1("ren_sato");
    const std::array<std::uint64_t, 5> coop_dialogues{{
        core::stable_content_id_v1("orchard_coop"),
        core::stable_content_id_v1("deepwood_coop"),
        core::stable_content_id_v1("watch_coop"),
        core::stable_content_id_v1("village_coop"),
        core::stable_content_id_v1("gate_coop"),
    }};
    for (std::size_t index = 0; index < stages.size(); ++index) {
        const Stage& stage = stages[index];
        const std::string key = std::string("steel_and_cedar/") + stage.key;
        const std::uint64_t id = core::stable_content_id_v1(key);
        const pr::EncounterLoadResult solo = pr::load_encounter(loaded.package, id, {1});
        const pr::EncounterLoadResult coop = pr::load_encounter(loaded.package, id, {2});
        expect(solo && coop, std::string(stage.key) + " loads for 1P and 2P");
        if (!solo || !coop) continue;

        expect(
            count_units(solo, sim::Side::first) == 2U &&
                count_units(solo, sim::Side::second) == stage.solo_enemies &&
                count_units(solo, sim::Side::first, 2) == 0U,
            std::string(stage.key) + " forms Rowan's complete solo battle"
        );
        expect(
            count_units(coop, sim::Side::first) == 4U &&
                count_units(coop, sim::Side::second) == stage.coop_enemies &&
                count_units(coop, sim::Side::first, 2) == 2U,
            std::string(stage.key) + " adds Akari's army and scaled opposition"
        );
        expect(
            std::none_of(
                solo.definition.units.begin(), solo.definition.units.end(),
                [akari_type, ren_type](const sim::UnitDefinition& unit) {
                    return unit.unit_type_id == akari_type || unit.unit_type_id == ren_type;
                }
            ),
            std::string(stage.key) + " keeps Akari's army out of the solo story"
        );
        expect(
            solo.moments.size() == 1U && coop.moments.size() == stage.coop_moments,
            std::string(stage.key) + " selects the authored story variant"
        );
        expect(
            std::none_of(
                solo.moments.begin(), solo.moments.end(),
                [index, &coop_dialogues](const pr::EncounterMoment& moment) {
                    return moment.dialogue_id == coop_dialogues[index];
                }
            ) && std::any_of(
                coop.moments.begin(), coop.moments.end(),
                [index, &coop_dialogues](const pr::EncounterMoment& moment) {
                    return moment.dialogue_id == coop_dialogues[index];
                }
            ),
            std::string(stage.key) + " gives Akari a co-op-only story beat"
        );
        expect(
            static_cast<bool>(sim::create_encounter(solo.definition)) &&
                static_cast<bool>(sim::create_encounter(coop.definition)),
            std::string(stage.key) + " opens under both player counts"
        );
        if (index > 0U) {
            expect(
                enemy_quadrants(coop).size() >= 3U,
                std::string(stage.key) + " distributes tactical groups across the board"
            );
        }
    }

    for (const char* key : {"cedar_deepwood", "cedar_watch", "gate_village", "twin_gate"}) {
        const std::uint64_t encounter = core::stable_content_id_v1(
            std::string("steel_and_cedar/") + key
        );
        const auto solo = pr::load_encounter(loaded.package, encounter, {1});
        const auto coop = pr::load_encounter(loaded.package, encounter, {2});
        const auto solo_rowan = unit_from_source_key(
            solo, core::stable_content_id_v1("rowan")
        );
        const auto coop_rowan = unit_from_source_key(
            coop, core::stable_content_id_v1("rowan")
        );
        expect(
            solo_rowan != nullptr && coop_rowan != nullptr &&
                !(solo_rowan->position == coop_rowan->position),
            std::string(key) + " separates the 2P opening without duplicating Rowan"
        );
        std::set<std::uint64_t> keys;
        for (const pr::PlacementIdentity& identity : coop.placements) {
            keys.insert(identity.source_key_id);
        }
        expect(
            keys.size() == coop.placements.size(),
            std::string(key) + " fields each persistent roster identity once"
        );
    }

    const auto watch = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/cedar_watch"), {2}
    );
    std::set<std::uint32_t> first_arrival_rounds;
    std::size_t recurring_waves = 0;
    std::size_t opening_enemies = 0;
    for (const sim::UnitDefinition& unit : watch.definition.units) {
        if (unit.side != sim::Side::second) continue;
        if (unit.arrival_round == 0U) {
            ++opening_enemies;
        } else {
            first_arrival_rounds.insert(unit.arrival_round);
            if (unit.arrival_times > 1U) ++recurring_waves;
        }
    }
    expect(opening_enemies == 2U, "Cedar Watch opens readably instead of as an enemy blob");
    expect(
        first_arrival_rounds == std::set<std::uint32_t>{3U, 4U, 5U, 6U, 8U, 10U, 12U} &&
            recurring_waves == 7U,
        "its seven staggered wave groups span both fronts and the full defense"
    );

    const auto finale = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/twin_gate"), {2}
    );
    expect(
        finale && std::any_of(
            finale.definition.units.begin(), finale.definition.units.end(),
            [boss_id](const sim::UnitDefinition& unit) {
                return unit.side == sim::Side::second &&
                    unit.unit_type_id == boss_id;
            }
        ),
        "the final map fields Lord Masatora as its boss"
    );
    expect(
        finale && std::count_if(
            finale.definition.units.begin(), finale.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.arrival_round != 0U;
            }
        ) == 2,
        "the final map adds two readable reinforcement pressures"
    );

    if (failures == 0) {
        std::cout << "steel and cedar: distinct solo and co-op campaigns across five designed battles\n";
    }
    return failures == 0 ? 0 : 1;
}
