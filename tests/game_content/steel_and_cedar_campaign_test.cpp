// SPDX-License-Identifier: MIT
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>
#include <grandleon/tactics/policy.hpp>
#include <grandleon/view/landmark.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace tac = grandleon::tactics;
namespace view = grandleon::view;

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
    {"cedar_watch", 16, 12, 14, 25, 2},
    {"gate_village", 18, 12, 6, 9, 2},
    {"twin_gate", 20, 14, 4, 7, 3},
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

struct ConflictExtent final {
    unsigned width{};
    unsigned height{};
};

ConflictExtent conflict_extent(const pr::EncounterLoadResult& board) {
    std::int16_t minimum_x = std::numeric_limits<std::int16_t>::max();
    std::int16_t minimum_y = std::numeric_limits<std::int16_t>::max();
    std::int16_t maximum_x = std::numeric_limits<std::int16_t>::min();
    std::int16_t maximum_y = std::numeric_limits<std::int16_t>::min();
    for (const sim::UnitDefinition& unit : board.definition.units) {
        minimum_x = std::min(minimum_x, unit.position.x);
        minimum_y = std::min(minimum_y, unit.position.y);
        maximum_x = std::max(maximum_x, unit.position.x);
        maximum_y = std::max(maximum_y, unit.position.y);
    }
    if (board.definition.units.empty()) return {};
    return {
        static_cast<unsigned>(maximum_x - minimum_x + 1),
        static_cast<unsigned>(maximum_y - minimum_y + 1),
    };
}

std::size_t largest_opening_group_cell(
    const pr::EncounterLoadResult& board
) {
    std::map<std::pair<int, int>, std::size_t> groups;
    for (const sim::UnitDefinition& unit : board.definition.units) {
        if (unit.side != sim::Side::second || unit.arrival_round != 0U) {
            continue;
        }
        ++groups[{unit.position.x / 4, unit.position.y / 4}];
    }
    std::size_t largest = 0U;
    for (const auto& [region, count] : groups) {
        static_cast<void>(region);
        largest = std::max(largest, count);
    }
    return largest;
}

std::vector<unsigned> terrain_distances(
    const sim::EncounterDefinition& board,
    const sim::UnitDefinition& unit
) {
    constexpr unsigned unreachable = std::numeric_limits<unsigned>::max();
    const std::size_t cells =
        static_cast<std::size_t>(board.width) * board.height;
    std::vector<unsigned> distance(cells, unreachable);
    const auto index_of = [&board](sim::Position position) {
        return static_cast<std::size_t>(position.y) * board.width +
            static_cast<std::size_t>(position.x);
    };
    std::deque<sim::Position> frontier;
    distance[index_of(unit.position)] = 0U;
    frontier.push_back(unit.position);
    constexpr std::array<sim::Position, 4> steps{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};
    while (!frontier.empty()) {
        const sim::Position position = frontier.front();
        frontier.pop_front();
        for (const sim::Position step : steps) {
            const sim::Position next{
                static_cast<std::int16_t>(position.x + step.x),
                static_cast<std::int16_t>(position.y + step.y),
            };
            if (next.x < 0 || next.y < 0 ||
                next.x >= static_cast<std::int16_t>(board.width) ||
                next.y >= static_cast<std::int16_t>(board.height)) {
                continue;
            }
            const std::size_t next_index = index_of(next);
            const sim::Terrain terrain = board.terrain.empty()
                ? sim::Terrain::open : board.terrain[next_index];
            if (!sim::can_enter(terrain, unit.crossings) ||
                distance[next_index] != unreachable) {
                continue;
            }
            distance[next_index] = distance[index_of(position)] + 1U;
            frontier.push_back(next);
        }
    }
    return distance;
}

struct NearestFront final {
    unsigned distance{std::numeric_limits<unsigned>::max()};
    sim::UnitId enemy{};
};

NearestFront nearest_front_for_player(
    const pr::EncounterLoadResult& board,
    std::uint8_t local_player
) {
    NearestFront nearest;
    for (const sim::UnitDefinition& ally : board.definition.units) {
        if (ally.side != sim::Side::first ||
            ally.local_player != local_player || ally.arrival_round != 0U) {
            continue;
        }
        const std::vector<unsigned> distance =
            terrain_distances(board.definition, ally);
        for (const sim::UnitDefinition& enemy : board.definition.units) {
            if (enemy.side != sim::Side::second ||
                enemy.arrival_round != 0U) {
                continue;
            }
            const std::size_t cell =
                static_cast<std::size_t>(enemy.position.y) *
                    board.definition.width +
                static_cast<std::size_t>(enemy.position.x);
            if (distance[cell] < nearest.distance ||
                (distance[cell] == nearest.distance &&
                 (nearest.enemy == 0U || enemy.id < nearest.enemy))) {
                nearest = {distance[cell], enemy.id};
            }
        }
    }
    return nearest;
}

bool every_enemy_has_a_route_from(
    const pr::EncounterLoadResult& board,
    std::uint8_t local_player
) {
    constexpr unsigned unreachable = std::numeric_limits<unsigned>::max();
    std::vector<std::vector<unsigned>> routes;
    for (const sim::UnitDefinition& ally : board.definition.units) {
        if (ally.side == sim::Side::first &&
            ally.local_player == local_player && ally.arrival_round == 0U) {
            routes.push_back(terrain_distances(board.definition, ally));
        }
    }
    if (routes.empty()) return false;
    for (const sim::UnitDefinition& enemy : board.definition.units) {
        if (enemy.side != sim::Side::second) continue;
        const std::size_t cell =
            static_cast<std::size_t>(enemy.position.y) *
                board.definition.width +
            static_cast<std::size_t>(enemy.position.x);
        if (std::none_of(
                routes.begin(), routes.end(),
                [cell](const std::vector<unsigned>& route) {
                    return route[cell] != unreachable;
                }
            )) {
            return false;
        }
    }
    return true;
}

std::uint32_t last_arrival_round(
    const sim::EncounterDefinition& board
) {
    std::uint32_t last = 0U;
    for (const sim::UnitDefinition& unit : board.units) {
        if (unit.arrival_round == 0U) continue;
        std::uint32_t arrival = unit.arrival_round;
        if (unit.arrival_times > 1U) {
            arrival += unit.arrival_every *
                static_cast<std::uint32_t>(unit.arrival_times - 1U);
        }
        last = std::max(last, arrival);
    }
    return last;
}

bool wait_until_completed_round(
    sim::Encounter& encounter,
    std::uint32_t target,
    std::size_t& commands
) {
    while (encounter.snapshot().round < target &&
           encounter.snapshot().outcome == sim::Outcome::ongoing &&
           commands < 10'000U) {
        const sim::EncounterSnapshot snapshot = encounter.snapshot();
        const auto actor = std::find_if(
            snapshot.units.begin(), snapshot.units.end(),
            [&snapshot](const sim::UnitSnapshot& unit) {
                return unit.side == snapshot.active_side &&
                    sim::on_board(unit) && !unit.has_acted;
            }
        );
        if (actor == snapshot.units.end()) return false;
        const sim::CommandResult applied = encounter.apply(
            {sim::CommandType::wait, actor->id, {}, 0}
        );
        if (!applied) return false;
        ++commands;
    }
    return encounter.snapshot().round >= target;
}

struct AutoplayResult final {
    sim::Outcome outcome{sim::Outcome::ongoing};
    std::uint32_t round{};
    std::size_t commands{};
    std::size_t first_survivors{};
    std::size_t second_survivors{};
    int first_health{};
    int second_health{};
    std::uint32_t first_combat_round{};
    std::uint32_t last_combat_round{};
    std::size_t combat_rounds{};
    std::size_t longest_quiet_streak{};
    std::array<std::size_t, 2> player_combat_actions{};
    std::array<std::size_t, 2> player_moves{};
};

AutoplayResult autoplay(
    const pr::EncounterLoadResult& board,
    bool passive_first_side = false
) {
    auto created = sim::create_encounter(board.definition);
    if (!created) return {};
    std::size_t commands = 0U;
    std::set<std::uint32_t> combat_rounds;
    std::array<std::size_t, 2> player_combat_actions{};
    std::array<std::size_t, 2> player_moves{};
    while (created.encounter.snapshot().outcome == sim::Outcome::ongoing &&
           created.encounter.snapshot().round < 100U &&
           commands < 10'000U) {
        const sim::EncounterSnapshot snapshot = created.encounter.snapshot();
        bool applied_anything = false;
        for (const sim::UnitSnapshot& unit : snapshot.units) {
            if (unit.side != snapshot.active_side || !sim::on_board(unit) ||
                unit.has_acted) {
                continue;
            }
            tac::Behavior behavior = tac::Behavior::pursue;
            if (unit.side == sim::Side::second) {
                // Match the playable client: authored bindings override hold,
                // while recurring copies without their own binding stay put.
                const auto authored = std::find_if(
                    board.behaviors.begin(), board.behaviors.end(),
                    [&unit](const pr::UnitBehaviorBinding& binding) {
                        return binding.unit_id == unit.id;
                    }
                );
                behavior = authored == board.behaviors.end()
                    ? tac::Behavior::hold : authored->behavior;
            }
            const tac::Plan plan = passive_first_side &&
                    unit.side == sim::Side::first
                ? tac::Plan{
                    {sim::CommandType::wait, unit.id, {}, 0}, true,
                }
                : tac::decide(
                    snapshot, unit.id, behavior, {},
                    created.encounter.abilities(), created.encounter.weapons()
                );
            if (!plan.actionable) continue;
            sim::CommandResult applied = created.encounter.apply(plan.command);
            if (!applied) {
                applied = created.encounter.apply(
                    {sim::CommandType::wait, unit.id, {}, 0}
                );
            }
            if (!applied) return {};
            const std::uint32_t current_round = snapshot.round + 1U;
            if (plan.command.type == sim::CommandType::attack ||
                plan.command.type == sim::CommandType::ability) {
                combat_rounds.insert(current_round);
            }
            if (unit.side == sim::Side::first) {
                const auto definition = std::find_if(
                    board.definition.units.begin(),
                    board.definition.units.end(),
                    [&unit](const sim::UnitDefinition& candidate) {
                        return candidate.id == unit.id;
                    }
                );
                if (definition != board.definition.units.end() &&
                    definition->local_player >= 1U &&
                    definition->local_player <= 2U) {
                    const std::size_t player = static_cast<std::size_t>(
                        definition->local_player - 1U
                    );
                    if (plan.command.type == sim::CommandType::attack ||
                        plan.command.type == sim::CommandType::ability) {
                        ++player_combat_actions[player];
                    }
                    if (plan.command.type == sim::CommandType::move) {
                        ++player_moves[player];
                    }
                }
            }
            applied_anything = true;
            break;
        }
        if (!applied_anything) return {};
        ++commands;
    }
    const sim::EncounterSnapshot final = created.encounter.snapshot();
    AutoplayResult result{final.outcome, final.round, commands};
    result.player_combat_actions = player_combat_actions;
    result.player_moves = player_moves;
    if (!combat_rounds.empty()) {
        result.first_combat_round = *combat_rounds.begin();
        result.last_combat_round = *combat_rounds.rbegin();
        result.combat_rounds = combat_rounds.size();
        std::size_t quiet = 0U;
        for (std::uint32_t round = 1U;
             round <= std::max(final.round, result.last_combat_round);
             ++round) {
            if (combat_rounds.count(round) != 0U) {
                quiet = 0U;
            } else {
                ++quiet;
                result.longest_quiet_streak = std::max(
                    result.longest_quiet_streak, quiet
                );
            }
        }
    }
    for (const sim::UnitSnapshot& unit : final.units) {
        if (!sim::in_the_battle(unit)) continue;
        if (unit.side == sim::Side::first) {
            ++result.first_survivors;
            result.first_health += unit.health;
        } else {
            ++result.second_survivors;
            result.second_health += unit.health;
        }
    }
    return result;
}

std::vector<sim::ObjectiveResult> victory_results(
    const sim::EncounterDefinition& board,
    std::uint64_t winning_objective
) {
    std::vector<sim::ObjectiveResult> results;
    for (const sim::ObjectiveDefinition& objective : board.objectives) {
        results.push_back({
            objective.id,
            objective.id == winning_objective
                ? sim::ObjectiveState::satisfied
                : sim::ObjectiveState::pending,
        });
    }
    return results;
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
    const std::uint64_t akari_id = core::stable_content_id_v1("akari_mori");
    const std::uint64_t tomas_id = core::stable_content_id_v1("tomas_reed");
    const std::uint64_t ren_id = core::stable_content_id_v1("ren_sato");
    const std::uint64_t boss_id = core::stable_content_id_v1("lord_masatora_unit");
    const std::map<std::uint64_t, std::uint8_t> expected_archetypes{
        {rowan_id, 5U}, {akari_id, 1U}, {tomas_id, 4U},
        {ren_id, 6U}, {boss_id, 5U},
    };
    for (const gc::UnitType& unit : source.unit_types) {
        const std::uint8_t style = gc::resolved_character_style(source, unit);
        if (style == 0U) ++styles[0];
        if (style == 4U) ++styles[1];
        const auto expected = expected_archetypes.find(unit.id);
        if (expected != expected_archetypes.end()) {
            expect(
                gc::resolved_archetype(source, unit) == expected->second,
                "campaign-specific hero classes keep their shared art roles"
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
    expect(
        landmark_cells("bell_orchard", "farmland_orchard_tree") == 6U &&
        landmark_cells("bell_orchard", "forest_orchard_tree") == 10U &&
        landmark_cells("bell_orchard", "paved_orchard_bell") == 2U,
        "Bell Orchard identifies its orchard rows and bell without changing layout"
    );
    expect(
        landmark_cells("cedar_watch", "ruins_watch_ruin") == 10U &&
        landmark_cells("cedar_watch", "paved_signal_beacon") == 2U &&
        landmark_cells("cedar_watch", "road_signal_beacon") == 4U,
        "Cedar Watch identifies its ruins and four approach beacons"
    );
    expect(
        landmark_cells("twin_gate", "mountain_castle_wall") == 18U &&
        landmark_cells("twin_gate", "paved_castle_door") == 2U &&
        landmark_cells("twin_gate", "paved_castle_floor") == 14U &&
        landmark_cells("twin_gate", "paved_castle_dais") == 2U,
        "Twin Gate authors one connected 6x6 keep with a passable chamber"
    );
    const auto landmark_quadrants = [&maps](
        std::string_view map_key, std::string_view terrain_key
    ) {
        std::array<std::size_t, 4> counts{};
        const gc::Map& map = *maps.at(core::stable_content_id_v1(map_key));
        const std::uint64_t identity = core::stable_content_id_v1(terrain_key);
        const std::uint64_t grove =
            core::stable_content_id_v1("mountain_ancient_grove");
        const std::uint64_t house =
            core::stable_content_id_v1("mountain_village_house");
        for (std::size_t index = 0; index < map.terrain.size(); ++index) {
            if (map.terrain[index] != identity) continue;
            const std::size_t x = index % map.width;
            const std::size_t y = index / map.width;
            const view::LandmarkCell cell = view::landmark_cell(
                identity,
                x > 0U ? map.terrain[index - 1U] : 0U,
                y > 0U ? map.terrain[index - map.width] : 0U,
                grove, house
            );
            ++counts[static_cast<std::size_t>(cell.quadrant)];
        }
        return counts;
    };
    expect(
        landmark_quadrants(
            "cedar_deepwood", "mountain_ancient_grove"
        ) == std::array<std::size_t, 4>{{2U, 2U, 2U, 2U}},
        "Deepwood's exact grove cells form two complete console composites"
    );
    expect(
        landmark_quadrants(
            "gate_village", "mountain_village_house"
        ) == std::array<std::size_t, 4>{{6U, 6U, 6U, 6U}},
        "Gate Village's exact house cells form six complete console composites"
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
    {
        const gc::Map& map = *maps.at(core::stable_content_id_v1("twin_gate"));
        const std::uint64_t wall =
            core::stable_content_id_v1("mountain_castle_wall");
        const std::uint64_t door =
            core::stable_content_id_v1("paved_castle_door");
        const std::uint64_t floor =
            core::stable_content_id_v1("paved_castle_floor");
        const std::uint64_t dais =
            core::stable_content_id_v1("paved_castle_dais");
        const std::array<std::uint64_t, 3> passable{{
            door,
            floor,
            dais,
        }};
        expect(
            map.visual_visibility.reveal_radius == 8U &&
                map.visual_visibility.hidden_cells.size() == 18U,
            "Twin Gate authors one portable eighteen-cell interior mask"
        );
        for (const std::uint32_t cell :
             map.visual_visibility.hidden_cells) {
            expect(
                cell < map.terrain.size() &&
                    std::find(
                        passable.begin(), passable.end(), map.terrain[cell]
                    ) != passable.end(),
                "every covered Twin Gate cell is a walkable room cell"
            );
        }
        for (std::size_t cell = 0; cell < map.terrain.size(); ++cell) {
            if (map.terrain[cell] == wall) {
                expect(
                    map.terrain_kinds[cell] ==
                        gc::terrain_kind_index("mountain"),
                    "castle walls keep blocked mountain rules"
                );
            }
            if (std::find(
                    passable.begin(), passable.end(), map.terrain[cell]
                ) != passable.end()) {
                expect(
                    map.terrain_kinds[cell] == gc::terrain_kind_index("paved"),
                    "castle doors, floor and dais keep passable paved rules"
                );
            }
        }
        constexpr std::size_t keep_x = 13U;
        constexpr std::size_t keep_y = 4U;
        constexpr std::size_t keep_size = 6U;
        for (std::size_t y = 0; y < map.height; ++y) {
            for (std::size_t x = 0; x < map.width; ++x) {
                const bool in_keep = x >= keep_x && x < keep_x + keep_size &&
                    y >= keep_y && y < keep_y + keep_size;
                const std::size_t local_x = x - (in_keep ? keep_x : 0U);
                const std::size_t local_y = y - (in_keep ? keep_y : 0U);
                std::uint64_t expected = 0U;
                if (in_keep) {
                    expected = floor;
                    if (local_y == 0U || local_y == keep_size - 1U ||
                        local_x == keep_size - 1U ||
                        (local_x == 0U &&
                         (local_y == 1U || local_y == 4U))) {
                        expected = wall;
                    } else if (local_x == 0U) {
                        expected = door;
                    } else if (local_x == 4U &&
                               (local_y == 2U || local_y == 3U)) {
                        expected = dais;
                    }
                    expect(
                        map.terrain[y * map.width + x] == expected,
                        "Twin Gate's exact 6x6 footprint matches the Godot keep art"
                    );
                } else {
                    expect(
                        std::find(
                            passable.begin(), passable.end(),
                            map.terrain[y * map.width + x]
                        ) == passable.end() &&
                            map.terrain[y * map.width + x] != wall,
                        "Twin Gate castle identities stay inside one connected footprint"
                    );
                }
            }
        }
    }
    for (const auto& [map_key, terrain_key, kind] : std::array{
            std::tuple{"bell_orchard", "farmland_orchard_tree", "farmland"},
            std::tuple{"bell_orchard", "forest_orchard_tree", "forest"},
            std::tuple{"bell_orchard", "paved_orchard_bell", "paved"},
            std::tuple{"cedar_watch", "ruins_watch_ruin", "ruins"},
            std::tuple{"cedar_watch", "paved_signal_beacon", "paved"},
            std::tuple{"cedar_watch", "road_signal_beacon", "road"},
        }) {
        const gc::Map& map = *maps.at(core::stable_content_id_v1(map_key));
        const std::uint64_t terrain = core::stable_content_id_v1(terrain_key);
        for (std::size_t cell = 0; cell < map.terrain.size(); ++cell) {
            if (map.terrain[cell] == terrain) {
                expect(
                    map.terrain_kinds[cell] == gc::terrain_kind_index(kind),
                    std::string(terrain_key) + " keeps its original movement kind"
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
        [](const gc::Objective& objective) { return objective.id == hold_id; }
    );
    const auto protect = std::find_if(
        source.objectives.begin(), source.objectives.end(),
        [](const gc::Objective& objective) {
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
                [](const sim::UnitDefinition& unit) {
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
                std::string(stage.key) +
                    " distributes co-op tactical groups across the board"
            );
            expect(
                enemy_quadrants(solo).size() >= 3U,
                std::string(stage.key) +
                    " distributes solo tactical groups across the board"
            );
            const ConflictExtent solo_extent = conflict_extent(solo);
            const ConflictExtent coop_extent = conflict_extent(coop);
            expect(
                solo_extent.width > 8U || solo_extent.height > 6U,
                std::string(stage.key) +
                    " makes the solo conflict extend beyond one console view"
            );
            expect(
                coop_extent.width > 8U && coop_extent.height > 6U,
                std::string(stage.key) +
                    " makes the co-op fronts scroll in both dimensions"
            );
            expect(
                largest_opening_group_cell(solo) <= 2U &&
                largest_opening_group_cell(coop) <= 2U,
                std::string(stage.key) +
                    " opens both variants with readable groups instead of an enemy blob"
            );
        }

        for (const auto& [board, mode] : std::array{
                 std::pair{&solo, "1P"}, std::pair{&coop, "2P"},
             }) {
            expect(
                every_enemy_has_a_route_from(*board, 1U),
                std::string(stage.key) + " " + mode +
                    " gives the March a terrain route to every opponent and wave"
            );
            const AutoplayResult played = autoplay(*board);
            const std::size_t starting_allies = count_units(
                *board, sim::Side::first
            );
            expect(
                played.outcome == sim::Outcome::first_side_won &&
                    played.first_survivors >= 1U,
                std::string(stage.key) + " " + mode +
                    " has a deterministic legal completion baseline"
            );
            expect(
                played.first_combat_round <= 2U &&
                    played.longest_quiet_streak <= 1U,
                std::string(stage.key) + " " + mode +
                    " reaches contact promptly without dead walking rounds"
            );
            if (count_units(*board, sim::Side::first, 2U) != 0U) {
                const std::size_t combined =
                    played.player_combat_actions[0] +
                    played.player_combat_actions[1];
                expect(
                    played.player_combat_actions[0] * 4U >= combined &&
                        played.player_combat_actions[1] * 4U >= combined,
                    std::string(stage.key) + " " + mode +
                        " gives both players a material share of combat"
                );
            }
            if (index == 0U) {
                expect(
                    played.first_survivors == starting_allies &&
                        played.round <= 4U,
                    std::string(stage.key) + " " + mode +
                        " remains a forgiving introduction"
                );
            } else {
                expect(
                    played.round >= 6U,
                    std::string(stage.key) + " " + mode +
                        " does not collapse into an immediate clear"
                );
            }
            if (index == 1U) {
                expect(
                    played.round <= 10U,
                    std::string(stage.key) + " " + mode +
                        " clears its forest routes without a long tail"
                );
            }
            if (index == 3U || index == 4U) {
                expect(
                    played.round <= 12U,
                    std::string(stage.key) + " " + mode +
                        " reaches its commander before the assault drags"
                );
            }
            if (index == 2U) {
                expect(
                    played.round == 20U,
                    std::string(stage.key) + " " + mode +
                        " resolves at the authored defense limit"
                );
            }
            if (index >= 2U) {
                expect(
                    played.first_survivors < starting_allies,
                    std::string(stage.key) + " " + mode +
                        " still applies meaningful late-campaign attrition"
                );
            }
            std::cout << "autoplay " << stage.key << ' ' << mode
                      << " winner="
                      << (played.outcome == sim::Outcome::first_side_won
                              ? "concord"
                              : played.outcome == sim::Outcome::second_side_won
                                  ? "war_host" : "none")
                      << " round=" << played.round
                      << " commands=" << played.commands
                      << " survivors=" << played.first_survivors << '/'
                      << played.second_survivors
                      << " health=" << played.first_health << '/'
                      << played.second_health
                      << " combat=" << played.first_combat_round << '-'
                      << played.last_combat_round << " in "
                      << played.combat_rounds << " rounds"
                      << " quiet=" << played.longest_quiet_streak
                      << " p-actions=" << played.player_combat_actions[0]
                      << '/' << played.player_combat_actions[1]
                      << " p-moves=" << played.player_moves[0]
                      << '/' << played.player_moves[1] << '\n';
        }
        expect(
            every_enemy_has_a_route_from(coop, 2U),
            std::string(stage.key) +
                " gives Cedar a terrain route to every opponent and wave"
        );
        unsigned solo_movement = 0U;
        unsigned solo_reach = 0U;
        for (const sim::UnitDefinition& unit : solo.definition.units) {
            if (unit.side != sim::Side::first || unit.arrival_round != 0U) {
                continue;
            }
            solo_movement = std::max(
                solo_movement, unsigned{unit.movement}
            );
            solo_reach = std::max(
                solo_reach, unsigned{unit.maximum_reach}
            );
        }
        const NearestFront solo_front = nearest_front_for_player(solo, 1U);
        expect(
            solo_front.distance <= solo_movement * 2U + solo_reach,
            std::string(stage.key) +
                " gives the solo company a consequential front within two moves"
        );
        std::array<sim::UnitId, 2> opening_fronts{};
        for (std::uint8_t player = 1U; player <= 2U; ++player) {
            unsigned movement = 0U;
            unsigned reach = 0U;
            for (const sim::UnitDefinition& unit : coop.definition.units) {
                if (unit.side != sim::Side::first ||
                    unit.local_player != player || unit.arrival_round != 0U) {
                    continue;
                }
                movement = std::max(movement, unsigned{unit.movement});
                reach = std::max(reach, unsigned{unit.maximum_reach});
            }
            const NearestFront nearest =
                nearest_front_for_player(coop, player);
            opening_fronts[player - 1U] = nearest.enemy;
            expect(
                nearest.distance <= movement * 2U + reach,
                std::string(stage.key) + " gives Player " +
                    std::to_string(player) +
                    " a consequential front within two moves"
            );
        }
        expect(
            opening_fronts[0] != 0U && opening_fronts[1] != 0U &&
                opening_fronts[0] != opening_fronts[1],
            std::string(stage.key) +
                " gives the co-op players distinct opening fronts"
        );
    }

    for (const Stage& stage : stages) {
        const char* key = stage.key;
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

    const auto orchard_coop = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/bell_orchard"), {2}
    );
    if (orchard_coop) {
        const sim::UnitDefinition* rowan = unit_from_source_key(
            orchard_coop, core::stable_content_id_v1("rowan")
        );
        const sim::UnitDefinition* tomas = unit_from_source_key(
            orchard_coop, core::stable_content_id_v1("tomas")
        );
        const sim::UnitDefinition* akari = unit_from_source_key(
            orchard_coop, core::stable_content_id_v1("akari")
        );
        const sim::UnitDefinition* ren = unit_from_source_key(
            orchard_coop, core::stable_content_id_v1("ren")
        );
        expect(
            rowan != nullptr && tomas != nullptr && akari != nullptr &&
                ren != nullptr && rowan->position == sim::Position{1, 1} &&
                tomas->position == sim::Position{2, 1} &&
                akari->position == sim::Position{1, 4} &&
                ren->position == sim::Position{2, 4},
            "Bell Orchard 2P gives Rowan the upper road and Akari the lower "
            "road named by the co-op story"
        );
    }

    const auto watch = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/cedar_watch"), {2}
    );
    std::set<std::uint32_t> first_arrival_rounds;
    std::size_t recurring_waves = 0;
    std::size_t opening_enemies = 0;
    std::size_t explicit_arrivals = 0;
    bool every_enemy_has_behavior = true;
    for (const sim::UnitDefinition& unit : watch.definition.units) {
        if (unit.side != sim::Side::second) continue;
        every_enemy_has_behavior = every_enemy_has_behavior &&
            std::any_of(
                watch.behaviors.begin(), watch.behaviors.end(),
                [&unit](const pr::UnitBehaviorBinding& binding) {
                    return binding.unit_id == unit.id;
                }
            );
        if (unit.arrival_round == 0U) {
            ++opening_enemies;
        } else {
            ++explicit_arrivals;
            first_arrival_rounds.insert(unit.arrival_round);
            if (unit.arrival_times > 1U) ++recurring_waves;
        }
    }
    expect(opening_enemies == 2U, "Cedar Watch opens readably instead of as an enemy blob");
    expect(
        first_arrival_rounds == std::set<std::uint32_t>{
            3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U,
            14U, 15U, 16U, 17U, 18U, 19U,
        } &&
            explicit_arrivals == 23U && recurring_waves == 0U &&
            every_enemy_has_behavior,
        "its explicit staggered arrivals span every front and each keeps an "
        "authored hold-or-advance order"
    );
    const auto solo_watch = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/cedar_watch"), {1}
    );
    const auto solo_watch_created = sim::create_encounter(
        solo_watch.definition
    );
    const auto coop_watch_created = sim::create_encounter(watch.definition);
    const auto expanded_enemies = [](const sim::EncounterSnapshot& snapshot) {
        return static_cast<std::size_t>(std::count_if(
            snapshot.units.begin(), snapshot.units.end(),
            [](const sim::UnitSnapshot& unit) {
                return unit.side == sim::Side::second;
            }
        ));
    };
    expect(
        solo_watch_created && coop_watch_created &&
            expanded_enemies(solo_watch_created.encounter.snapshot()) == 14U &&
            expanded_enemies(coop_watch_created.encounter.snapshot()) == 25U,
        "Cedar Watch keeps a lighter three-front solo defense and a "
        "seven-group, four-front co-op defense"
    );

    for (const Stage& stage : stages) {
        for (std::uint8_t players = 1U; players <= 2U; ++players) {
            const auto board = pr::load_encounter(
                loaded.package,
                core::stable_content_id_v1(
                    std::string("steel_and_cedar/") + stage.key
                ),
                {players}
            );
            const std::uint32_t final_arrival =
                last_arrival_round(board.definition);
            if (final_arrival == 0U) continue;
            auto created = sim::create_encounter(board.definition);
            std::size_t commands = 0U;
            expect(
                created && wait_until_completed_round(
                    created.encounter, final_arrival - 1U, commands
                ),
                std::string(stage.key) + " " +
                    std::to_string(players) +
                    "P legally reaches its last authored arrival"
            );
            if (!created) continue;
            const sim::EncounterSnapshot arrived = created.encounter.snapshot();
            std::set<std::pair<std::int16_t, std::int16_t>> occupied;
            bool legal = true;
            for (const sim::UnitSnapshot& unit : arrived.units) {
                if (!unit.arrived) legal = false;
                if (!sim::on_board(unit)) continue;
                const std::size_t cell =
                    static_cast<std::size_t>(unit.position.y) * arrived.width +
                    static_cast<std::size_t>(unit.position.x);
                const sim::Terrain terrain = arrived.terrain.empty()
                    ? sim::Terrain::open : arrived.terrain[cell];
                legal = legal && sim::can_enter(terrain, unit.crossings) &&
                    occupied.insert({unit.position.x, unit.position.y}).second;
            }
            expect(
                legal,
                std::string(stage.key) + " " +
                    std::to_string(players) +
                    "P lands every wave on a unique legal cell"
            );
        }
    }

    for (std::uint8_t players = 1U; players <= 2U; ++players) {
        const auto board = pr::load_encounter(
            loaded.package,
            core::stable_content_id_v1("steel_and_cedar/cedar_watch"),
            {players}
        );
        const AutoplayResult passive = autoplay(board, true);
        std::cout << "passive watch " << unsigned{players}
                  << "P winner="
                  << (passive.outcome == sim::Outcome::second_side_won
                          ? "war_host" : "concord")
                  << " round=" << passive.round
                  << " survivors=" << passive.first_survivors << '/'
                  << passive.second_survivors << '\n';
        expect(
            passive.outcome == sim::Outcome::second_side_won &&
                passive.round < 20U,
            std::string("Cedar Watch ") + std::to_string(players) +
                "P cannot win by passively waiting through the defense"
        );
    }

    const auto finale = pr::load_encounter(
        loaded.package,
        core::stable_content_id_v1("steel_and_cedar/twin_gate"), {2}
    );
    expect(
        finale && std::any_of(
            finale.definition.units.begin(), finale.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.side == sim::Side::second &&
                    unit.unit_type_id == boss_id && unit.health == 27;
            }
        ),
        "the final map keeps Lord Masatora's full boss durability"
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
    if (finale) {
        const sim::UnitDefinition* rowan = unit_from_source_key(
            finale, core::stable_content_id_v1("rowan")
        );
        const sim::UnitDefinition* akari = unit_from_source_key(
            finale, core::stable_content_id_v1("akari")
        );
        const auto boss = std::find_if(
            finale.definition.units.begin(), finale.definition.units.end(),
            [](const sim::UnitDefinition& unit) {
                return unit.unit_type_id == boss_id;
            }
        );
        expect(
            rowan != nullptr && akari != nullptr &&
                rowan->position == sim::Position{3, 4} &&
                akari->position == sim::Position{3, 9},
            "Twin Gate starts both companies beyond the empty apron on "
            "separate high and low approaches"
        );
        if (rowan != nullptr && boss != finale.definition.units.end()) {
            sim::EncounterDefinition sealed = finale.definition;
            for (const sim::Position door :
                 std::array<sim::Position, 2>{{{13, 6}, {13, 7}}}) {
                sealed.terrain[
                    static_cast<std::size_t>(door.y) * sealed.width +
                    static_cast<std::size_t>(door.x)
                ] = sim::Terrain::heights;
            }
            const std::size_t boss_cell =
                static_cast<std::size_t>(boss->position.y) * sealed.width +
                static_cast<std::size_t>(boss->position.x);
            expect(
                terrain_distances(finale.definition, *rowan)[boss_cell] !=
                    std::numeric_limits<unsigned>::max() &&
                terrain_distances(sealed, *rowan)[boss_cell] ==
                    std::numeric_limits<unsigned>::max(),
                "Lord Masatora is fought inside the keep and both authored "
                "doors control access to his chamber"
            );
        }
    }

    const auto campaign = pr::load_campaign(
        loaded.package, core::stable_content_id_v1("steel_and_cedar")
    );
    expect(static_cast<bool>(campaign), "the campaign flow loads");
    if (campaign) {
        pr::CampaignCursor cursor(campaign.definition);
        expect(
            cursor.current().kind == pr::CampaignNodeKind::story &&
                cursor.advance_story() == pr::CampaignError::none,
            "the prologue reaches the first battle"
        );
        const std::array<std::uint64_t, 5> winning_objectives{{
            core::stable_content_id_v1("defeat_all"),
            core::stable_content_id_v1("defeat_all"),
            core::stable_content_id_v1("hold_watch"),
            core::stable_content_id_v1("fell_castellan"),
            core::stable_content_id_v1("fell_masatora"),
        }};
        for (std::size_t index = 0; index < stages.size(); ++index) {
            const Stage& stage = stages[index];
            const std::uint64_t encounter_id = core::stable_content_id_v1(
                std::string("steel_and_cedar/") + stage.key
            );
            expect(
                !cursor.complete() &&
                    cursor.current().kind ==
                        pr::CampaignNodeKind::encounter &&
                    cursor.current().encounter_id == encounter_id,
                std::string(stage.key) +
                    " is the next campaign battle"
            );
            const auto board = pr::load_encounter(
                loaded.package, encounter_id, {1}
            );
            expect(
                cursor.advance_after(
                    sim::Outcome::first_side_won,
                    victory_results(
                        board.definition, winning_objectives[index]
                    )
                ) == pr::CampaignError::none,
                std::string(stage.key) +
                    " advances on the objective state the rules produce"
            );
            while (!cursor.complete() &&
                   cursor.current().kind == pr::CampaignNodeKind::story) {
                expect(
                    cursor.advance_story() == pr::CampaignError::none,
                    std::string(stage.key) +
                        " advances through its aftermath story"
                );
            }
        }
        expect(
            cursor.complete() &&
                cursor.current().id ==
                    core::stable_content_id_v1("campaign_complete"),
            "five victories reach the authored campaign ending"
        );
    }

    if (failures == 0) {
        std::cout << "steel and cedar: connected 1P/2P routes, legal waves, "
                     "decisive autoplay and five-battle progression\n";
    }
    return failures == 0 ? 0 : 1;
}
