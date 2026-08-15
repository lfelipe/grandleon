// SPDX-License-Identifier: MIT
// The one thing in a package that belongs to the game rather than to its
// content: the name its author gave it.
//
// This is a small reader and the test is small with it, but the four things it
// proves are the four the consoles now stand on. A title survives the round
// trip from an authored project to a package to a screen. It is *borrowed*
// rather than copied, which is the property that lets a 64 KiB machine reading
// its cartridge in place ask the question at all. A package that does not carry
// one answers "no name" rather than failing. And a record that is present but
// damaged answers the same way rather than reading past the bytes it was
// given. On a console that is the difference between an unnamed screen and a
// cartridge reading its own program out as a title.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_runtime/manifest.hpp>
#include <grandleon/package_runtime/names.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace gc = grandleon::game_content;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

gc::GameSource source(const std::string& title) {
    gc::GameSource value;
    value.game_id[0] = 0x4DU;
    value.title = title;
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {{30, "Line", {6, 4, 1, 0, 3}, {10}}};
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{50, "Tonic", 20, 1}};
    value.unit_types = {{60, "Trainee", 30, 0, {40}, {50}}};
    return value;
}

pf::LoadResult load(const std::vector<std::uint8_t>& bytes) {
    return pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
}

// The whole point, stated once: a project's own name reaches a reader that has
// nothing but the package.
void a_package_names_its_game() {
    const auto compiled = gc::compile(source("The Orchard Road"));
    expect(static_cast<bool>(compiled), "a named project compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "the package loads");
    expect(
        pr::project_title(loaded.package) == "The Orchard Road",
        "the package carries the name its author gave it"
    );
}

// Case and spacing are the author's, not the reader's. A console that wants
// capitals folds them where it draws, because the machine that can only draw
// capitals is not the only machine reading this.
void the_name_is_the_authors_own() {
    const auto compiled = gc::compile(source("a lower case name"));
    const auto loaded = load(compiled.package);
    expect(
        pr::project_title(loaded.package) == "a lower case name",
        "the reader folds nothing"
    );
}

// Borrowed, not copied. The view has to point *into* the package, because the
// cartridge loads its package in place out of ROM and a reader that
// allocated a copy of every title would be a reader that machine could not
// afford to call.
void the_name_is_borrowed_from_the_package() {
    const auto compiled = gc::compile(source("Borrowed"));
    const auto loaded = load(compiled.package);
    const std::string_view title = pr::project_title(loaded.package);
    const auto* begin = reinterpret_cast<const std::uint8_t*>(title.data());
    const std::uint8_t* first = loaded.package.byte_data();
    expect(
        begin >= first && begin + title.size() <= first + loaded.package.byte_size(),
        "the title is a window onto the package's own bytes"
    );
}

// A package with no manifest section at all. Not an error and not a guess: a
// screen with no name to draw draws no name.
void a_package_without_a_manifest_is_unnamed() {
    pf::PackageSource bare;
    bare.game_id[0] = 0x4DU;
    bare.content_revision = 1;
    bare.required_engine = {{0, 1, 0}, {0, 1, 99}};
    bare.target = pf::TargetProfile::portable;
    const auto loaded = load(pf::write_mock_package(bare));
    expect(static_cast<bool>(loaded), "a package with no sections loads");
    expect(
        pr::project_title(loaded.package).empty(),
        "and names nothing rather than failing"
    );
}

// A record whose stated length runs past the record. The refusal that matters:
// a reader that believed the length would hand a console a view over bytes
// belonging to the next record, or to no record at all.
void a_damaged_record_is_refused() {
    pf::PackageSource damaged;
    damaged.game_id[0] = 0x4DU;
    damaged.content_revision = 1;
    damaged.required_engine = {{0, 1, 0}, {0, 1, 99}};
    damaged.target = pf::TargetProfile::portable;
    damaged.sections = {
        pf::SectionSource{
            pf::SectionType::manifest,
            1,
            0,
            pf::section_flag_required,
            {
                // Says forty bytes of name and carries four.
                pf::RecordSource{
                    pf::manifest_title_record_id, {0x28, 0x00, 'N', 'a', 'm', 'e'}
                }
            }
        }
    };
    const auto loaded = load(pf::write_mock_package(damaged));
    expect(static_cast<bool>(loaded), "the damaged package still loads");
    expect(
        pr::project_title(loaded.package).empty(),
        "a length that runs past its record names nothing"
    );

    // And the smaller damage beneath it: a record too short to hold even the
    // two bytes that state a length.
    damaged.sections[0].records[0].payload = {0x05};
    const auto stub = load(pf::write_mock_package(damaged));
    expect(
        pr::project_title(stub.package).empty(),
        "a record too short to state a length names nothing"
    );
}

// The compiler and the reader have to agree about which record the name is in,
// and a disagreement would be silent: the reader would find no record and every
// cartridge would go unnamed.
void the_two_sides_agree_on_the_record() {
    const auto compiled = gc::compile(source("Agreed"));
    const auto loaded = load(compiled.package);
    expect(
        loaded.package.find(
            pf::SectionType::manifest, pf::manifest_title_record_id
        ) != nullptr,
        "the compiler wrote the record the reader looks for"
    );
}

// The same decoder, pointed at content rather than at the project.
//
// Every content section writes its definition's display name as the record's
// first field, so one reader serves all four. This is the accessor a console
// asks when somebody goes down, and the reason it exists is that until it did,
// every client resolved a content identity through a hardcoded table of the
// shipped projects' ids and answered `UNIT` for anything else.
void a_package_names_its_content() {
    const auto compiled = gc::compile(source("Named content"));
    const auto loaded = load(compiled.package);
    expect(
        pr::content_name(
            loaded.package, pf::SectionType::unit_types, 60
        ) == "Trainee",
        "a unit type is called what its author called it"
    );
    expect(
        pr::content_name(loaded.package, pf::SectionType::weapons, 40) ==
            "Sword",
        "and so is a weapon"
    );
    expect(
        pr::content_name(loaded.package, pf::SectionType::items, 50) == "Tonic",
        "and so is an item"
    );
    // An identity the package does not hold, and a section it does not carry.
    // Both answer "no name" rather than a neighbour's, because a lookup that
    // fell through to the next record would put one character's name over
    // another's body.
    expect(
        pr::content_name(loaded.package, pf::SectionType::unit_types, 61)
            .empty(),
        "a unit type the package does not hold has no name"
    );
    expect(
        pr::content_name(loaded.package, pf::SectionType::abilities, 60)
            .empty(),
        "and neither does a section the package does not carry"
    );
}

// The title and a content name are the same decode, so they had better agree.
// The title record is the manifest's, and its payload is a counted string at
// offset zero exactly as a unit type's name is.
void the_title_is_a_name_like_any_other() {
    const auto compiled = gc::compile(source("One Decoder"));
    const auto loaded = load(compiled.package);
    expect(
        pr::project_title(loaded.package) ==
            pr::content_name(
                loaded.package,
                pf::SectionType::manifest,
                pf::manifest_title_record_id
            ),
        "the project's title reads as the manifest record's leading string"
    );
}

}  // namespace

int main() {
    a_package_names_its_game();
    a_package_names_its_content();
    the_title_is_a_name_like_any_other();
    the_name_is_the_authors_own();
    the_name_is_borrowed_from_the_package();
    a_package_without_a_manifest_is_unnamed();
    a_damaged_record_is_refused();
    the_two_sides_agree_on_the_record();
    return failures == 0 ? 0 : 1;
}
