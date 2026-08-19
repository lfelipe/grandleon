// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/simulation/encounter.hpp>

// The art library's generated registries. The content reader mirrors the theme
// menu, the character style menu and the terrain keyword table; these are what
// prove the copies agree.
#include "backdrops.h"
#include "styles.h"
#include "themes.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::string_view source = R"json({
  "schemaVersion": "1.1.0",
  "packageId": "d05f4dc5-592f-4c6a-9093-f4090a722ffc",
  "gameId": "vertical.demo",
  "title": "Vertical Demo",
  "contentRevision": "1.2.3",
  "weaponTypes": [{"id":"blade","name":"Blade"}],
  "itemTypes": [{"id":"healing","name":"Healing"}],
  "classes": [{
    "id":"vanguard","name":"Vanguard",
    "baseStats":{"health":20,"strength":7,"defense":6,"movement":5},
    "allowedWeaponTypeIds":["blade"]
  }],
  "factions": [
    {"id":"blue","name":"Blue"},
    {"id":"red","name":"Red"}
  ],
  "unitTypes": [
    {"id":"blue_unit","name":"Blue unit","classId":"vanguard",
     "factionId":"blue","startingWeaponIds":["sword"]},
    {"id":"red_unit","name":"Red unit","classId":"vanguard",
     "factionId":"red","startingWeaponIds":["sword"]}
  ],
  "weapons": [{
    "id":"sword","name":"Sword","weaponTypeId":"blade","power":3,"range":1
  }],
  "items": [{
    "id":"tonic","name":"Tonic","itemTypeId":"healing","stackLimit":5
  }],
  "maps": [{
    "id":"field","name":"Field","width":2,"height":1,
    "terrain":["plain","plain"]
  }],
  "objectives": [{
    "id":"defeat_all_opponents","name":"Defeat all opponents"
  }],
  "campaigns": [{
    "id":"demo","name":"Demo campaign",
    "roster":[{"id":"blue_one","name":"Blue One","unitTypeId":"blue_unit"}],
    "flow":{
      "contractVersion":"1.0.0","entryNodeId":"battle",
      "nodes":[
        {"id":"battle","name":"Battle","kind":"encounter","mapId":"field",
         "objectiveIds":["defeat_all_opponents"],
         "placements":[
           {"id":"blue_1","memberId":"blue_one","unitTypeId":"blue_unit",
            "side":"first","x":0,"y":0},
           {"id":"red_1","unitTypeId":"red_unit","side":"second","x":1,"y":0}
         ],
         "transitions":[{"id":"done","targetNodeId":"end","priority":0}]},
        {"id":"end","name":"End","kind":"terminal","transitions":[]}
      ]
    }
  }]
})json";

bool has_source_diagnostic(
    const gc::SourceParseResult& result,
    gc::SourceDiagnosticCode code
) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [code](const gc::SourceDiagnostic& diagnostic) {
            return diagnostic.code == code;
        }
    );
}

void compiles_canonical_vertical_project() {
    const auto parsed = gc::parse_source_project_json(source);
    expect(static_cast<bool>(parsed), "canonical vertical project parses");
    expect(parsed.source.maps.size() == 1, "map is mapped");
    expect(parsed.source.factions.size() == 2, "factions are mapped");
    expect(parsed.source.encounters.size() == 1, "encounter node is mapped");
    expect(
        parsed.source.encounters.front().placements.size() == 2,
        "encounter placements are mapped"
    );
    const auto compiled = gc::compile(parsed.source);
    // Said only when it fails, and said by name: a refusal here is a rule this
    // fixture broke, and "canonical vertical project compiles" alone sends the
    // reader looking through every rule the compiler has.
    if (!compiled) {
        for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
            std::cerr << "  refused: " << gc::diagnostic_name(diagnostic.code)
                      << " at " << diagnostic.path << '\n';
        }
    }
    expect(static_cast<bool>(compiled), "canonical vertical project compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    expect(static_cast<bool>(loaded), "vertical package loads");
    expect(
        loaded.package.find(pf::SectionType::encounters) != nullptr,
        "vertical package contains encounters"
    );
}

void rejects_unsupported_gameplay() {
    std::string conditional{source};
    const std::string needle =
        R"json("targetNodeId":"end","priority":0)json";
    const auto position = conditional.find(needle);
    // `inventoryAtLeast` is the one predicate the source schema names that
    // still has no bytes in a package. It is refused by name rather than
    // quietly answered false, which is what this asserts.
    conditional.insert(
        position + needle.size(),
        R"json(,"when":{"kind":"inventoryAtLeast","itemId":"x","quantity":1})json"
    );
    const auto parsed = gc::parse_source_project_json(conditional);
    expect(!static_cast<bool>(parsed), "unsupported condition rejects project");
    expect(
        has_source_diagnostic(
            parsed, gc::SourceDiagnosticCode::unsupported_content
        ),
        "unsupported gameplay has stable diagnostic"
    );
}

// The predicate beside it that is accepted, because a talk can raise a flag for
// it to read. Both value types the campaign state has a home for.
void compiles_a_world_flag_predicate() {
    const std::string needle =
        R"json("targetNodeId":"end","priority":0)json";
    const std::string conditions[] = {
        R"json(,"when":{"kind":"worldFlagEquals","flagId":"heard-him-out","value":true})json",
        R"json(,"when":{"kind":"worldFlagEquals","flagId":"heard-him-out","value":3})json",
    };
    for (const std::string& condition : conditions) {
        std::string conditional{source};
        const auto position = conditional.find(needle);
        conditional.insert(position + needle.size(), condition);
        const auto parsed = gc::parse_source_project_json(conditional);
        expect(
            static_cast<bool>(parsed),
            "a world-flag predicate compiles rather than being refused"
        );
        expect(
            !has_source_diagnostic(
                parsed, gc::SourceDiagnosticCode::unsupported_content
            ),
            "and earns no unsupported diagnostic on the way"
        );
    }

    // A string world value stays refused: the campaign state's vocabulary is
    // boolean and integer, and hashing a string into a number nobody authored
    // would be answering a question the author did not ask.
    std::string stringy{source};
    stringy.insert(
        stringy.find(needle) + needle.size(),
        R"json(,"when":{"kind":"worldFlagEquals","flagId":"h","value":"yes"})json"
    );
    const auto refused = gc::parse_source_project_json(stringy);
    expect(
        !static_cast<bool>(refused) &&
            has_source_diagnostic(
                refused, gc::SourceDiagnosticCode::unsupported_content
            ),
        "a world flag compared against a string is refused by name"
    );
}

void rejects_malformed_json() {
    const auto parsed = gc::parse_source_project_json("{");
    expect(!static_cast<bool>(parsed), "malformed JSON is rejected");
    expect(
        has_source_diagnostic(parsed, gc::SourceDiagnosticCode::invalid_json),
        "malformed JSON has syntax diagnostic"
    );
}

void maps_authored_resistance() {
    std::string with_resistance{source};
    const std::string needle = R"json("health":20,)json";
    with_resistance.insert(
        with_resistance.find(needle) + needle.size(),
        R"json("resistance":4,)json"
    );
    const auto parsed = gc::parse_source_project_json(with_resistance);
    expect(static_cast<bool>(parsed), "a project with resistance parses");
    expect(
        parsed.source.classes.size() == 1 &&
            parsed.source.classes.front().base_stats.resistance == 4,
        "authored resistance is mapped"
    );
    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.classes.front().base_stats.resistance == 0,
        "omitted resistance defaults to zero"
    );
}

// The last stat a board will open on, and the first one it will not.
//
// The compiler asks `simulation::maximum_stat` for both ends rather than
// naming a number, so this test says nothing the engine does not: raise the
// engine's bound and the accepted value rises with it. What it pins is that
// the seam is exactly one number wide: a project at the bound compiles, a
// project one past it is refused before a package exists, and the refusal
// quotes what the author wrote and what they may write.
void refuses_a_stat_the_rules_cannot_execute() {
    constexpr std::int64_t bound = grandleon::simulation::maximum_stat;
    const std::string needle = R"json("strength":7)json";
    const auto with_strength = [&needle](std::int64_t value) {
        std::string project{source};
        const auto at = project.find(needle);
        project.replace(
            at, needle.size(), R"json("strength":)json" + std::to_string(value)
        );
        return project;
    };

    const auto at_the_bound = gc::parse_source_project_json(with_strength(bound));
    expect(
        static_cast<bool>(at_the_bound) &&
            at_the_bound.source.classes.front().base_stats.strength == bound,
        "a stat at the bound the rules hold compiles, and arrives unchanged"
    );

    const auto past_it = gc::parse_source_project_json(with_strength(bound + 1));
    expect(
        !past_it, "a stat one past that bound is refused rather than written"
    );
    const auto found = std::find_if(
        past_it.diagnostics.begin(), past_it.diagnostics.end(),
        [](const gc::SourceDiagnostic& diagnostic) {
            return diagnostic.path == "$.classes[0].baseStats.strength";
        }
    );
    expect(
        found != past_it.diagnostics.end(),
        "the refusal names the record and the field the number is in"
    );
    if (found == past_it.diagnostics.end()) return;
    expect(
        found->detail.find(std::to_string(bound + 1)) != std::string::npos,
        "the refusal quotes the number the author wrote"
    );
    expect(
        found->detail.find(std::to_string(bound)) != std::string::npos,
        "and the largest one they may write"
    );
    expect(
        found->detail.find("bounded_stat") == std::string::npos &&
            found->detail.find("int16") == std::string::npos,
        "in words an author can act on without reading the engine"
    );
}

// Three stats the same size that are not bounded the same way. They are
// percentage points on a hit chance the rules clamp to nought and a hundred
// before anything reads them, so they reach no arithmetic that can overflow;
// the engine asks only that they not be negative, and narrowing them to the
// damage bound would refuse content no board would ever refuse.
void keeps_the_whole_range_for_the_chance_stats() {
    constexpr std::int64_t widest = 32767;
    std::string project{source};
    const std::string needle = R"json("health":20,)json";
    project.insert(
        project.find(needle) + needle.size(),
        R"json("skill":32767,"luck":32767,"evasion":32767,)json"
    );
    const auto parsed = gc::parse_source_project_json(project);
    expect(
        static_cast<bool>(parsed),
        "skill, luck and evasion take the whole of what the field holds"
    );
    if (!parsed) return;
    const auto& stats = parsed.source.classes.front().base_stats;
    expect(
        stats.skill == widest && stats.luck == widest &&
            stats.evasion == widest,
        "and arrive unchanged"
    );
}

void maps_authored_accuracy() {
    // Omitted and authored are distinct spellings of the same field, and the
    // omitted one has to mean certainty rather than zero. A weapon that reads
    // as never landing would be the worst possible default.
    std::string with_accuracy{source};
    const std::string needle = R"json("power":3,)json";
    with_accuracy.insert(
        with_accuracy.find(needle) + needle.size(), R"json("accuracy":90,)json"
    );
    const auto parsed = gc::parse_source_project_json(with_accuracy);
    expect(static_cast<bool>(parsed), "a project with accuracy parses");
    expect(
        parsed.source.weapons.size() == 1 &&
            parsed.source.weapons.front().accuracy == 90,
        "authored weapon accuracy is mapped"
    );
    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.weapons.front().accuracy == 100,
        "omitted accuracy always lands"
    );

    // And the bound is the schema's: a percentage, not an arbitrary number.
    std::string overconfident{source};
    overconfident.insert(
        overconfident.find(needle) + needle.size(), R"json("accuracy":120,)json"
    );
    const auto refused = gc::parse_source_project_json(overconfident);
    expect(
        has_source_diagnostic(
            refused, gc::SourceDiagnosticCode::invalid_value
        ),
        "an accuracy outside a percentage is refused by the reader"
    );
}

void maps_authored_item_effects() {
    // The same two spellings an accuracy has, and the same rule about what the
    // absent one means: an item with no authored effect is one nothing can do
    // anything with, which is what every item written before items could be
    // spent was. Reading the absence as a restore of zero would turn every
    // keepsake in every shipped project into something a character can waste
    // an activation drinking.
    std::string with_effect{source};
    const std::string needle = R"json("stackLimit":5)json";
    with_effect.insert(
        with_effect.find(needle) + needle.size(),
        R"json(,"kind":"restore","power":4)json"
    );
    const auto parsed = gc::parse_source_project_json(with_effect);
    expect(static_cast<bool>(parsed), "a project with an item effect parses");
    expect(
        parsed.source.items.size() == 1 &&
            parsed.source.items.front().kind == gc::ItemKind::restore &&
            parsed.source.items.front().power == 4,
        "an authored restoring item is mapped"
    );
    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.items.front().kind == gc::ItemKind::none &&
            defaulted.source.items.front().power == 0,
        "an item with no authored effect does nothing a battle can apply"
    );

    // And the vocabulary is closed: a kind this build does not know is an
    // author meaning something the rules cannot express.
    std::string unknown{source};
    unknown.insert(
        unknown.find(needle) + needle.size(), R"json(,"kind":"poison")json"
    );
    expect(
        has_source_diagnostic(
            gc::parse_source_project_json(unknown),
            gc::SourceDiagnosticCode::invalid_value
        ),
        "an item kind the rules have no verb for is refused by the reader"
    );
}

void maps_authored_growth() {
    // Three fields with three different absences, and each absence has to mean
    // the thing an author who wrote nothing meant. Nothing to defeat, a hundred
    // a level, and no chance of growing at all: exactly what every unit type
    // written before growth existed says.
    const std::string needle =
        R"json({"id":"blue_unit","name":"Blue unit","classId":"vanguard",)json";
    std::string growing{source};
    growing.insert(
        growing.find(needle) + needle.size(),
        R"json("experienceAward":45,"experiencePerLevel":30,)json"
        R"json("growthRates":{"health":70,"defense":25,"actionPoints":5},)json"
    );
    const auto parsed = gc::parse_source_project_json(growing);
    expect(static_cast<bool>(parsed), "a project with growth parses");
    expect(
        parsed.source.unit_types.size() == 2 &&
            parsed.source.unit_types.front().experience_award == 45 &&
            parsed.source.unit_types.front().experience_per_level == 30,
        "the award and the level cost are mapped"
    );
    expect(
        parsed.source.unit_types.size() == 2 &&
            parsed.source.unit_types.front().growth.chance ==
                std::array<std::uint8_t, gc::growable_stat_count>{
                    70, 0, 25, 0, 0, 5
                },
        "and each named chance lands in the slot a level-up rolls it from, "
        "while a stat the block does not name stays at nothing"
    );

    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.unit_types.front().experience_award == 0 &&
            defaulted.source.unit_types.front().experience_per_level ==
                gc::default_experience_per_level &&
            defaulted.source.unit_types.front().growth.chance ==
                std::array<std::uint8_t, gc::growable_stat_count>{},
        "and a unit type that says nothing is worth nothing, costs a hundred a "
        "level, and never grows"
    );

    // The bound is the schema's here too: a percentage, not a number.
    std::string overconfident{source};
    overconfident.insert(
        overconfident.find(needle) + needle.size(),
        R"json("growthRates":{"strength":140},)json"
    );
    expect(
        has_source_diagnostic(
            gc::parse_source_project_json(overconfident),
            gc::SourceDiagnosticCode::invalid_value
        ),
        "a growth chance outside a percentage is refused by the reader"
    );
}

void maps_authored_faction_colour() {
    const std::string needle = R"json({"id":"red","name":"Red")json";
    std::string chosen{source};
    chosen.insert(
        chosen.find(needle) + needle.size(), R"json(,"color":"violet")json"
    );
    const auto parsed = gc::parse_source_project_json(chosen);
    expect(static_cast<bool>(parsed), "a project choosing a colour parses");
    expect(
        parsed.source.factions.size() == 2 &&
            parsed.source.factions[1].colour ==
                gc::faction_colour_index("violet"),
        "an authored faction colour is mapped"
    );
    expect(
        parsed.source.factions[0].colour == gc::faction_colour_unchosen,
        "a faction that chose nothing stays unchosen, so a presenter can "
        "fall back to its position"
    );

    std::string invented{source};
    invented.insert(
        invented.find(needle) + needle.size(), R"json(,"color":"chartreuse")json"
    );
    const auto refused = gc::parse_source_project_json(invented);
    expect(
        !static_cast<bool>(refused),
        "a colour the art library does not offer is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

void maps_authored_theme() {
    const std::string needle = R"json("contentRevision":)json";
    const auto insert_theme = [&needle](const char* name) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(at, std::string(R"json("themeId":")json") + name + "\",");
        return project;
    };

    const auto parsed = gc::parse_source_project_json(insert_theme("winter"));
    expect(static_cast<bool>(parsed), "a project choosing a season parses");
    expect(
        parsed.source.theme == gc::theme_index("winter"),
        "an authored theme is mapped"
    );

    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.theme == gc::default_theme,
        "a project that names no season takes the default theme, so it is "
        "drawn exactly as it was before the menu existed"
    );

    const auto refused = gc::parse_source_project_json(insert_theme("monsoon"));
    expect(
        !static_cast<bool>(refused),
        "a season the art library does not offer is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

void maps_authored_character_style() {
    const std::string needle = R"json("contentRevision":)json";
    const auto insert_style = [&needle](const char* name) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(
            at, std::string(R"json("characterStyleId":")json") + name + "\","
        );
        return project;
    };

    const auto parsed = gc::parse_source_project_json(insert_style("medieval"));
    expect(
        static_cast<bool>(parsed), "a project choosing a character style parses"
    );
    expect(
        parsed.source.character_style == gc::character_style_index("medieval"),
        "an authored character style is mapped"
    );

    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.character_style == gc::default_character_style,
        "a project that names no character style takes the default style, so "
        "its characters are drawn exactly as they were before the menu existed"
    );

    // The second entry on the menu: a project that names it is drawn in it,
    // and it is not the default, so naming it is a visible choice.
    const auto second = gc::parse_source_project_json(insert_style("scifi"));
    expect(
        static_cast<bool>(second) &&
            second.source.character_style == gc::character_style_index("scifi"),
        "a project choosing the sci-fi style is mapped to it"
    );
    expect(
        gc::character_style_index("scifi") != gc::default_character_style,
        "the sci-fi style is a choice away from the default, not the default"
    );

    // Every commissioned style beyond the default pair. Each maps
    // to its own index, and no two styles share one, so the menu the schema,
    // the generator, the editor and this reader agree on is still one list in
    // one order.
    for (const auto* named :
         {"mythical", "nature", "sengoku", "undead", "pirates"}) {
        const auto parsed = gc::parse_source_project_json(insert_style(named));
        expect(
            static_cast<bool>(parsed) &&
                parsed.source.character_style == gc::character_style_index(named),
            "a project choosing a commissioned style is mapped to it"
        );
        expect(
            gc::character_style_index(named) != gc::default_character_style,
            "a commissioned style is a choice away from the default"
        );
    }
    expect(
        gc::character_style_index("mythical") != gc::character_style_index("nature")
            && gc::character_style_index("nature")
                   != gc::character_style_index("sengoku"),
        "every style on the menu holds its own index"
    );
    // Every name on the menu resolves to its own index, and every index on the
    // menu is claimed. Written as a sweep rather than as a pair so a style
    // appended later is covered by the assertion rather than by a reader
    // remembering to widen it.
    bool claimed[gc::character_style_count] = {};
    for (const auto* named :
         {"medieval", "scifi", "mythical", "nature", "sengoku", "undead",
          "pirates"}) {
        const auto index = gc::character_style_index(named);
        expect(index < gc::character_style_count, "a menu name resolves");
        expect(!claimed[index], "every style on the menu holds its own index");
        claimed[index] = true;
    }
    for (std::uint8_t index = 0; index < gc::character_style_count; ++index) {
        expect(claimed[index], "every index on the menu is a style a name reaches");
    }

    // The menu is appended to, never inserted into, and this is where that is
    // asserted rather than assumed. The index is what a compiled project
    // carries and what a console build resolves its art through, so a style
    // added anywhere but the end would not add a style. It would silently
    // change the drawing every project past the insertion point compiles to
    // use, with no field of any record having changed. It is also what lets two
    // commissions be drawn at once without either rewriting the other's art.
    // Written out rather than read from the library's own header: the check
    // beside `styles.h` below proves the reader and the generator agree with
    // each other, which any permutation would also satisfy. This one is the
    // list itself, so moving an entry has to be done here, deliberately.
    constexpr std::array<std::string_view, gc::character_style_count> menu = {
        "medieval", "scifi", "mythical", "nature", "sengoku", "undead",
        "pirates",
    };
    for (std::uint8_t index = 0; index < gc::character_style_count; ++index) {
        expect(
            gc::character_style_index(menu[index]) == index,
            "every style on the menu holds the index its position gives it, and "
            "an added style takes the next one rather than moving another"
        );
    }

    const auto refused = gc::parse_source_project_json(insert_style("gothic"));
    expect(
        !static_cast<bool>(refused),
        "a character style the art library does not offer is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );

    // The style is presentation, so it may not disturb the choice beside it.
    const auto both = gc::parse_source_project_json(insert_style("medieval"));
    expect(
        both.source.theme == gc::default_theme,
        "choosing a character style leaves the season alone"
    );
}

// A character drawn in a style of its own. The project's style is a default
// and a theme, never a gate: any style the library holds is legal on any
// character, and a character that names none follows the game.
//
// Four things are worth asserting, and the third is the one the whole shape
// rests on: that an authored style is carried, that a character naming none
// carries the "names none" marker rather than a style, that the two resolve in
// that order (the game's, then the character's), and that a style the library
// does not hold is refused with the path naming the character rather than the
// project.
void maps_a_characters_own_style() {
    const std::string needle = R"json("factionId":"red",)json";
    const auto with_unit_style = [&needle](const char* name) {
        std::string project{source};
        const auto at = project.find(needle) + needle.size();
        project.insert(
            at, std::string(R"json("characterStyleId":")json") + name + "\","
        );
        return project;
    };
    // The project holds two characters in source order: the blue one, which
    // never names a style here, and the red one, which is the one the helper
    // above dresses.
    const auto plain = gc::parse_source_project_json(source);
    expect(static_cast<bool>(plain), "the vertical project still parses");
    expect(
        plain.source.unit_types.size() == 2,
        "the vertical project holds the two characters this asserts over"
    );
    expect(
        plain.source.unit_types.back().character_style ==
            gc::character_style_unnamed,
        "a character that names no style of its own carries the marker for "
        "'follows the game', not a style — which is what every character "
        "authored before this field existed says"
    );

    const auto raised = gc::parse_source_project_json(with_unit_style("undead"));
    expect(static_cast<bool>(raised), "a character naming its own style parses");
    expect(
        raised.source.unit_types.back().character_style ==
            gc::character_style_index("undead"),
        "a character's own style is mapped"
    );
    expect(
        raised.source.unit_types.front().character_style ==
            gc::character_style_unnamed,
        "and it is that character's alone: the one beside it still follows "
        "the game"
    );
    expect(
        raised.source.character_style == gc::default_character_style,
        "a character drawn as undead does not make the game an undead game"
    );

    // Every style on the menu is legal on a character, because the project's
    // choice is a default and never a gate. Written as a sweep so a style
    // appended later is covered rather than remembered.
    for (const auto* named :
         {"medieval", "scifi", "mythical", "nature", "sengoku", "undead",
          "pirates"}) {
        const auto parsed = gc::parse_source_project_json(with_unit_style(named));
        expect(
            static_cast<bool>(parsed) &&
                parsed.source.unit_types.back().character_style ==
                    gc::character_style_index(named),
            "any style the library holds may be named by any character"
        );
    }

    // The figure axis, on the same terms and asserted the same way. A figure
    // is the build a role is drawn at rather than the role, so it combines
    // freely with any style: the two are independent choices about the same
    // drawing, and neither gates the other.
    const std::string figure_needle = R"json("factionId":"blue",)json";
    const auto with_unit_figure = [&figure_needle](const char* name) {
        std::string project{source};
        const auto at = project.find(figure_needle) + figure_needle.size();
        project.insert(
            at, std::string(R"json("characterFigureId":")json") + name + "\","
        );
        return project;
    };
    expect(
        plain.source.unit_types.front().character_figure ==
            gc::character_figure_unnamed,
        "a character that names no figure carries the marker for 'follows the "
        "game', which is what every character drawn before figures says"
    );
    for (const auto* named : {"first", "second"}) {
        const auto parsed = gc::parse_source_project_json(with_unit_figure(named));
        expect(
            static_cast<bool>(parsed) &&
                parsed.source.unit_types.front().character_figure ==
                    gc::character_figure_index(named),
            "any figure the library draws may be named by any character"
        );
    }
    const auto bad_figure =
        gc::parse_source_project_json(with_unit_figure("stooped"));
    expect(
        !static_cast<bool>(bad_figure) &&
            has_source_diagnostic(
                bad_figure, gc::SourceDiagnosticCode::invalid_value
            ),
        "a figure the art library does not draw is refused by name"
    );

    // The menu is appended to, never inserted into, for the reason the style
    // menu is: the index is what a package carries and what a console build
    // resolves its art through, so moving an entry would silently redraw every
    // character past it.
    constexpr std::array<std::string_view, gc::character_figure_count>
        figure_menu = {"first", "second"};
    for (std::uint8_t index = 0; index < gc::character_figure_count; ++index) {
        expect(
            gc::character_figure_index(figure_menu[index]) == index,
            "every figure on the menu holds the index its position gives it"
        );
    }
    expect(
        gc::character_figure_index("first") == gc::default_character_figure,
        "the first figure is the default, so a project that names none is "
        "drawn exactly as it was before figures existed"
    );

    // Geometry: the third axis of the same choice, and the only one that is a
    // statement about the whole game rather than about one character. It is
    // asserted here on the project root for exactly that reason -- there is no
    // per-character override to sweep, because "this unit is a model and that
    // one is a sprite" is not a thing a board can draw coherently.
    const std::string geometry_needle = R"json("schemaVersion": "1.1.0",)json";
    const auto with_geometry = [&geometry_needle](const char* name) {
        std::string project{source};
        const auto at = project.find(geometry_needle) + geometry_needle.size();
        project.insert(
            at, std::string(R"json("characterGeometry":")json") + name + "\","
        );
        return project;
    };
    expect(
        plain.source.character_geometry == gc::default_character_geometry,
        "a project that names no geometry is drawn as sprites, which is what "
        "every board showed before models existed"
    );
    for (const auto* named : {"sprites", "models"}) {
        const auto parsed = gc::parse_source_project_json(with_geometry(named));
        expect(
            static_cast<bool>(parsed) &&
                parsed.source.character_geometry ==
                    gc::character_geometry_index(named),
            "either way of drawing a character may be named by a project"
        );
    }
    const auto bad_geometry =
        gc::parse_source_project_json(with_geometry("voxels"));
    expect(
        !static_cast<bool>(bad_geometry) &&
            has_source_diagnostic(
                bad_geometry, gc::SourceDiagnosticCode::invalid_value
            ),
        "a way of drawing the art library does not offer is refused by name"
    );
    constexpr std::array<std::string_view, gc::character_geometry_count>
        geometry_menu = {"sprites", "models"};
    for (std::uint8_t index = 0; index < gc::character_geometry_count; ++index) {
        expect(
            gc::character_geometry_index(geometry_menu[index]) == index,
            "every way of drawing holds the index its position gives it"
        );
    }

    const auto refused = gc::parse_source_project_json(with_unit_style("gothic"));
    expect(
        !static_cast<bool>(refused),
        "a character style the art library does not offer is refused on a "
        "character too"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
    bool named_the_character = false;
    for (const auto& diagnostic : refused.diagnostics) {
        named_the_character = named_the_character ||
            diagnostic.path.find("unitTypes") != std::string::npos;
    }
    expect(
        named_the_character,
        "and the path names the character, because a project may hold a "
        "hundred and only one of them is wrong"
    );
}

// What a conversation between maps is drawn against. The fourth presentation
// choice and the first authored per record rather than per project, so the
// three things worth asserting are that a named backdrop is carried, that a
// scene naming none carries nothing (the byte the package writes is the
// difference between a rewritten campaign and an untouched one), and that the
// menu is one list in one order.
void maps_a_scene_backdrop() {
    const std::string needle = R"json("objectives":)json";
    const auto with_scene = [&needle](const std::string& scene) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(at, R"json("dialogues": [{"id":"opening",)json" + scene +
                               R"json("name":"Opening"}],)json");
        return project;
    };

    const auto named = gc::parse_source_project_json(
        with_scene(R"json("backgroundId":"throne_hall",)json"));
    expect(
        static_cast<bool>(named), "a scene choosing a backdrop parses"
    );
    expect(
        named.source.dialogues.size() == 1 &&
            named.source.dialogues.front().backdrop ==
                gc::backdrop_index("throne_hall") + 1,
        "an authored backdrop is carried as its menu index plus one"
    );

    const auto silent = gc::parse_source_project_json(with_scene(""));
    expect(
        static_cast<bool>(silent) && silent.source.dialogues.size() == 1 &&
            silent.source.dialogues.front().backdrop == 0,
        "a scene that names no backdrop carries nothing, which is what keeps "
        "every campaign written before backdrops existed byte-identical"
    );

    // Every name on the menu resolves to its own index and every index is
    // claimed, written as a sweep so a backdrop appended later is covered.
    constexpr std::array<std::string_view, gc::backdrop_count> menu = {
        "throne_hall", "night_camp", "deep_wood", "mountain_dusk", "open_sea",
        "star_field", "crypt",
    };
    bool claimed[gc::backdrop_count] = {};
    for (const auto& name : menu) {
        const auto index = gc::backdrop_index(name);
        expect(index < gc::backdrop_count, "a menu name resolves");
        expect(!claimed[index], "every backdrop holds its own index");
        claimed[index] = true;
    }
    for (std::uint8_t index = 0; index < gc::backdrop_count; ++index) {
        expect(claimed[index], "every index on the menu is a backdrop a name reaches");
        expect(
            gc::backdrop_index(menu[index]) == index,
            "every backdrop holds the index its position gives it, so an added "
            "backdrop takes the next one rather than moving another"
        );
    }

    const auto refused = gc::parse_source_project_json(
        with_scene(R"json("backgroundId":"observatory",)json"));
    expect(
        !static_cast<bool>(refused),
        "a backdrop the art library does not offer is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );

    // Presentation, so it disturbs neither choice beside it.
    expect(
        named.source.theme == gc::default_theme &&
            named.source.character_style == gc::default_character_style,
        "choosing a backdrop leaves the season and the style alone"
    );
}

// Who a scene's speakers are. The join from a speaker string to a character is
// made here, by the one component that holds the cast and the lines together,
// so what is worth asserting is that it lands on the right entry, that a
// speaker nobody cast lands on none, and that the two ways a cast can be
// self-contradictory are refused rather than settled by picking one.
void maps_a_scene_cast() {
    const std::string needle = R"json("objectives":)json";
    const auto with_scene = [&needle](const std::string& body) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(at, R"json("dialogues": [{"id":"opening",)json" + body +
                               R"json("name":"Opening"}],)json");
        return project;
    };
    constexpr std::string_view lines =
        R"json("lines":[{"speaker":"Ana","text":"One"},)json"
        R"json({"speaker":"Bo","text":"Two"},)json"
        R"json({"speaker":"Ana","text":"Three"},)json"
        R"json({"speaker":"Nobody","text":"Four"}],)json";

    const auto cast = gc::parse_source_project_json(with_scene(
        R"json("cast":[{"speaker":"Ana","unitTypeId":"blue_unit"},)json"
        R"json({"speaker":"Bo","unitTypeId":"red_unit"}],)json" +
        std::string{lines}
    ));
    expect(static_cast<bool>(cast), "a scene naming its cast parses");
    const auto& scene = cast.source.dialogues.front();
    expect(
        scene.cast.size() == 2 &&
            scene.cast[0].unit_type_id ==
                grandleon::core::stable_content_id_v1("blue_unit") &&
            scene.cast[1].unit_type_id ==
                grandleon::core::stable_content_id_v1("red_unit"),
        "a cast entry carries the unit type identity it names"
    );
    expect(
        scene.lines.size() == 4 && scene.lines[0].cast_entry == 1 &&
            scene.lines[1].cast_entry == 2 && scene.lines[2].cast_entry == 1,
        "every line is joined to the entry its speaker names, and one "
        "character speaking twice is the same entry both times"
    );
    expect(
        scene.lines[3].cast_entry == 0,
        "a speaker the cast does not name is joined to nobody, which is what "
        "every line carried before a scene could name anybody"
    );

    // A scene with lines and no cast at all: every line names nobody, which is
    // the state that keeps every package written before this byte-identical.
    const auto silent =
        gc::parse_source_project_json(with_scene(std::string{lines}));
    expect(
        static_cast<bool>(silent) &&
            silent.source.dialogues.front().cast.empty(),
        "a scene that casts nobody carries no cast"
    );
    bool any_named = false;
    for (const auto& line : silent.source.dialogues.front().lines) {
        if (line.cast_entry != 0) any_named = true;
    }
    expect(!any_named, "and joins none of its lines to anybody");

    // Two answers to "who is Ana" is a question this reader must not settle by
    // picking one.
    const auto twice = gc::parse_source_project_json(with_scene(
        R"json("cast":[{"speaker":"Ana","unitTypeId":"blue_unit"},)json"
        R"json({"speaker":"Ana","unitTypeId":"red_unit"}],)json" +
        std::string{lines}
    ));
    expect(
        !static_cast<bool>(twice) &&
            has_source_diagnostic(
                twice, gc::SourceDiagnosticCode::invalid_value
            ),
        "one speaker cast twice is refused"
    );

    // What renaming a speaker leaves behind. Its symptom on screen is the old
    // drawing rather than an error, so it has to be an error here.
    const auto unspoken = gc::parse_source_project_json(with_scene(
        R"json("cast":[{"speaker":"Ana","unitTypeId":"blue_unit"},)json"
        R"json({"speaker":"Cy","unitTypeId":"red_unit"}],)json" +
        std::string{lines}
    ));
    expect(
        !static_cast<bool>(unspoken) &&
            has_source_diagnostic(
                unspoken, gc::SourceDiagnosticCode::invalid_value
            ),
        "a cast entry that speaks no line is refused"
    );
    bool named_the_entry = false;
    for (const auto& diagnostic : unspoken.diagnostics) {
        if (diagnostic.path == "$.dialogues[0].cast[1].speaker") {
            named_the_entry = true;
        }
    }
    expect(
        named_the_entry,
        "and the refusal names the entry an author can find, at the position "
        "it was authored at"
    );

    // Presentation, so it disturbs neither the backdrop beside it nor the
    // words themselves. The approved display name is not the cast's to change.
    expect(
        cast.source.dialogues.front().backdrop == 0 &&
            scene.lines[0].speaker == "Ana" &&
            scene.lines[3].speaker == "Nobody",
        "casting a scene leaves its backdrop and every speaker string alone"
    );
}

// The order the turns come in is a rule, and the only one a project may state
// once for every battle. The reader resolves it where the encounter's byte is
// already decided: a board that states its own keeps it, a board that states
// nothing takes the game's default, and a project that states no default is the
// project it always was, byte for byte.
void maps_the_game_wide_turn_order() {
    const std::string needle = R"json("contentRevision":)json";
    const auto insert_default = [&needle](const char* name) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(
            at, std::string(R"json("defaultTurnOrder":")json") + name + "\","
        );
        return project;
    };
    const auto with_board_order = [](const std::string& project,
                                     const char* name) {
        const std::string board = R"json("kind":"encounter",)json";
        std::string result{project};
        const auto at = result.find(board) + board.size();
        result.insert(
            at, std::string(R"json("turnOrder":")json") + name + "\","
        );
        return result;
    };

    const auto defaulted = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(defaulted) &&
            defaulted.source.encounters.front().turn_order ==
                gc::TurnOrder::alternating,
        "a project that states no default leaves a board that states nothing "
        "alternating, which is what it always was"
    );

    const auto inherited =
        gc::parse_source_project_json(insert_default("initiative"));
    expect(
        static_cast<bool>(inherited) &&
            inherited.source.encounters.front().turn_order ==
                gc::TurnOrder::initiative,
        "a board that states no turn order takes the game's default"
    );

    const auto overridden = gc::parse_source_project_json(
        with_board_order(insert_default("initiative"), "sideBlocks")
    );
    expect(
        static_cast<bool>(overridden) &&
            overridden.source.encounters.front().turn_order ==
                gc::TurnOrder::side_blocks,
        "a board that states its own turn order keeps it against the default"
    );

    // The one that would be silently wrong if the default were applied over an
    // authored value: alternating is both the fallback and a real choice, so a
    // board authoring it must survive a project defaulting to something else.
    const auto authored_alternating = gc::parse_source_project_json(
        with_board_order(insert_default("sideBlocks"), "alternating")
    );
    expect(
        static_cast<bool>(authored_alternating) &&
            authored_alternating.source.encounters.front().turn_order ==
                gc::TurnOrder::alternating,
        "a board that authors alternating keeps it under a different default"
    );

    // The package does not grow and does not move: stating the default a
    // project already had produces the same bytes, because the resolution
    // happens before the one byte an encounter has always carried.
    const auto stated =
        gc::parse_source_project_json(insert_default("alternating"));
    const auto silent_bytes = gc::compile(defaulted.source);
    const auto stated_bytes = gc::compile(stated.source);
    expect(
        static_cast<bool>(silent_bytes) && static_cast<bool>(stated_bytes) &&
            silent_bytes.package == stated_bytes.package,
        "stating the default a project already had compiles to the same bytes"
    );

    const auto refused = gc::parse_source_project_json(insert_default("speed"));
    expect(
        !static_cast<bool>(refused),
        "a turn order the simulation does not offer is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

// What a fall costs the company is a rule too, and the second one a project may
// state once for the whole game. Unlike the turn order it is not resolved away
// in the reader: it is carried on the source and written on every campaign
// record. So what is asserted here is that the words arrive, that a word off
// the menu is refused, and that a project saying nothing is the project it
// always was, byte for byte.
void maps_the_character_loss_rule() {
    const std::string needle = R"json("contentRevision":)json";
    const auto with_rule = [&needle](const char* name) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(
            at, std::string(R"json("characterLoss":")json") + name + "\","
        );
        return project;
    };

    const auto silent = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(silent) &&
            silent.source.character_loss == gc::CharacterLoss::permanent,
        "a project that states no rule loses a fallen character for good, "
        "which is the only thing a fall ever meant before this"
    );

    const auto carried = gc::parse_source_project_json(with_rule("recoverable"));
    expect(
        static_cast<bool>(carried) &&
            carried.source.character_loss == gc::CharacterLoss::recoverable,
        "a project that states its characters are carried off says so on the "
        "source the compiler reads"
    );

    // The two settings are separate, and stating one must not answer for the
    // other: a project that only changes what a fall costs protects nobody.
    expect(
        static_cast<bool>(carried) && !carried.source.invulnerable_for_testing,
        "and asks for no testing invulnerability by saying it, because the "
        "testing aid is not a value of this rule"
    );

    // The package does not grow and does not move. Two claims here, and both
    // matter: a project that says nothing writes no tail, and a project that
    // says in words the thing an absent rule already means writes no tail
    // either. A board authoring alternating is shown the same courtesy.
    const auto stated_default = gc::parse_source_project_json(
        with_rule("permanent")
    );
    const auto silent_bytes = gc::compile(silent.source);
    const auto stated_bytes = gc::compile(stated_default.source);
    const auto carried_bytes = gc::compile(carried.source);
    expect(
        static_cast<bool>(silent_bytes) && static_cast<bool>(stated_bytes) &&
            silent_bytes.package == stated_bytes.package,
        "stating permanent loss in so many words compiles to the same bytes as "
        "stating nothing, so no golden and no console expectation moves"
    );
    expect(
        static_cast<bool>(carried_bytes) &&
            carried_bytes.package != silent_bytes.package,
        "and stating a rule the campaign did not already follow does reach the "
        "package, so the claim above is about silence and not about the tail "
        "never being written"
    );

    const auto refused = gc::parse_source_project_json(with_rule("invincible"));
    expect(
        !static_cast<bool>(refused),
        "a fate for a fallen character that is not on the menu is refused "
        "rather than replaced, because falling back to permanent loss would "
        "hide a misspelling that costs a player their characters"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

// The testing aid beside it. Deliberately not a third value of the rule above,
// and deliberately in the package all the same: it changes what the rules do,
// so two people holding the same package must get the same battles.
void maps_the_testing_invulnerability() {
    const std::string needle = R"json("contentRevision":)json";
    const auto with_flag = [&needle](const char* value) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(
            at, std::string(R"json("invulnerableForTesting":)json") + value + ","
        );
        return project;
    };

    const auto silent = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(silent) && !silent.source.invulnerable_for_testing,
        "a project that asks for nothing protects nobody, which is what every "
        "project before this setting existed did"
    );

    const auto asked = gc::parse_source_project_json(with_flag("true"));
    expect(
        static_cast<bool>(asked) && asked.source.invulnerable_for_testing,
        "a project that asks for it says so on the source the compiler reads"
    );
    expect(
        static_cast<bool>(asked) &&
            asked.source.character_loss == gc::CharacterLoss::permanent,
        "and says nothing about what a fall costs, because these are two "
        "settings and not one"
    );

    // Written down as false is the same project as one that never mentioned
    // it, for the same reason an empty menu is: absent is how nothing is said.
    const auto declined = gc::parse_source_project_json(with_flag("false"));
    const auto silent_bytes = gc::compile(silent.source);
    const auto declined_bytes = gc::compile(declined.source);
    const auto asked_bytes = gc::compile(asked.source);
    expect(
        static_cast<bool>(declined) && !declined.source.invulnerable_for_testing,
        "asking for it and saying no is asking for nothing"
    );
    expect(
        static_cast<bool>(silent_bytes) && static_cast<bool>(declined_bytes) &&
            silent_bytes.package == declined_bytes.package,
        "and compiles to the same bytes as a project that never mentioned it"
    );
    expect(
        static_cast<bool>(asked_bytes) &&
            asked_bytes.package != silent_bytes.package,
        "while asking for it reaches the package, because it is canonical: a "
        "client that could switch it on for itself would be a client whose "
        "battles nobody else could reproduce"
    );

    const auto refused = gc::parse_source_project_json(with_flag(R"json("yes")json"));
    expect(
        !static_cast<bool>(refused),
        "and a value that is not a yes or a no is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

// What a class crosses is a rule, so the reader carries the authored words
// into the bits the engine acts on, refuses a word the vocabulary does not
// hold, and leaves a class that says nothing walking.
void maps_authored_traversal() {
    const std::string needle = R"json("allowedWeaponTypeIds":["blade"])json";
    const auto with_traversal = [&needle](const char* fields) {
        std::string project{source};
        const auto at = project.find(needle);
        project.insert(
            at + needle.size(),
            std::string(R"json(,"traversal":{)json") + fields + "}"
        );
        return project;
    };

    const auto walker = gc::parse_source_project_json(source);
    expect(
        static_cast<bool>(walker) &&
            walker.source.classes.front().crossings == gc::crossing_none,
        "a class that authors no traversal walks, which is what every class "
        "did before terrain could stop anyone"
    );

    const auto flier = gc::parse_source_project_json(
        with_traversal(R"json("flying":true)json")
    );
    expect(
        static_cast<bool>(flier) &&
            flier.source.classes.front().crossings == gc::crossing_every,
        "an authored flier crosses everything"
    );

    const auto grounded = gc::parse_source_project_json(
        with_traversal(R"json("flying":false)json")
    );
    expect(
        static_cast<bool>(grounded) &&
            grounded.source.classes.front().crossings == gc::crossing_none,
        "authoring flight as false is a walker, not a flier"
    );

    const auto wader = gc::parse_source_project_json(
        with_traversal(R"json("crossings":["water"])json")
    );
    expect(
        static_cast<bool>(wader) &&
            wader.source.classes.front().crossings == gc::crossing_water,
        "a named crossing is carried on its own"
    );

    const auto both = gc::parse_source_project_json(
        with_traversal(R"json("crossings":["water","heights"])json")
    );
    expect(
        static_cast<bool>(both) &&
            both.source.classes.front().crossings ==
                static_cast<std::uint8_t>(
                    gc::crossing_water | gc::crossing_heights
                ),
        "two named crossings are both carried"
    );

    const auto refused = gc::parse_source_project_json(
        with_traversal(R"json("crossings":["lava"])json")
    );
    expect(
        !static_cast<bool>(refused),
        "a crossing the vocabulary does not name is refused"
    );
    expect(
        has_source_diagnostic(refused, gc::SourceDiagnosticCode::invalid_value),
        "the refusal names the value, not the syntax"
    );
}

void agrees_with_the_art_library() {
    expect(
        grandleon_theme_count == static_cast<int>(gc::theme_count),
        "the theme menu is one list: the reader holds as many as the library"
    );
    for (int index = 0; index < grandleon_theme_count; ++index) {
        expect(
            gc::theme_index(grandleon_theme_names[index]) == index,
            "every theme resolves to its own position in the library's menu"
        );
    }
    expect(
        grandleon_character_style_count ==
            static_cast<int>(gc::character_style_count),
        "the character style menu is one list: the reader holds as many as "
        "the library"
    );
    for (int index = 0; index < grandleon_character_style_count; ++index) {
        expect(
            gc::character_style_index(grandleon_character_style_names[index]) ==
                index,
            "every character style resolves to its own position in the "
            "library's menu"
        );
    }
    expect(
        gc::character_style_index("gothic") == gc::character_style_count,
        "a style the library does not hold resolves to no menu position"
    );
    expect(
        grandleon_character_figure_count ==
            static_cast<int>(gc::character_figure_count),
        "the figure menu is one list: the reader holds as many as the library"
    );
    for (int index = 0; index < grandleon_character_figure_count; ++index) {
        expect(
            gc::character_figure_index(
                grandleon_character_figure_names[index]
            ) == index,
            "every figure resolves to its own position in the library's menu"
        );
    }
    expect(
        gc::character_figure_index("stooped") == gc::character_figure_count,
        "a figure the library does not draw resolves to no menu position"
    );
    expect(
        grandleon_backdrop_count == static_cast<int>(gc::backdrop_count),
        "the backdrop menu is one list: the reader holds as many as the library"
    );
    for (int index = 0; index < grandleon_backdrop_count; ++index) {
        expect(
            gc::backdrop_index(grandleon_backdrop_names[index]) == index,
            "every backdrop resolves to its own position in the library's menu"
        );
    }
    expect(
        gc::backdrop_index("observatory") == gc::backdrop_count,
        "a backdrop the library does not hold resolves to no menu position"
    );
    // The bands are the whole of a backdrop, so a library that published an
    // empty one, or one whose bands did not fill the frame, would be a
    // backdrop no client could draw. Checked against the header every client
    // reads rather than against the generator that wrote it.
    for (int index = 0; index < grandleon_backdrop_count; ++index) {
        const int bands = grandleon_backdrop_band_count[index];
        expect(bands > 0, "every backdrop names at least one band");
        int rows = 0;
        for (int band = 0; band < bands; ++band) {
            rows += grandleon_backdrop_bands[index][band][1];
            expect(
                grandleon_backdrop_bands[index][band][0] ==
                    rows - grandleon_backdrop_bands[index][band][1],
                "a band begins where the band above it ended"
            );
        }
        expect(
            rows == grandleon_backdrop_rows,
            "a backdrop's bands fill the frame exactly, top to bottom"
        );
    }
    expect(
        grandleon_archetype_count == static_cast<int>(gc::archetype_count),
        "the archetype roster is one list: the reader holds as many as the "
        "library"
    );
    for (int index = 0; index < grandleon_archetype_count; ++index) {
        expect(
            gc::archetype_index(grandleon_archetype_names[index]) == index,
            "every archetype the library publishes selects its own position"
        );
    }
    expect(
        gc::archetype_index("quartermaster") == gc::archetype_unnamed,
        "a name the roster does not spell selects no archetype"
    );
    expect(
        gc::archetype_default < gc::archetype_count,
        "the archetype a unit type that names none wears is drawable"
    );
    expect(
        grandleon_terrain_kind_count == static_cast<int>(gc::terrain_kind_count),
        "the terrain registry is one list"
    );
    for (int kind = 0; kind < grandleon_terrain_kind_count; ++kind) {
        for (int word = 0; word < grandleon_terrain_keyword_count; ++word) {
            const char* keyword = grandleon_terrain_keywords[kind][word];
            if (keyword == nullptr) continue;
            expect(
                gc::terrain_kind_index(keyword) == kind,
                "every keyword the library publishes selects its own terrain"
            );
        }
    }
    // The convention itself, not just the table: a name is matched by the
    // words inside it, in the library's order.
    expect(
        gc::terrain_kind_index("Stone Bridge") ==
            gc::terrain_kind_index("road"),
        "a name is matched case-insensitively by the words inside it"
    );
    expect(
        gc::terrain_kind_index("mountain road") ==
            gc::terrain_kind_index("road"),
        "an ambiguous name is settled by the library's match order"
    );
    expect(
        gc::terrain_kind_index("crystal") == gc::terrain_kind_unknown,
        "a name the library does not know resolves to no terrain, so a "
        "presenter can draw its own fallback"
    );
}

void caps_json_nesting_depth() {
    // The parser also runs on the Nintendo 64's small stack, so nesting must
    // fail as a diagnostic well before it can exhaust the stack.
    std::string deep = "{\"a\":";
    for (int level = 0; level < 200; ++level) deep += "[";
    deep += "1";
    for (int level = 0; level < 200; ++level) deep += "]";
    deep += "}";
    const auto parsed = gc::parse_source_project_json(deep);
    expect(!static_cast<bool>(parsed), "deep nesting is rejected");
    expect(
        has_source_diagnostic(parsed, gc::SourceDiagnosticCode::invalid_json),
        "deep nesting is a syntax diagnostic, not a crash"
    );

    // The canonical project sits nowhere near the cap and still parses.
    expect(
        static_cast<bool>(gc::parse_source_project_json(source)),
        "ordinary nesting is untouched by the cap"
    );
}

// The canonical project with one substring swapped, which is how every case
// below states the single thing it is about.
std::string source_with(std::string_view needle, std::string_view replacement) {
    std::string document{source};
    const auto position = document.find(needle);
    if (position == std::string::npos) return document;
    document.replace(position, needle.size(), replacement);
    return document;
}

// Both halves of the typed-compatibility bridge `SOURCE_FORMAT.md` describes.
// Omitting the allowance is legacy unrestricted access; stating an empty one is
// an author saying "no weapon type" and meaning it. The reader could not tell
// the two apart, so it read every legacy project as the second.
void an_omitted_weapon_allowance_is_unrestricted() {
    const auto unrestricted = gc::parse_source_project_json(
        source_with(R"json(,
    "allowedWeaponTypeIds":["blade"])json", "")
    );
    expect(
        static_cast<bool>(unrestricted),
        "a class may state no weapon allowance"
    );
    expect(
        !unrestricted.source.classes.front().states_allowed_weapon_types,
        "and the reader records that it stated none"
    );
    expect(
        static_cast<bool>(gc::compile(unrestricted.source)),
        "a class that states none permits the weapons its units carry"
    );

    const auto forbids_everything = gc::parse_source_project_json(
        source_with(R"json("allowedWeaponTypeIds":["blade"])json",
                    R"json("allowedWeaponTypeIds":[])json")
    );
    expect(
        static_cast<bool>(forbids_everything) &&
            forbids_everything.source.classes.front()
                .states_allowed_weapon_types,
        "an empty allowance is still an allowance the author stated"
    );
    expect(
        !static_cast<bool>(gc::compile(forbids_everything.source)),
        "and it permits no weapon type at all"
    );
}

// `weapon.schema.json` and `item.schema.json` require neither, and
// `SOURCE_FORMAT.md` calls omitting them legacy unclassified content. The
// reader required both, which made a doc-blessed project shape uncompilable.
void a_weapon_or_item_may_state_no_type() {
    std::string document{source};
    for (const std::string_view typed : {
             R"json("weaponTypeId":"blade",)json",
             R"json("itemTypeId":"healing",)json",
         }) {
        const auto position = document.find(typed);
        expect(position != std::string::npos, "the fixture states both types");
        document.erase(position, typed.size());
    }
    // The one class in the fixture states an allowance, and an untyped weapon
    // has unknown compatibility rather than known compatibility, so the
    // allowance goes with the type.
    const auto position =
        document.find(R"json(,
    "allowedWeaponTypeIds":["blade"])json");
    document.erase(position, std::string_view(R"json(,
    "allowedWeaponTypeIds":["blade"])json").size());

    const auto parsed = gc::parse_source_project_json(document);
    expect(
        static_cast<bool>(parsed),
        "unclassified weapons and items are not missing values"
    );
    expect(
        static_cast<bool>(gc::compile(parsed.source)),
        "and an unclassified project compiles"
    );
}

// The one enumeration in the reader where answering a word it does not know by
// picking one would do real damage. A fireball spelled `Magical` taken as
// physical is reduced by defense instead of resistance, reads nothing from
// magic, and is never mentioned to the author, so it is refused by name.
void refuses_a_damage_type_the_vocabulary_does_not_hold() {
    const std::string_view spark =
        R"json("objectives": [{)json";
    for (const auto& [written, accepted] : std::initializer_list<
             std::pair<std::string_view, bool>>{
             {R"json("damageType":"magical",)json", true},
             {R"json("damageType":"physical",)json", true},
             {R"json("damageType":"Magical",)json", false},
             {R"json("damageType":"holy",)json", false},
         }) {
        std::string document{source};
        const auto position = document.find(spark);
        document.insert(
            position,
            std::string(R"json("abilities": [{"id":"fireball","name":"Fireball",
    "kind":"damage",)json") + std::string(written) +
                R"json("power":4,"minimumRange":1,"maximumRange":2}],
  )json"
        );
        const auto parsed = gc::parse_source_project_json(document);
        expect(
            static_cast<bool>(parsed) == accepted,
            std::string("a damage type of '") + std::string(written) +
                "' is answered by the vocabulary"
        );
    }
}

// The cap is asked of the authored array before a single entry is read.
// Reading first would make sixty thousand entries take seventy seconds to be
// told they are fifty-nine thousand too many, because every one would be read,
// allocated and compared against every entry before it. Asserted by shape
// rather than by clock: nothing past the cap is looked at, so a fault authored
// past it earns no diagnostic of its own.
void caps_a_cast_before_reading_it() {
    std::string entries;
    for (int index = 0; index < 2'000; ++index) {
        // Two entries share a speaker, and both sit far past the cap.
        const int named = index == 501 ? 500 : index;
        entries += index == 0 ? "" : ",";
        entries += R"json({"speaker":"Speaker )json" +
            std::to_string(named) + R"json(","unitTypeId":"blue_unit"})json";
    }
    const auto parsed = gc::parse_source_project_json(source_with(
        R"json("objectives": [{)json",
        R"json("dialogues": [{"id":"crowd","name":"Crowd","cast":[)json" +
            entries + R"json(]}],
  "objectives": [{)json"
    ));
    expect(!static_cast<bool>(parsed), "a cast of two thousand is refused");
    expect(
        parsed.diagnostics.front().path == "$.dialogues[0].cast",
        "at the cast's own path, before anything else is said"
    );
    // Two hundred and fifty-five entries kept, each speaking no line, plus the
    // cap itself. The number is decided by the cap and not by what was
    // authored, which is the whole of the claim: the two thousandth entry costs
    // nothing because it is never read.
    expect(
        parsed.diagnostics.size() == 256,
        "the work is bounded by the cap rather than by the authored size"
    );
    expect(
        std::none_of(
            parsed.diagnostics.begin(),
            parsed.diagnostics.end(),
            [](const gc::SourceDiagnostic& diagnostic) {
                return diagnostic.detail.find("cast twice") !=
                       std::string::npos;
            }
        ),
        "and a speaker repeated past the cap is never compared against"
    );
}

// A scene may author a cast and no lines, which the schema allows. The
// reconciliation runs whether or not `lines` is present, so the presence of one
// key never decides whether a documented refusal exists at all.
void reconciles_a_cast_against_no_lines_at_all() {
    const auto parsed = gc::parse_source_project_json(source_with(
        R"json("objectives": [{)json",
        R"json("dialogues": [{"id":"quiet","name":"Quiet",
    "cast":[{"speaker":"Kestrel","unitTypeId":"blue_unit"}]}],
  "objectives": [{)json"
    ));
    expect(
        !static_cast<bool>(parsed),
        "a cast entry that speaks no line is refused with no lines at all"
    );
    expect(
        parsed.diagnostics.size() == 1 &&
            parsed.diagnostics.front().path ==
                "$.dialogues[0].cast[0].speaker",
        "at the entry's own authored position"
    );
}

// `common.schema.json` states the package identity as lowercase hex with a
// version nibble in [1-5] and a variant nibble in [89ab]. The reader accepted
// any hex in any case, so agreement was by convention. This identity is the
// one authored string that reaches the package bytes unhashed.
void holds_a_package_identity_to_the_shape_the_schema_states() {
    for (const auto& [identity, accepted] : std::initializer_list<
             std::pair<std::string_view, bool>>{
             {"d05f4dc5-592f-4c6a-9093-f4090a722ffc", true},
             {"D05F4DC5-592F-4C6A-9093-F4090A722FFC", false},
             {"d05f4dc5-592f-0c6a-9093-f4090a722ffc", false},
             {"d05f4dc5-592f-4c6a-0093-f4090a722ffc", false},
         }) {
        const auto parsed = gc::parse_source_project_json(source_with(
            "d05f4dc5-592f-4c6a-9093-f4090a722ffc", identity
        ));
        expect(
            static_cast<bool>(parsed) == accepted,
            std::string("package identity '") + std::string(identity) +
                "' is judged as the schema judges it"
        );
    }
}

// `map.schema.json` caps a board at 256 on a side. The reader allowed 65535,
// and the only thing that refused such a board downstream was a placement
// coordinate cast going negative.
void holds_a_board_to_the_size_the_schema_states() {
    expect(
        static_cast<bool>(gc::parse_source_project_json(
            source_with(R"json("width":2)json", R"json("width":256)json")
        )),
        "a board may be as wide as the schema allows"
    );
    expect(
        !static_cast<bool>(gc::parse_source_project_json(
            source_with(R"json("width":2)json", R"json("width":257)json")
        )),
        "and no wider"
    );
}

// JSON names four whitespace characters. `std::isspace` names more, and answers
// according to the C locale besides.
void accepts_only_the_whitespace_json_names() {
    std::string tabbed{source};
    tabbed.insert(tabbed.find('{') + 1, "\t\r\n ");
    expect(
        static_cast<bool>(gc::parse_source_project_json(tabbed)),
        "the four JSON calls whitespace are skipped"
    );

    std::string vertical{source};
    vertical.insert(vertical.find('{') + 1, "\v");
    expect(
        !static_cast<bool>(gc::parse_source_project_json(vertical)),
        "and a vertical tab, which JSON forbids, is not"
    );
}

// Two things the campaign flow states and the reader never read: the contract
// the flow was written against, and (where the flow is absent altogether) the
// fact that there is no node to enter and no runtime that can walk one.
void reads_what_the_campaign_flow_states_about_itself() {
    expect(
        !static_cast<bool>(gc::parse_source_project_json(source_with(
            R"json("contractVersion":"1.0.0")json",
            R"json("contractVersion":"2.0.0")json"
        ))),
        "a flow contract this compiler does not implement is refused"
    );

    const auto flowless = gc::parse_source_project_json(source_with(
        R"json(,
    "flow":{)json",
        R"json(,
    "unusedFlow":{)json"
    ));
    expect(
        !static_cast<bool>(flowless),
        "a campaign with no flow has no node to enter"
    );
    expect(
        has_source_diagnostic(
            flowless, gc::SourceDiagnosticCode::unsupported_content
        ),
        "and is refused as unsupported rather than as a missing required value"
    );
}

// Three components of at most 1023, packed ten bits each. A pre-release
// revision has nowhere to go in thirty bits and dropping its suffix would
// compile two different revisions to one number, so it is refused by name.
void refuses_a_revision_it_cannot_represent() {
    for (const auto& [revision, accepted] : std::initializer_list<
             std::pair<std::string_view, bool>>{
             {"1.2.3", true},
             {"1023.1023.1023", true},
             {"1.2.3-beta", false},
             {"1024.0.0", false},
             {"1.2", false},
         }) {
        const auto parsed = gc::parse_source_project_json(
            source_with("1.2.3", revision)
        );
        expect(
            static_cast<bool>(parsed) == accepted,
            std::string("content revision '") + std::string(revision) +
                "' is judged by what thirty bits can hold"
        );
    }
}

void names_both_versions_when_it_refuses_one() {
    // This compiler migrates nothing, and that is the decision rather than an
    // omission: bringing a project up is a thing an author does once, to a file
    // they keep, and it belongs where the author is. What this end owes them is
    // a refusal they can act on: which version the file is, which version this
    // wants, and where to go. The word "unsupported" alone cannot tell an old
    // file from old tools from a file that was never a project.
    // And **which** way out depends on the direction. An author holding a file
    // from a newer Grandleon who is sent to `upgrade.mjs` is sent to a tool
    // that refuses them: going backwards can only be done by throwing something
    // away, and `upgrade.mjs` says so rather than doing it. Offering a route
    // that cannot work is worse than offering none, so each direction is pinned
    // separately here and a version this end cannot even order promises
    // nothing.
    struct Case final {
        std::string_view declared;
        std::string_view offers;
        std::string_view withholds;
    };
    for (const auto& item : {
             Case{"0.9.0", "upgrade.mjs", "Upgrade the tools"},
             Case{"2.0.0", "Upgrade the tools", "upgrade.mjs"},
             Case{"nonsense", "", "upgrade.mjs"},
         }) {
        const auto parsed = gc::parse_source_project_json(
            source_with(R"json("schemaVersion": "1.1.0")json",
                        std::string(R"json("schemaVersion": ")json") +
                            std::string(item.declared) + "\"")
        );
        expect(!parsed, "a project at another version does not compile");
        bool named = false;
        for (const auto& diagnostic : parsed.diagnostics) {
            if (diagnostic.path != "$.schemaVersion") continue;
            named =
                diagnostic.detail.find(item.declared) != std::string::npos &&
                diagnostic.detail.find(gc::supported_source_schema) !=
                    std::string::npos &&
                (item.offers.empty() ||
                 diagnostic.detail.find(item.offers) != std::string::npos) &&
                diagnostic.detail.find(item.withholds) == std::string::npos;
        }
        expect(
            named,
            std::string("the refusal of '") + std::string(item.declared) +
                "' must name it, name " +
                std::string(gc::supported_source_schema) +
                ", offer the way out that exists for its direction, and not "
                "offer the one that does not"
        );
    }
    expect(
        gc::supported_source_schema == "1.1.0",
        "the version this compiler reads; tools/source_schema/test.mjs holds "
        "it level with the schema and the migration registry"
    );
}

}  // namespace

int main() {
    names_both_versions_when_it_refuses_one();
    compiles_canonical_vertical_project();
    an_omitted_weapon_allowance_is_unrestricted();
    a_weapon_or_item_may_state_no_type();
    refuses_a_damage_type_the_vocabulary_does_not_hold();
    caps_a_cast_before_reading_it();
    reconciles_a_cast_against_no_lines_at_all();
    holds_a_package_identity_to_the_shape_the_schema_states();
    holds_a_board_to_the_size_the_schema_states();
    accepts_only_the_whitespace_json_names();
    reads_what_the_campaign_flow_states_about_itself();
    refuses_a_revision_it_cannot_represent();
    rejects_unsupported_gameplay();
    compiles_a_world_flag_predicate();
    rejects_malformed_json();
    maps_authored_resistance();
    refuses_a_stat_the_rules_cannot_execute();
    keeps_the_whole_range_for_the_chance_stats();
    maps_authored_accuracy();
    maps_authored_item_effects();
    maps_authored_growth();
    maps_authored_faction_colour();
    maps_authored_theme();
    maps_authored_character_style();
    maps_a_characters_own_style();
    maps_a_scene_backdrop();
    maps_a_scene_cast();
    maps_the_game_wide_turn_order();
    maps_the_character_loss_rule();
    maps_the_testing_invulnerability();
    maps_authored_traversal();
    agrees_with_the_art_library();
    caps_json_nesting_depth();
    return failures == 0 ? 0 : 1;
}
