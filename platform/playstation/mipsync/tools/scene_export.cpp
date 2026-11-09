// SPDX-License-Identifier: MIT
// The neutral boundary between a real Grandleon package/simulation and the
// experimental Mipsync renderer. It exports facts, never Mipsync structs: the
// target-side generator may change without teaching the engine about a third-
// party renderer ABI.

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/localization.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/package_runtime/names.hpp>
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

void write_string(std::ostream& output, std::string_view value) {
    output << '"';
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (byte >= 32U) output << static_cast<char>(byte);
                break;
        }
    }
    output << '"';
}

std::string first_dialogue_text(
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& campaign,
    std::uint64_t node_id,
    std::string_view language
) {
    for (const pr::CampaignNode& node : campaign.nodes) {
        if (node.id != node_id || node.dialogue_ids.empty()) continue;
        const pr::DialogueLoadResult dialogue = pr::load_dialogue(
            package, node.dialogue_ids.front()
        );
        if (!dialogue || dialogue.dialogue.lines.empty()) return {};
        const std::string_view source = dialogue.dialogue.lines.front().text;
        return std::string(pr::localized_dialogue_line(
            package, language, node.dialogue_ids.front(), 0U, "text", source
        ));
    }
    return {};
}

std::string campaign_node_name(
    const pf::LoadedPackage& package,
    const pr::CampaignNode& node,
    std::uint64_t campaign_id,
    std::string_view language
) {
    std::string source;
    if (node.kind == pr::CampaignNodeKind::encounter && node.encounter_id != 0U) {
        source = std::string(pr::content_name(
            package, pf::SectionType::encounters, node.encounter_id
        ));
    } else {
        source = node.kind == pr::CampaignNodeKind::terminal
            ? "Campaign ended" : "Story";
    }
    return std::string(pr::localized_campaign_node_name(
        package, language, campaign_id, node.id, source
    ));
}

void write_moments(
    std::ostream& output,
    const std::vector<pr::EncounterMoment>& moments
) {
    output << '[';
    for (std::size_t index = 0; index < moments.size(); ++index) {
        if (index != 0U) output << ',';
        const pr::EncounterMoment& moment = moments[index];
        output << "{\"id\":";
        write_id(output, moment.id);
        output << ",\"trigger\":" << static_cast<unsigned>(moment.trigger)
               << ",\"placement_id\":";
        write_id(output, moment.placement_id);
        output << ",\"dialogue_id\":";
        write_id(output, moment.dialogue_id);
        output << '}';
    }
    output << ']';
}

void write_localized_moments(
    std::ostream& output,
    const pf::LoadedPackage& package,
    const std::vector<pr::EncounterMoment>& moments,
    std::string_view language
) {
    output << '[';
    for (std::size_t index = 0; index < moments.size(); ++index) {
        if (index != 0U) output << ',';
        const pr::EncounterMoment& moment = moments[index];
        const pr::DialogueLoadResult loaded = pr::load_dialogue(
            package, moment.dialogue_id
        );
        output << "{\"dialogue_id\":";
        write_id(output, moment.dialogue_id);
        output << ",\"lines\":[";
        if (loaded) {
            for (std::size_t line_index = 0;
                 line_index < loaded.dialogue.lines.size(); ++line_index) {
                if (line_index != 0U) output << ',';
                const pr::DialogueLine& line =
                    loaded.dialogue.lines[line_index];
                output << "{\"speaker\":";
                write_string(output, pr::localized_dialogue_line(
                    package, language, moment.dialogue_id, line_index,
                    "speaker", line.speaker
                ));
                output << ",\"text\":";
                write_string(output, pr::localized_dialogue_line(
                    package, language, moment.dialogue_id, line_index,
                    "text", line.text
                ));
                output << '}';
            }
        }
        output << "]}";
    }
    output << ']';
}

void write_campaign(
    std::ostream& output,
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& campaign,
    std::uint64_t campaign_id,
    std::uint64_t current_encounter_id,
    const sim::EncounterDefinition& encounter,
    std::string_view language
) {
    output << "{\"id\":";
    write_id(output, campaign_id);
    output << ",\"entry_node_id\":";
    write_id(output, campaign.entry_node_id);
    output << ",\"player_mode\":"
           << static_cast<unsigned>(campaign.player_mode);
    output << ",\"current_node_id\":";
    std::uint64_t current_node_id = 0U;
    for (const pr::CampaignNode& node : campaign.nodes) {
        if (node.kind == pr::CampaignNodeKind::encounter &&
            node.encounter_id == current_encounter_id) {
            current_node_id = node.id;
            break;
        }
    }
    write_id(output, current_node_id);
    output << ",\"objective\":";
    if (!encounter.objectives.empty()) {
        const std::uint64_t objective_id = encounter.objectives.front().id;
        const std::string_view source = pr::content_name(
            package, pf::SectionType::objectives,
            objective_id
        );
        write_string(output, pr::localized_objective_name(
            package, language, objective_id, source
        ));
    } else {
        write_string(output, {});
    }
    output << ",\"members\":[";
    for (std::size_t member_index = 0;
         member_index < campaign.members.size(); ++member_index) {
        if (member_index != 0U) output << ',';
        const pr::CampaignMember& member = campaign.members[member_index];
        output << "{\"persistent_id\":" << member_index + 1U
               << ",\"source_id\":";
        write_id(output, member.id);
        output << ",\"unit_type_id\":";
        write_id(output, member.unit_type_id);
        output << ",\"join_node_id\":";
        write_id(output, member.join_node_id);
        output << ",\"name\":";
        write_string(output, member.name_in(package));
        output << ",\"player_nameable\":"
               << (member.player_nameable ? 1 : 0) << '}';
    }
    output << "],\"nodes\":[";
    for (std::size_t node_index = 0;
         node_index < campaign.nodes.size(); ++node_index) {
        if (node_index != 0) output << ',';
        const pr::CampaignNode& node = campaign.nodes[node_index];
        output << "{\"id\":";
        write_id(output, node.id);
        output << ",\"kind\":" << static_cast<unsigned>(node.kind)
               << ",\"encounter_id\":";
        write_id(output, node.encounter_id);
        output << ",\"name\":";
        write_string(output, campaign_node_name(
            package, node, campaign_id, language
        ));
        output << ",\"dialogue\":";
        write_string(output, first_dialogue_text(
            package, campaign, node.id, language
        ));
        output << ",\"has_fallback\":"
               << (node.has_unconditional_target ? 1 : 0)
               << ",\"fallback\":";
        write_id(output, node.unconditional_target_id);
        output << ",\"branches\":[";
        for (std::size_t branch_index = 0;
             branch_index < node.branches.size(); ++branch_index) {
            if (branch_index != 0) output << ',';
            const pr::CampaignBranch& branch = node.branches[branch_index];
            output << "{\"target\":";
            write_id(output, branch.target_id);
            output << ",\"priority\":" << branch.priority
                   << ",\"combinator\":"
                   << static_cast<unsigned>(branch.combinator)
                   << ",\"predicates\":[";
            for (std::size_t predicate_index = 0;
                 predicate_index < branch.predicates.size(); ++predicate_index) {
                if (predicate_index != 0) output << ',';
                const pr::CampaignPredicate& predicate =
                    branch.predicates[predicate_index];
                output << "{\"kind\":" << static_cast<unsigned>(predicate.kind)
                       << ",\"subject\":";
                write_id(output, predicate.subject);
                output << ",\"result\":"
                       << static_cast<unsigned>(predicate.result)
                       << ",\"value_type\":"
                       << static_cast<unsigned>(predicate.value_type)
                       << ",\"value\":" << predicate.value << '}';
            }
            output << "]}";
        }
        output << "]}";
    }
    output << "]}";
}

void write_localizations(
    std::ostream& output,
    const pf::LoadedPackage& package,
    const pr::CampaignDefinition& campaign,
    std::uint64_t campaign_id,
    const sim::EncounterDefinition& encounter,
    const std::vector<pr::EncounterMoment>& moments
) {
    std::vector<std::string_view> languages = pr::available_languages(package);
    if (languages.empty()) {
        languages.push_back(pr::source_language(package));
    }
    output << '[';
    for (std::size_t language_index = 0;
         language_index < languages.size(); ++language_index) {
        if (language_index != 0U) output << ',';
        const std::string_view language = languages[language_index];
        const std::string_view source_title = pr::project_title(package);
        output << "{\"code\":";
        write_string(output, language);
        output << ",\"project_title\":";
        write_string(output, pr::localized_project_title(
            package, language, source_title
        ));
        output << ",\"objective\":";
        if (!encounter.objectives.empty()) {
            const std::uint64_t objective_id = encounter.objectives.front().id;
            const std::string_view source = pr::content_name(
                package, pf::SectionType::objectives, objective_id
            );
            write_string(output, pr::localized_objective_name(
                package, language, objective_id, source
            ));
        } else {
            write_string(output, {});
        }
        output << ",\"members\":[";
        for (std::size_t member_index = 0;
             member_index < campaign.members.size(); ++member_index) {
            if (member_index != 0U) output << ',';
            const pr::CampaignMember& member = campaign.members[member_index];
            const std::string_view source = member.name_in(package);
            output << "{\"persistent_id\":" << member_index + 1U
                   << ",\"name\":";
            write_string(output, pr::localized_campaign_member_name(
                package, language, campaign_id, member.id, source
            ));
            output << '}';
        }
        output << "],\"nodes\":[";
        for (std::size_t node_index = 0;
             node_index < campaign.nodes.size(); ++node_index) {
            if (node_index != 0U) output << ',';
            const pr::CampaignNode& node = campaign.nodes[node_index];
            output << "{\"id\":";
            write_id(output, node.id);
            output << ",\"name\":";
            write_string(output, campaign_node_name(
                package, node, campaign_id, language
            ));
            output << ",\"dialogue\":";
            write_string(output, first_dialogue_text(
                package, campaign, node.id, language
            ));
            output << '}';
        }
        output << "],\"moments\":";
        write_localized_moments(output, package, moments, language);
        output << '}';
    }
    output << ']';
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
    const std::string encounter_key = argv[2];
    const std::size_t slash = encounter_key.find('/');
    const std::string campaign_key = encounter_key.substr(0, slash);
    const std::uint64_t campaign_id = core::stable_content_id_v1(campaign_key);
    const pr::CampaignLoadResult campaign = pr::load_campaign(
        package.package, campaign_id
    );
    if (!campaign) {
        std::cerr << "campaign rejected: " << pr::error_name(campaign.error) << '\n';
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
           << "  \"game_id\": \"";
    static constexpr char hex[] = "0123456789abcdef";
    for (const std::uint8_t byte : package.package.game_id) {
        output << hex[byte >> 4U] << hex[byte & 15U];
    }
    output << "\",\n  \"content_revision\": "
           << package.package.content_revision
           << ",\n  \"project_title\": \"";
    for (const char byte : pr::project_title(package.package)) {
        if (byte == '\\' || byte == '"') output << '\\';
        output << byte;
    }
    output << "\",\n"
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
    output << "]},\n  \"visual_visibility\": {\"reveal_radius\": "
           << static_cast<unsigned>(loaded.visual_visibility.reveal_radius)
           << ", \"hidden_cells\": [";
    for (std::size_t index = 0;
         index < loaded.visual_visibility.hidden_cells.size(); ++index) {
        if (index != 0) output << ',';
        output << loaded.visual_visibility.hidden_cells[index];
    }
    output << "]},\n  \"campaign\": ";
    write_campaign(
        output, package.package, campaign.definition, campaign_id,
        encounter_id, loaded.definition,
        pr::source_language(package.package)
    );
    output << ",\n  \"languages\": ";
    write_localizations(
        output, package.package, campaign.definition, campaign_id,
        loaded.definition, loaded.moments
    );
    output << ",\n  \"moments\": ";
    write_moments(output, loaded.moments);
    output << ",\n  \"terrain\": [";
    for (std::size_t index = 0; index < loaded.terrain.size(); ++index) {
        if (index != 0) output << ',';
        output << static_cast<unsigned>(
            shown.presentation.kind_of_terrain(loaded.terrain[index])
        );
    }
    output << "],\n  \"terrain_ids\": [";
    for (std::size_t index = 0; index < loaded.terrain.size(); ++index) {
        if (index != 0) output << ',';
        output << '"' << loaded.terrain[index] << '"';
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
