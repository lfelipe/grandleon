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
           << "  \"schema\": \"grandleon.mipsync.scene.v1\",\n"
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
    output << "  ]\n}\n";
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
