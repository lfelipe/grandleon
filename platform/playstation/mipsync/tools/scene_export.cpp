// SPDX-License-Identifier: MIT
// The neutral boundary between a real Grandleon package/simulation and the
// experimental Mipsync renderer. It exports facts, never Mipsync structs: the
// target-side generator may change without teaching the engine about a third-
// party renderer ABI.

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/presentation.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

namespace {

void write_id(std::ostream& output, std::uint64_t id) {
    // JSON numbers cannot carry every content identity losslessly in common
    // consumers. Decimal strings can, and Python's target generator reads them
    // without a second binary format.
    output << '"' << id << '"';
}

template <typename T>
void write_numbers(std::ostream& output, const std::vector<T>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned long long>(values[index]);
    }
    output << ']';
}

void write_ids(std::ostream& output, const std::vector<sim::ContentId>& values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        write_id(output, values[index]);
    }
    output << ']';
}

void write_positions(
    std::ostream& output,
    const std::vector<sim::Position>& values
) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) output << ',';
        output << "[" << values[index].x << ',' << values[index].y << ']';
    }
    output << ']';
}

bool read_file(const char* path, std::vector<std::uint8_t>& bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
    return static_cast<bool>(input) || input.eof();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: grandleon_ps1_mipsync_scene_export "
                     "<game.gpk> <encounter-key> <1|2> <scene.json>\n";
        return 64;
    }

    const int local_players = std::string(argv[3]) == "2" ? 2 :
        std::string(argv[3]) == "1" ? 1 : 0;
    if (local_players == 0) {
        std::cerr << "local players must be 1 or 2\n";
        return 64;
    }

    std::vector<std::uint8_t> bytes;
    if (!read_file(argv[1], bytes)) {
        std::cerr << "cannot open package: " << argv[1] << '\n';
        return 66;
    }

    pf::LoadOptions options;
    options.engine_version = {0, 1, 0};
    options.target = pf::TargetProfile::desktop;
    options.maximum_units_per_encounter = 48;
    options.maximum_records_per_section = 20'000;
    const pf::LoadResult package = pf::load_mock_package(bytes, options);
    if (!package) {
        std::cerr << "package rejected: " << pf::error_name(package.error) << '\n';
        return 65;
    }

    const std::uint64_t encounter_id = core::stable_content_id_v1(argv[2]);
    const pr::EncounterLoadResult loaded = pr::load_encounter(
        package.package, encounter_id,
        {static_cast<std::uint8_t>(local_players)}
    );
    if (!loaded) {
        std::cerr << "encounter rejected: " << pr::error_name(loaded.error) << '\n';
        return 65;
    }
    sim::Encounter::CreateResult created = sim::create_encounter(loaded.definition);
    if (!created) {
        std::cerr << "simulation rejected: " << sim::error_name(created.error) << '\n';
        return 65;
    }
    const pr::PresentationLoadResult shown = pr::load_presentation(package.package);
    if (!shown) {
        std::cerr << "presentation rejected: " << pr::error_name(shown.error) << '\n';
        return 65;
    }

    const sim::EncounterSnapshot snapshot = created.encounter.snapshot();
    std::ofstream output(argv[4], std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "cannot create scene manifest: " << argv[4] << '\n';
        return 73;
    }

    output << "{\n"
           << "  \"schema\": \"grandleon.mipsync.scene.v2\",\n"
           << "  \"renderer\": \"mipsync\",\n"
           << "  \"local_players\": " << local_players << ",\n"
           << "  \"encounter_id\": ";
    write_id(output, encounter_id);
    output << ",\n  \"map_id\": ";
    write_id(output, loaded.map_id);
    output << ",\n  \"canonical_hash\": ";
    write_id(output, created.encounter.canonical_hash());
    output << ",\n  \"width\": " << snapshot.width
           << ",\n  \"height\": " << snapshot.height
           << ",\n  \"theme\": " << static_cast<unsigned>(shown.presentation.theme)
           << ",\n  \"surroundings\": {\"mode\": "
           << static_cast<unsigned>(loaded.surroundings.mode)
           << ", \"colour\": " << loaded.surroundings.colour
           << ", \"depth\": " << static_cast<unsigned>(loaded.surroundings.depth)
           << ", \"terrain\": [";
    for (std::size_t index = 0; index < loaded.surroundings.terrain.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned>(
            shown.presentation.kind_of_terrain(loaded.surroundings.terrain[index])
        );
    }
    output << "]},\n  \"terrain\": [";
    for (std::size_t index = 0; index < loaded.terrain.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned>(
            shown.presentation.kind_of_terrain(loaded.terrain[index])
        );
    }
    output << "],\n  \"passability\": [";
    for (std::size_t index = 0; index < snapshot.terrain.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned>(snapshot.terrain[index]);
    }
    output << "],\n  \"units\": [\n";
    for (std::size_t index = 0; index < snapshot.units.size(); ++index) {
        const sim::UnitSnapshot& unit = snapshot.units[index];
        output << "    {\"id\": ";
        write_id(output, unit.id);
        output << ", \"unit_type_id\": ";
        write_id(output, unit.unit_type_id);
        output << ", \"side\": " << static_cast<unsigned>(unit.side)
               << ", \"x\": " << unit.position.x
               << ", \"y\": " << unit.position.y
               << ", \"health\": " << unit.health
               << ", \"maximum_health\": " << unit.maximum_health
               << ", \"colour\": " << static_cast<unsigned>(
                      shown.presentation.colour_of_unit_type(unit.unit_type_id))
               << ", \"archetype\": " << static_cast<unsigned>(
                      shown.presentation.archetype_of_unit_type(unit.unit_type_id))
               << ", \"style\": " << static_cast<unsigned>(
                      shown.presentation.character_style_of_unit_type(unit.unit_type_id))
               << ", \"figure\": " << static_cast<unsigned>(
                      shown.presentation.character_figure_of_unit_type(unit.unit_type_id))
               << '}';
        if (index + 1 != snapshot.units.size()) output << ',';
        output << '\n';
    }
    output << "  ],\n  \"simulation\": {\n"
           << "    \"turn_order\": "
           << static_cast<unsigned>(loaded.definition.turn_order)
           << ",\n    \"random_seed\": ";
    write_id(output, loaded.definition.random_seed);
    output << ",\n    \"movement_cost\": ";
    write_numbers(output, loaded.definition.movement_cost);
    output << ",\n    \"deployment_tiles\": ";
    write_positions(output, loaded.definition.deployment_tiles);
    output << ",\n    \"units\": [\n";
    for (std::size_t index = 0; index < loaded.definition.units.size(); ++index) {
        const sim::UnitDefinition& unit = loaded.definition.units[index];
        output << "      {\"id\":";
        write_id(output, unit.id);
        output << ",\"unit_type_id\":";
        write_id(output, unit.unit_type_id);
        output << ",\"side\":" << static_cast<unsigned>(unit.side)
               << ",\"position\":[" << unit.position.x << ',' << unit.position.y << ']'
               << ",\"health\":" << unit.health
               << ",\"strength\":" << unit.strength
               << ",\"power\":" << unit.power
               << ",\"defense\":" << unit.defense
               << ",\"resistance\":" << unit.resistance
               << ",\"skill\":" << unit.skill
               << ",\"luck\":" << unit.luck
               << ",\"evasion\":" << unit.evasion
               << ",\"magic\":" << unit.magic
               << ",\"movement\":" << static_cast<unsigned>(unit.movement)
               << ",\"action_points\":" << static_cast<unsigned>(unit.action_points)
               << ",\"speed\":" << static_cast<unsigned>(unit.speed)
               << ",\"acts_after_attacking\":" << (unit.acts_after_attacking ? 1 : 0)
               << ",\"minimum_reach\":" << static_cast<unsigned>(unit.minimum_reach)
               << ",\"maximum_reach\":" << static_cast<unsigned>(unit.maximum_reach)
               << ",\"ability_ids\":";
        write_ids(output, unit.ability_ids);
        output << ",\"weapon_ids\":";
        write_ids(output, unit.weapon_ids);
        output << ",\"crossings\":" << static_cast<unsigned>(unit.crossings)
               << ",\"accuracy\":" << static_cast<unsigned>(unit.accuracy)
               << ",\"item_ids\":";
        write_ids(output, unit.item_ids);
        output << ",\"item_counts\":";
        write_numbers(output, unit.item_counts);
        output << ",\"drop_item_id\":";
        write_id(output, unit.drop_item_id);
        output << ",\"drop_chance\":" << static_cast<unsigned>(unit.drop_chance)
               << ",\"reach_bonus\":" << static_cast<unsigned>(unit.reach_bonus)
               << ",\"talk_record_id\":";
        write_id(output, unit.talk_record_id);
        output << ",\"arrival_round\":" << unit.arrival_round
               << ",\"arrival_every\":" << unit.arrival_every
               << ",\"arrival_times\":" << unit.arrival_times
               << ",\"endures\":" << (unit.endures ? 1 : 0)
               << ",\"local_player\":" << static_cast<unsigned>(unit.local_player)
               << '}';
        if (index + 1 != loaded.definition.units.size()) output << ',';
        output << '\n';
    }
    output << "    ],\n    \"abilities\": [";
    for (std::size_t index = 0; index < loaded.definition.abilities.size(); ++index) {
        const sim::AbilityDefinition& ability = loaded.definition.abilities[index];
        if (index != 0) output << ',';
        output << "{\"id\":";
        write_id(output, ability.id);
        output << ",\"kind\":" << static_cast<unsigned>(ability.kind)
               << ",\"damage_type\":" << static_cast<unsigned>(ability.damage_type)
               << ",\"area\":" << static_cast<unsigned>(ability.area)
               << ",\"power\":" << ability.power
               << ",\"minimum_reach\":" << static_cast<unsigned>(ability.minimum_reach)
               << ",\"maximum_reach\":" << static_cast<unsigned>(ability.maximum_reach)
               << ",\"radius\":" << static_cast<unsigned>(ability.radius)
               << ",\"accuracy\":" << static_cast<unsigned>(ability.accuracy) << '}';
    }
    output << "],\n    \"objectives\": [";
    for (std::size_t index = 0; index < loaded.definition.objectives.size(); ++index) {
        const sim::ObjectiveDefinition& objective = loaded.definition.objectives[index];
        if (index != 0) output << ',';
        output << "{\"id\":";
        write_id(output, objective.id);
        output << ",\"kind\":" << static_cast<unsigned>(objective.kind)
               << ",\"side\":" << static_cast<unsigned>(objective.side)
               << ",\"target_unit_id\":";
        write_id(output, objective.target_unit_id);
        output << ",\"round_count\":" << objective.round_count << '}';
    }
    output << "],\n    \"weapons\": [";
    for (std::size_t index = 0; index < loaded.definition.weapons.size(); ++index) {
        const sim::WeaponDefinition& weapon = loaded.definition.weapons[index];
        if (index != 0) output << ',';
        output << "{\"id\":";
        write_id(output, weapon.id);
        output << ",\"power\":" << weapon.power
               << ",\"minimum_reach\":" << static_cast<unsigned>(weapon.minimum_reach)
               << ",\"maximum_reach\":" << static_cast<unsigned>(weapon.maximum_reach)
               << ",\"accuracy\":" << static_cast<unsigned>(weapon.accuracy)
               << ",\"weapon_type\":";
        write_id(output, weapon.weapon_type);
        output << '}';
    }
    output << "],\n    \"weapon_types\": [";
    for (std::size_t index = 0; index < loaded.definition.weapon_types.size(); ++index) {
        const sim::WeaponTypeDefinition& type = loaded.definition.weapon_types[index];
        if (index != 0) output << ',';
        output << "{\"id\":";
        write_id(output, type.id);
        output << ",\"strong_against\":";
        write_ids(output, type.strong_against);
        output << ",\"damage\":" << type.damage
               << ",\"accuracy\":" << static_cast<unsigned>(type.accuracy) << '}';
    }
    output << "],\n    \"items\": [";
    for (std::size_t index = 0; index < loaded.definition.items.size(); ++index) {
        const sim::ItemDefinition& item = loaded.definition.items[index];
        if (index != 0) output << ',';
        output << "{\"id\":";
        write_id(output, item.id);
        output << ",\"kind\":" << static_cast<unsigned>(item.kind)
               << ",\"power\":" << item.power << '}';
    }
    output << "],\n    \"behaviors\": [";
    for (std::size_t index = 0; index < loaded.behaviors.size(); ++index) {
        const pr::UnitBehaviorBinding& binding = loaded.behaviors[index];
        if (index != 0) output << ',';
        output << "{\"unit_id\":";
        write_id(output, binding.unit_id);
        output << ",\"behavior\":" << static_cast<unsigned>(binding.behavior)
               << ",\"patrol\":";
        write_positions(output, binding.patrol);
        output << '}';
    }
    output << "]\n  }\n}\n";
    if (!output) {
        std::cerr << "failed writing scene manifest: " << argv[4] << '\n';
        return 74;
    }

    std::cout << "GRANDLEON_PS1_RENDERER=MIPSYNC manifest=" << argv[4]
              << " map=" << snapshot.width << 'x' << snapshot.height
              << " units=" << snapshot.units.size()
              << " local_players=" << local_players << '\n';
    return 0;
}
