// SPDX-License-Identifier: MIT
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/progression.hpp>
#include <grandleon/package_runtime/starting_kit.hpp>
#include <grandleon/core/content_identity.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

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
    value.game_id[0] = 0x47U;
    value.title = "Runtime slice";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {
        // Blue authors resistance; Red leaves it defaulted, covering both the
        // carried and the omitted spellings in one package.
        {30, "Blue class", {6, 4, 1, 2, 3}, {10}},
        {31, "Red class", {4, 3, 1, 0, 3}, {10}},
    };
    // Blue carries two weapons and Red one, so the same fixture covers both
    // the carried list and the unchanged single-weapon path.
    // The sword says nothing about accuracy and the bow says ninety, so one
    // package covers both the omitted spelling and the authored one.
    value.weapons = {
        {40, "Sword", 10, 3, 1, 1}, {41, "Bow", 10, 2, 2, 3, 90}
    };
    // The draught restores four; the token authors no effect at all, so one
    // package covers both the spelling a rule reads and the spelling that
    // means an item nothing can spend.
    value.items = {
        {50, "Tonic", 20, 5, gc::ItemKind::restore, 4},
        {51, "Gate token", 20, 1},
    };
    gc::UnitType blue{60, "Blue unit", 30, 80, {40, 41}, {50, 51}};
    blue.experience_per_level = 40;
    blue.growth.chance = {95, 80, 65, 50, 35, 20};
    // What it leaves behind, on top of the block above, so the record carries
    // both appended tails and the two decoders have to agree about the length
    // of the whole thing.
    blue.drop_item = 51;
    blue.drop_chance = 40;
    value.unit_types = {
        blue,
        // Authors nothing about growing, which is what every unit type written
        // before growth existed authors.
        {61, "Red unit", 31, 81, {40}, {}},
    };
    value.maps = {{70, "Field", 4, 3, std::vector<std::uint64_t>(12, 1)}};
    value.factions = {{80, "Blue"}, {81, "Red"}};
    value.objectives = {
        {90, "Defeat the opponent",
         gc::ObjectiveKind::defeat_all_opponents}
    };
    value.encounters = {
        {
            100,
            "First clash",
            70,
            {90},
            {
                {1000, 2000, 2000, 60, gc::EncounterSide::first, 0, 1},
                {1001, 2001, 0, 61, gc::EncounterSide::second, 2, 1},
            }
        }
    };
    value.campaigns = {
        {
            110,
            "Demo",
            111,
            {
                // id, kind, encounter, dialogue, unconditional, conditional.
                {111, gc::CampaignNodeKind::encounter, 100, {}, {112}, {}},
                {112, gc::CampaignNodeKind::terminal, 0, {}, {}, {}},
            },
            {{2000, "Kestrel", 60, 0}},
        }
    };
    return value;
}

pf::LoadedPackage compile_and_load() {
    const auto compiled = gc::compile(source());
    expect(static_cast<bool>(compiled), "runtime fixture compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "runtime fixture package loads");
    return loaded.package;
}

void decodes_compiled_encounter() {
    const auto loaded = compile_and_load();
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(static_cast<bool>(decoded), "encounter payload decodes");
    expect(
        decoded.definition.width == 4 &&
            decoded.definition.height == 3 &&
            decoded.definition.units.size() == 2,
        "map and placements are published"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().health == 6 &&
            decoded.definition.units.front().strength == 4,
        "unit runtime stats are resolved through its class"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().power == 3 &&
            decoded.definition.units.back().power == 3,
        "equipped weapon power is carried into the definition"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().resistance == 2 &&
            decoded.definition.units.back().resistance == 0,
        "authored resistance is carried and omitted resistance stays zero"
    );
}

// Every weapon identity a unit type lists survives into the definition rather
// than only the first, the registry resolves each of them once, and the first
// is still what the unit's own band describes.
void carries_every_weapon_a_unit_type_lists() {
    const auto loaded = compile_and_load();
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(static_cast<bool>(decoded), "multi-weapon encounter decodes");
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().weapon_ids ==
                std::vector<sim::ContentId>{40, 41},
        "both carried weapons reach the definition, in carried order"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.back().weapon_ids ==
                std::vector<sim::ContentId>{40},
        "a unit carrying one weapon carries exactly that one"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().minimum_reach == 1 &&
            decoded.definition.units.front().maximum_reach == 1 &&
            decoded.definition.units.front().power == 3,
        "the first carried weapon is still the weapon in hand"
    );
    // Both units carry the sword, and the registry holds it once.
    expect(
        decoded.definition.weapons.size() == 2,
        "the weapon registry names each carried identity once"
    );
    const auto bow = std::find_if(
        decoded.definition.weapons.begin(),
        decoded.definition.weapons.end(),
        [](const sim::WeaponDefinition& weapon) {
            return weapon.id == 41;
        }
    );
    expect(
        bow != decoded.definition.weapons.end() && bow->power == 2 &&
            bow->minimum_reach == 2 && bow->maximum_reach == 3,
        "a carried weapon's power and band are decoded from its record"
    );

    // The engine accepts the definition and the carried list survives a
    // round trip through the snapshot.
    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "the multi-weapon encounter is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto carrier = std::find_if(
        snapshot.units.begin(),
        snapshot.units.end(),
        [](const sim::UnitSnapshot& unit) { return unit.id == 1000; }
    );
    expect(
        carrier != snapshot.units.end() &&
            carrier->weapon_ids ==
                std::vector<sim::ContentId>{40, 41},
        "the snapshot reports the weapons the unit carries"
    );
}

void carries_authored_accuracy_into_the_rules() {
    const auto loaded = compile_and_load();
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(static_cast<bool>(decoded), "the accurate encounter decodes");
    const auto bow = std::find_if(
        decoded.definition.weapons.begin(),
        decoded.definition.weapons.end(),
        [](const sim::WeaponDefinition& weapon) { return weapon.id == 41; }
    );
    const auto sword = std::find_if(
        decoded.definition.weapons.begin(),
        decoded.definition.weapons.end(),
        [](const sim::WeaponDefinition& weapon) { return weapon.id == 40; }
    );
    expect(
        bow != decoded.definition.weapons.end() && bow->accuracy == 90,
        "an authored accuracy survives the package"
    );
    expect(
        sword != decoded.definition.weapons.end() && sword->accuracy == 100,
        "and a weapon that says nothing always lands"
    );
    // The weapon in hand is the first carried, and its accuracy is resolved
    // onto the unit exactly as its power and band are. That is the sword for
    // both units here, so both of them always land with what they are holding.
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().accuracy == 100 &&
            decoded.definition.units.back().accuracy == 100,
        "the unit takes the accuracy of the weapon in hand"
    );
}

// The growth block, which the board never reads and the campaign layer does.
//
// Two claims here, and the second one is why the block is appended rather than
// inserted: an authored block comes back exactly as authored, and a unit type
// that authored nothing reads as the documented defaults instead of failing.
void carries_authored_growth_beside_the_board() {
    const auto loaded = compile_and_load();

    const pr::UnitProgressionLoad blue = pr::load_unit_progression(loaded, 60);
    expect(static_cast<bool>(blue), "an authored growth block decodes");
    expect(
        blue.progression.experience_per_level == 40U &&
            blue.progression.growth ==
                std::array<std::uint8_t, pr::growable_stat_count>{
                    95, 80, 65, 50, 35, 20
                },
        "with the numbers the author wrote, in the order a level-up rolls them"
    );
    expect(
        blue.progression.experience_award == 0U && blue.progression.grows(),
        "a type worth nothing to defeat can still be one that grows"
    );

    const pr::UnitProgressionLoad red = pr::load_unit_progression(loaded, 61);
    expect(
        static_cast<bool>(red) && red.progression.experience_award == 0U &&
            red.progression.experience_per_level ==
                pr::default_experience_per_level &&
            !red.progression.grows(),
        "and a unit type that authored nothing reads as the defaults: worth "
        "nothing, a hundred a level, and growing nothing"
    );

    expect(
        !pr::load_unit_progression(loaded, 999),
        "while a unit type the package does not carry is not a unit type"
    );

    // And the board is untouched by any of it. The encounter loader skips this
    // block, so a package that authors growth loads exactly the units it would
    // have loaded without it.
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(
        static_cast<bool>(decoded) && decoded.definition.units.size() == 2U,
        "the board a package with growth produces is the board it always was"
    );
}

// The same record, read for the other caller that has no board: a campaign
// putting a member's starting kit in their hands the day they join. The
// encounter loader reads this list into a battle pack; this decoder reads it
// with no encounter in sight, which is the whole reason it exists.
void reads_a_unit_types_starting_items_without_a_board() {
    const auto loaded = compile_and_load();

    const pr::UnitStartingItemsLoad blue =
        pr::load_unit_starting_items(loaded, 60);
    expect(static_cast<bool>(blue), "an authored item list decodes");
    expect(
        blue.items == std::vector<std::uint64_t>{50, 51},
        "in the order the author wrote it, which is the order a satchel is "
        "built in"
    );

    const pr::UnitStartingItemsLoad red =
        pr::load_unit_starting_items(loaded, 61);
    expect(
        static_cast<bool>(red) && red.items.empty(),
        "and a unit type that lists nothing is a successful read of nothing "
        "rather than a failure"
    );

    expect(
        !pr::load_unit_starting_items(loaded, 999),
        "while a unit type the package does not carry is not a unit type"
    );

    // The two decoders of one record agree, which is the property that matters:
    // what the board carries is what the campaign would hand out.
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(
        static_cast<bool>(decoded) && !decoded.definition.units.empty() &&
            decoded.definition.units.front().item_ids ==
                std::vector<sim::ContentId>{50, 51},
        "the encounter loader and the campaign's decoder read the same list "
        "out of the same record"
    );
}

// The other appended tail, which the board *does* read: what a unit type leaves
// behind when it falls. It sits after the growth block, so a record carrying
// both proves the two decoders agree about the record's whole length.
void carries_an_authored_drop_onto_the_board() {
    const auto loaded = compile_and_load();
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(static_cast<bool>(decoded), "the dropping package's board loads");
    bool checked_dropper = false;
    bool checked_silent = false;
    for (const auto& unit : decoded.definition.units) {
        if (unit.unit_type_id == 60U) {
            expect(
                unit.drop_item_id == 51U && unit.drop_chance == 40U,
                "the drop reaches the board exactly as it was authored"
            );
            checked_dropper = true;
        }
        if (unit.unit_type_id == 61U) {
            expect(
                unit.drop_item_id == 0U && unit.drop_chance == 0U,
                "and a unit type that authored none leaves nothing"
            );
            checked_silent = true;
        }
    }
    expect(
        checked_dropper && checked_silent,
        "both unit types were on the board to check"
    );
    // The dropped identity is not registered among the items the encounter
    // defines unless somebody carries it, because a drop is recorded and handed
    // to nobody. Item 51 is carried here, so what this pins is the reverse: the
    // registry is what the *pack* asked for and never what a drop asked for.
    const pr::UnitProgressionLoad blue = pr::load_unit_progression(loaded, 60);
    expect(
        static_cast<bool>(blue),
        "and the campaign-side decoder still reads past the drop tail"
    );
}

void compiled_package_reaches_outcome() {
    auto decoded = pr::load_encounter(compile_and_load(), 100);
    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "decoded encounter starts");
    const sim::Command commands[] = {
        {sim::CommandType::move, 1000, {1, 1}, 0},
        {sim::CommandType::wait, 1001, {}, 0},
        {sim::CommandType::attack, 1000, {}, 1001},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(created.encounter.apply(command)),
            "reference command is accepted"
        );
    }
    // Strength 4 plus sword power 3 minus defense 1 fells the 4-health scout
    // in one blow: the authored weapon is part of the outcome, not decoration.
    expect(
        created.encounter.snapshot().outcome ==
            sim::Outcome::first_side_won,
        "compiled package reaches declared defeat-all result"
    );
}

// Rewrites a loaded package with one record's payload cut short, which is how
// this file poses "a package written before the field existed" without keeping
// a stale binary around to rot. Everything else is copied through unchanged:
// every section, every other record, every identity. The writer recomputes the
// envelope so the result is a package a loader has no reason to distrust.
std::vector<std::uint8_t> package_with_record_shortened(
    const pf::LoadedPackage& whole,
    pf::SectionType section_type,
    std::uint64_t stable_id,
    std::uint32_t bytes_removed
) {
    pf::PackageSource rebuilt;
    rebuilt.game_id = whole.game_id;
    rebuilt.content_revision = whole.content_revision;
    rebuilt.required_engine = whole.required_engine;
    rebuilt.target = whole.target;
    rebuilt.required_features = whole.required_features;
    for (const pf::SectionView& section : whole.sections) {
        pf::SectionSource out;
        out.type = section.type;
        out.schema_major = section.schema_major;
        out.schema_minor = section.schema_minor;
        out.flags = section.flags;
        for (const pf::RecordView& record : section.records) {
            std::uint32_t size = record.payload_size;
            if (section.type == section_type && record.stable_id == stable_id) {
                size = size > bytes_removed ? size - bytes_removed : 0U;
            }
            const auto begin = whole.bytes.begin() +
                               static_cast<std::ptrdiff_t>(record.payload_offset);
            out.records.push_back(
                {record.stable_id,
                 {begin, begin + static_cast<std::ptrdiff_t>(size)}}
            );
        }
        rebuilt.sections.push_back(std::move(out));
    }
    return pf::write_mock_package(rebuilt);
}

// The four stats that decide whether a blow lands, and what a cast is worth in
// the caster's hands, over the same vertical path: authored on a class,
// compiled into the tail of its record, and read back out as unit state the
// simulation prices a strike with.
//
// The second half is the compatibility claim, and it is the reason the tail is
// a tail. A class record written before these stats existed ends after the
// crossings byte, and shortening one here is exactly that record: the loader
// must read it as a class with all four at zero rather than refuse it, because
// absence and a zero mean the same thing and neither needs a version.
// A unit type's item list is decoded into the definition rather than into a
// local that is thrown away, which would leave a character holding a potion
// nothing could drink. Every identity survives, the registry resolves each of
// them once, and what an author wrote on the item is what the rules read.
void carries_every_item_a_unit_type_lists() {
    const auto loaded = compile_and_load();
    const auto decoded = pr::load_encounter(loaded, 100);
    expect(static_cast<bool>(decoded), "the satchel encounter decodes");
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().item_ids ==
                std::vector<sim::ContentId>{50, 51},
        "both carried items reach the definition, in carried order"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.back().item_ids.empty(),
        "and a unit type carrying nothing carries nothing"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().item_counts.empty(),
        "the loader states no counts, so create_encounter decides what one is"
    );
    expect(
        decoded.definition.items.size() == 2,
        "the item registry names each carried identity once"
    );
    const auto tonic = std::find_if(
        decoded.definition.items.begin(),
        decoded.definition.items.end(),
        [](const sim::ItemDefinition& item) { return item.id == 50; }
    );
    expect(
        tonic != decoded.definition.items.end() &&
            tonic->kind == sim::ItemKind::restore && tonic->power == 4,
        "an authored restoring item is decoded from its record"
    );
    const auto token = std::find_if(
        decoded.definition.items.begin(),
        decoded.definition.items.end(),
        [](const sim::ItemDefinition& item) { return item.id == 51; }
    );
    expect(
        token != decoded.definition.items.end() &&
            token->kind == sim::ItemKind::none && token->power == 0,
        "and one with no authored effect is decoded as doing nothing"
    );

    // The engine accepts the definition, and what the loader left unstated is
    // filled in exactly once, in the one place that decides it.
    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "the satchel encounter is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto carrier = std::find_if(
        snapshot.units.begin(),
        snapshot.units.end(),
        [](const sim::UnitSnapshot& unit) { return unit.id == 1000; }
    );
    expect(
        carrier != snapshot.units.end() &&
            carrier->item_ids == std::vector<sim::ContentId>{50, 51} &&
            carrier->item_counts == std::vector<std::uint16_t>{1, 1},
        "and the satchel survives into the snapshot, one of each"
    );
}

void carries_the_richer_stat_line() {
    auto authored = source();
    authored.classes.front().base_stats.skill = 5;
    authored.classes.front().base_stats.luck = 2;
    authored.classes.front().base_stats.evasion = 4;
    authored.classes.front().base_stats.magic = 7;

    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "the richer stat fixture compiles");
    const pf::LoadOptions options{
        {0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000
    };
    const auto loaded = pf::load_mock_package(compiled.package, options);
    expect(static_cast<bool>(loaded), "the richer stat package loads");
    const auto decoded = pr::load_encounter(loaded.package, 100);
    expect(static_cast<bool>(decoded), "the richer stat encounter decodes");
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().skill == 5 &&
            decoded.definition.units.front().luck == 2 &&
            decoded.definition.units.front().evasion == 4 &&
            decoded.definition.units.front().magic == 7,
        "an authored class arrives on the board with all four stats"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.back().skill == 0 &&
            decoded.definition.units.back().luck == 0 &&
            decoded.definition.units.back().evasion == 0 &&
            decoded.definition.units.back().magic == 0,
        "and a class that says nothing about them carries zeros"
    );

    // The same content with the eight appended bytes gone from the first class
    // record, which is what a package written before the stat line grew holds.
    const std::vector<std::uint8_t> older_bytes = package_with_record_shortened(
        loaded.package, pf::SectionType::classes,
        authored.classes.front().id, 8U
    );
    const auto older = pf::load_mock_package(older_bytes, options);
    expect(
        static_cast<bool>(older),
        "a package whose class record ends before the tail still loads"
    );
    const auto legacy = pr::load_encounter(older.package, 100);
    expect(
        static_cast<bool>(legacy),
        "and its encounter decodes rather than being refused"
    );
    expect(
        legacy.definition.units.size() == 2 &&
            legacy.definition.units.front().skill == 0 &&
            legacy.definition.units.front().luck == 0 &&
            legacy.definition.units.front().evasion == 0 &&
            legacy.definition.units.front().magic == 0,
        "as a class with all four at zero, which is what absence means"
    );
    expect(
        legacy.definition.units.size() == 2 &&
            legacy.definition.units.front().strength ==
                decoded.definition.units.front().strength &&
            legacy.definition.units.front().defense ==
                decoded.definition.units.front().defense &&
            legacy.definition.units.front().crossings ==
                decoded.definition.units.front().crossings,
        "and with everything before the tail read exactly as it always was"
    );
}

// The whole vertical path for what the ground allows and what a character
// crosses: authored names in, compiled package in the middle, and rules the
// engine can apply out the far side.
void carries_passability_and_traversal() {
    auto authored = source();
    // A river down the middle column and a mountain in the corner, named the
    // way an author would name them.
    authored.maps = {
        {70, "Ford", 4, 3,
         {1, 2, 3, 4, 1, 2, 3, 5, 1, 2, 3, 4},
         {
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("mountain"),
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("swamp"),
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("mountain"),
         }}
    };
    // Blue flies; Red says nothing and therefore walks.
    authored.classes.front().crossings = gc::crossing_every;

    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "the passability fixture compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "the passability package loads");
    const auto decoded = pr::load_encounter(loaded.package, 100);
    expect(static_cast<bool>(decoded), "the passability encounter decodes");

    const auto& terrain = decoded.definition.terrain;
    expect(terrain.size() == 12, "every cell says what it asks");
    expect(
        terrain.size() == 12 && terrain[0] == sim::Terrain::open &&
            terrain[1] == sim::Terrain::water &&
            terrain[2] == sim::Terrain::open &&
            terrain[3] == sim::Terrain::heights,
        "a river is water, a road is open, and a mountain is a climb"
    );
    expect(
        terrain.size() == 12 && terrain[7] == sim::Terrain::open,
        "a swamp is open ground, and slow is what the price beside it says"
    );

    // The price, on the same vertical path. Two questions, two answers: the
    // swamp takes anyone and charges them double, and the road takes anyone and
    // charges a step.
    const auto& price = decoded.definition.movement_cost;
    expect(price.size() == 12, "every cell says what it charges");
    expect(
        price.size() == 12 && price[7] == 2U && price[6] == 1U &&
            price[0] == 1U && price[1] == 1U,
        "a swamp costs two, and road, grass and water cost one"
    );
    expect(
        decoded.definition.units.size() == 2 &&
            decoded.definition.units.front().crossings == sim::crossing_every &&
            decoded.definition.units.back().crossings == sim::crossing_none,
        "an authored flier flies and a class that says nothing walks"
    );

    // And the rules the engine then applies are the authored ones.
    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "the passability encounter is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto lists = [](const std::vector<sim::Position>& tiles,
                          std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };
    expect(
        lists(sim::reachable_tiles(snapshot, 1000), 1, 1),
        "the flier stands over the river"
    );
    expect(
        !lists(sim::reachable_tiles(snapshot, 1001), 1, 1),
        "the walker does not"
    );
}

// A package written before ground had a price still means what it meant. Its
// map record ends after the passability block, and this is the shape the reader
// has to keep reading as "every step costs one" for as long as such a package
// can be opened.
void a_map_with_no_price_reads_as_one_step_a_cell() {
    auto authored = source();
    authored.maps = {
        {70, "Ford", 4, 3,
         {1, 2, 3, 4, 1, 2, 3, 5, 1, 2, 3, 4},
         {
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("mountain"),
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("swamp"),
             gc::terrain_kind_index("grass"), gc::terrain_kind_index("river"),
             gc::terrain_kind_index("road"), gc::terrain_kind_index("mountain"),
         }}
    };
    authored.classes.front().crossings = gc::crossing_every;
    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "the priced fixture compiles");
    const pf::LoadOptions options{
        {0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000
    };
    const auto loaded = pf::load_mock_package(compiled.package, options);
    expect(static_cast<bool>(loaded), "the priced package loads");
    const auto decoded = pr::load_encounter(loaded.package, 100);
    expect(static_cast<bool>(decoded), "the priced encounter decodes");
    expect(
        decoded.definition.movement_cost.size() == 12,
        "with a price for every cell"
    );

    // The same content with the twelve appended bytes gone from the map record,
    // which is what a package written before ground had a price holds: the
    // passability block ends the record.
    const std::vector<std::uint8_t> older_bytes = package_with_record_shortened(
        loaded.package, pf::SectionType::maps, 70, 12U
    );
    const auto older = pf::load_mock_package(older_bytes, options);
    expect(
        static_cast<bool>(older),
        "a package whose map record ends before the price still loads"
    );
    const auto legacy = pr::load_encounter(older.package, 100);
    expect(
        static_cast<bool>(legacy),
        "and its encounter decodes rather than being refused"
    );
    expect(
        legacy.definition.movement_cost.empty(),
        "as a board with no price on it, which is what absence means"
    );
    expect(
        legacy.definition.terrain == decoded.definition.terrain,
        "and with the passability before it read exactly as it always was"
    );

    // And it plays as it always played. A board with no price and the same
    // board priced at one everywhere are one battle, which is the promise that
    // makes an absent list safe.
    auto flat = legacy.definition;
    flat.movement_cost.assign(12U, 1U);
    auto without = sim::create_encounter(legacy.definition);
    auto with_ones = sim::create_encounter(flat);
    expect(
        static_cast<bool>(without) && static_cast<bool>(with_ones),
        "both boards are valid"
    );
    expect(
        without.encounter.canonical_hash() ==
            with_ones.encounter.canonical_hash(),
        "they are one battle, down to the canonical hash"
    );
    expect(
        sim::reachable_tiles(without.encounter.snapshot(), 1001) ==
            sim::reachable_tiles(with_ones.encounter.snapshot(), 1001),
        "and offer the walker the same tiles"
    );
    // While the priced board, which charges two for its swamp, does not.
    auto charged = sim::create_encounter(decoded.definition);
    expect(
        static_cast<bool>(charged) &&
            charged.encounter.canonical_hash() !=
                without.encounter.canonical_hash(),
        "and the priced board is a different battle from both"
    );
}

// A character standing where its class could never walk is an authoring
// mistake, and the compiler names the placement rather than leaving the
// engine to refuse the whole encounter.
void refuses_a_placement_on_ground_it_cannot_enter() {
    auto authored = source();
    authored.maps = {
        {70, "Ford", 4, 3,
         std::vector<std::uint64_t>(12, 1),
         std::vector<std::uint8_t>(12, gc::terrain_kind_index("river"))}
    };
    const auto compiled = gc::compile(authored);
    expect(
        !static_cast<bool>(compiled),
        "a board of water refuses the walkers standing in it"
    );
    expect(
        std::any_of(
            compiled.diagnostics.begin(),
            compiled.diagnostics.end(),
            [](const gc::Diagnostic& diagnostic) {
                return diagnostic.code ==
                           gc::DiagnosticCode::invalid_placement &&
                       diagnostic.path == "encounters[100].placements";
            }
        ),
        "the placement is named"
    );
    expect(
        compiled.package.empty(),
        "a project whose characters cannot stand up writes no package"
    );

    // The same board with wings on both classes is a flight over water.
    authored.classes[0].crossings = gc::crossing_every;
    authored.classes[1].crossings = gc::crossing_every;
    expect(
        static_cast<bool>(gc::compile(authored)),
        "the same board compiles for characters that can be there"
    );
}

// The region round-trips through the package, and an encounter that authors
// none decodes as one with no phase. A record written before regions existed
// decodes as that too, because they are the same bytes.
void carries_the_deployment_region_onto_the_board() {
    const auto plain = pr::load_encounter(compile_and_load(), 100);
    expect(
        static_cast<bool>(plain) &&
            plain.definition.deployment_tiles.empty() &&
            plain.deployment_zone_id == 0,
        "an encounter that authors no region decodes as one with no phase"
    );
    auto created = sim::create_encounter(plain.definition);
    expect(
        static_cast<bool>(created) && !created.encounter.snapshot().deploying,
        "and the battle it makes opens on the first activation"
    );

    auto authored = source();
    authored.encounters.front().deployment = {120, {{0, 1}, {0, 2}, {1, 1}}};
    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "a region compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    const auto decoded = pr::load_encounter(loaded.package, 100);
    expect(
        static_cast<bool>(decoded) &&
            decoded.definition.deployment_tiles.size() == 3 &&
            decoded.deployment_zone_id == 120,
        "and decodes into the tiles and the identity the author wrote"
    );
    auto arranged = sim::create_encounter(decoded.definition);
    expect(
        static_cast<bool>(arranged) &&
            arranged.encounter.snapshot().deploying,
        "the battle it makes opens in the deployment phase"
    );
    const auto opening = arranged.encounter.snapshot();
    const sim::UnitSnapshot* blue = nullptr;
    for (const sim::UnitSnapshot& value : opening.units) {
        if (value.side == sim::Side::first) blue = &value;
    }
    expect(
        blue != nullptr && sim::is_deployable(opening, *blue),
        "the first-side character standing inside it is the one arranged"
    );
    if (blue != nullptr) {
        expect(
            sim::deployable_tiles(opening, blue->id).size() == 3,
            "and is offered every tile of the region"
        );
    }
}

// The runtime fixture with a second rank: one more first-side placement, and
// one more authored member to stand in it.
//
// A cap is refused at or above the number of first-side placements the board
// authors, so a one-placement board has no authorable capacity at all. Two
// placements is the smallest board a cap of one can bind on, and every test
// below that needs a capacity needs this.
gc::GameSource ranked_source() {
    auto value = source();
    value.campaigns.front().roster.push_back({2003, "Rook", 60, 0});
    value.encounters.front().placements.push_back(
        {1003, 2003, 2003, 60, gc::EncounterSide::first, 0, 2}
    );
    return value;
}

pf::LoadedPackage compile_and_load(const gc::GameSource& authored) {
    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "the authored fixture compiles");
    for (const gc::Diagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "compiler diagnostic: "
                  << gc::diagnostic_name(diagnostic.code) << ' '
                  << diagnostic.path << '\n';
    }
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and its package loads");
    return loaded.package;
}

// One record's payload, copied out of a loaded package so a test can say what
// a byte of it would have to be for the decoder to refuse it.
std::vector<std::uint8_t> record_payload(
    const pf::LoadedPackage& whole,
    pf::SectionType section_type,
    std::uint64_t stable_id
) {
    const pf::RecordView* const record = whole.find(section_type, stable_id);
    expect(record != nullptr, "the package carries the record");
    if (record == nullptr) return {};
    const auto begin =
        whole.bytes.begin() +
        static_cast<std::ptrdiff_t>(record->payload_offset);
    return {begin, begin + static_cast<std::ptrdiff_t>(record->payload_size)};
}

// The same rewrite `package_with_record_shortened` performs, with a payload
// the caller supplies rather than a truncation: a byte pattern no compiler
// would emit, posed to the decoder inside a package that is otherwise beyond
// suspicion.
std::vector<std::uint8_t> package_with_record_replaced(
    const pf::LoadedPackage& whole,
    pf::SectionType section_type,
    std::uint64_t stable_id,
    const std::vector<std::uint8_t>& payload
) {
    pf::PackageSource rebuilt;
    rebuilt.game_id = whole.game_id;
    rebuilt.content_revision = whole.content_revision;
    rebuilt.required_engine = whole.required_engine;
    rebuilt.target = whole.target;
    rebuilt.required_features = whole.required_features;
    for (const pf::SectionView& section : whole.sections) {
        pf::SectionSource out;
        out.type = section.type;
        out.schema_major = section.schema_major;
        out.schema_minor = section.schema_minor;
        out.flags = section.flags;
        for (const pf::RecordView& record : section.records) {
            if (section.type == section_type && record.stable_id == stable_id) {
                out.records.push_back({record.stable_id, payload});
                continue;
            }
            const auto begin =
                whole.bytes.begin() +
                static_cast<std::ptrdiff_t>(record.payload_offset);
            const auto end =
                begin + static_cast<std::ptrdiff_t>(record.payload_size);
            out.records.push_back({record.stable_id, {begin, end}});
        }
        rebuilt.sections.push_back(std::move(out));
    }
    return pf::write_mock_package(rebuilt);
}

// The capacity round-trips beside the board rather than inside it, and an
// encounter that authors none decodes as one with no cap. A record written
// before capacities existed decodes as that too, because a record that ends
// with its tiles is that record.
// Who can be talked to, out of a section of its own rather than out of the
// encounter record. That is what lets a package where nobody is talkable be
// the package it always was.
void carries_what_is_said_during_a_battle() {
    // The ordinary case first, and the one that matters most: a project that
    // authors no moment decodes through the same path with nothing to say,
    // having had no section to read. That is what keeps every package written
    // before moments existed byte for byte the package it was.
    const pf::LoadedPackage silent = compile_and_load(ranked_source());
    const auto quiet = pr::load_encounter(silent, 100);
    expect(static_cast<bool>(quiet), "a battle nobody speaks during loads");
    expect(
        silent.find(pf::SectionType::moments) == nullptr,
        "and its package has no moments section at all"
    );
    expect(quiet.moments.empty(), "so the board carries no words");

    // Then the authored case, one moment of each occasion a battle has.
    auto authored = ranked_source();
    const auto about = authored.encounters.front().placements.back().id;
    authored.encounters.front().moments = {
        {11U, gc::MomentTrigger::stage_opens, 0U, 101U},
        {12U, gc::MomentTrigger::character_talked, about, 102U},
        {13U, gc::MomentTrigger::character_falls, about, 103U},
    };
    const pf::LoadedPackage spoken = compile_and_load(authored);
    expect(
        spoken.find(pf::SectionType::moments) != nullptr,
        "an authored moment gives the package a moments section"
    );
    const auto decoded = pr::load_encounter(spoken, 100);
    expect(static_cast<bool>(decoded), "the speaking board loads");
    expect(decoded.moments.size() == 3, "carrying every moment authored");
    if (decoded.moments.size() == 3) {
        expect(
            decoded.moments[0].trigger == pr::MomentTrigger::stage_opens &&
                decoded.moments[0].placement_id == 0U &&
                decoded.moments[0].dialogue_id == 101U,
            "the one about the board is about nobody, and names its scene"
        );
        expect(
            decoded.moments[1].trigger == pr::MomentTrigger::character_talked &&
                decoded.moments[1].placement_id == about,
            "the one about a character talked away names that character"
        );
        expect(
            decoded.moments[2].trigger == pr::MomentTrigger::character_falls &&
                decoded.moments[2].placement_id == about,
            "and so does the one about a character falling, which is a "
            "different occasion and a different event"
        );
    }

    // In the order they were authored, because that is the only order there is:
    // two moments on one occasion play in the order somebody wrote them.
    expect(
        decoded.moments.size() == 3 && decoded.moments[0].id == 11U &&
            decoded.moments[1].id == 12U && decoded.moments[2].id == 13U,
        "in the order they were authored"
    );

    // And the board is otherwise the board it was. A moment says what is heard,
    // never who stands where.
    expect(
        decoded.definition.units.size() == quiet.definition.units.size(),
        "the same characters are on it"
    );
}

void refuses_a_moment_that_names_nobody_on_this_board() {
    // A moment about a character the board does not field is a package
    // disagreeing with itself, and saying so beats going quiet at the moment
    // the words were due. The same rule a talk mark is held to.
    auto authored = ranked_source();
    authored.encounters.front().moments = {
        {14U, gc::MomentTrigger::character_falls, 999999U, 104U},
    };
    const auto decoded = pr::load_encounter(compile_and_load(authored), 100);
    expect(
        !decoded && decoded.error == pr::EncounterLoadError::missing_reference,
        "a moment about nobody on this board is refused by name"
    );
}

void carries_the_talk_marks_onto_the_board() {
    // First the ordinary case, and the one that matters most: a project where
    // nobody authors a talk decodes with nobody talkable, through the same code
    // path, having had no section to read.
    const pf::LoadedPackage silent = compile_and_load(ranked_source());
    const auto quiet = pr::load_encounter(silent, 100);
    expect(static_cast<bool>(quiet), "a talkless board loads");
    expect(
        silent.find(pf::SectionType::talks) == nullptr,
        "and its package has no talks section at all"
    );
    bool anybody = false;
    for (const sim::UnitDefinition& unit : quiet.definition.units) {
        anybody = anybody || unit.talk_record_id != 0U;
    }
    expect(!anybody, "so nobody on it can be talked to");

    // Then the authored case: the mark reaches the placement it names, and
    // nobody else.
    auto authored = ranked_source();
    const auto talkable_id =
        authored.encounters.front().placements.back().id;
    authored.encounters.front().placements.back().talk_flag_id = 4242;
    const pf::LoadedPackage spoken = compile_and_load(authored);
    expect(
        spoken.find(pf::SectionType::talks) != nullptr,
        "an authored talk gives the package a talks section"
    );
    const auto decoded = pr::load_encounter(spoken, 100);
    expect(static_cast<bool>(decoded), "the talkable board loads");
    int marked = 0;
    for (const sim::UnitDefinition& unit : decoded.definition.units) {
        if (unit.talk_record_id == 0U) continue;
        ++marked;
        expect(
            unit.id == talkable_id && unit.talk_record_id == 4242U,
            "the mark lands on the placement that authored it, carrying the "
            "flag that placement named"
        );
    }
    expect(marked == 1, "and on nobody else");

    // The board is otherwise the same board: a talk mark says who may be
    // talked to and changes nothing else about who stands where.
    expect(
        decoded.definition.units.size() == quiet.definition.units.size(),
        "the same characters are on it"
    );
}

void carries_the_deployment_capacity_beside_the_board() {
    auto authored = ranked_source();
    authored.encounters.front().deployment = {120, {{0, 1}, {0, 2}}, 1};
    const pf::LoadedPackage capped = compile_and_load(authored);
    const auto decoded = pr::load_encounter(capped, 100);
    expect(
        static_cast<bool>(decoded) && decoded.deployment_capacity == 1U &&
            decoded.definition.deployment_tiles.size() == 2U &&
            decoded.deployment_zone_id == 120U,
        "a region and a cap decode into the tiles, the identity and the count "
        "the author wrote"
    );

    // The same record with the capacity's two bytes taken off the end, which
    // is exactly the record this encounter compiled to before capacities
    // existed. It is a region and no cap, not a refusal.
    const auto before_capacities = pf::load_mock_package(
        package_with_record_shortened(
            capped, pf::SectionType::encounters, 100, 2U
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(
        static_cast<bool>(before_capacities),
        "a package whose deployment tail ends with its tiles loads"
    );
    const auto older = pr::load_encounter(before_capacities.package, 100);
    expect(
        static_cast<bool>(older) && older.deployment_capacity == 0U &&
            older.definition.deployment_tiles.size() == 2U &&
            older.deployment_zone_id == 120U,
        "and decodes as the same region with no cap rather than being rejected"
    );
}

// A cap and no region: the encounter says how many may come and nothing about
// where they stand, so there is no phase to open and nothing for a client to
// arrange.
void carries_a_capacity_with_no_region() {
    auto authored = ranked_source();
    authored.encounters.front().deployment = {120, {}, 1};
    const pf::LoadedPackage capped = compile_and_load(authored);
    const auto decoded = pr::load_encounter(capped, 100);
    expect(
        static_cast<bool>(decoded) &&
            decoded.definition.deployment_tiles.empty() &&
            decoded.deployment_capacity == 1U &&
            decoded.deployment_zone_id == 120U,
        "a capacity-only deployment decodes into an empty region, its identity "
        "and the cap"
    );
    auto created = sim::create_encounter(decoded.definition);
    expect(
        static_cast<bool>(created) && !created.encounter.snapshot().deploying,
        "and the battle it makes opens on the first activation, because a cap "
        "is not a phase"
    );

    // The same tail with the capacity taken off is a zone that states neither
    // tiles nor a cap. It was malformed before capacities existed, and the
    // decoder's refusal of it is replaced rather than removed: no byte pattern
    // that was refused then is accepted now.
    const auto states_nothing = pf::load_mock_package(
        package_with_record_shortened(
            capped, pf::SectionType::encounters, 100, 2U
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(
        static_cast<bool>(states_nothing),
        "a package whose deployment tail states nothing still loads "
        "structurally"
    );
    const auto refused = pr::load_encounter(states_nothing.package, 100);
    expect(
        refused.error == pr::EncounterLoadError::malformed_payload,
        "and the encounter decoder refuses it exactly as it always did"
    );
    expect(
        refused.definition.units.empty(),
        "publishing no partial definition, as every malformed payload does"
    );
}

// What a campaign puts in its store by authoring, over the wire: the founding
// stock under the reserved zero node, each node's grants under that node, and
// the authored order preserved because the order is the order the operations
// are built in.
void carries_authored_grants_onto_the_campaign() {
    const pf::LoadedPackage plain = compile_and_load();
    const auto ungranted = pr::load_campaign(plain, 110);
    expect(
        static_cast<bool>(ungranted) && ungranted.definition.grants.empty(),
        "a campaign that authors no grant carries none"
    );

    auto authored = source();
    authored.campaigns.front().grants = {{50, 3, 0}, {51, 1, 0}, {50, 2, 111}};
    const pf::LoadedPackage stocked = compile_and_load(authored);
    const auto decoded = pr::load_campaign(stocked, 110);
    expect(
        static_cast<bool>(decoded) && decoded.definition.grants.size() == 3U,
        std::string_view{pr::error_name(decoded.error)}
    );
    if (decoded.definition.grants.size() != 3U) return;
    const std::vector<pr::CampaignItemGrant>& grants = decoded.definition.grants;
    expect(
        grants[0].item_id == 50U && grants[0].quantity == 3U &&
            grants[0].join_node_id == 0U && grants[1].item_id == 51U &&
            grants[1].quantity == 1U && grants[1].join_node_id == 0U,
        "the founding stock comes back in the authored order, under the "
        "reserved zero node a founding member already uses"
    );
    expect(
        grants[2].item_id == 50U && grants[2].quantity == 2U &&
            grants[2].join_node_id == 111U,
        "and a node's grant under the node whose completion gives it"
    );

    // The tail taken off, which is the campaign record this campaign compiled
    // to before grants existed: a count and three twenty-byte entries.
    const auto before_grants = pf::load_mock_package(
        package_with_record_shortened(
            stocked, pf::SectionType::campaigns, 110, 2U + 3U * 20U
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(
        static_cast<bool>(before_grants),
        "a package whose campaign record ends after its roster loads"
    );
    const auto older = pr::load_campaign(before_grants.package, 110);
    expect(
        static_cast<bool>(older) && older.definition.grants.empty() &&
            older.definition.members.size() ==
                decoded.definition.members.size(),
        "and decodes as a campaign that grants nothing, with the company it "
        "always carried, rather than being rejected"
    );

    // A grant naming a node the campaign does not hold is the same fault as a
    // member joining at one, and answers with the same word. No compiler emits
    // it, so it is written into the bytes.
    std::vector<std::uint8_t> payload =
        record_payload(stocked, pf::SectionType::campaigns, 110);
    expect(payload.size() > 20U, "the campaign record carries its grant tail");
    if (payload.size() <= 20U) return;
    // The last entry's join node is the first eight bytes of the last twenty.
    std::size_t cursor = payload.size() - 20U;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        payload[cursor++] =
            static_cast<std::uint8_t>((999ULL >> shift) & 0xffULL);
    }
    const auto adrift = pf::load_mock_package(
        package_with_record_replaced(
            stocked, pf::SectionType::campaigns, 110, payload
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(adrift), "the rewritten package loads");
    expect(
        pr::load_campaign(adrift.package, 110).error ==
            pr::CampaignError::missing_reference,
        "a grant naming a node the campaign does not hold is refused by the "
        "loader's own word for a reference that names nothing"
    );
}

// ---------------------------------------------------------------------------
// What an author made of individual characters, over the wire
// ---------------------------------------------------------------------------

// One delta-able stat's own index, so a test says `speed` rather than `10`.
constexpr std::uint8_t stat_index(pr::SpecificStat stat) {
    return static_cast<std::uint8_t>(stat);
}

void state_delta(
    gc::CampaignMemberSpecificity& specificity,
    gc::SpecificStat stat,
    std::int16_t delta
) {
    const auto index = static_cast<std::size_t>(stat);
    specificity.stat_deltas[index] = delta;
    specificity.stated[index] = true;
}

// What the decoded table says about one member, read by identity rather than
// by position, so the assertions about *what* a member is do not also depend
// on the assertion about the order they arrive in.
const pr::MemberSpecificity* specificity_of(
    const pr::CampaignDefinition& definition,
    std::uint64_t member_id
) {
    for (const pr::MemberSpecificity& entry : definition.specificities) {
        if (entry.member_id == member_id) return &entry;
    }
    return nullptr;
}

// The runtime fixture with three characters written to be more than their
// classes, one of each shape the tail has to carry.
//
// The class behind all three is Blue: 6 health, 4 strength, 1 defense, 2
// resistance and 3 movement, with the one action point and one speed a class
// that authors neither has. Every delta below is a number that could have been
// written on that class.
gc::GameSource specific_source() {
    auto value = source();
    std::vector<gc::CampaignMember>& roster = value.campaigns.front().roster;
    // Deltas and no bonus, including one that takes a stat down rather than
    // up, so the signed encoding is exercised in both directions.
    roster.front().states_specificity = true;
    state_delta(roster.front().specificity, gc::SpecificStat::health, 3);
    state_delta(roster.front().specificity, gc::SpecificStat::defense, -1);
    state_delta(roster.front().specificity, gc::SpecificStat::speed, 2);
    // A bonus and no deltas: a character who is their class in every number
    // and still fights at a different distance.
    gc::CampaignMember reacher{2003, "Rook", 60, 0};
    reacher.states_specificity = true;
    reacher.specificity.reach_bonus = 2;
    roster.push_back(reacher);
    // A recruit, who is a member of the company from the moment they join and
    // is written in the same table by the same rules.
    gc::CampaignMember recruit{2004, "Wren", 60, 111};
    recruit.states_specificity = true;
    state_delta(recruit.specificity, gc::SpecificStat::magic, 5);
    recruit.specificity.reach_bonus = 1;
    roster.push_back(recruit);
    return value;
}

// Authored, compiled, decoded: the deltas land on the stats they were written
// over, the bonus survives, and the entries arrive in the order the company
// was authored in. That is the order persistent identities are handed out in,
// and therefore content rather than a detail.
void carries_authored_specificities_onto_the_campaign() {
    const pf::LoadedPackage plain = compile_and_load();
    const auto ordinary = pr::load_campaign(plain, 110);
    expect(
        static_cast<bool>(ordinary) &&
            ordinary.definition.specificities.empty(),
        "a campaign whose members are exactly their unit types carries no "
        "specificity at all"
    );

    const pf::LoadedPackage package = compile_and_load(specific_source());
    const auto decoded = pr::load_campaign(package, 110);
    expect(
        static_cast<bool>(decoded) &&
            decoded.definition.specificities.size() == 3U,
        std::string_view{pr::error_name(decoded.error)}
    );
    if (decoded.definition.specificities.size() != 3U) return;
    const std::vector<pr::MemberSpecificity>& carried =
        decoded.definition.specificities;
    expect(
        carried[0].member_id == 2000U && carried[1].member_id == 2003U &&
            carried[2].member_id == 2004U,
        "the entries come back in the order the company was authored in, "
        "recruit last"
    );
    expect(
        decoded.definition.grants.empty(),
        "and a campaign that authors a specificity and no grant grants "
        "nothing, whatever count the record had to write to hold the tail's "
        "place"
    );

    const pr::MemberSpecificity* const numbers =
        specificity_of(decoded.definition, 2000U);
    expect(numbers != nullptr, "the member who authored deltas is in the table");
    if (numbers != nullptr) {
        expect(
            numbers->stat_deltas[stat_index(pr::SpecificStat::health)] == 3 &&
                numbers->stat_deltas[stat_index(pr::SpecificStat::defense)] ==
                    -1 &&
                numbers->stat_deltas[stat_index(pr::SpecificStat::speed)] == 2,
            "each delta comes back on the stat it was written over, negative "
            "numbers included"
        );
        std::int32_t written = 0;
        for (const std::int16_t delta : numbers->stat_deltas) {
            if (delta != 0) ++written;
        }
        expect(
            written == 3 && numbers->reach_bonus == 0U,
            "and nothing else: an omitted stat decodes as the zero that says "
            "the author said nothing about it"
        );
    }

    const pr::MemberSpecificity* const reach =
        specificity_of(decoded.definition, 2003U);
    expect(reach != nullptr, "so is the member who authored only a bonus");
    if (reach != nullptr) {
        expect(
            reach->reach_bonus == 2U && !reach->empty(),
            "whose record says something although it names no stat"
        );
        bool untouched = true;
        for (const std::int16_t delta : reach->stat_deltas) {
            untouched = untouched && delta == 0;
        }
        expect(untouched, "and moves no number");
    }

    const pr::MemberSpecificity* const joined =
        specificity_of(decoded.definition, 2004U);
    expect(joined != nullptr, "and so is the recruit");
    if (joined != nullptr) {
        expect(
            joined->stat_deltas[stat_index(pr::SpecificStat::magic)] == 5 &&
                joined->reach_bonus == 1U,
            "who reaches the tail with both halves intact, because a recruit "
            "is a member of the company and not a second kind of thing"
        );
    }

    // A campaign that authors both. The grants tail is written for its own
    // sake here rather than to hold a place, and the specificity tail follows
    // it exactly as it follows the count of zero above.
    auto stocked_source = specific_source();
    stocked_source.campaigns.front().grants = {{50, 3, 0}, {51, 1, 111}};
    const pf::LoadedPackage stocked = compile_and_load(stocked_source);
    const auto both = pr::load_campaign(stocked, 110);
    expect(
        static_cast<bool>(both) && both.definition.grants.size() == 2U &&
            both.definition.specificities.size() == 3U,
        "a campaign that stocks a store and authors characters carries both "
        "tails, and neither reads the other's bytes"
    );
    if (both.definition.grants.size() == 2U) {
        expect(
            both.definition.grants[0].item_id == 50U &&
                both.definition.grants[0].quantity == 3U &&
                both.definition.grants[1].item_id == 51U &&
                both.definition.grants[1].join_node_id == 111U,
            "with the store holding what it was granted"
        );
    }
    const pr::MemberSpecificity* const beside_the_store =
        specificity_of(both.definition, 2004U);
    expect(
        beside_the_store != nullptr && beside_the_store->reach_bonus == 1U &&
            beside_the_store->stat_deltas[stat_index(pr::SpecificStat::magic)] ==
                5,
        "and the characters what they were written to be"
    );

    // The tail taken off, which is the record this campaign compiled to before
    // a member could be more than their class: three entries of ten, thirteen
    // and thirteen bytes, the tail's own count, and the grant count of zero
    // that held its place.
    const std::uint32_t removed =
        2U + 2U + (10U + 3U * 3U) + 10U + (10U + 3U * 1U);
    const auto before_specificities = pf::load_mock_package(
        package_with_record_shortened(
            package, pf::SectionType::campaigns, 110, removed
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(
        static_cast<bool>(before_specificities),
        "a package whose campaign record ends after its roster loads"
    );
    const auto older = pr::load_campaign(before_specificities.package, 110);
    expect(
        static_cast<bool>(older) && older.definition.specificities.empty() &&
            older.definition.members.size() ==
                decoded.definition.members.size(),
        "and decodes as a campaign in which nobody is anything but their "
        "class, with the company it always carried, rather than being rejected"
    );
}

void append_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

// One entry, written the way the compiler writes one, except that the count is
// the caller's, so a test can write a count that disagrees with the deltas
// after it.
void append_entry(
    std::vector<std::uint8_t>& out,
    std::uint64_t member_id,
    std::uint8_t delta_count,
    const std::vector<std::pair<std::uint8_t, std::int16_t>>& deltas,
    std::uint8_t reach_bonus
) {
    append_u64(out, member_id);
    out.push_back(delta_count);
    for (const std::pair<std::uint8_t, std::int16_t>& delta : deltas) {
        out.push_back(delta.first);
        append_u16(out, static_cast<std::uint16_t>(delta.second));
    }
    out.push_back(reach_bonus);
}

// A campaign record posed to the decoder: the flow and the roster a real
// compile produced, the grant count of zero that holds the positional tail's
// place, and whatever bytes the caller wants read as a specificity tail. No
// compiler emits most of what follows, which is exactly why it is written by
// hand.
pr::CampaignLoadResult decode_with_tail(
    const pf::LoadedPackage& plain,
    const std::vector<std::uint8_t>& tail
) {
    std::vector<std::uint8_t> payload =
        record_payload(plain, pf::SectionType::campaigns, 110);
    append_u16(payload, 0);
    payload.insert(payload.end(), tail.begin(), tail.end());
    const auto rebuilt = pf::load_mock_package(
        package_with_record_replaced(
            plain, pf::SectionType::campaigns, 110, payload
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(rebuilt), "the rewritten package loads");
    if (!rebuilt) return {};
    return pr::load_campaign(rebuilt.package, 110);
}

// A record that ends before the tail and a record carrying an empty one are
// the same campaign. The first is every campaign compiled before this existed;
// the second is a record a writer could produce by holding a place it then had
// nothing to put in, and neither is a refusal.
void reads_an_absent_specificity_tail_as_an_empty_one() {
    const pf::LoadedPackage plain = compile_and_load();
    const auto absent = pr::load_campaign(plain, 110);
    expect(
        static_cast<bool>(absent) && absent.definition.specificities.empty(),
        "a record that ends at its last member carries no specificity"
    );

    const auto stated_empty = decode_with_tail(plain, {0x00U, 0x00U});
    expect(
        static_cast<bool>(stated_empty) &&
            stated_empty.definition.specificities.empty(),
        std::string_view{pr::error_name(stated_empty.error)}
    );
    expect(
        stated_empty.definition.members.size() ==
                absent.definition.members.size() &&
            stated_empty.definition.grants.empty() &&
            stated_empty.definition.nodes.size() ==
                absent.definition.nodes.size(),
        "and a record stating a count of none decodes into the same campaign, "
        "member for member and node for node"
    );
}

// Every way a specificity tail can be wrong, and one word for all of them. A
// tail this decoder cannot believe is a record it will not half-read: nothing
// here is skipped, clamped, or read as a shrug.
void refuses_a_specificity_tail_it_cannot_believe() {
    const pf::LoadedPackage plain = compile_and_load();

    // The control. Every refusal below is this tail with one thing changed, so
    // each test is about the change and not about the shape.
    std::vector<std::uint8_t> sound;
    append_u16(sound, 1);
    append_entry(sound, 2000U, 1, {{stat_index(pr::SpecificStat::health), 3}}, 2);
    const auto believed = decode_with_tail(plain, sound);
    expect(
        static_cast<bool>(believed) &&
            believed.definition.specificities.size() == 1U,
        "a hand-written tail the compiler could have emitted is read"
    );

    const auto refuses = [&plain](
                             const std::vector<std::uint8_t>& tail,
                             std::string_view message
                         ) {
        const auto decoded = decode_with_tail(plain, tail);
        expect(decoded.error == pr::CampaignError::malformed_payload, message);
        expect(
            decoded.definition.specificities.empty() &&
                decoded.definition.members.empty(),
            "and publishes no partial campaign, as every malformed payload does"
        );
    };

    // A stat this engine does not have. The package was written by a newer
    // compiler, and reading it as the deltas this build recognised would be a
    // board that silently differs from the one the author built.
    std::vector<std::uint8_t> unknown_stat;
    append_u16(unknown_stat, 1);
    append_entry(
        unknown_stat, 2000U, 1,
        {{static_cast<std::uint8_t>(pr::specific_stat_count), 3}}, 0
    );
    refuses(unknown_stat, "a stat selector past the last stat is refused");

    std::vector<std::uint8_t> zero_delta;
    append_u16(zero_delta, 1);
    append_entry(
        zero_delta, 2000U, 1, {{stat_index(pr::SpecificStat::health), 0}}, 2
    );
    refuses(
        zero_delta,
        "a written delta of zero is refused, because omitting the stat is how "
        "nothing is said"
    );

    std::vector<std::uint8_t> twice;
    append_u16(twice, 1);
    append_entry(
        twice, 2000U, 2,
        {{stat_index(pr::SpecificStat::health), 3},
         {stat_index(pr::SpecificStat::health), 1}},
        0
    );
    refuses(
        twice,
        "one stat written twice in one entry is a record that answers the same "
        "question twice"
    );

    std::vector<std::uint8_t> nobody;
    append_u16(nobody, 1);
    append_entry(
        nobody, 0U, 1, {{stat_index(pr::SpecificStat::health), 3}}, 0
    );
    refuses(nobody, "an entry about member zero is about nobody");

    std::vector<std::uint8_t> too_many;
    append_u16(too_many, 1);
    append_entry(
        too_many, 2000U, static_cast<std::uint8_t>(pr::specific_stat_count + 1),
        {{stat_index(pr::SpecificStat::health), 3}}, 0
    );
    refuses(
        too_many,
        "a delta count above the number of stats there are is refused before "
        "a byte of it is read"
    );

    std::vector<std::uint8_t> says_nothing;
    append_u16(says_nothing, 1);
    append_entry(says_nothing, 2000U, 0, {}, 0);
    refuses(
        says_nothing,
        "an entry with no delta and no bonus is a member written to be exactly "
        "their class, which is never written down"
    );

    std::vector<std::uint8_t> trailing = sound;
    trailing.push_back(0x00U);
    refuses(
        trailing,
        "and a byte after the tail is a record this decoder does not "
        "understand, whatever the byte is"
    );
}

// ---------------------------------------------------------------------------
// What a fall costs the company, over the wire
// ---------------------------------------------------------------------------

// The project's two words about falling, compiled and read back. They are the
// project's and not the campaign's, and they are written on every campaign a
// project holds, because a runtime reads one campaign at a time and never sees
// a project. The rule has to be where the company is.
void carries_the_loss_rule_onto_the_campaign() {
    const pf::LoadedPackage plain = compile_and_load();
    const auto silent = pr::load_campaign(plain, 110);
    expect(
        static_cast<bool>(silent) &&
            silent.definition.character_loss ==
                pr::CharacterLoss::permanent &&
            !silent.definition.invulnerable_for_testing,
        "a campaign whose project stated nothing loses a fallen character for "
        "good and protects nobody, which is what every campaign compiled "
        "before this meant"
    );

    auto carried_source = source();
    carried_source.character_loss = gc::CharacterLoss::recoverable;
    const auto carried =
        pr::load_campaign(compile_and_load(carried_source), 110);
    expect(
        static_cast<bool>(carried) &&
            carried.definition.character_loss ==
                pr::CharacterLoss::recoverable &&
            !carried.definition.invulnerable_for_testing,
        "a project that states its characters are carried off reaches the "
        "campaign saying so, and asks for no invulnerability by saying it"
    );

    auto tested_source = source();
    tested_source.invulnerable_for_testing = true;
    const auto tested = pr::load_campaign(compile_and_load(tested_source), 110);
    expect(
        static_cast<bool>(tested) &&
            tested.definition.character_loss == pr::CharacterLoss::permanent &&
            tested.definition.invulnerable_for_testing,
        "and the testing aid travels on its own, without answering the "
        "question the loss rule asks"
    );

    // Both at once, on a campaign that also authors a specificity, so the two
    // bytes are read after a tail that actually holds something rather than
    // only after the count of zero that holds its place.
    auto both_source = specific_source();
    both_source.character_loss = gc::CharacterLoss::recoverable;
    both_source.invulnerable_for_testing = true;
    const auto both = pr::load_campaign(compile_and_load(both_source), 110);
    expect(
        static_cast<bool>(both) &&
            both.definition.character_loss == pr::CharacterLoss::recoverable &&
            both.definition.invulnerable_for_testing &&
            both.definition.specificities.size() == 3U,
        "both settings survive a campaign whose members are written to be more "
        "than their classes, and neither tail reads the other's bytes"
    );

    // And the same two settings on a campaign that authors neither a grant nor
    // a specificity, which is the case that has to write two counts of zero to
    // hold the positional tails' places. That the roster still comes back whole
    // is what proves the counts were placeholders and not a shifted record.
    const auto held_open = pr::load_campaign(compile_and_load(tested_source), 110);
    expect(
        static_cast<bool>(held_open) && held_open.definition.grants.empty() &&
            held_open.definition.specificities.empty() &&
            held_open.definition.members.size() ==
                silent.definition.members.size(),
        "a campaign that grants nothing and writes nobody up still carries its "
        "company, whatever counts the record had to write to reach the tail"
    );
}

// A record that ends before the tail is every campaign compiled before a
// project could state a rule, and it is a permanent-loss campaign in which
// nobody is protected. A record whose tail says something this engine does not
// know is refused rather than shrugged at: a package written by a newer
// compiler would otherwise be played under a rule its author did not choose,
// and getting that wrong costs a player a character.
void refuses_a_loss_tail_it_cannot_believe() {
    const pf::LoadedPackage plain = compile_and_load();

    // The control: the specificity count of zero that holds the tail's place,
    // then the two bytes a compiler emitting this rule writes.
    const auto believed = decode_with_tail(plain, {0x00U, 0x00U, 0x02U, 0x01U});
    expect(
        static_cast<bool>(believed) &&
            believed.definition.character_loss ==
                pr::CharacterLoss::recoverable &&
            believed.definition.invulnerable_for_testing,
        "a hand-written tail the compiler could have emitted is read"
    );

    const auto absent = decode_with_tail(plain, {0x00U, 0x00U});
    expect(
        static_cast<bool>(absent) &&
            absent.definition.character_loss == pr::CharacterLoss::permanent &&
            !absent.definition.invulnerable_for_testing,
        "a record that stops at the end of its specificity tail decodes as the "
        "permanent-loss campaign it always was, rather than being rejected"
    );

    const auto refuses = [&plain](
                             const std::vector<std::uint8_t>& tail,
                             std::string_view message
                         ) {
        const auto decoded = decode_with_tail(plain, tail);
        expect(decoded.error == pr::CampaignError::malformed_payload, message);
        expect(
            decoded.definition.members.empty(),
            "and publishes no partial campaign, as every malformed payload does"
        );
    };

    refuses(
        {0x00U, 0x00U, 0x00U, 0x00U},
        "a loss rule of zero is no rule at all, and zero is not the way an "
        "absent rule is said — running out of bytes is"
    );
    refuses(
        {0x00U, 0x00U, 0x03U, 0x00U},
        "a loss rule past the last one this engine knows was written by a "
        "newer compiler, and playing it as permanent would be playing a rule "
        "the author did not choose"
    );
    refuses(
        {0x00U, 0x00U, 0x02U, 0x02U},
        "and a testing flag that is neither yes nor no is refused for the same "
        "reason, although only one of its values could ever mean anything new"
    );
    refuses(
        {0x00U, 0x00U, 0x02U},
        "half a tail is not a tail: a rule byte with nothing after it is a "
        "record this decoder does not understand"
    );
    refuses(
        {0x00U, 0x00U, 0x02U, 0x01U, 0x00U},
        "and a byte after the tail is one too, whatever the byte is"
    );
}

void rejects_missing_encounter_atomically() {
    const auto decoded = pr::load_encounter(compile_and_load(), 999);
    expect(
        decoded.error == pr::EncounterLoadError::missing_record,
        "unknown encounter has a stable error"
    );
    expect(
        decoded.definition.units.empty(),
        "failed decoding publishes no partial units"
    );
}

void rejects_malformed_payload_atomically() {
    pf::PackageSource package;
    package.game_id[0] = 1;
    package.required_engine = {{0, 1, 0}, {0, 1, 99}};
    package.sections = {
        {pf::SectionType::classes, 1, 0,
         pf::section_flag_required, {{30, {0xffU}}}},
        {pf::SectionType::unit_types, 1, 0,
         pf::section_flag_required, {{60, {0xffU}}}},
        {pf::SectionType::maps, 1, 0,
         pf::section_flag_required, {{70, {0xffU}}}},
        {pf::SectionType::objectives, 1, 0,
         pf::section_flag_required, {{90, {0xffU}}}},
        {pf::SectionType::encounters, 1, 0,
         pf::section_flag_required, {{100, {0xffU}}}},
    };
    const auto loaded = pf::load_mock_package(
        pf::write_mock_package(package),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "structural malformed fixture loads");
    const auto decoded = pr::load_encounter(loaded.package, 100);
    expect(
        decoded.error == pr::EncounterLoadError::malformed_payload,
        "semantic payload corruption is rejected by runtime decoder"
    );
    expect(
        decoded.definition.units.empty(),
        "malformed payload publishes no partial definition"
    );
}

// A node holding more than one conditionless target: a campaign whose taken
// edge would depend on the order the targets happened to be written in.
//
// Both halves are asserted, and they are asserted separately because they are
// two different promises. `game_content` refuses such a node outright, at the
// author's own path, so no compiler can produce the record. That is why the
// record below is written by hand. The runtime's refusal is a promise about any
// package it is handed, not only about packages this repository produced, so it
// is worth pinning even though the compiler cannot reach it.
void rejects_ambiguous_campaign_flow() {
    auto ambiguous = source();
    ambiguous.campaigns.front().nodes.front().unconditional_targets = {
        112, 113
    };
    ambiguous.campaigns.front().nodes.push_back(
        {113, gc::CampaignNodeKind::terminal, 0, {}}
    );
    expect(
        !static_cast<bool>(gc::compile(ambiguous)),
        "no compiler emits an ambiguous flow"
    );

    const pf::LoadedPackage plain = compile_and_load();
    std::vector<std::uint8_t> payload;
    append_u16(payload, 0);    // a nameless campaign, which is enough here
    append_u64(payload, 111);  // the node it enters at
    append_u16(payload, 1);    // one node
    append_u64(payload, 111);  // that node's identity
    payload.push_back(3U);     // a story node
    append_u64(payload, 0);    // naming no encounter
    append_u16(payload, 0);    // presenting no scene
    append_u16(payload, 2);    // and holding two conditionless targets
    append_u64(payload, 112);
    append_u64(payload, 113);
    const auto rebuilt = pf::load_mock_package(
        package_with_record_replaced(
            plain, pf::SectionType::campaigns, 110, payload
        ),
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(rebuilt), "the hand-written package loads");
    expect(
        pr::load_campaign(rebuilt.package, 110).error ==
            pr::CampaignError::unsupported_flow,
        "and the runtime refuses the flow inside it by name"
    );
}

void maintained_demo_reaches_reference_result() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) +
        "/games/demo/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "maintained demo source opens");
    const std::string json{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
    const auto parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "maintained demo source maps natively");
    const auto compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "maintained demo package compiles");
    const auto loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "maintained demo package loads");
    const auto encounter_id =
        grandleon::core::stable_content_id_v1(
            "demo_campaign/bridge_encounter"
        );
    auto decoded = pr::load_encounter(loaded.package, encounter_id);
    expect(static_cast<bool>(decoded), "maintained demo encounter decodes");
    auto created = sim::create_encounter(decoded.definition);
    expect(static_cast<bool>(created), "maintained demo encounter starts");
    const auto campaign_id =
        grandleon::core::stable_content_id_v1("demo_campaign");
    auto campaign = pr::load_campaign(loaded.package, campaign_id);
    expect(static_cast<bool>(campaign), "maintained demo campaign decodes");
    pr::CampaignCursor cursor(std::move(campaign.definition));
    expect(
        cursor.current().encounter_id == encounter_id && !cursor.complete(),
        "campaign begins at maintained encounter"
    );
    expect(
        cursor.advance_after(sim::Outcome::ongoing) ==
            pr::CampaignError::outcome_incomplete,
        "campaign does not advance before battle completion"
    );

    const auto first_id = grandleon::core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/dawn_guard_leader"
    );
    const auto second_id = grandleon::core::stable_content_id_v1(
        "demo_campaign/bridge_encounter/river_watch_leader"
    );
    const sim::Command commands[] = {
        {sim::CommandType::move, first_id, {1, 1}, 0},
        {sim::CommandType::attack, first_id, {}, second_id},
        {sim::CommandType::attack, second_id, {}, first_id},
        {sim::CommandType::attack, first_id, {}, second_id},
    };
    for (const sim::Command& command : commands) {
        const auto applied = created.encounter.apply(command);
        if (!applied) {
            std::cerr << "demo command error: "
                      << sim::error_name(applied.error) << '\n';
        }
        expect(static_cast<bool>(applied),
               "maintained demo reference command succeeds");
    }
    expect(
        created.encounter.snapshot().outcome ==
            sim::Outcome::first_side_won,
        "maintained demo reference stream reaches first-side victory"
    );
    expect(
        cursor.advance_after(created.encounter.snapshot().outcome) ==
            pr::CampaignError::none,
        "completed battle advances unconditional campaign edge"
    );
    expect(
        cursor.complete() &&
            cursor.current().id ==
                grandleon::core::stable_content_id_v1("demo_complete"),
        "campaign recombines at maintained terminal node"
    );
    expect(
        cursor.advance_after(sim::Outcome::first_side_won) ==
            pr::CampaignError::already_complete,
        "terminal campaign cursor cannot advance again"
    );
}


void carries_the_arrivals_onto_the_board() {
    // The ordinary case first, and the one that matters most: a project where
    // nothing arrives decodes through the same code path, having had no section
    // to read, with every character standing on the board from the opening.
    const pf::LoadedPackage still = compile_and_load(ranked_source());
    const auto steady = pr::load_encounter(still, 100);
    expect(static_cast<bool>(steady), "a waveless board loads");
    expect(
        still.find(pf::SectionType::arrivals) == nullptr,
        "and its package has no arrivals section at all"
    );
    bool anybody = false;
    for (const sim::UnitDefinition& unit : steady.definition.units) {
        anybody = anybody || unit.arrival_round != 0U;
    }
    expect(!anybody, "so nobody on it is still marching");

    // The wave is the opposing placement, because a placement that fields a
    // roster member cannot arrive.
    auto authored = ranked_source();
    gc::Placement& wave = *std::find_if(
        authored.encounters.front().placements.begin(),
        authored.encounters.front().placements.end(),
        [](const gc::Placement& placement) {
            return placement.side == gc::EncounterSide::second;
        }
    );
    const auto arriving_id = wave.id;
    wave.arrival_round = 3;
    wave.arrival_every = 3;
    wave.arrival_times = 4;
    wave.behavior = gc::UnitBehavior::pursue;
    const pf::LoadedPackage waved = compile_and_load(authored);
    expect(
        waved.find(pf::SectionType::arrivals) != nullptr,
        "an authored wave gives the package an arrivals section"
    );
    const auto decoded = pr::load_encounter(waved, 100);
    expect(static_cast<bool>(decoded), "the board with a wave loads");
    int marching = 0;
    for (const sim::UnitDefinition& unit : decoded.definition.units) {
        if (unit.arrival_round == 0U) continue;
        ++marching;
        expect(
            unit.id == arriving_id && unit.arrival_round == 3U &&
                unit.arrival_every == 3U && unit.arrival_times == 4U,
            "the arrival lands on the placement that authored it, carrying the "
            "round, the gap and the number of arrivals that placement named"
        );
    }
    expect(marching == 1, "and on nobody else");
    // The stance rides on the placement it always rode on, so a wave authored
    // to come at the player is authored through the field that already exists.
    bool pursuing = false;
    for (const pr::UnitBehaviorBinding& binding : decoded.behaviors) {
        if (binding.unit_id != arriving_id) continue;
        pursuing = binding.behavior == grandleon::tactics::Behavior::pursue;
    }
    expect(pursuing, "and its stance comes across untouched");
    expect(
        decoded.definition.units.size() == steady.definition.units.size(),
        "the same characters are on the board"
    );
}

void carries_the_survive_count_onto_the_objective() {
    auto authored = ranked_source();
    authored.objectives.front().kind = gc::ObjectiveKind::survive_rounds;
    authored.objectives.front().rounds = 7;
    const pf::LoadedPackage holding = compile_and_load(authored);
    const auto decoded = pr::load_encounter(holding, 100);
    expect(static_cast<bool>(decoded), "a survive board loads");
    expect(
        decoded.definition.objectives.size() == 1U &&
            decoded.definition.objectives.front().kind ==
                sim::ObjectiveKind::survive_rounds &&
            decoded.definition.objectives.front().round_count == 7U,
        "the count the author wrote is the count the battle is judged by"
    );

    // And the record of every other kind is the record it always was: the
    // count is a tail written only for the kind that reads one, so a reader
    // that stops where it always stopped is still right.
    const auto plain = pr::load_encounter(compile_and_load(ranked_source()), 100);
    expect(
        static_cast<bool>(plain) &&
            plain.definition.objectives.front().round_count == 0U,
        "and an objective of any other kind carries no count at all"
    );
}

}  // namespace

// A triangle authored in a project reaches the blow, through every layer
// between: the compiler writes it, the package carries it, the loader resolves
// only the kinds the board actually uses, and the rules price a strike by it.
//
// The two boards differ in one thing - whether one kind is written to beat the
// other - so whatever the absolute numbers are, the difference between them is
// the advantage and nothing else.
void a_triangle_reaches_the_blow() {
    const auto board = [](bool with_an_edge) {
        auto authored = source();
        // Two kinds, and one weapon of each, so the two characters face each
        // other holding different kinds.
        authored.weapon_types = {
            {10, "Blade", with_an_edge ? std::vector<gc::StableId>{11}
                                       : std::vector<gc::StableId>{}},
            {11, "Haft", {}},
        };
        // Reach two, because this fixture stands the two of them two tiles
        // apart. Both the same, so the only difference between the boards
        // stays the edge between the kinds.
        // Seventy, because a weapon that says nothing about accuracy always
        // lands, and fifteen points added to certainty is still certainty: the
        // clamp would hide the very thing this is measuring.
        authored.weapons = {
            {40, "Sword", 10, 3, 1, 2, 70},
            {41, "Pike", 11, 3, 1, 2, 70},
        };
        authored.classes[0].allowed_weapon_types = {10, 11};
        authored.classes[1].allowed_weapon_types = {10, 11};
        authored.unit_types[0].starting_weapons = {40};
        authored.unit_types[1].starting_weapons = {41};
        authored.weapon_advantage = {2, 15};
        return authored;
    };

    const auto priced = [](const gc::GameSource& authored) {
        const auto compiled = gc::compile(authored);
        expect(static_cast<bool>(compiled), "the triangle fixture compiles");
        const pf::LoadOptions options{
            {0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000
        };
        const auto loaded = pf::load_mock_package(compiled.package, options);
        expect(static_cast<bool>(loaded), "the triangle package loads");
        const auto decoded = pr::load_encounter(loaded.package, 100);
        expect(static_cast<bool>(decoded), "the triangle encounter decodes");
        auto created = sim::create_encounter(decoded.definition);
        expect(static_cast<bool>(created), "the triangle board is valid");
        const auto snapshot = created.encounter.snapshot();
        expect(snapshot.units.size() == 2, "two characters face each other");
        return sim::forecast_attack(
            snapshot, snapshot.units[0].id, snapshot.units[1].id
        );
    };

    const auto without = priced(board(false));
    const auto with = priced(board(true));
    expect(
        static_cast<bool>(without) && static_cast<bool>(with),
        "both boards price the same strike"
    );
    expect(
        with.damage == static_cast<std::int16_t>(without.damage + 2),
        "a kind written to beat the other is worth the authored damage"
    );
    expect(
        with.hit_chance == static_cast<std::uint8_t>(without.hit_chance + 15),
        "and the authored points of accuracy beside it"
    );
    // And the table only carries the kinds the board can actually produce: the
    // kind that beats nothing is not worth a row every blow walks past.
    {
        const auto compiled = gc::compile(board(true));
        const pf::LoadOptions options{
            {0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000
        };
        const auto loaded = pf::load_mock_package(compiled.package, options);
        const auto decoded = pr::load_encounter(loaded.package, 100);
        expect(
            decoded.definition.weapon_types.size() == 1 &&
                decoded.definition.weapon_types[0].id == 10 &&
                decoded.definition.weapon_types[0].strong_against ==
                    std::vector<sim::ContentId>{11},
            "only the kinds that beat something are carried onto the board"
        );
        expect(
            decoded.definition.weapon_types[0].damage == 2 &&
                decoded.definition.weapon_types[0].accuracy == 15,
            "and each carries what the game says the advantage is worth"
        );
    }
}

int main() {
    decodes_compiled_encounter();
    carries_every_weapon_a_unit_type_lists();
    carries_the_richer_stat_line();
    carries_passability_and_traversal();
    a_map_with_no_price_reads_as_one_step_a_cell();
    refuses_a_placement_on_ground_it_cannot_enter();
    carries_authored_accuracy_into_the_rules();
    carries_every_item_a_unit_type_lists();
    reads_a_unit_types_starting_items_without_a_board();
    carries_authored_growth_beside_the_board();
    carries_an_authored_drop_onto_the_board();
    compiled_package_reaches_outcome();
    carries_the_deployment_region_onto_the_board();
    carries_the_talk_marks_onto_the_board();
    carries_what_is_said_during_a_battle();
    refuses_a_moment_that_names_nobody_on_this_board();
    carries_the_arrivals_onto_the_board();
    carries_the_survive_count_onto_the_objective();
    a_triangle_reaches_the_blow();
    carries_the_deployment_capacity_beside_the_board();
    carries_a_capacity_with_no_region();
    carries_authored_grants_onto_the_campaign();
    carries_authored_specificities_onto_the_campaign();
    reads_an_absent_specificity_tail_as_an_empty_one();
    refuses_a_specificity_tail_it_cannot_believe();
    carries_the_loss_rule_onto_the_campaign();
    refuses_a_loss_tail_it_cannot_believe();
    rejects_missing_encounter_atomically();
    rejects_malformed_payload_atomically();
    rejects_ambiguous_campaign_flow();
    maintained_demo_reaches_reference_result();
    return failures == 0 ? 0 : 1;
}
