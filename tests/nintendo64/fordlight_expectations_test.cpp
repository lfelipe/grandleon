// SPDX-License-Identifier: MIT
#include <grandleon/campaign_runtime/campaign_runtime.hpp>
#include <grandleon/core/content_identity.hpp>
#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "fordlight_expectations.h"

// What the Fordlight autopilot is allowed to believe about a blow, derived
// here.
//
// The console checks that the Archer's opening shot at an Ashen Knight prices
// itself the way the rules say it does. A console assertion is only evidence if
// the number it compares against was written somewhere a person could not
// quietly adjust to make a run go green, and where a rule change breaks it in
// the ordinary gate rather than in a console run nobody is required to make.
// This is that somewhere.
//
// It exists because the numbers used to be literals in `play_rom.cpp`. The
// weapon triangle shipped, a blade became strong against a bow, the shot went
// from `95% HIT 3 LEFT 9` to `80% HIT 2 LEFT 10`, and the console check failed
// from that commit onward with nothing reporting it -- no automated build
// compiles either console port, and the run that would have caught it is
// opt-in. This test runs in the default gate and takes no emulator at all.
//
// The pairing is found by what the two characters are holding rather than by
// identifier or by square, so the derivation still describes itself when the
// board is edited, and fails loudly rather than silently picking somebody else.

namespace core = grandleon::core;
namespace cr = grandleon::campaign_runtime;
namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace expect_ns = grandleon::tarnholt;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

pf::LoadedPackage compile_tarnholt() {
    const std::string filename =
        std::string(GRANDLEON_SOURCE_DIR) + "/games/tarnholt/source/project.json";
    std::ifstream input(filename, std::ios::binary);
    expect(static_cast<bool>(input), "the Tarnholt project opens");
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "and parses");
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package, {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and loads");
    return loaded.package;
}

// Which kind of weapon a character has in hand on this board. Zero when they
// carry nothing the encounter defines, which is what makes the searches below
// refuse to match rather than match something arbitrary.
sim::ContentId kind_in_hand(
    const sim::EncounterDefinition& board,
    const sim::UnitDefinition& unit
) {
    if (unit.weapon_ids.empty()) return 0;
    for (const sim::WeaponDefinition& weapon : board.weapons) {
        if (weapon.id == unit.weapon_ids.front()) return weapon.weapon_type;
    }
    return 0;
}

}  // namespace

int main() {
    const pf::LoadedPackage package = compile_tarnholt();
    if (failures != 0) return 1;

    // The shipped campaign, its Fordlight node, and the board fought there --
    // through the same three lookups the session makes, so this is the board
    // the ROM actually loads rather than a re-authored copy of it.
    const std::uint64_t campaign_id =
        core::stable_content_id_v1("tarnholt_line");
    const pr::CampaignLoadResult campaign =
        pr::load_campaign(package, campaign_id);
    expect(static_cast<bool>(campaign), "the Tarnholt campaign loads");
    if (!campaign) return 1;

    const std::uint64_t encounter_id = cr::encounter_of_node(
        campaign.definition,
        cr::campaign_node_ref(
            package.game_id, core::stable_content_id_v1("fordlight_battle")
        )
    );
    expect(encounter_id != 0, "and the Fordlight node names a board");
    if (encounter_id == 0) return 1;

    const pr::EncounterLoadResult board = pr::load_encounter(package, encounter_id);
    expect(static_cast<bool>(board), "and the Fordlight board decodes");
    if (!board) return 1;

    // The two the checkpoint is about, found by what they are holding. The
    // whole point of the numbers below is that a bow is being fired at a blade,
    // so that is what the search asks for.
    const sim::ContentId blade = core::stable_content_id_v1("blade");
    const sim::ContentId bow = core::stable_content_id_v1("bow");
    const sim::UnitDefinition* archer = nullptr;
    const sim::UnitDefinition* knight = nullptr;
    for (const sim::UnitDefinition& unit : board.definition.units) {
        const sim::ContentId kind = kind_in_hand(board.definition, unit);
        if (archer == nullptr && unit.side == sim::Side::first && kind == bow) {
            archer = &unit;
        }
        if (knight == nullptr && unit.side == sim::Side::second &&
            kind == blade) {
            knight = &unit;
        }
    }
    expect(archer != nullptr, "somebody on the company carries a bow");
    expect(knight != nullptr, "and somebody of the Coil carries a blade");
    if (archer == nullptr || knight == nullptr) return 1;

    // The table has to actually name this pairing, or the constants below would
    // be pinning a rule that never fires and would go on passing after somebody
    // deleted the edge.
    bool blade_beats_bow = false;
    for (const sim::WeaponTypeDefinition& type : board.definition.weapon_types) {
        if (type.id != blade) continue;
        for (const sim::ContentId beaten : type.strong_against) {
            if (beaten == bow) blade_beats_bow = true;
        }
    }
    expect(
        blade_beats_bow,
        "and the shipped table makes a blade strong against a bow"
    );

    // Priced from a board where the two stand exactly the bow's own shortest
    // shot apart, read off the weapon rather than assumed: a bow has a minimum
    // reach as well as a maximum, and standing them next to each other prices
    // nothing at all. Everything else about the board is the shipped one.
    const sim::UnitId archer_id = archer->id;
    const sim::UnitId knight_id = knight->id;
    std::int16_t shot = 1;
    for (const sim::WeaponDefinition& weapon : board.definition.weapons) {
        if (weapon.id != archer->weapon_ids.front()) continue;
        shot = static_cast<std::int16_t>(weapon.minimum_reach);
    }
    expect(shot >= 1, "the bow states a reach it can be fired at");

    sim::EncounterDefinition apart = board.definition;
    for (sim::UnitDefinition& unit : apart.units) {
        if (unit.id == archer_id) unit.position = {1, 1};
        if (unit.id == knight_id) unit.position = {
            static_cast<std::int16_t>(1 + shot), 1
        };
    }
    // Anybody else standing on those two squares is moved out of the way rather
    // than left to collide, which `create_encounter` would refuse.
    std::int16_t parked = 0;
    for (sim::UnitDefinition& unit : apart.units) {
        if (unit.id == archer_id || unit.id == knight_id) continue;
        if (unit.position.y == 1 &&
            (unit.position.x == 1 || unit.position.x == 1 + shot)) {
            unit.position = {parked++, 0};
        }
    }
    sim::EncounterDefinition& adjacent = apart;

    auto created = sim::create_encounter(adjacent);
    expect(static_cast<bool>(created), "the Fordlight board opens");
    if (!created) return 1;

    const sim::AttackForecast forecast =
        sim::forecast_attack(created.encounter.snapshot(), archer_id, knight_id);
    expect(static_cast<bool>(forecast), "and the bow's shot prices itself");

    expect(
        forecast.damage == expect_ns::bow_at_knight_damage,
        "the header's damage is what the rules answer"
    );
    expect(
        forecast.hit_chance == expect_ns::bow_at_knight_chance,
        "the header's chance is what the rules answer"
    );
    expect(
        forecast.lethal == expect_ns::bow_at_knight_lethal,
        "and the header agrees about whether it fells"
    );
    // The health after is checked against the rules and against the knight's
    // own line, so the constant cannot drift with the character's health.
    expect(
        forecast.target_health_after == expect_ns::bow_at_knight_health_after &&
            forecast.target_health_after ==
                knight->health - expect_ns::bow_at_knight_damage,
        "and about what the knight is left standing on"
    );

    // And the mark the bar leads with, which is the whole reason a player can
    // see any of this. A shot into a blade leans down.
    expect(
        forecast.lean == sim::WeaponLean::disadvantage,
        "and the shot leans the way the table says it does"
    );

    // ------------------------------------------------------------------
    // And the blow that ends the Fordlight run: the mage's staff into the
    // knight the line has worn down to three.
    //
    // The other side of the same table. A staff is strong against a blade, so
    // this one is struck *with* the advantage where the bow's was struck into
    // it, and the rule that cost the company its knights is the rule that
    // finishes this knight.
    const sim::ContentId staff = core::stable_content_id_v1("staff");
    const sim::UnitDefinition* mage = nullptr;
    for (const sim::UnitDefinition& unit : board.definition.units) {
        if (unit.side != sim::Side::first) continue;
        if (kind_in_hand(board.definition, unit) != staff) continue;
        mage = &unit;
        break;
    }
    expect(mage != nullptr, "somebody on the company carries a staff");

    bool staff_beats_blade = false;
    for (const sim::WeaponTypeDefinition& type : board.definition.weapon_types) {
        if (type.id != staff) continue;
        for (const sim::ContentId beaten : type.strong_against) {
            if (beaten == blade) staff_beats_blade = true;
        }
    }
    expect(
        staff_beats_blade,
        "and the shipped table makes a staff strong against a blade"
    );

    if (mage != nullptr && staff_beats_blade) {
        // Worn to three and standing next to the mage, which is where the run
        // leaves the two of them.
        sim::EncounterDefinition endgame = board.definition;
        const sim::UnitId mage_id = mage->id;
        std::int16_t moved = 0;
        for (sim::UnitDefinition& unit : endgame.units) {
            if (unit.id == mage_id) {
                unit.position = {1, 1};
            } else if (unit.id == knight_id) {
                unit.position = {1, 2};
                unit.health = expect_ns::worn_knight_health;
            } else if ((unit.position.x == 1 && unit.position.y == 1) ||
                       (unit.position.x == 1 && unit.position.y == 2)) {
                unit.position = {moved++, 0};
            }
        }
        auto ending = sim::create_encounter(endgame);
        expect(static_cast<bool>(ending), "the endgame board opens");
        if (ending) {
            const sim::AttackForecast blow = sim::forecast_attack(
                ending.encounter.snapshot(), mage_id, knight_id
            );
            expect(static_cast<bool>(blow), "and the staff's blow prices itself");
            expect(
                blow.damage == expect_ns::staff_at_worn_knight_damage,
                "the header's staff damage is what the rules answer"
            );
            expect(
                blow.lethal == expect_ns::staff_at_worn_knight_lethal,
                "and the header agrees that it fells"
            );
            expect(
                blow.lean == sim::WeaponLean::advantage,
                "and that the staff strikes with the advantage, not into it"
            );
        }
    }

    if (failures == 0) {
        std::cout << "fordlight expectations: the bow is priced against the "
                     "blade it meets\n";
    }
    return failures == 0 ? 0 : 1;
}
