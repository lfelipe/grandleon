// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
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

gc::GameSource valid_source() {
    gc::GameSource source;
    source.game_id[0] = 0x44U;
    source.title = "Two Teams";
    source.content_revision = 3;
    source.required_engine = {{0, 1, 0}, {0, 1, 99}};
    source.weapon_types = {
        {10, "Blade"},
        {11, "Bow"},
    };
    source.item_types = {
        {20, "Consumable"},
        {21, "Key item"},
    };
    // Both classes state their allowance, which is what makes the allowance a
    // rule they can be held to. A class stating none has unrestricted access.
    source.classes = {
        {30, "Vanguard", {25, 8, 5, 0, 4}, {10}, true},
        {31, "Ranger", {16, 6, 2, 0, 6}, {11}, true},
    };
    source.weapons = {
        {40, "Practice sword", 10, 3, 1, 1},
        {41, "Short bow", 11, 2, 2, 3},
    };
    source.items = {
        {50, "Tonic", 20, 10},
        {51, "Gate token", 21, 1},
    };
    source.unit_types = {
        {60, "Blue trainee", 30, 0, {40}, {50}},
        {61, "Red scout", 31, 0, {41}, {}},
    };
    return source;
}

pf::LoadResult load(const std::vector<std::uint8_t>& bytes) {
    return pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
}

bool has_diagnostic(
    const gc::CompileResult& result,
    gc::DiagnosticCode code,
    std::string_view path
) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [code, path](const gc::Diagnostic& diagnostic) {
            return diagnostic.code == code && diagnostic.path == path;
        }
    );
}

// Whether a code was reported at all, wherever. The negative form is the one
// that matters: a test that says "this fault is reported as that word" is only
// half a claim until the other word is known to be absent.
bool has_code(const gc::CompileResult& result, gc::DiagnosticCode code) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [code](const gc::Diagnostic& diagnostic) {
            return diagnostic.code == code;
        }
    );
}

void compiles_semantic_sections() {
    const auto compiled = gc::compile(valid_source());
    expect(static_cast<bool>(compiled), "valid semantic source compiles");
    expect(!compiled.package.empty(), "compiler produces package bytes");

    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "compiled semantic package loads");
    expect(
        loaded.package.sections.size() == 8,
        "manifest, presentation, and six definition sections load"
    );
    expect(
        loaded.package.find(pf::SectionType::classes, 31) != nullptr,
        "class is addressable by stable typed ID"
    );
    expect(
        loaded.package.find(pf::SectionType::weapon_types, 10) != nullptr,
        "weapon type is addressable"
    );
    expect(
        loaded.package.find(pf::SectionType::item_types, 20) != nullptr,
        "item type is addressable"
    );
    expect(
        loaded.package.find(pf::SectionType::unit_types, 60) != nullptr,
        "unit type is addressable"
    );
}

void output_is_canonical() {
    auto first = valid_source();
    auto second = first;
    std::reverse(second.weapon_types.begin(), second.weapon_types.end());
    std::reverse(second.item_types.begin(), second.item_types.end());
    std::reverse(second.classes.begin(), second.classes.end());
    std::reverse(second.weapons.begin(), second.weapons.end());
    std::reverse(second.items.begin(), second.items.end());
    std::reverse(second.unit_types.begin(), second.unit_types.end());
    expect(
        gc::compile(first).package == gc::compile(second).package,
        "authoring order does not affect compiled bytes"
    );
}

void rejects_missing_references_atomically() {
    auto source = valid_source();
    source.unit_types.front().class_id = 999;
    const auto compiled = gc::compile(source);
    expect(!static_cast<bool>(compiled), "missing reference fails compilation");
    expect(compiled.package.empty(), "failed compilation exposes no package");
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::missing_reference,
            "unit_types[60].class_id"
        ),
        "missing class diagnostic identifies semantic path"
    );
}

void rejects_duplicate_ids_and_references() {
    auto source = valid_source();
    source.weapons.push_back(source.weapons.front());
    source.classes.front().allowed_weapon_types.push_back(10);
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::duplicate_id,
            "weapons[40].id"
        ),
        "duplicate ID is diagnosed in its typed namespace"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::duplicate_reference,
            "classes[30].allowed_weapon_types"
        ),
        "duplicate relationship is diagnosed"
    );
}

void rejects_invalid_values() {
    auto source = valid_source();
    source.weapons.front().minimum_range = 3;
    source.weapons.front().maximum_range = 1;
    source.classes.front().base_stats.health = 0;
    source.classes.back().base_stats.resistance = -1;
    source.items.front().name.clear();
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_range,
            "weapons[40].range"
        ),
        "invalid weapon range is diagnosed"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_stat,
            "classes[30].base_stats"
        ),
        "invalid unit stats are diagnosed"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_stat,
            "classes[31].base_stats"
        ),
        "negative resistance is diagnosed"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::missing_name,
            "items[50].name"
        ),
        "missing display name is diagnosed"
    );
}

void rejects_an_accuracy_that_is_not_a_percentage() {
    // A chance is a whole percentage. Above a hundred the roll would read as
    // certainty and below zero as impossibility, neither of which is what the
    // author wrote, so both are refused where an author can still see them.
    auto source = valid_source();
    source.weapons.front().accuracy = 120;
    source.abilities = {{70, "Wild swing", gc::AbilityKind::damage,
                         gc::DamageType::physical, gc::AreaShape::single, 6, 1,
                         1, 0, 200}};
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_range,
            "weapons[40].accuracy"
        ),
        "an accuracy above a hundred is diagnosed on the weapon"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_range,
            "abilities[70].accuracy"
        ),
        "and on the ability"
    );
}

void rejects_an_item_effect_the_rules_cannot_read() {
    // Every field an item authors is either read or refused. A restoring item
    // with no power restores nothing, which is an item authored to be wasted;
    // a power on an item that restores nothing is a number no rule will ever
    // look at. Accepting either would let a number be authored, stored and
    // silently never read, so both halves are refused here.
    auto restores_nothing = valid_source();
    restores_nothing.items.front().kind = gc::ItemKind::restore;
    restores_nothing.items.front().power = 0;
    expect(
        has_diagnostic(
            gc::compile(restores_nothing),
            gc::DiagnosticCode::invalid_range,
            "items[50].power"
        ),
        "a restoring item that restores nothing is diagnosed on the item"
    );

    auto unread_power = valid_source();
    unread_power.items.front().power = 4;
    expect(
        has_diagnostic(
            gc::compile(unread_power),
            gc::DiagnosticCode::invalid_range,
            "items[50].power"
        ),
        "and a power no effect would read is refused rather than shipped"
    );
}

void an_authored_item_effect_reaches_the_package() {
    // The vertical slice: what an author writes on an item is what the record
    // carries, appended after the stack limit so a package written before
    // items could be spent still reads as items nothing can do anything with.
    auto source = valid_source();
    source.items.front().kind = gc::ItemKind::restore;
    source.items.front().power = 4;
    const auto compiled = gc::compile(source);
    expect(static_cast<bool>(compiled), "an item with an effect compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "and the package loads");
    const pf::RecordView* record =
        loaded.package.find(pf::SectionType::items, 50);
    expect(record != nullptr, "the draught has a record of its own");
    if (record == nullptr) return;
    const std::size_t offset = record->payload_offset;
    const std::size_t size = record->payload_size;
    // name (u16 length + bytes) + type id (u64) + stack limit (u16), then the
    // two appended bytes and the two after them.
    const std::size_t effect =
        offset + 2U + std::string_view("Tonic").size() + 8U + 2U;
    expect(
        size == effect - offset + 3U,
        "the record is exactly the fields it carries and no slack"
    );
    expect(
        loaded.package.bytes[effect] ==
            static_cast<std::uint8_t>(gc::ItemKind::restore),
        "the kind is the byte after the stack limit"
    );
    expect(
        loaded.package.bytes[effect + 1U] == 4U &&
            loaded.package.bytes[effect + 2U] == 0U,
        "and the power follows it, little-endian like every other number"
    );

    const auto plain = gc::compile(valid_source());
    const auto plain_loaded = load(plain.package);
    const pf::RecordView* keepsake =
        plain_loaded.package.find(pf::SectionType::items, 51);
    expect(keepsake != nullptr, "the key item has a record too");
    if (keepsake == nullptr) return;
    expect(
        plain_loaded.package
                .bytes[keepsake->payload_offset + keepsake->payload_size - 3U] ==
            static_cast<std::uint8_t>(gc::ItemKind::none),
        "and an item with no authored effect says so in the same byte"
    );
}

void rejects_a_growth_rate_that_is_not_a_percentage() {
    // Exactly the accuracy rule, on the other authored chance. The number an
    // author writes is the number the growth stream rolls against, so a chance
    // above a hundred is not enthusiasm, it is a rule the engine cannot express
    // and would silently read as certainty.
    auto source = valid_source();
    source.unit_types.front().growth.chance[2] = 140;
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_range,
            "unit_types[" + std::to_string(source.unit_types.front().id) +
                "].growth_rates"
        ),
        "a growth chance above a hundred is diagnosed on the unit type"
    );

    // And a level that costs nothing is a threshold no total ever crosses, and
    // a division with no answer.
    auto free_levels = valid_source();
    free_levels.unit_types.front().experience_per_level = 0;
    const auto without = gc::compile(free_levels);
    expect(
        has_diagnostic(
            without,
            gc::DiagnosticCode::invalid_range,
            "unit_types[" + std::to_string(free_levels.unit_types.front().id) +
                "].experience_per_level"
        ),
        "and a level that costs nothing is refused rather than divided by"
    );
}

void an_authored_growth_reaches_the_package() {
    // The vertical slice for the other authored chance: what an author writes
    // on a unit type is what the compiled record carries, in the order a
    // level-up rolls it, and a type that says nothing carries the defaults.
    auto source = valid_source();
    source.unit_types.front().experience_award = 45;
    source.unit_types.front().experience_per_level = 30;
    source.unit_types.front().growth.chance =
        {5, 15, 25, 35, 45, 55, 65, 75, 85, 95};
    const auto compiled = gc::compile(source);
    expect(compiled.diagnostics.empty(), "the growing package compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "the growing package loads");
    const pf::RecordView* record = loaded.package.find(
        pf::SectionType::unit_types, source.unit_types.front().id
    );
    expect(record != nullptr, "the package carries its unit types");
    if (record != nullptr) {
        // The growth block is fourteen bytes, appended after the ability list,
        // so a reader that stops earlier reads exactly the unit type it always
        // read. Ten of them are the chances, one per growable stat, and the
        // four the richer stat line added are at the end of that run rather
        // than inside it. The drop's nine bytes follow it, which is why the
        // block is found by counting back from the end of both tails.
        const std::uint32_t start =
            record->payload_offset + record->payload_size - 14U - 9U;
        const std::vector<std::uint8_t>& bytes = loaded.package.bytes;
        expect(
            bytes[start] == 45U && bytes[start + 1U] == 0U &&
                bytes[start + 2U] == 30U && bytes[start + 3U] == 0U,
            "the award and the level cost, little-endian and where they were "
            "written"
        );
        expect(
            bytes[start + 4U] == 5U && bytes[start + 5U] == 15U &&
                bytes[start + 6U] == 25U && bytes[start + 7U] == 35U &&
                bytes[start + 8U] == 45U && bytes[start + 9U] == 55U &&
                bytes[start + 10U] == 65U && bytes[start + 11U] == 75U &&
                bytes[start + 12U] == 85U && bytes[start + 13U] == 95U,
            "then one chance per stat in the one order a level-up rolls them"
        );
    }
}

void an_authored_drop_reaches_the_package() {
    auto source = valid_source();
    source.unit_types.front().drop_item = 50;
    source.unit_types.front().drop_chance = 60;
    const auto compiled = gc::compile(source);
    expect(compiled.diagnostics.empty(), "the dropping package compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "the dropping package loads");
    const pf::RecordView* record = loaded.package.find(
        pf::SectionType::unit_types, source.unit_types.front().id
    );
    expect(record != nullptr, "the package carries its unit types");
    if (record != nullptr) {
        // The drop is the last nine bytes of the record: eight for the item's
        // identity, little-endian like everything else, and one for the chance.
        const std::uint32_t start =
            record->payload_offset + record->payload_size - 9U;
        const std::vector<std::uint8_t>& bytes = loaded.package.bytes;
        expect(
            bytes[start] == 50U && bytes[start + 1U] == 0U &&
                bytes[start + 7U] == 0U && bytes[start + 8U] == 60U,
            "what falls and how often, where they were written"
        );
    }
    // A unit type that authors nothing still carries the tail, and it says
    // exactly what leaving nothing says.
    const pf::RecordView* silent = loaded.package.find(
        pf::SectionType::unit_types, source.unit_types.back().id
    );
    expect(silent != nullptr, "and the type that authored no drop too");
    if (silent != nullptr) {
        const std::uint32_t start =
            silent->payload_offset + silent->payload_size - 9U;
        const std::vector<std::uint8_t>& bytes = loaded.package.bytes;
        bool zeroed = true;
        for (std::uint32_t index = 0; index < 9U; ++index) {
            zeroed = zeroed && bytes[start + index] == 0U;
        }
        expect(zeroed, "nine zero bytes, which is what leaving nothing means");
    }
}

void a_drop_is_refused_in_both_directions() {
    auto unknown = valid_source();
    unknown.unit_types.front().drop_item = 999;
    unknown.unit_types.front().drop_chance = 60;
    expect(
        has_diagnostic(
            gc::compile(unknown),
            gc::DiagnosticCode::missing_reference,
            "unit_types[60].drop_item"
        ),
        "a drop naming an item nothing defines is refused where the author "
        "can see it, because the encounter deliberately does not resolve it"
    );

    auto no_chance = valid_source();
    no_chance.unit_types.front().drop_item = 50;
    expect(
        has_diagnostic(
            gc::compile(no_chance),
            gc::DiagnosticCode::incomplete_pair,
            "unit_types[60].drop_item"
        ),
        "something to leave with no chance of leaving it is refused"
    );

    auto nothing_to_leave = valid_source();
    nothing_to_leave.unit_types.front().drop_chance = 60;
    expect(
        has_diagnostic(
            gc::compile(nothing_to_leave),
            gc::DiagnosticCode::incomplete_pair,
            "unit_types[60].drop_chance"
        ),
        "and so is a chance with nothing to leave, each naming the half that "
        "is there"
    );

    auto impossible = valid_source();
    impossible.unit_types.front().drop_item = 50;
    impossible.unit_types.front().drop_chance = 140;
    expect(
        has_diagnostic(
            gc::compile(impossible),
            gc::DiagnosticCode::invalid_range,
            "unit_types[60].drop_chance"
        ),
        "a chance above a hundred is a rule the engine cannot express"
    );
}

void an_authored_accuracy_reaches_the_package() {
    // The whole vertical slice in one check: what an author writes on a weapon
    // and on a cast is what the compiled record carries, and a record that
    // says nothing means a hundred rather than zero.
    auto source = valid_source();
    source.weapons.front().accuracy = 90;
    source.abilities = {{70, "Wild swing", gc::AbilityKind::damage,
                         gc::DamageType::physical, gc::AreaShape::single, 6, 1,
                         1, 0, 85}};
    const auto compiled = gc::compile(source);
    expect(compiled.diagnostics.empty(), "the accurate package compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "the accurate package loads");
    const auto* weapons = loaded.package.find(pf::SectionType::weapons);
    expect(weapons != nullptr, "the package carries its weapons");
    if (weapons != nullptr) {
        // Accuracy is the last byte of a weapon record, appended after the
        // band, so a reader that stops earlier reads a weapon that always
        // lands.
        for (const pf::RecordView& record : weapons->records) {
            const std::uint32_t last_byte =
                record.payload_offset + record.payload_size - 1U;
            const std::uint8_t last = loaded.package.bytes[last_byte];
            const std::uint8_t expected = record.stable_id == 40U ? 90U : 100U;
            expect(
                last == expected,
                "the weapon record ends with the accuracy the author wrote"
            );
        }
    }
    const auto* abilities = loaded.package.find(pf::SectionType::abilities);
    expect(abilities != nullptr, "the package carries its abilities");
    if (abilities != nullptr && !abilities->records.empty()) {
        const pf::RecordView& record = abilities->records.front();
        const std::uint32_t last_byte =
            record.payload_offset + record.payload_size - 1U;
        expect(
            loaded.package.bytes[last_byte] == 85U,
            "and so does the ability record"
        );
    }
}

void requires_manifest_title() {
    auto source = valid_source();
    source.title.clear();
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::missing_name,
            "manifest.title"
        ),
        "game title is required by the manifest"
    );
    expect(compiled.package.empty(), "invalid manifest emits no package");
}

void validates_class_weapon_rules() {
    auto source = valid_source();
    source.unit_types.front().starting_weapons = {41};
    const auto compiled = gc::compile(source);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::disallowed_weapon,
            "unit_types[60].starting_weapons"
        ),
        "starting weapon must be permitted by the unit class"
    );
}

// The vocabulary the sample campaigns exercise: abilities on unit types, an
// encounter with placements, and a campaign with a conditional transition.
gc::GameSource campaign_source() {
    auto source = valid_source();
    source.abilities = {
        {70, "Spark", gc::AbilityKind::damage, gc::DamageType::magical,
         gc::AreaShape::single, 4, 1, 2, 0}
    };
    source.unit_types.front().abilities = {70};
    source.maps = {{75, "Field", 4, 3, std::vector<std::uint64_t>(12, 1)}};
    source.objectives = {
        {90, "Defeat", gc::ObjectiveKind::defeat_all_opponents}
    };
    source.encounters = {
        {
            100,
            "Clash",
            75,
            {90},
            {
                {1000, 2000, 2000, 60, gc::EncounterSide::first, 0, 1},
                {1001, 2001, 0, 61, gc::EncounterSide::second, 2, 1},
            }
        }
    };
    source.campaigns = {
        {
            110,
            "Line",
            111,
            {
                {111, gc::CampaignNodeKind::encounter, 100, {}, {112},
                 {{112, 0, gc::ConditionCombinator::all,
                   {{gc::CampaignPredicateKind::objective_result, 90,
                     gc::ObjectiveOutcome::satisfied}}}}},
                {112, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
            },
            {{2000, "Kestrel", 60, 0}},
        }
    };
    return source;
}

void validates_registries_and_campaign_references() {
    expect(
        static_cast<bool>(gc::compile(campaign_source())),
        "the campaign fixture compiles"
    );

    auto duplicated = campaign_source();
    duplicated.abilities.push_back(duplicated.abilities.front());
    expect(
        has_diagnostic(
            gc::compile(duplicated),
            gc::DiagnosticCode::duplicate_id,
            "abilities[70].id"
        ),
        "the ability registry checks identity like every other registry"
    );

    auto unknown_ability = campaign_source();
    unknown_ability.unit_types.front().abilities = {999};
    expect(
        has_diagnostic(
            gc::compile(unknown_ability),
            gc::DiagnosticCode::missing_reference,
            "unit_types[60].abilities"
        ),
        "a unit type cannot name an ability that does not exist"
    );

    auto broken_branch = campaign_source();
    auto& branch =
        broken_branch.campaigns.front().nodes.front().conditional_targets
            .front();
    branch.target_id = 999;
    branch.predicates.front().subject = 998;
    const auto compiled = gc::compile(broken_branch);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::missing_reference,
            "campaigns[110].nodes.transitions.target_id"
        ),
        "a conditional transition must name a real node"
    );
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::missing_reference,
            "campaigns[110].nodes.transitions.when.objective_id"
        ),
        "a transition predicate must name a real objective"
    );

    auto overlapping = campaign_source();
    overlapping.encounters.front().placements[1].x = 0;
    overlapping.encounters.front().placements[1].y = 1;
    expect(
        has_diagnostic(
            gc::compile(overlapping),
            gc::DiagnosticCode::invalid_placement,
            "encounters[100].placements"
        ),
        "two placements cannot share a tile"
    );
}

// The deployment region: what an author may say, and each of the four ways of
// saying it wrongly. The region belongs to the encounter, so every diagnostic
// lands on the encounter's own path.
void validates_the_deployment_region() {
    auto arranged = campaign_source();
    // The first side stands at (0,1); the region is its own tile and the two
    // beside it.
    arranged.encounters.front().deployment = {120, {{0, 0}, {0, 1}, {1, 1}}};
    expect(
        static_cast<bool>(gc::compile(arranged)),
        "an encounter may state the region its player arranges in"
    );

    auto nameless = arranged;
    nameless.encounters.front().deployment.id = 0;
    expect(
        has_diagnostic(
            gc::compile(nameless),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a region needs an identity of its own"
    );

    auto off_board = arranged;
    off_board.encounters.front().deployment.tiles.push_back({9, 9});
    expect(
        has_diagnostic(
            gc::compile(off_board),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a region tile must be on the encounter's map"
    );

    auto repeated = arranged;
    repeated.encounters.front().deployment.tiles.push_back({0, 1});
    expect(
        has_diagnostic(
            gc::compile(repeated),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "and must be named once"
    );

    auto unoccupied = arranged;
    unoccupied.encounters.front().deployment.tiles = {{3, 2}};
    expect(
        has_diagnostic(
            gc::compile(unoccupied),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a region no first-side placement stands in is one nobody arranges in"
    );

    // The region is offered whole to everybody it arranges, so ground one of
    // them could not stand on is refused here rather than becoming a tile that
    // lights up and then refuses.
    auto flooded = arranged;
    flooded.maps.front().terrain_kinds =
        std::vector<std::uint8_t>(12, gc::terrain_kind_index("grass"));
    flooded.maps.front().terrain_kinds[0] = gc::terrain_kind_index("water");
    expect(
        has_diagnostic(
            gc::compile(flooded),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a region tile a walker could not stand on is refused"
    );
}

// The tail is written only when there is a region, so an encounter that states
// none produces exactly the bytes it produced before regions existed.
void writes_the_region_as_a_tail() {
    const auto plain = gc::compile(campaign_source());
    auto arranged_source = campaign_source();
    arranged_source.encounters.front().deployment = {120, {{0, 1}, {1, 1}}};
    const auto arranged = gc::compile(arranged_source);
    expect(
        static_cast<bool>(plain) && static_cast<bool>(arranged),
        "both encounters compile"
    );
    const auto plain_loaded = load(plain.package);
    const auto arranged_loaded = load(arranged.package);
    const pf::RecordView* without =
        plain_loaded.package.find(pf::SectionType::encounters, 100);
    const pf::RecordView* with =
        arranged_loaded.package.find(pf::SectionType::encounters, 100);
    expect(
        without != nullptr && with != nullptr,
        "both packages carry the encounter"
    );
    if (without == nullptr || with == nullptr) return;
    expect(
        with->payload_size == without->payload_size + 8U + 2U + 8U,
        "the tail is an identity, a count, and two tiles, and nothing else"
    );
    for (std::uint32_t index = 0; index < without->payload_size; ++index) {
        expect(
            plain_loaded.package.bytes[without->payload_offset + index] ==
                arranged_loaded.package.bytes[with->payload_offset + index],
            "and everything before it is byte for byte what it was"
        );
    }
}

// The campaign fixture with a second rank: one more first-side placement and
// one more member to stand in it.
//
// A cap is refused at or above the number of first-side placements the board
// authors, so a one-placement board has no authorable capacity at all. Two
// placements is the smallest board on which a cap of one can bind, which makes
// this the smallest fixture the capacity has anything to say about.
gc::GameSource ranked_source() {
    auto source = campaign_source();
    source.campaigns.front().roster.push_back({2003, "Rook", 60, 0});
    source.encounters.front().placements.push_back(
        {1003, 2003, 2003, 60, gc::EncounterSide::first, 0, 2}
    );
    return source;
}

// What an author may put in the store, and each of the three ways of saying it
// wrongly. An unknown identity is refused by the reference check every other
// authored reference goes through; the two faults that are about the list
// itself are the grant's own diagnostic.
void validates_authored_grants() {
    auto stocked = ranked_source();
    stocked.campaigns.front().grants = {{50, 3, 0}, {51, 1, 0}};
    expect(
        static_cast<bool>(gc::compile(stocked)),
        "a campaign may state what its store is founded with"
    );

    auto unknown_item = stocked;
    unknown_item.campaigns.front().grants = {{999, 1, 0}};
    expect(
        has_diagnostic(
            gc::compile(unknown_item),
            gc::DiagnosticCode::missing_reference,
            "campaigns[110].starting_store.item_id"
        ),
        "a stock naming an item the project does not hold is refused where "
        "every other missing reference is"
    );

    auto nothing_of_it = stocked;
    nothing_of_it.campaigns.front().grants = {{50, 0, 0}};
    expect(
        has_diagnostic(
            gc::compile(nothing_of_it),
            gc::DiagnosticCode::invalid_grant,
            "campaigns[110].starting_store"
        ),
        "and a quantity of nothing is a grant that gives nothing"
    );

    auto said_twice = stocked;
    said_twice.campaigns.front().grants = {{50, 3, 0}, {50, 1, 0}};
    expect(
        has_diagnostic(
            gc::compile(said_twice),
            gc::DiagnosticCode::invalid_grant,
            "campaigns[110].starting_store"
        ),
        "one identity twice in one list is an author answering how many twice"
    );

    // Two nodes granting one item is two occasions and not one statement, so
    // the list a duplicate is judged within is one node's own.
    auto twice_over = stocked;
    twice_over.campaigns.front().grants = {{50, 1, 111}, {50, 1, 112}};
    expect(
        static_cast<bool>(gc::compile(twice_over)),
        "while the same item granted at two different nodes is two grants"
    );

    auto duplicated_on_one_node = stocked;
    duplicated_on_one_node.campaigns.front().grants = {
        {50, 1, 111}, {50, 2, 111}
    };
    expect(
        has_diagnostic(
            gc::compile(duplicated_on_one_node),
            gc::DiagnosticCode::invalid_grant,
            "campaigns[110].nodes.grants"
        ),
        "and twice on one node is refused exactly as twice in the stock is"
    );
}

// The capacity: what an author may cap, and the two ways of writing a cap that
// says nothing. Both land on the encounter's own path, beside every other
// deployment fault.
void validates_the_deployment_capacity() {
    auto capped = ranked_source();
    capped.encounters.front().deployment = {120, {}, 1};
    expect(
        static_cast<bool>(gc::compile(capped)),
        "an encounter may cap how many of a company take its field, without "
        "also saying where they stand"
    );

    auto says_nothing = ranked_source();
    says_nothing.encounters.front().deployment = {120, {}, 0};
    expect(
        has_diagnostic(
            gc::compile(says_nothing),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a deployment stating neither a region nor a cap is refused rather "
        "than read as a deployment that says nothing"
    );

    // Two first-side placements, so two is a cap no company could ever exceed
    // and three is further past the same edge. Neither is a permissive
    // setting; both are a knob that cannot turn.
    auto at_the_count = ranked_source();
    at_the_count.encounters.front().deployment = {120, {}, 2};
    expect(
        has_diagnostic(
            gc::compile(at_the_count),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "a cap at the board's own first-side placement count is refused"
    );

    auto above_the_count = ranked_source();
    above_the_count.encounters.front().deployment = {120, {}, 3};
    expect(
        has_diagnostic(
            gc::compile(above_the_count),
            gc::DiagnosticCode::invalid_deployment,
            "encounters[100].deployment"
        ),
        "and so is one above it"
    );
}

// One stat an author wrote a number over, stated as written: the delta and the
// flag that says it was written at all, which is the only thing that tells a
// stated zero from an omission.
void state_delta(
    gc::CampaignMemberSpecificity& specificity,
    gc::SpecificStat stat,
    std::int16_t delta
) {
    const auto index = static_cast<std::size_t>(stat);
    specificity.stat_deltas[index] = delta;
    specificity.stated[index] = true;
}

// The campaign fixture with its one founding member written to be more than
// their class: two more points of strength, one more of movement, and a step of
// reach on every weapon they swing.
//
// `campaign_source`'s member is a Vanguard, whose class line is 25 health, 8
// strength, 5 defense, no resistance and 4 movement, with the one action point
// and one speed a class that authors neither has. Every delta below is a number
// that could have been written on that class, which is the whole of what the
// bounds admit.
gc::GameSource specific_source() {
    auto source = campaign_source();
    gc::CampaignMember& member = source.campaigns.front().roster.front();
    member.states_specificity = true;
    state_delta(member.specificity, gc::SpecificStat::strength, 2);
    state_delta(member.specificity, gc::SpecificStat::movement, 1);
    member.specificity.reach_bonus = 1;
    return source;
}

// What an author may make of one character, and each of the three ways of
// saying it wrongly. The order the refusals are decided in matters more than
// any of them: a delta whose base cannot be determined is a bad reference and
// never a bad number.
void validates_authored_specificities() {
    expect(
        static_cast<bool>(gc::compile(specific_source())),
        "a member may be written to be more than their class"
    );

    // Stated and holding nothing. Distinct from omitting the object, which is
    // what every member who is exactly their unit type does.
    auto says_nothing = campaign_source();
    says_nothing.campaigns.front().roster.front().states_specificity = true;
    expect(
        has_diagnostic(
            gc::compile(says_nothing),
            gc::DiagnosticCode::invalid_specificity,
            "campaigns[110].roster.specificity"
        ),
        "a stated specificity with neither a delta nor a bonus in it is a "
        "claim that a character is specific without saying how"
    );

    // A number that changes nothing, beside a number that does, so the entry
    // is not refused for being empty.
    auto stated_zero = specific_source();
    state_delta(
        stated_zero.campaigns.front().roster.front().specificity,
        gc::SpecificStat::health,
        0
    );
    expect(
        has_diagnostic(
            gc::compile(stated_zero),
            gc::DiagnosticCode::invalid_specificity,
            "campaigns[110].roster.specificity.stats.health"
        ),
        "a stated zero is an author saying nothing with a number, and omitting "
        "the stat is how nothing is said"
    );

    // The Vanguard walks four squares, and a class may not walk none. The
    // delta that takes it to one is the last one that lands inside the bounds
    // its own class field admits, and the delta after it is refused.
    auto at_the_floor = specific_source();
    state_delta(
        at_the_floor.campaigns.front().roster.front().specificity,
        gc::SpecificStat::movement,
        -3
    );
    expect(
        static_cast<bool>(gc::compile(at_the_floor)),
        "a delta landing a stat on the floor its class field admits is a "
        "character an author could have written as a class"
    );

    auto through_the_floor = specific_source();
    state_delta(
        through_the_floor.campaigns.front().roster.front().specificity,
        gc::SpecificStat::movement,
        -4
    );
    expect(
        has_diagnostic(
            gc::compile(through_the_floor),
            gc::DiagnosticCode::invalid_specificity,
            "campaigns[110].roster.specificity.stats.movement"
        ),
        "and one square further is a character no class could be"
    );

    auto through_the_ceiling = specific_source();
    state_delta(
        through_the_ceiling.campaigns.front().roster.front().specificity,
        gc::SpecificStat::health,
        32767
    );
    expect(
        has_diagnostic(
            gc::compile(through_the_ceiling),
            gc::DiagnosticCode::invalid_specificity,
            "campaigns[110].roster.specificity.stats.health"
        ),
        "and the ceiling binds in the same direction the class field's does"
    );

    // The ordering rule. A delta that would be far out of bounds, on a member
    // whose base cannot be found: the answer is the unresolved reference and
    // nothing else, because telling an author to fix a number when the thing
    // to fix is a name sends them to the wrong file.
    auto unknown_type = specific_source();
    auto& adrift = unknown_type.campaigns.front().roster.front();
    adrift.unit_type_id = 999;
    state_delta(adrift.specificity, gc::SpecificStat::health, -1000);
    const auto no_type = gc::compile(unknown_type);
    expect(
        has_diagnostic(
            no_type,
            gc::DiagnosticCode::missing_reference,
            "campaigns[110].roster.unit_type_id"
        ),
        "a member whose unit type does not resolve reports the reference"
    );
    expect(
        !has_code(no_type, gc::DiagnosticCode::invalid_specificity),
        "and reports no delta problem, because a delta whose base cannot be "
        "determined is not a delta problem"
    );

    auto unknown_class = specific_source();
    unknown_class.unit_types.front().class_id = 999;
    state_delta(
        unknown_class.campaigns.front().roster.front().specificity,
        gc::SpecificStat::health,
        -1000
    );
    const auto no_class = gc::compile(unknown_class);
    expect(
        has_diagnostic(
            no_class,
            gc::DiagnosticCode::missing_reference,
            "unit_types[60].class_id"
        ),
        "and a member whose unit type's class does not resolve reports that "
        "reference"
    );
    expect(
        !has_code(no_class, gc::DiagnosticCode::invalid_specificity),
        "with no delta problem on top of it, for the same reason"
    );

    // The two refusals that are wrong on their own terms are decided before
    // any class is looked at, so a bad reference does not suppress them.
    auto empty_and_adrift = campaign_source();
    auto& claimed = empty_and_adrift.campaigns.front().roster.front();
    claimed.states_specificity = true;
    claimed.unit_type_id = 999;
    expect(
        has_diagnostic(
            gc::compile(empty_and_adrift),
            gc::DiagnosticCode::invalid_specificity,
            "campaigns[110].roster.specificity"
        ),
        "while a specificity that says nothing says nothing whatever the "
        "member's references point at"
    );
}

// What one authored specificity costs the record, byte by byte, and what one
// nobody authored costs it: nothing at all. That is the claim every golden in
// this repository rests on.
//
// The lengths are stated arithmetic rather than a comparison of the compiler
// against itself, because "the same as it is now" is a claim a regression
// satisfies.
// The zero-cost claim for the authored mark, made against the whole package
// rather than against a record: a project where nobody is talkable has no talks
// section at all, so the mark costs it not one byte.
void writes_a_talks_section_only_when_authored() {
    const auto plain = gc::compile(campaign_source());
    expect(static_cast<bool>(plain), "the talkless campaign compiles");

    auto talkative_source = campaign_source();
    talkative_source.encounters.front().placements.back().talk_flag_id = 4242;
    const auto talkative = gc::compile(talkative_source);
    expect(static_cast<bool>(talkative), "the talkable campaign compiles too");

    const auto plain_loaded = pf::load_mock_package(
        plain.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    const auto talkative_loaded = pf::load_mock_package(
        talkative.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    expect(
        static_cast<bool>(plain_loaded) && static_cast<bool>(talkative_loaded),
        "both packages load"
    );
    expect(
        plain_loaded.package.find(pf::SectionType::talks) == nullptr,
        "a project where nobody is talkable has no talks section at all"
    );
    expect(
        talkative_loaded.package.find(pf::SectionType::talks) != nullptr,
        "and one where somebody is has exactly one"
    );

    // The measurement that matters: the section is the *only* difference, and
    // it is pure growth. Every byte the talkless package had, the talkable one
    // still has, and the talkless one is smaller by exactly a section.
    expect(
        plain.package.size() < talkative.package.size(),
        "the mark costs bytes only where it is authored"
    );

    // And the same project compiled twice is the same bytes, so "unchanged"
    // means unchanged rather than merely the same size.
    const auto again = gc::compile(campaign_source());
    expect(
        again.package == plain.package,
        "a talkless project compiles to identical bytes every time"
    );

    // One record, naming one placement and one flag.
    const pf::RecordView* record = talkative_loaded.package.find(
        pf::SectionType::talks,
        talkative_source.encounters.front().id
    );
    expect(
        record != nullptr && record->payload_size == 2U + 8U + 8U,
        "the record carries a count and one placement-and-flag pair"
    );
}

void writes_a_specificity_tail_only_when_authored() {
    // The campaign record `campaign_source` compiled to before a member could
    // be anything but their unit type, counted out of the encoding:
    //
    //   name "Line"            2 + 4  = 6
    //   entry node                     8
    //   node count                     2
    //   node 111: id 8, kind 1, encounter 8, dialogue ids 2, unconditional
    //             ids 2 + 8, conditional count 2, one branch
    //             (target 8, priority 2, combinator 1, predicate count 2,
    //              one predicate 8 + 1)                      = 53
    //   node 112: id 8, kind 1, encounter 8, dialogue ids 2,
    //             unconditional ids 2, conditional count 2   = 23
    //   roster count                   2
    //   member 2000: id 8, name "Kestrel" 2 + 7, unit type 8,
    //                join node 8                             = 33
    //
    // and not one byte more: no grant tail and no specificity tail, because
    // this campaign authors neither.
    constexpr std::uint32_t flow_and_roster = 6U + 8U + 2U + 53U + 23U + 2U +
                                              33U;

    const auto plain = gc::compile(campaign_source());
    const auto specific = gc::compile(specific_source());
    auto granting_source = campaign_source();
    granting_source.campaigns.front().grants = {{50, 3, 0}};
    auto both_source = specific_source();
    both_source.campaigns.front().grants = {{50, 3, 0}};
    const auto granting = gc::compile(granting_source);
    const auto both = gc::compile(both_source);
    expect(
        static_cast<bool>(plain) && static_cast<bool>(specific) &&
            static_cast<bool>(granting) && static_cast<bool>(both),
        "all four campaigns compile"
    );
    const auto plain_loaded = load(plain.package);
    const auto specific_loaded = load(specific.package);
    const auto granting_loaded = load(granting.package);
    const auto both_loaded = load(both.package);
    const pf::RecordView* without =
        plain_loaded.package.find(pf::SectionType::campaigns, 110);
    const pf::RecordView* with =
        specific_loaded.package.find(pf::SectionType::campaigns, 110);
    const pf::RecordView* granted =
        granting_loaded.package.find(pf::SectionType::campaigns, 110);
    const pf::RecordView* granted_and_specific =
        both_loaded.package.find(pf::SectionType::campaigns, 110);
    expect(
        without != nullptr && with != nullptr && granted != nullptr &&
            granted_and_specific != nullptr,
        "all four packages carry the campaign"
    );
    if (without == nullptr || with == nullptr || granted == nullptr ||
        granted_and_specific == nullptr) {
        return;
    }
    expect(
        without->payload_size == flow_and_roster,
        "a campaign nobody authored a specificity in ends where it always "
        "ended, at its last member"
    );

    // A member authoring nothing is not the same source text as a member with
    // no specificity field, and it had better be the same bytes.
    auto explicitly_nothing = campaign_source();
    explicitly_nothing.campaigns.front().roster.front().specificity =
        gc::CampaignMemberSpecificity{};
    expect(
        gc::compile(explicitly_nothing).package == plain.package,
        "and an empty specificity carried through the compiler produces the "
        "package byte for byte"
    );

    // One entry: an eight-byte member identity, a delta count, three bytes per
    // delta (the stat's own index and a signed sixteen-bit number), and the
    // reach bonus. Two deltas, so 8 + 1 + 3 * 2 + 1 = 16.
    //
    // Before it, the tail's own two-byte count, and before that the grant
    // count of zero this campaign has to write to hold the positional tail's
    // place: 2 + 2 + 16 = 20.
    constexpr std::uint32_t entry = 8U + 1U + 3U * 2U + 1U;
    expect(
        with->payload_size == without->payload_size + 2U + 2U + entry &&
            with->payload_size == flow_and_roster + 20U,
        "one member authoring two deltas and a bonus costs a grant count of "
        "zero, a specificity count, and sixteen bytes of entry"
    );
    for (std::uint32_t index = 0; index < without->payload_size; ++index) {
        expect(
            plain_loaded.package.bytes[without->payload_offset + index] ==
                specific_loaded.package.bytes[with->payload_offset + index],
            "and everything before it is byte for byte what it was"
        );
    }

    // The tail itself, so the count above is a count of these bytes and not of
    // whatever the compiler happened to emit.
    const std::uint8_t expected_tail[20] = {
        // The grant count of zero that holds this tail's place.
        0x00U, 0x00U,
        // One specificity follows.
        0x01U, 0x00U,
        // Member 2000, little-endian, as every identity in the package is.
        0xd0U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        // Two deltas.
        0x02U,
        // Strength, which is `SpecificStat` index 1, by two.
        0x01U, 0x02U, 0x00U,
        // Movement, index 4, by one. It is written after strength because the
        // deltas go out in `SpecificStat` order and not in authoring order.
        0x04U, 0x01U, 0x00U,
        // And a step of reach.
        0x01U,
    };
    for (std::uint32_t index = 0; index < 20U; ++index) {
        expect(
            specific_loaded.package.bytes[with->payload_offset +
                                          flow_and_roster + index] ==
                expected_tail[index],
            "the tail says which member, which stats, by how much, and how "
            "much further they reach"
        );
    }

    // And with a grant already there, the specificity costs only its own count
    // and its own entry: the grant tail was going to be written anyway.
    expect(
        granted->payload_size == flow_and_roster + 2U + 20U,
        "a grant tail is a count and a twenty-byte grant"
    );
    expect(
        granted_and_specific->payload_size ==
            granted->payload_size + 2U + entry,
        "and a specificity beside a grant is a count and an entry, with no "
        "second place-holding count"
    );
    for (std::uint32_t index = 0; index < granted->payload_size; ++index) {
        expect(
            granting_loaded.package.bytes[granted->payload_offset + index] ==
                both_loaded.package
                    .bytes[granted_and_specific->payload_offset + index],
            "over a campaign otherwise byte for byte the one that grants the "
            "same item and authors nobody"
        );
    }
}

// The tail is written only when there is a grant, so a campaign that states
// none produces exactly the bytes it produced before grants existed.
void writes_a_grant_tail_only_when_granted() {
    const auto plain = gc::compile(campaign_source());
    auto granting_source = campaign_source();
    granting_source.campaigns.front().grants = {{50, 3, 0}};
    const auto granting = gc::compile(granting_source);
    expect(
        static_cast<bool>(plain) && static_cast<bool>(granting),
        "both campaigns compile"
    );
    const auto plain_loaded = load(plain.package);
    const auto granting_loaded = load(granting.package);
    const pf::RecordView* without =
        plain_loaded.package.find(pf::SectionType::campaigns, 110);
    const pf::RecordView* with =
        granting_loaded.package.find(pf::SectionType::campaigns, 110);
    expect(
        without != nullptr && with != nullptr,
        "both packages carry the campaign"
    );
    if (without == nullptr || with == nullptr) return;
    expect(
        with->payload_size == without->payload_size + 2U + 8U + 8U + 4U,
        "the tail is a count, a join node, an item and a quantity, and nothing "
        "else"
    );
    for (std::uint32_t index = 0; index < without->payload_size; ++index) {
        expect(
            plain_loaded.package.bytes[without->payload_offset + index] ==
                granting_loaded.package.bytes[with->payload_offset + index],
            "and everything before it is byte for byte what it was"
        );
    }
}

// The capacity is a further tail inside the deployment's own, written only when
// there is one. So an encounter that states a region and no cap produces
// exactly the bytes it produced before capacities existed, and one that states
// only a cap produces a zone, a tile count of zero, and the cap.
void writes_the_capacity_only_when_capped() {
    auto region_source = ranked_source();
    region_source.encounters.front().deployment = {120, {{0, 1}, {0, 2}}};
    auto capped_source = region_source;
    capped_source.encounters.front().deployment.capacity = 1;
    auto bare_source = ranked_source();
    auto capacity_only_source = ranked_source();
    capacity_only_source.encounters.front().deployment = {120, {}, 1};

    const auto region = gc::compile(region_source);
    const auto capped = gc::compile(capped_source);
    const auto bare = gc::compile(bare_source);
    const auto capacity_only = gc::compile(capacity_only_source);
    expect(
        static_cast<bool>(region) && static_cast<bool>(capped) &&
            static_cast<bool>(bare) && static_cast<bool>(capacity_only),
        "all four encounters compile"
    );
    const auto region_loaded = load(region.package);
    const auto capped_loaded = load(capped.package);
    const auto bare_loaded = load(bare.package);
    const auto capacity_only_loaded = load(capacity_only.package);
    const pf::RecordView* uncapped =
        region_loaded.package.find(pf::SectionType::encounters, 100);
    const pf::RecordView* with_cap =
        capped_loaded.package.find(pf::SectionType::encounters, 100);
    const pf::RecordView* without_deployment =
        bare_loaded.package.find(pf::SectionType::encounters, 100);
    const pf::RecordView* cap_alone =
        capacity_only_loaded.package.find(pf::SectionType::encounters, 100);
    expect(
        uncapped != nullptr && with_cap != nullptr &&
            without_deployment != nullptr && cap_alone != nullptr,
        "all four packages carry the encounter"
    );
    if (uncapped == nullptr || with_cap == nullptr ||
        without_deployment == nullptr || cap_alone == nullptr) {
        return;
    }
    expect(
        with_cap->payload_size == uncapped->payload_size + 2U,
        "a cap on a region is two further bytes and nothing else"
    );
    for (std::uint32_t index = 0; index < uncapped->payload_size; ++index) {
        expect(
            region_loaded.package.bytes[uncapped->payload_offset + index] ==
                capped_loaded.package.bytes[with_cap->payload_offset + index],
            "and everything before it — the region included — is byte for byte "
            "what an encounter written before capacities existed wrote"
        );
    }
    expect(
        cap_alone->payload_size == without_deployment->payload_size + 8U + 2U +
                                       2U,
        "a deployment that states only a cap writes a zone identity, a tile "
        "count of zero and the cap"
    );
    for (std::uint32_t index = 0; index < without_deployment->payload_size;
         ++index) {
        expect(
            bare_loaded.package
                    .bytes[without_deployment->payload_offset + index] ==
                capacity_only_loaded.package
                    .bytes[cap_alone->payload_offset + index],
            "over an encounter otherwise byte for byte the one that authors no "
            "deployment at all"
        );
    }
}

void supports_large_definition_sets() {
    auto source = valid_source();
    source.items.clear();
    source.unit_types.clear();
    for (std::uint64_t id = 1; id <= 2'000; ++id) {
        source.items.push_back(
            {id, "Item " + std::to_string(id), 20, 1}
        );
    }
    const auto compiled = gc::compile(source);
    expect(static_cast<bool>(compiled), "2,000 semantic items compile");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "large semantic package loads");
    expect(
        loaded.package.find(pf::SectionType::items, 1'999) != nullptr,
        "large semantic section remains indexed"
    );
}

// The rule that an unchosen colour follows the faction's position in the
// project's list lives in one function, because the package writer, the
// console, and the editor must not be able to disagree about it.
void resolves_faction_colour_from_position() {
    const gc::Faction chosen{1, "Amber Company", 4};
    expect(
        gc::resolved_faction_colour(chosen, 0) == 4,
        "a faction that chose a colour keeps it wherever it sits"
    );

    const gc::Faction unchosen{2, "Second Company"};
    expect(
        gc::resolved_faction_colour(unchosen, 0) == 0,
        "the first unchosen faction is the first colour on the menu"
    );
    expect(
        gc::resolved_faction_colour(unchosen, 1) == 1,
        "the second unchosen faction is the second colour on the menu"
    );
    expect(
        gc::resolved_faction_colour(unchosen, gc::faction_colour_count) == 0,
        "a faction past the end of the menu still resolves to a colour"
    );
}

// The archetype convention lives in one function for the same reason the
// colour fallback does: three clients held a copy of the roster, and a
// renderer that re-derives a rule is a renderer that can disagree.
void resolves_archetype_from_class_then_name() {
    gc::GameSource source;
    source.classes = {
        {30, "Royal Archer"},
        {31, "Vanguard"},
    };

    const gc::UnitType named_by_class{60, "Elyse", 30};
    expect(
        gc::resolved_archetype(source, named_by_class) ==
            gc::archetype_index("archer"),
        "a class that spells an archetype chooses it"
    );

    // The class spells none, so the unit type's own name is asked next.
    const gc::UnitType named_by_itself{61, "Storm Mage Kel", 31};
    expect(
        gc::resolved_archetype(source, named_by_itself) ==
            gc::archetype_index("mage"),
        "a unit type spells its own archetype when its class does not"
    );

    const gc::UnitType named_by_neither{62, "Elyse", 31};
    expect(
        gc::resolved_archetype(source, named_by_neither) ==
            gc::archetype_default,
        "a unit type spelling no archetype wears the roster's first"
    );

    // A unit type whose class the project does not define still resolves,
    // because a package that failed to compile is not the fallback here.
    const gc::UnitType classless{63, "Wandering Rogue", 999};
    expect(
        gc::resolved_archetype(source, classless) ==
            gc::archetype_index("rogue"),
        "a unit type naming no defined class falls back to its own name"
    );

    expect(
        gc::archetype_index("STORMCALLER") ==
            gc::archetype_index("stormcaller"),
        "the convention is case-insensitive"
    );
    expect(
        gc::archetype_index("quartermaster") == gc::archetype_unnamed,
        "a name spelling no archetype names none"
    );
    // The roster order settles a name that spells two, and it is the order
    // every client searches in.
    expect(
        gc::archetype_index("knight archer") == gc::archetype_index("knight"),
        "the roster's order settles a name that spells two archetypes"
    );
}

// The package carries one terrain kind per identity, so a project in which one
// identity draws two ways would compile into a join that is true of one map
// and false of another. Parsing cannot produce it, which is exactly why it is
// checked rather than assumed.
void rejects_one_terrain_identity_drawn_two_ways() {
    auto source = valid_source();
    gc::Map consistent;
    consistent.id = 70;
    consistent.name = "Ford";
    consistent.width = 2;
    consistent.height = 1;
    consistent.terrain = {900, 901};
    consistent.terrain_kinds = {
        gc::terrain_kind_index("water"), gc::terrain_kind_index("grass")
    };
    source.maps = {consistent};
    expect(
        static_cast<bool>(gc::compile(source)),
        "one kind per identity compiles"
    );

    gc::Map disagreeing;
    disagreeing.id = 71;
    disagreeing.name = "Ridge";
    disagreeing.width = 1;
    disagreeing.height = 1;
    // The same identity as the first map's opening cell, drawn as something
    // else.
    disagreeing.terrain = {900};
    disagreeing.terrain_kinds = {gc::terrain_kind_index("mountain")};
    source.maps = {consistent, disagreeing};

    const auto compiled = gc::compile(source);
    expect(
        !static_cast<bool>(compiled),
        "one identity drawn two ways does not compile"
    );
    expect(
        has_diagnostic(compiled, gc::DiagnosticCode::invalid_map,
                       "maps[71].terrain[0]"),
        "the disagreeing cell is named"
    );
    expect(
        compiled.package.empty(),
        "a project with a contradictory terrain join writes no package"
    );
}


void writes_the_survive_count_only_for_the_kind_that_reads_it() {
    const auto plain = gc::compile(campaign_source());
    expect(static_cast<bool>(plain), "the countless campaign compiles");

    auto holding_source = campaign_source();
    holding_source.objectives.front().kind = gc::ObjectiveKind::survive_rounds;
    holding_source.objectives.front().rounds = 7;
    const auto holding = gc::compile(holding_source);
    expect(static_cast<bool>(holding), "the survive campaign compiles too");

    const auto plain_loaded = pf::load_mock_package(
        plain.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    const auto holding_loaded = pf::load_mock_package(
        holding.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    const pf::RecordView* without =
        plain_loaded.package.find(pf::SectionType::objectives, 90U);
    const pf::RecordView* with =
        holding_loaded.package.find(pf::SectionType::objectives, 90U);
    expect(
        without != nullptr && with != nullptr,
        "both packages carry the objective"
    );
    expect(
        with->payload_size == without->payload_size + 2U,
        "the count is two further bytes and nothing else"
    );
    // And nothing ahead of it moved. The record is a name, a kind, a side and a
    // target identity, so everything up to the kind byte, ten from the end, is
    // the record it always was, byte for byte.
    const std::uint8_t* before =
        plain_loaded.package.bytes.data() + without->payload_offset;
    const std::uint8_t* after =
        holding_loaded.package.bytes.data() + with->payload_offset;
    for (std::uint32_t index = 0; index + 10U < without->payload_size;
         ++index) {
        expect(
            before[index] == after[index],
            "the count is pure growth at the tail"
        );
    }

    const auto again = gc::compile(campaign_source());
    expect(
        again.package == plain.package,
        "and a countless project compiles to identical bytes every time"
    );
}

void a_survive_count_is_authored_with_its_kind_or_not_at_all() {
    auto missing = campaign_source();
    missing.objectives.front().kind = gc::ObjectiveKind::survive_rounds;
    expect(
        !static_cast<bool>(gc::compile(missing)),
        "a survive objective with no count is refused"
    );
    auto spurious = campaign_source();
    spurious.objectives.front().rounds = 3;
    expect(
        !static_cast<bool>(gc::compile(spurious)),
        "and so is a count on a kind that could never read one"
    );
}

void writes_an_arrivals_section_only_when_authored() {
    const auto plain = gc::compile(campaign_source());
    expect(static_cast<bool>(plain), "the waveless campaign compiles");

    auto waved_source = campaign_source();
    waved_source.encounters.front().placements.back().arrival_round = 3;
    waved_source.encounters.front().placements.back().arrival_every = 3;
    waved_source.encounters.front().placements.back().arrival_times = 4;
    const auto waved = gc::compile(waved_source);
    expect(static_cast<bool>(waved), "the campaign with a wave compiles too");

    const auto plain_loaded = pf::load_mock_package(
        plain.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    const auto waved_loaded = pf::load_mock_package(
        waved.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 1024}
    );
    expect(
        static_cast<bool>(plain_loaded) && static_cast<bool>(waved_loaded),
        "both packages load"
    );
    expect(
        plain_loaded.package.find(pf::SectionType::arrivals) == nullptr,
        "a project where nothing arrives has no arrivals section at all"
    );
    expect(
        waved_loaded.package.find(pf::SectionType::arrivals) != nullptr,
        "and one where something does has exactly one"
    );

    // The encounter record itself did not move a byte. The arrivals travel
    // beside it rather than after its deployment tail, which is the whole
    // reason they are a section of their own.
    const pf::RecordView* without =
        plain_loaded.package.find(pf::SectionType::encounters, 100U);
    const pf::RecordView* with =
        waved_loaded.package.find(pf::SectionType::encounters, 100U);
    expect(
        without != nullptr && with != nullptr &&
            without->payload_size == with->payload_size,
        "the encounter record is the record it always was"
    );

    // One record: a count, then one placement's identity, round, gap and
    // number of arrivals.
    const pf::RecordView* record =
        waved_loaded.package.find(pf::SectionType::arrivals, 100U);
    expect(
        record != nullptr && record->payload_size == 2U + 8U + 2U + 2U + 2U,
        "the arrivals record is a count and one placement's four numbers"
    );
    expect(
        plain.package.size() < waved.package.size(),
        "a wave costs bytes only where it is authored"
    );
}

void a_wave_is_judged_like_a_placement_but_not_against_one() {
    auto stacked = campaign_source();
    stacked.encounters.front().placements.back().x =
        stacked.encounters.front().placements.front().x;
    stacked.encounters.front().placements.back().y =
        stacked.encounters.front().placements.front().y;
    expect(
        !static_cast<bool>(gc::compile(stacked)),
        "two placements on one tile are still refused"
    );

    auto arriving = stacked;
    arriving.encounters.front().placements.back().arrival_round = 4;
    expect(
        static_cast<bool>(gc::compile(arriving)),
        "but a wave may be authored onto a tile somebody starts on"
    );

    auto opening = campaign_source();
    opening.encounters.front().placements.back().arrival_round = 1;
    expect(
        !static_cast<bool>(gc::compile(opening)),
        "arriving in the round the battle opens in is refused"
    );

    auto half = campaign_source();
    half.encounters.front().placements.back().arrival_round = 3;
    half.encounters.front().placements.back().arrival_every = 3;
    expect(
        !static_cast<bool>(gc::compile(half)),
        "and so is a gap between arrivals with no number of them"
    );

    // A roster member cannot be a wave: the campaign layer has no state for
    // one of your own who is neither fielded nor withheld.
    auto member = campaign_source();
    member.encounters.front().placements.front().arrival_round = 4;
    expect(
        !static_cast<bool>(gc::compile(member)),
        "and a placement that fields a roster member cannot arrive at all"
    );
}

// A scene, so that the dialogue registry is exercised by the same rules every
// other registry is. `campaign_source` gives it a node to be presented from.
gc::GameSource dialogue_source() {
    auto source = campaign_source();
    source.dialogues = {
        {
            130,
            "Before the gate",
            {{"Kestrel", "Hold the line.", 1}},
            {{"Kestrel", 60}},
        }
    };
    source.campaigns.front().nodes.front().dialogue_ids = {130};
    return source;
}

// The registry that had no identity check at all. A duplicate identity here
// produced a package holding two records under one stable id, which
// `grandleon_package_check` then refuses as `duplicate_record`. That is the
// compiler emitting something no reader will accept, the one thing its README
// promises it never does.
void validates_dialogue_identity() {
    expect(
        static_cast<bool>(gc::compile(dialogue_source())),
        "the dialogue fixture compiles"
    );

    auto duplicated = dialogue_source();
    duplicated.dialogues.push_back(duplicated.dialogues.front());
    expect(
        has_diagnostic(
            gc::compile(duplicated),
            gc::DiagnosticCode::duplicate_id,
            "dialogues[130].id"
        ),
        "two scenes cannot share one identity"
    );

    auto nameless = dialogue_source();
    nameless.dialogues.front().name.clear();
    expect(
        has_diagnostic(
            gc::compile(nameless),
            gc::DiagnosticCode::missing_name,
            "dialogues[130].name"
        ),
        "a scene needs a name like every other definition"
    );

    auto anonymous = dialogue_source();
    anonymous.dialogues.front().id = 0;
    expect(
        has_diagnostic(
            gc::compile(anonymous),
            gc::DiagnosticCode::missing_id,
            "dialogues[0].id"
        ),
        "a scene needs an identity like every other definition"
    );
}

// Every string a record carries is written behind a `uint16` length and every
// list behind a `uint16` or `uint8` count. Exceeding either wraps rather than
// fails, and the record that comes back declares a length nothing wrote.
// Without a bound at the compiler, a campaign whose member is named seventy
// thousand characters would compile, pass `grandleon_package_check`, and only
// then be refused by the runtime as `graph_rejected`, with no diagnostic
// anywhere in between.
void bounds_every_width_the_package_writes() {
    const std::string too_long(70'000, 'M');

    auto named = dialogue_source();
    named.campaigns.front().roster.front().name = too_long;
    expect(
        has_diagnostic(
            gc::compile(named),
            gc::DiagnosticCode::name_too_long,
            "campaigns[110].roster.name"
        ),
        "a member's name is bounded by the width it is written in"
    );

    auto spoken = dialogue_source();
    spoken.dialogues.front().lines.front().text = too_long;
    expect(
        has_diagnostic(
            gc::compile(spoken),
            gc::DiagnosticCode::name_too_long,
            "dialogues[130].lines[0].text"
        ),
        "a line's words are bounded, and they pass no identity check"
    );

    auto attributed = dialogue_source();
    attributed.dialogues.front().lines.front().speaker = too_long;
    expect(
        has_diagnostic(
            gc::compile(attributed),
            gc::DiagnosticCode::name_too_long,
            "dialogues[130].lines[0].speaker"
        ),
        "and so is the name a line puts them in the mouth of"
    );

    auto crowded = dialogue_source();
    crowded.dialogues.front().cast.assign(
        256, gc::DialogueCastEntry{"Kestrel", 60}
    );
    expect(
        has_diagnostic(
            gc::compile(crowded),
            gc::DiagnosticCode::invalid_range,
            "dialogues[130].cast"
        ),
        "a cast is counted in one byte, so 256 of them is refused"
    );
}

// A node presents its scenes when it is entered. One it cannot find is not a
// scene played badly: it is a scene that never plays, and the campaign walks on
// as though the author had written nothing.
void validates_node_dialogue_references() {
    auto dangling = dialogue_source();
    dangling.campaigns.front().nodes.front().dialogue_ids = {999};
    expect(
        has_diagnostic(
            gc::compile(dangling),
            gc::DiagnosticCode::missing_reference,
            "campaigns[110].nodes.dialogue_ids"
        ),
        "a node cannot present a scene the project does not hold"
    );

    auto twice = dialogue_source();
    twice.campaigns.front().nodes.front().dialogue_ids = {130, 130};
    expect(
        has_diagnostic(
            gc::compile(twice),
            gc::DiagnosticCode::duplicate_reference,
            "campaigns[110].nodes.dialogue_ids"
        ),
        "and cannot name one twice, as with every other reference list"
    );
}

// Where a campaign goes next must not depend on the order its transitions
// happen to be written in. Two ways of breaking that: a shared priority, which
// the runtime cannot even see because it stable-sorts, and a second
// conditionless fallback, which the runtime refuses outright with
// `unsupported_flow` after this compiler has already written the package.
void refuses_transitions_that_leave_the_route_to_authoring_order() {
    auto tied = campaign_source();
    auto& targets = tied.campaigns.front().nodes.front().conditional_targets;
    targets.push_back(targets.front());
    expect(
        has_diagnostic(
            gc::compile(tied),
            gc::DiagnosticCode::invalid_transition,
            "campaigns[110].nodes.transitions.priority"
        ),
        "two transitions cannot share one priority"
    );

    auto separated = tied;
    separated.campaigns.front()
        .nodes.front()
        .conditional_targets.back()
        .priority = 1;
    expect(
        !has_code(
            gc::compile(separated), gc::DiagnosticCode::invalid_transition
        ),
        "two transitions with priorities of their own are fine"
    );

    auto forked = campaign_source();
    forked.campaigns.front().nodes.front().unconditional_targets = {112, 112};
    const auto compiled = gc::compile(forked);
    expect(
        has_diagnostic(
            compiled,
            gc::DiagnosticCode::invalid_transition,
            "campaigns[110].nodes.targets"
        ),
        "a node cannot hold two conditionless fallbacks"
    );

    auto single = campaign_source();
    single.campaigns.front().nodes.front().unconditional_targets = {112};
    expect(
        !has_code(
            gc::compile(single), gc::DiagnosticCode::invalid_transition
        ),
        "one is exactly what the runtime accepts"
    );
}

// An objective about a character has to be about a character standing on the
// board that names the objective. `encounter_loader` resolves the target
// against that encounter's placements and refuses the whole encounter when it
// cannot, so this is a battle no client could load rather than one played
// badly.
void checks_an_objective_target_against_the_board() {
    auto targeted = campaign_source();
    targeted.objectives = {
        {90, "Cut down the captain", gc::ObjectiveKind::defeat_target, {}, 2001}
    };
    expect(
        static_cast<bool>(gc::compile(targeted)),
        "an objective may name a placement the board fields"
    );

    auto elsewhere = targeted;
    elsewhere.objectives.front().target_placement_id = 4444;
    expect(
        has_diagnostic(
            gc::compile(elsewhere),
            gc::DiagnosticCode::missing_reference,
            "encounters[100].objective_ids.target_placement_id"
        ),
        "and may not name one nothing places"
    );

    // The key matched is the placement's source key, which for a placement
    // fielding a roster member is the member's own: the character, not the
    // tile. The first-side placement here fields member 2000, so that is what
    // an objective about them names.
    auto member = targeted;
    member.objectives.front().target_placement_id = 2000;
    expect(
        static_cast<bool>(gc::compile(member)),
        "an objective about a fielded member names the member"
    );
}

// A board has to say how it is won or lost, because the runtime will not open
// one that does not.
//
// This is the shape of a real report. Two Stages of an authored campaign were
// built without a win condition; the package compiled, a ROM was built from it,
// and the campaign stopped dead at the second Stage with the console redrawing
// its company screen. The loader reads the objective count first and refuses a
// payload declaring none, so what shipped was a board nothing could open.
void refuses_a_board_that_nothing_decides() {
    auto undecided = campaign_source();
    undecided.encounters.front().objective_ids.clear();
    expect(
        has_diagnostic(
            gc::compile(undecided),
            gc::DiagnosticCode::undecided_encounter,
            "encounters[100].objective_ids"
        ),
        "an encounter naming no objective is refused here"
    );

    // And the same board with one is emitted, so the refusal is about the
    // absence rather than about anything else on the board.
    expect(
        static_cast<bool>(gc::compile(campaign_source())),
        "and a board that says how it is won compiles"
    );
}

// A class that says nothing about weapon types permits every one of them, which
// is what `SOURCE_FORMAT.md` says an omitted `allowedWeaponTypeIds` means. A
// class that states an empty one has said something, and is held to it.
void an_unstated_weapon_allowance_permits_every_weapon() {
    auto unrestricted = valid_source();
    unrestricted.classes.front().states_allowed_weapon_types = false;
    unrestricted.classes.front().allowed_weapon_types.clear();
    unrestricted.unit_types.front().starting_weapons = {41};
    expect(
        !has_code(
            gc::compile(unrestricted), gc::DiagnosticCode::disallowed_weapon
        ),
        "a class that states no allowance restricts nothing"
    );

    auto forbids_everything = unrestricted;
    forbids_everything.classes.front().states_allowed_weapon_types = true;
    expect(
        has_diagnostic(
            gc::compile(forbids_everything),
            gc::DiagnosticCode::disallowed_weapon,
            "unit_types[60].starting_weapons"
        ),
        "a class that states an empty allowance permits no weapon type"
    );
}

// A weapon or an item may name no type. That is legacy unclassified content
// rather than a broken reference, which is what the schema and
// `SOURCE_FORMAT.md` both say, and it must not be reported as one.
void a_weapon_or_item_may_name_no_type() {
    auto untyped = valid_source();
    untyped.classes.front().states_allowed_weapon_types = false;
    untyped.classes.back().states_allowed_weapon_types = false;
    for (gc::Weapon& weapon : untyped.weapons) weapon.type_id = 0;
    for (gc::Item& item : untyped.items) item.type_id = 0;
    expect(
        static_cast<bool>(gc::compile(untyped)),
        "unclassified weapons and items are not broken references"
    );

    auto wrong = untyped;
    wrong.weapons.front().type_id = 999;
    expect(
        has_diagnostic(
            gc::compile(wrong),
            gc::DiagnosticCode::missing_reference,
            "weapons[40].type_id"
        ),
        "but a type it does name still has to exist"
    );
}

}  // namespace

int main() {
    compiles_semantic_sections();
    validates_dialogue_identity();
    bounds_every_width_the_package_writes();
    validates_node_dialogue_references();
    refuses_transitions_that_leave_the_route_to_authoring_order();
    checks_an_objective_target_against_the_board();
    refuses_a_board_that_nothing_decides();
    an_unstated_weapon_allowance_permits_every_weapon();
    a_weapon_or_item_may_name_no_type();
    resolves_faction_colour_from_position();
    resolves_archetype_from_class_then_name();
    rejects_one_terrain_identity_drawn_two_ways();
    output_is_canonical();
    rejects_missing_references_atomically();
    rejects_duplicate_ids_and_references();
    rejects_invalid_values();
    rejects_an_accuracy_that_is_not_a_percentage();
    an_authored_accuracy_reaches_the_package();
    rejects_an_item_effect_the_rules_cannot_read();
    an_authored_item_effect_reaches_the_package();
    rejects_a_growth_rate_that_is_not_a_percentage();
    an_authored_growth_reaches_the_package();
    an_authored_drop_reaches_the_package();
    a_drop_is_refused_in_both_directions();
    requires_manifest_title();
    validates_class_weapon_rules();
    validates_registries_and_campaign_references();
    validates_the_deployment_region();
    validates_the_deployment_capacity();
    validates_authored_grants();
    validates_authored_specificities();
    writes_the_region_as_a_tail();
    writes_the_capacity_only_when_capped();
    writes_a_grant_tail_only_when_granted();
    writes_a_specificity_tail_only_when_authored();
    writes_a_talks_section_only_when_authored();
    writes_the_survive_count_only_for_the_kind_that_reads_it();
    a_survive_count_is_authored_with_its_kind_or_not_at_all();
    writes_an_arrivals_section_only_when_authored();
    a_wave_is_judged_like_a_placement_but_not_against_one();
    supports_large_definition_sets();
    return failures == 0 ? 0 : 1;
}
