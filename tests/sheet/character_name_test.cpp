// SPDX-License-Identifier: MIT
// Every character on a board has a name, and there is one answer to what it is.
//
// `sheet::character_name` owns the three sources, in order: the campaign's own
// name for a member, the author's own name for a placement, and the character
// type with an ordinal. Every client asks it rather than re-deriving. What
// this file has to earn is that the answer is right and that it is *stable*:
// an ordinal taken from anything that moves during a battle would rename a
// character in the middle of the sentence reporting their death.
//
// The board is authored in JSON and goes through the real source reader, the
// real compiler, the real container and the real loader, because half of what
// is being tested is a package section: a name an author wrote has to survive
// being encoded and read back, and the fold that makes it drawable is applied
// to the bytes rather than to the string this test happens to hold.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/game_content/source_project.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/encounter_loader.hpp>
#include <grandleon/package_runtime/names.hpp>
#include <grandleon/sheet/unit_sheet.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sheet = grandleon::sheet;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_name(
    const sheet::ContentName& got, const char* wanted, std::string_view message
) {
    if (std::string(got.c_str()) != wanted) {
        std::cerr << "FAIL: " << message << "\n  wanted: " << wanted
                  << "\n  got:    " << got.c_str() << '\n';
        ++failures;
    }
}

// The unit standing on a tile, which is how this file names the characters it
// is about: the fixture puts each of them somewhere no two share.
sim::UnitId at(const sim::EncounterSnapshot& snapshot, int x, int y) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.position.x == x && unit.position.y == y) return unit.id;
    }
    return 0;
}

const sim::UnitSnapshot* unit_by_id(
    const sim::EncounterSnapshot& snapshot, sim::UnitId id
) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.id == id) return &unit;
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << (argc > 0 ? argv[0] : "test")
                  << " <character-names.json>\n";
        return 64;
    }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 66;
    }
    const std::string json{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()
    };
    const gc::SourceParseResult parsed = gc::parse_source_project_json(json);
    expect(static_cast<bool>(parsed), "the fixture parses");
    if (!parsed) return 1;
    const gc::CompileResult compiled = gc::compile(parsed.source);
    expect(static_cast<bool>(compiled), "and compiles");
    if (!compiled) return 1;

    const pf::LoadResult loaded = pf::load_mock_package(
        compiled.package,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
    expect(static_cast<bool>(loaded), "and the container reads it back");
    if (!loaded) return 1;

    // The one encounter this fixture authors, found by asking the package
    // rather than by hashing a key this file chose.
    const pf::SectionView* encounters =
        loaded.package.find(pf::SectionType::encounters);
    expect(encounters != nullptr, "the package carries an encounter");
    if (encounters == nullptr || encounters->records.empty()) return 1;
    const pr::EncounterLoadResult board =
        pr::load_encounter(
            loaded.package, encounters->records.front().stable_id
        );
    expect(
        static_cast<bool>(board),
        std::string("and the loader decodes it, error ") +
            std::to_string(static_cast<int>(board.error))
    );
    if (!board) return 1;

    auto created = sim::create_encounter(board.definition);
    expect(static_cast<bool>(created), "and it is valid content");
    if (!created) return 1;
    const sim::EncounterSnapshot snapshot = created.encounter.snapshot();
    expect(snapshot.units.size() == 5U, "five characters stand on it");
    if (snapshot.units.size() != 5U) return 1;

    const sim::UnitId wren = at(snapshot, 0, 0);
    const sim::UnitId captain = at(snapshot, 4, 0);
    const sim::UnitId west = at(snapshot, 2, 1);
    const sim::UnitId middle = at(snapshot, 3, 1);
    const sim::UnitId east = at(snapshot, 4, 1);

    // ---------------------------------------------------------------------
    // 1. The campaign's own name, which only a client can supply and which
    //    outranks everything the package says.
    // ---------------------------------------------------------------------
    expect_name(
        sheet::character_name(
            &loaded.package, snapshot, wren, "Wren Ashdown"
        ),
        "WREN ASHDOWN",
        "a roster member is called what the campaign calls them"
    );
    // Folded, and that is the point of routing it through here at all: the
    // font every console shares has no lower case in it, so a name handed
    // straight to a renderer would be drawn as blanks.
    expect_name(
        sheet::person_name("Wren Ashdown"), "WREN ASHDOWN",
        "and a name a client already holds folds the same way"
    );

    // ---------------------------------------------------------------------
    // 2. The author's own name for a placement, out of the package. This is
    //    the only way anybody on the second side is named: no campaign roster
    //    reaches them.
    // ---------------------------------------------------------------------
    expect_name(
        sheet::character_name(&loaded.package, snapshot, captain),
        "WARDEN KESH",
        "a named placement is called what its author called it"
    );
    expect(
        !pr::content_name(
             loaded.package, pf::SectionType::placement_names, captain
        ).empty(),
        "which is a record in the package rather than anything derived here"
    );
    // And nowhere else. A section holding a record per named placement holds
    // exactly one here, because exactly one placement is named.
    const pf::SectionView* names =
        loaded.package.find(pf::SectionType::placement_names);
    expect(
        names != nullptr && names->records.size() == 1U,
        "one record for the one placement somebody named"
    );

    // ---------------------------------------------------------------------
    // 3. Derived, which is what makes "always" true.
    // ---------------------------------------------------------------------
    // Three levies stand on this board, so each is numbered; the ordinal runs
    // in ascending identity, which is the same order on every machine.
    std::vector<sim::UnitId> levies{west, middle, east};
    std::sort(levies.begin(), levies.end());
    expect_name(
        sheet::character_name(&loaded.package, snapshot, levies[0]),
        "ASHEN LEVY 1", "the first of a kind by identity is numbered one"
    );
    expect_name(
        sheet::character_name(&loaded.package, snapshot, levies[1]),
        "ASHEN LEVY 2", "the second is numbered two"
    );
    expect_name(
        sheet::character_name(&loaded.package, snapshot, levies[2]),
        "ASHEN LEVY 3", "and the third, three"
    );
    // A character nobody shares a type with keeps a plain name. The archer is
    // the only one of hers, so `DAWN ARCHER` and not `DAWN ARCHER 1`. This
    // asks with no campaign name, which is what a rosterless board hands in.
    expect_name(
        sheet::character_name(&loaded.package, snapshot, wren), "DAWN ARCHER",
        "a character unique on the board is named without an ordinal"
    );
    // The authored name wins over the derived one even though the captain
    // would otherwise be unique: a name is not a fallback for an ordinal.
    expect(
        std::string(
            sheet::character_name(&loaded.package, snapshot, captain).c_str()
        ) != "ASHEN CAPTAIN",
        "and an authored name is not overwritten by the type it belongs to"
    );

    // ---------------------------------------------------------------------
    // 4. Stability. The ordinal is counted over everybody the snapshot
    //    carries rather than over whoever is still standing, so a character
    //    keeps their name through the battle that kills them.
    // ---------------------------------------------------------------------
    {
        const sheet::ContentName before =
            sheet::character_name(&loaded.package, snapshot, levies[2]);
        // Strike the first levy down. Health is set on the snapshot rather
        // than fought for, because what is being tested is the naming rule
        // and not the combat that reaches it.
        sim::EncounterSnapshot after = snapshot;
        for (sim::UnitSnapshot& unit : after.units) {
            if (unit.id == levies[0]) unit.health = 0;
        }
        expect(
            !sim::on_board(*unit_by_id(after, levies[0])),
            "the first levy is off the board"
        );
        expect_name(
            sheet::character_name(&loaded.package, after, levies[2]),
            before.c_str(),
            "and nobody behind them is renumbered by it"
        );
    }

    // A unit the snapshot does not carry at all: a stale identity out of an
    // event, which is the one case a client cannot describe.
    expect_name(
        sheet::character_name(&loaded.package, snapshot, 0), "SOMEBODY",
        "an identity the board does not hold is somebody and nothing more"
    );

    // ---------------------------------------------------------------------
    // 5. The class under the name: the unit type's class, not the unit type.
    // ---------------------------------------------------------------------
    const sim::UnitSnapshot* levy = unit_by_id(snapshot, levies[0]);
    const sim::UnitSnapshot* archer = unit_by_id(snapshot, wren);
    const sim::UnitSnapshot* officer = unit_by_id(snapshot, captain);
    expect(
        levy != nullptr && archer != nullptr && officer != nullptr,
        "the three characters the class rows are asked about are on the board"
    );
    if (levy == nullptr || archer == nullptr || officer == nullptr) return 1;
    expect_name(
        sheet::class_name(&loaded.package, archer->unit_type_id), "ARCHER",
        "the class is the author's own word for it"
    );
    // Two unit types, one class. This is the whole reason the class is worth
    // drawing beside a name: `ASHEN LEVY` and `ASHEN CAPTAIN` are two kinds of
    // character with one set of stats and one set of things they can do.
    expect_name(
        sheet::class_name(&loaded.package, levy->unit_type_id), "FOOTMAN",
        "and two unit types that share a class both report it"
    );
    expect_name(
        sheet::class_name(&loaded.package, officer->unit_type_id), "FOOTMAN",
        "including the one with a name of its own"
    );
    expect(
        std::string(sheet::class_name(nullptr, archer->unit_type_id).c_str())
            .empty(),
        "a client with no package has no class to draw and says nothing"
    );
    expect(
        std::string(sheet::class_name(&loaded.package, 0).c_str()).empty(),
        "and neither has one asked about a unit type the package never held"
    );

    // ---------------------------------------------------------------------
    // 6. The sheet every client draws: the name, and the class under it.
    // ---------------------------------------------------------------------
    {
        const sheet::UnitSheet built = sheet::build(
            snapshot, *archer, "Wren Ashdown", board.definition.weapons,
            board.definition.abilities, board.definition.items, nullptr,
            &loaded.package
        );
        expect(
            std::string(built.line(0)) == "WREN ASHDOWN",
            "the sheet opens on who this is"
        );
        expect(
            std::string(built.line(1)) == "ARCHER",
            "and says what kind of character they are on the row beneath"
        );
    }

    if (failures != 0) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "ok\n";
    return 0;
}
