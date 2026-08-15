// SPDX-License-Identifier: MIT
// Host-side test for the shared unit sheet.
//
// The sheet is the one thing three clients draw and none of them composes, so
// it is pinned here line for line against a snapshot built by hand. A console
// that drew the wrong sheet would be a rendering bug; a sheet that said the
// wrong thing would be every client saying it at once, which is what this
// suite exists to stop.

#include "grandleon/sheet/unit_sheet.hpp"

#include <grandleon/core/content_identity.hpp>

#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace sim = grandleon::simulation;
namespace sheet = grandleon::sheet;
namespace core = grandleon::core;

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_line(
    const sheet::UnitSheet& built, int index, const char* text,
    std::string_view message
) {
    const bool equal = std::strcmp(built.line(index), text) == 0;
    if (!equal) {
        std::cerr << "FAIL: " << message << "\n  wanted: " << text
                  << "\n  got:    " << built.line(index) << '\n';
        ++failures;
    }
}

// An archer carrying one bow and knowing one volley, with every stat the
// richer stat line added set to something a reader could tell apart from zero.
sim::UnitSnapshot make_archer() {
    sim::UnitSnapshot unit;
    unit.id = 3;
    unit.unit_type_id = core::stable_content_id_v1("dawn_archer");
    unit.side = sim::Side::first;
    unit.position = {4, 4};
    unit.health = 5;
    unit.maximum_health = 8;
    unit.strength = 4;
    unit.power = 5;
    unit.defense = 2;
    unit.resistance = 1;
    unit.skill = 5;
    unit.luck = 2;
    unit.evasion = 3;
    unit.magic = 0;
    unit.movement = 4;
    unit.action_points = 1;
    unit.speed = 6;
    unit.minimum_reach = 2;
    unit.maximum_reach = 3;
    unit.accuracy = 90;
    unit.weapon_ids = {core::stable_content_id_v1("long_bow")};
    unit.ability_ids = {core::stable_content_id_v1("volley")};
    unit.item_ids = {core::stable_content_id_v1("field_tonic")};
    unit.item_counts = {2};
    return unit;
}

std::vector<sim::WeaponDefinition> make_weapons() {
    sim::WeaponDefinition bow;
    bow.id = core::stable_content_id_v1("long_bow");
    bow.power = 5;
    bow.minimum_reach = 2;
    bow.maximum_reach = 3;
    bow.accuracy = 90;
    sim::WeaponDefinition sword;
    sword.id = core::stable_content_id_v1("guard_sword");
    sword.power = 3;
    sword.minimum_reach = 1;
    sword.maximum_reach = 1;
    sword.accuracy = 100;
    return {bow, sword};
}

std::vector<sim::AbilityDefinition> make_abilities() {
    sim::AbilityDefinition volley;
    volley.id = core::stable_content_id_v1("volley");
    volley.kind = sim::AbilityKind::damage;
    volley.power = 4;
    volley.minimum_reach = 2;
    volley.maximum_reach = 2;
    sim::AbilityDefinition arc;
    arc.id = core::stable_content_id_v1("cinder_arc");
    arc.kind = sim::AbilityKind::damage;
    arc.power = 4;
    arc.minimum_reach = 2;
    arc.maximum_reach = 3;
    return {volley, arc};
}

std::vector<sim::ItemDefinition> make_items() {
    sim::ItemDefinition tonic;
    tonic.id = core::stable_content_id_v1("field_tonic");
    tonic.kind = sim::ItemKind::restore;
    tonic.power = 4;
    return {tonic};
}

}  // namespace

int main() {
    const std::vector<sim::WeaponDefinition> weapons = make_weapons();
    const std::vector<sim::AbilityDefinition> abilities = make_abilities();
    const std::vector<sim::ItemDefinition> items = make_items();

    // The whole sheet, line for line. Every number here is one this test set on
    // the snapshot, which is the property that matters: nothing on the sheet is
    // derived, defaulted or rounded on its way to the screen.
    {
        sim::EncounterSnapshot snapshot;
        snapshot.width = 10;
        snapshot.height = 8;
        snapshot.units = {make_archer()};
        const sheet::UnitSheet built = sheet::build(
            snapshot, snapshot.units[0], nullptr, weapons, abilities, items
        );
        expect(built.count == 10, "the archer's sheet is ten lines");
        // Who, and nothing else on the row. With no package there is no class
        // section to read, so the class row is absent rather than blank. The
        // name is the derived one, because a lone archer needs no ordinal
        // to be told apart from anybody.
        expect_line(built, 0, "ARCHER", "the header names the character");
        expect_line(
            built, 1, "HP 5/8  AP 1  MOV 4  SPD 6",
            "the first stat row is health, points, movement and speed"
        );
        expect_line(
            built, 2, "STR 4  DEF 2  RES 1  MAG 0",
            "the second stat row is what it deals and what it takes"
        );
        expect_line(
            built, 3, "SKL 5  LCK 2  EVA 3",
            "the third stat row is what decides whether a blow lands"
        );
        expect_line(built, 4, "WEAPONS", "the weapons are headed");
        expect_line(
            built, 5, "  LONG BOW  RNG 2-3  HIT 90%",
            "a carried weapon carries its band and its accuracy"
        );
        expect_line(built, 6, "ABILITIES", "the abilities are headed");
        expect_line(
            built, 7, "  VOLLEY  RNG 2",
            "a known ability carries the band it is cast within"
        );
        expect_line(built, 8, "ITEMS", "the pack is headed like the rest");
        expect_line(
            built, 9, "  FIELD TONIC  x2  HEAL 4",
            "a carried item states how many are left and what it gives back"
        );
    }

    // Every line fits the narrowest console surface, at the far end of every
    // authored range. Forty columns is not a style preference: it is what 320
    // pixels of an eight-pixel font holds, and a line past it is a line a
    // console silently cuts. Health is sixteen bits and the three numbers
    // beside it are eight, so this character's first line would be forty-one
    // characters if the composer did not stop at forty. That is the one bound
    // paging could never have fixed, because the overflow runs along the line
    // rather than down the screen.
    {
        sim::UnitSnapshot crowded = make_archer();
        crowded.health = 32767;
        crowded.maximum_health = 32767;
        crowded.movement = 255;
        crowded.action_points = 255;
        crowded.speed = 255;
        crowded.strength = 32767;
        crowded.defense = 32767;
        crowded.resistance = 32767;
        crowded.magic = 32767;
        crowded.weapon_ids = {
            core::stable_content_id_v1("training_sword"),
            core::stable_content_id_v1("mending_staff"),
        };
        crowded.ability_ids = {
            core::stable_content_id_v1("power_strike"),
            core::stable_content_id_v1("chain_storm"),
        };
        crowded.item_ids = {core::stable_content_id_v1("field_tonic")};
        crowded.item_counts = {99};
        sim::EncounterSnapshot snapshot;
        snapshot.units = {crowded};
        const sheet::UnitSheet built =
            sheet::build(snapshot, crowded, nullptr, weapons, abilities, items);
        for (int i = 0; i < built.count; ++i) {
            expect(
                std::strlen(built.line(i)) <=
                    static_cast<std::size_t>(sheet::unit_sheet_columns),
                "every line fits forty columns"
            );
        }
        expect(
            built.count <= sheet::unit_sheet_capacity,
            "the sheet never writes past its capacity"
        );
    }

    // The action points are the snapshot's remaining count for the unit
    // part-way through an activation, and the class's allowance for anyone
    // else. A sheet that always showed the allowance would tell a player they
    // could still act when they could not.
    {
        sim::UnitSnapshot acting = make_archer();
        acting.action_points = 2;
        sim::EncounterSnapshot snapshot;
        snapshot.active_unit_id = acting.id;
        snapshot.remaining_action_points = 1;
        snapshot.units = {acting};
        const sheet::UnitSheet built =
            sheet::build(snapshot, acting, nullptr, weapons, abilities, items);
        expect_line(
            built, 1, "HP 5/8  AP 1  MOV 4  SPD 6",
            "the acting unit's points are what it has left"
        );

        sim::EncounterSnapshot idle;
        idle.active_unit_id = 0;
        idle.remaining_action_points = 0;
        idle.units = {acting};
        const sheet::UnitSheet resting =
            sheet::build(idle, acting, nullptr, weapons, abilities, items);
        expect_line(
            resting, 1, "HP 5/8  AP 2  MOV 4  SPD 6",
            "a unit that is not acting shows the allowance its class gives it"
        );
    }

    // A character with nothing says so, rather than heading an empty list.
    {
        sim::UnitSnapshot bare = make_archer();
        bare.weapon_ids.clear();
        bare.ability_ids.clear();
        bare.item_ids.clear();
        bare.item_counts.clear();
        sim::EncounterSnapshot snapshot;
        snapshot.units = {bare};
        const sheet::UnitSheet built =
            sheet::build(snapshot, bare, nullptr, weapons, abilities, items);
        expect(built.count == 10, "an empty character still fills ten lines");
        expect_line(built, 5, "  NONE", "no weapon reads as none");
        expect_line(built, 7, "  NONE", "no ability reads as none");
        expect_line(built, 9, "  NONE", "no item reads as none");
    }

    // A weapon the encounter's registry does not describe is still the
    // character's weapon. Naming it and admitting the band is unknown is the
    // honest answer; borrowing the band of the weapon in hand would be a number
    // this character has no claim to.
    {
        sim::UnitSnapshot stranger = make_archer();
        stranger.weapon_ids = {core::stable_content_id_v1("warden_blade")};
        sim::EncounterSnapshot snapshot;
        snapshot.units = {stranger};
        const sheet::UnitSheet built =
            sheet::build(
                snapshot, stranger, nullptr, weapons, abilities, items
            );
        expect_line(
            built, 5, "  WARDEN BLADE  RNG ?",
            "an undescribed weapon is named without inventing its band"
        );
    }

    // ---------------------------------------------------------------------
    // The character's own reach, on the rows it belongs to.
    //
    // A weapon row's band is the *character's* band with that weapon: the
    // registry record is the authored bow, shared by everybody carrying one,
    // and the reach bonus is the fact about the archer. A row that printed the
    // record alone would tell a player written to shoot three tiles that she
    // shoots two, on every client that draws a sheet.
    // ---------------------------------------------------------------------

    // Nothing authored, nothing moved. Every unit in every board written before
    // a character could be more than their type has a bonus of zero, and their
    // sheets have to be the sheets they already were, pinned here as the exact
    // strings rather than as a comparison, so that a change to both at once
    // cannot pass by agreeing with itself.
    {
        sim::UnitSnapshot plain = make_archer();
        plain.reach_bonus = 0;
        sim::EncounterSnapshot snapshot;
        snapshot.units = {plain};
        const sheet::UnitSheet built =
            sheet::build(snapshot, plain, nullptr, weapons, abilities, items);
        expect(built.count == 10, "a character with no bonus fills ten lines");
        expect_line(
            built, 5, "  LONG BOW  RNG 2-3  HIT 90%",
            "no bonus leaves a weapon row exactly the weapon's own band"
        );
        expect_line(
            built, 7, "  VOLLEY  RNG 2",
            "and leaves an ability row exactly the ability's own band"
        );
    }

    // A bonus raises the ceiling of every weapon the character carries, and
    // leaves every floor alone. Lowering the floor would let an archer whose
    // band starts at two answer a swordsman standing on top of her, which is
    // the refusal the band's minimum exists to state.
    {
        sim::UnitSnapshot reaching = make_archer();
        reaching.reach_bonus = 2;
        reaching.weapon_ids = {
            core::stable_content_id_v1("long_bow"),
            core::stable_content_id_v1("guard_sword"),
        };
        sim::EncounterSnapshot snapshot;
        snapshot.units = {reaching};
        const sheet::UnitSheet built =
            sheet::build(
                snapshot, reaching, nullptr, weapons, abilities, items
            );
        expect_line(
            built, 5, "  LONG BOW  RNG 2-5  HIT 90%",
            "a bonus widens a weapon row's maximum and not its minimum"
        );
        // The bonus is the character's, not the weapon in hand's: a second
        // carried weapon is widened by the same number, which is the rule the
        // engine applies when a strike names a weapon rather than draws one.
        expect_line(
            built, 6, "  GUARD SWORD  RNG 1-3  HIT 100%",
            "and widens every other weapon the character carries by the same"
        );
        // A band that was one tile wide becomes a range, written the way every
        // other range on the sheet is written rather than as a second spelling.
        expect_line(
            built, 8, "  VOLLEY  RNG 2",
            "and an ability keeps the band the ability itself was authored with"
        );
    }

    // The ceiling saturates rather than wrapping. A bonus that wrapped would
    // hand a player an archer who cannot reach the tile in front of her for
    // having been written to shoot further.
    {
        sim::UnitSnapshot far = make_archer();
        far.reach_bonus = 255;
        sim::EncounterSnapshot snapshot;
        snapshot.units = {far};
        const sheet::UnitSheet built =
            sheet::build(snapshot, far, nullptr, weapons, abilities, items);
        expect_line(
            built, 5, "  LONG BOW  RNG 2-255  HIT 90%",
            "a bonus past the widest band a byte holds stops at 255"
        );
    }

    // A weapon the registry does not describe stays undescribed. The bonus is
    // an addend over a number this sheet does not have, and printing it alone
    // would be the sheet inventing the band it just refused to invent.
    {
        sim::UnitSnapshot stranger = make_archer();
        stranger.reach_bonus = 2;
        stranger.weapon_ids = {core::stable_content_id_v1("warden_blade")};
        sim::EncounterSnapshot snapshot;
        snapshot.units = {stranger};
        const sheet::UnitSheet built =
            sheet::build(
                snapshot, stranger, nullptr, weapons, abilities, items
            );
        expect_line(
            built, 5, "  WARDEN BLADE  RNG ?",
            "a bonus over an unknown band is still an unknown band"
        );
    }

    // The width holds at the far end of everything at once: every stat at its
    // statBlock ceiling, the longest names the shipped tables hold, a weapon
    // band that is already the widest a byte can state, and a bonus that
    // saturates on top of it. Forty columns is what 320 pixels of an
    // eight-pixel font holds, and a line past it is a line a console silently
    // cuts. So the widest sheet the vocabulary can produce is the one that has
    // to be measured, not the widest one the shipped content produces.
    {
        sim::WeaponDefinition longest;
        longest.id = core::stable_content_id_v1("training_sword");
        longest.power = 32767;
        longest.minimum_reach = 254;
        longest.maximum_reach = 255;
        longest.accuracy = 100;
        const std::vector<sim::WeaponDefinition> widest_weapons = {longest};

        sim::AbilityDefinition reaching;
        reaching.id = core::stable_content_id_v1("power_strike");
        reaching.kind = sim::AbilityKind::damage;
        reaching.power = 32767;
        reaching.minimum_reach = 254;
        reaching.maximum_reach = 255;
        const std::vector<sim::AbilityDefinition> widest_abilities = {reaching};

        sim::ItemDefinition tonic;
        tonic.id = core::stable_content_id_v1("field_tonic");
        tonic.kind = sim::ItemKind::restore;
        tonic.power = 32767;
        const std::vector<sim::ItemDefinition> widest_items = {tonic};

        sim::UnitSnapshot crowded = make_archer();
        crowded.unit_type_id = core::stable_content_id_v1("ashen_stormcaller");
        crowded.health = 32767;
        crowded.maximum_health = 32767;
        crowded.movement = 255;
        crowded.action_points = 255;
        crowded.speed = 255;
        crowded.strength = 32767;
        crowded.defense = 32767;
        crowded.resistance = 32767;
        crowded.magic = 32767;
        crowded.skill = 32767;
        crowded.luck = 32767;
        crowded.evasion = 32767;
        crowded.reach_bonus = 255;
        crowded.weapon_ids = {core::stable_content_id_v1("training_sword")};
        crowded.ability_ids = {core::stable_content_id_v1("power_strike")};
        crowded.item_ids = {core::stable_content_id_v1("field_tonic")};
        crowded.item_counts = {65535};

        sim::EncounterSnapshot snapshot;
        snapshot.units = {crowded};
        const sheet::CampaignContext context{65535, 2147483647u};
        const sheet::UnitSheet built = sheet::build(
            snapshot, crowded, "CHARACTER", widest_weapons, widest_abilities,
            widest_items, &context
        );
        for (int i = 0; i < built.count; ++i) {
            expect(
                std::strlen(built.line(i)) <=
                    static_cast<std::size_t>(sheet::unit_sheet_columns),
                "every line of the widest possible sheet fits forty columns"
            );
        }
        expect(
            built.count <= sheet::unit_sheet_capacity,
            "and the widest possible sheet never writes past its capacity"
        );
        // And the widest weapon row is still the whole row rather than a row
        // the composer stopped part-way through: a truncated line would pass
        // the width bound above while saying something the character is not.
        expect_line(
            built, 6, "  TRAINING SWORD  RNG 254-255  HIT 100%",
            "the widest weapon row states a whole band and a whole accuracy"
        );
        expect_line(
            built, 8, "  POWER STRIKE  RNG 254-255",
            "and the widest ability row states the ability's own whole band"
        );
    }

    // Content this build has never met reads as its category rather than as a
    // number, which is the same rule every client's menu already followed.
    {
        expect(
            std::string(sheet::unit_type_name(0)) == "UNIT",
            "an unknown unit type reads as UNIT"
        );
        expect(
            std::string(sheet::weapon_name(0)) == "WEAPON",
            "an unknown weapon reads as WEAPON"
        );
        expect(
            std::string(sheet::ability_name(0)) == "ABILITY",
            "an unknown ability reads as ABILITY"
        );
        expect(
            std::string(sheet::unit_type_name(
                core::stable_content_id_v1("ashen_stormcaller")
            )) == "STORMCALLER",
            "the shipped table is what every client reads"
        );
        // And no unit-type row is a person. `dawn_commander` is an entirely
        // plausible id for a stranger's project, and a row here holding one of
        // this campaign's character names would label their unit type with it.
        // A person's name comes off the campaign roster.
        expect(
            std::string(sheet::unit_type_name(
                core::stable_content_id_v1("dawn_commander")
            )) == "COMMANDER" &&
                std::string(sheet::unit_type_name(
                    core::stable_content_id_v1("ashen_commander")
                )) == "COMMANDER",
            "a unit type is a class and never somebody's name"
        );
    }

    // The resolver, with no package to ask. A client that has none (a fixture,
    // a preview, this test) must get exactly what the table alone gave before
    // the package could be read at all, or every surface would change under a
    // caller that gained nothing.
    {
        expect(
            std::string(
                sheet::unit_type_name(
                    nullptr, core::stable_content_id_v1("dawn_archer")
                ).c_str()
            ) == "ARCHER",
            "with no package the table answers, exactly as it always did"
        );
        expect(
            std::string(sheet::unit_type_name(nullptr, 0).c_str()) == "UNIT" &&
                std::string(sheet::weapon_name(nullptr, 0).c_str()) ==
                    "WEAPON" &&
                std::string(sheet::ability_name(nullptr, 0).c_str()) ==
                    "ABILITY" &&
                std::string(sheet::item_name(nullptr, 0).c_str()) == "ITEM",
            "and content nobody has ever met still reads as its category"
        );
    }

    // The campaign block: what a campaign made of a character, when there is a
    // campaign. Present as a pointer rather than as a defaulted level of one,
    // because "no campaign" and "level one" are different facts and a sheet
    // that spelled them the same would print a nought at every character in
    // every game that has no roster.
    {
        sim::EncounterSnapshot snapshot;
        snapshot.width = 10;
        snapshot.height = 8;
        const sim::UnitSnapshot archer = make_archer();
        snapshot.units = {archer};

        const sheet::UnitSheet without = sheet::build(
            snapshot, archer, "3", weapons, abilities, items
        );
        const sheet::CampaignContext context{7, 640};
        const sheet::UnitSheet with = sheet::build(
            snapshot, archer, "3", weapons, abilities, items, &context
        );

        expect_line(
            with, 1, "LEVEL 7  EXP 640",
            "a character the campaign holds states what it made of them"
        );
        expect(
            with.count == without.count + 1,
            "which is exactly one line more than the same sheet without one"
        );
        // And every other line is where it was, shifted by the one row. A
        // client that drew a fixed number of rows must not find a stat moved
        // out from under it by anything except the block it asked for.
        bool shifted = true;
        for (int index = 1; index < without.count; ++index) {
            if (std::strcmp(without.line(index), with.line(index + 1)) != 0) {
                shifted = false;
            }
        }
        expect(
            shifted && std::strcmp(without.line(0), with.line(0)) == 0,
            "and the rest of the sheet is the sheet it was, one row down"
        );
        expect(
            std::string(without.line(1)).rfind("HP ", 0) == 0,
            "a battle with no campaign behind it states no level at all"
        );
    }

    if (failures == 0) std::cout << "unit sheet: ok\n";
    return failures == 0 ? 0 : 1;
}
