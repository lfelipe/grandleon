// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_runtime/localization.hpp>

#include <iostream>
#include <string_view>
#include <vector>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace core = grandleon::core;

namespace {
int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

gc::GameSource source() {
    gc::GameSource value;
    const auto objective_id = core::stable_content_id_v1("hold_gate");
    const auto encounter_id =
        core::stable_content_id_v1("first_adventure/village");
    const auto placement_id = core::stable_content_id_v1(
        "first_adventure/village/gate_captain"
    );
    value.game_id[0] = 0x4d;
    value.title = "Localized";
    value.source_language = "en";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.classes = {{30, "Knight", {8, 4, 2, 0, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {};
    value.unit_types = {{60, "Rowan", 30, 0, {40}, {}}};
    value.maps = {{70, "Gate", 3, 2, std::vector<std::uint64_t>(6, 1)}};
    value.objectives = {{
        objective_id, "Hold the gate",
        gc::ObjectiveKind::defeat_all_opponents
    }};
    gc::Placement hero{
        1000, 2000, 0, 60, gc::EncounterSide::first, 0, 0
    };
    gc::Placement captain{
        placement_id, 2001, 0, 60, gc::EncounterSide::second, 2, 1
    };
    captain.name = "Gate Captain";
    value.encounters = {{
        encounter_id, "Village", 70, {objective_id}, {hero, captain}
    }};
    value.translations = {
        {"es", {
            {"project.title", "Localized", "Localizado"},
            {"unit_types.rowan.name", "Rowan", "Roldán"},
            {"objectives.hold_gate.name", "Hold the gate", "Defiende la puerta"},
            {"campaigns.first_adventure.nodes.village.placements.gate_captain.name",
             "Gate Captain", "Capitán de la puerta"},
            {"dialogues.opening.name", "At Dawn", "Al amanecer"},
            {"dialogues.opening.lines.0.speaker", "Rowan", "Roldán"},
            {"dialogues.opening.lines.0.text", "Hold the gate.", "Defiende la puerta."},
            {"campaigns.first_adventure.name", "First Adventure", "Primera aventura"},
            {"campaigns.first_adventure.nodes.village.name", "Village", "Aldea"},
            {"campaigns.first_adventure.members.rowan.name", "Sir Rowan", "Don Roldán"}
        }},
        {"pt-BR", {{"unit_types.rowan.name", "Rowan", "Rowan"}}}
    };
    return value;
}

}  // namespace

int main() {
    const auto compiled = gc::compile(source());
    expect(static_cast<bool>(compiled), "localized source compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 64, 10000}
    );
    expect(static_cast<bool>(loaded), "localized package loads");
    expect(pr::source_language(loaded.package) == "en", "source language is portable");
    expect(
        pr::localized_text(
            loaded.package, "es", "unit_types.rowan.name", "Rowan"
        ) == "Roldán",
        "reviewed Spanish text resolves"
    );
    expect(
        pr::localized_text(
            loaded.package, "pt-BR", "unit_types.rowan.name", "Rowan"
        ) == "Rowan",
        "Brazilian Portuguese is a portable catalog"
    );
    expect(
        pr::localized_text(
            loaded.package, "es", "unit_types.rowan.name", "Sir Rowan"
        ) == "Sir Rowan",
        "outdated translation visibly falls back to current source"
    );
    expect(
        pr::localized_text(
            loaded.package, "fr", "unit_types.rowan.name", "Rowan"
        ) == "Rowan",
        "missing language visibly falls back to source"
    );
    expect(
        pr::localized_project_title(
            loaded.package, "es", "Localized"
        ) == "Localizado",
        "project title resolves through its manifest-level message"
    );
    expect(
        pr::localized_project_title(
            loaded.package, "es", "Localized revised"
        ) == "Localized revised",
        "stale project-title translation falls back to source"
    );
    const auto languages = pr::available_languages(loaded.package);
    expect(
        languages.size() == 3U && languages[0] == "en" &&
            languages[1] == "es" && languages[2] == "pt-BR",
        "authored source and catalog languages are offered source-first"
    );
    expect(
        pr::localized_content_name(
            loaded.package, "es", pf::SectionType::unit_types,
            core::stable_content_id_v1("rowan"), "Rowan"
        ) == "Roldán",
        "portable unit-type identity resolves its authored translation"
    );
    const auto objective_id = core::stable_content_id_v1("hold_gate");
    expect(
        pr::localized_objective_name(
            loaded.package, "es", objective_id, "Hold the gate"
        ) == "Defiende la puerta",
        "objective label resolves by portable identity"
    );
    expect(
        pr::localized_objective_name(
            loaded.package, "es", objective_id, "Hold this gate"
        ) == "Hold this gate",
        "stale objective translation falls back to current source"
    );
    const auto placement_id = core::stable_content_id_v1(
        "first_adventure/village/gate_captain"
    );
    expect(
        pr::localized_placement_name(
            loaded.package, "es", placement_id, "Gate Captain"
        ) == "Capitán de la puerta",
        "named battle placement resolves through its scoped identity"
    );
    expect(
        pr::localized_placement_name(
            loaded.package, "fr", placement_id, "Gate Captain"
        ) == "Gate Captain",
        "missing placement language falls back to source"
    );
    expect(
        pr::localized_placement_name(
            loaded.package, "es", placement_id, "Gate Warden"
        ) == "Gate Warden",
        "stale placement translation falls back to current source"
    );
    expect(
        pr::localized_dialogue_name(
            loaded.package, "es", core::stable_content_id_v1("opening"),
            "At Dawn"
        ) == "Al amanecer",
        "dialogue title resolves by portable identity"
    );
    expect(
        pr::localized_dialogue_line(
            loaded.package, "es", core::stable_content_id_v1("opening"),
            0, "speaker", "Rowan"
        ) == "Roldán",
        "dialogue speaker resolves by scene and line"
    );
    expect(
        pr::localized_dialogue_line(
            loaded.package, "es", core::stable_content_id_v1("opening"),
            0, "text", "Hold the gate."
        ) == "Defiende la puerta.",
        "dialogue copy resolves by scene and line"
    );
    expect(
        pr::localized_campaign_name(
            loaded.package, "es",
            core::stable_content_id_v1("first_adventure"), "First Adventure"
        ) == "Primera aventura",
        "campaign title resolves by portable identity"
    );
    expect(
        pr::localized_campaign_node_name(
            loaded.package, "es",
            core::stable_content_id_v1("first_adventure"),
            core::stable_content_id_v1("village"), "Village"
        ) == "Aldea",
        "stage name resolves within its campaign"
    );
    expect(
        pr::localized_campaign_member_name(
            loaded.package, "es",
            core::stable_content_id_v1("first_adventure"),
            core::stable_content_id_v1("rowan"), "Sir Rowan"
        ) == "Don Roldán",
        "character name resolves within its campaign"
    );
    expect(
        pr::localized_campaign_member_name(
            loaded.package, "es",
            core::stable_content_id_v1("first_adventure"),
            core::stable_content_id_v1("rowan"), "Sir Rowan II"
        ) == "Sir Rowan II",
        "semantic lookup also rejects stale character-name translations"
    );
    return failures == 0 ? 0 : 1;
}
