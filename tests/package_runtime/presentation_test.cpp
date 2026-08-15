// SPDX-License-Identifier: MIT
// The authored presentation choices a package carries, from the compiler that
// writes them to the reader a client asks.
//
// Three things are worth proving separately: that a choice survives the round
// trip, that a package written before the section existed still loads and
// resolves to what it drew then, and that a section which is present but
// damaged is refused rather than half-believed.
//
// The same three, one record at a time. The section carries up to five records
// now: the project's theme and faction colours, the terrain join, the
// archetype join, and the two drawing joins that say whose hand drew each
// archetype and at what build. And "written before this existed" is a real
// state for each of them separately, not only for the section as a whole. The
// last two go further and are absent from most packages by design: a game
// where every character follows the game's own choices writes neither.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_runtime/presentation.hpp>

#include <algorithm>
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

// The reader's own constants and the compiler's have to agree, and a mismatch
// would be silent: the compiler writes an index and the reader hands it
// straight to a client that indexes art with it.
static_assert(
    pr::default_theme == gc::default_theme,
    "the reader and the compiler disagree about the default theme"
);
static_assert(
    pr::colour_unresolved == gc::faction_colour_unchosen,
    "the reader and the compiler disagree about an unresolved colour"
);
// The opposite kind of agreement, and the one that matters here: "the art
// library matched no keyword" is a value the compiler carries, and "this
// package says nothing about that identity" is a fact about the package. A
// client that saw one value for both would draw the wrong fallback for one.
static_assert(
    pr::terrain_kind_unresolved != gc::terrain_kind_unknown,
    "an unresolved terrain kind is not an unknown terrain name"
);
static_assert(
    gc::terrain_kind_unknown < pr::terrain_kind_unresolved &&
        gc::archetype_count < pr::archetype_unresolved,
    "the unresolved markers sit outside the art library's index ranges"
);

gc::GameSource source() {
    gc::GameSource value;
    value.game_id[0] = 0x50U;
    value.title = "Presentation slice";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {
        // Spells no archetype, so a unit type on it falls back to its own name
        // and then to the roster's first.
        {30, "Line", {6, 4, 1, 0, 3}, {10}},
        {31, "Royal Archer", {5, 3, 1, 0, 4}, {10}},
    };
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{50, "Tonic", 20, 1}};
    value.unit_types = {
        {60, "Chosen unit", 30, 80, {40}, {50}},
        {61, "Positional unit", 30, 81, {40}, {}},
        // Names no faction at all, so nothing claims its colour.
        {62, "Unclaimed unit", 30, 0, {40}, {}},
        // The class spells the archetype.
        {63, "Elyse", 31, 80, {40}, {}},
        // The class spells none, so its own name is asked.
        {64, "Rogue runner", 30, 80, {40}, {}},
    };
    // Three terrain identities across twelve cells: one the art library draws
    // as water, one it draws as grass, and one whose authored name matches no
    // keyword the library publishes.
    gc::Map field;
    field.id = 70;
    field.name = "Field";
    field.width = 4;
    field.height = 3;
    for (int cell = 0; cell < 12; ++cell) {
        const auto choice = static_cast<std::uint64_t>(cell % 3);
        field.terrain.push_back(900 + choice);
        field.terrain_kinds.push_back(
            choice == 0   ? gc::terrain_kind_index("river")
            : choice == 1 ? gc::terrain_kind_index("meadow grass")
                          : gc::terrain_kind_unknown
        );
    }
    value.maps = {field};
    // The first faction chose amber; the second chose nothing and therefore
    // takes the menu colour at its own position, which is red.
    value.factions = {
        {80, "Amber Company", gc::faction_colour_index("amber")},
        {81, "Second Company"},
    };
    return value;
}

pf::LoadResult load(const std::vector<std::uint8_t>& bytes) {
    return pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
}

void carries_authored_choices() {
    auto authored = source();
    authored.theme = gc::theme_index("winter");
    expect(authored.theme < gc::theme_count, "winter is on the theme menu");

    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "a themed project compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "a themed package loads");

    const auto presentation = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(presentation), "the presentation section decodes");
    expect(
        presentation.presentation.theme == gc::theme_index("winter"),
        "the authored theme survives compilation"
    );
    expect(
        presentation.presentation.colour_of_faction(80) ==
            gc::faction_colour_index("amber"),
        "a faction's chosen colour survives compilation"
    );
    expect(
        presentation.presentation.colour_of_faction(81) == 1,
        "a faction that chose nothing takes the colour at its own position"
    );
    expect(
        presentation.presentation.colour_of_faction(999) ==
            pr::colour_unresolved,
        "a faction the package does not hold claims no colour"
    );
    expect(
        presentation.presentation.colour_of_unit_type(60) ==
            gc::faction_colour_index("amber"),
        "a unit type wears its faction's chosen colour"
    );
    expect(
        presentation.presentation.colour_of_unit_type(61) == 1,
        "a unit type wears its faction's positional colour"
    );
    expect(
        presentation.presentation.colour_of_unit_type(62) ==
            pr::colour_unresolved,
        "a unit type naming no faction wears no colour"
    );
    expect(
        presentation.presentation.colour_of_unit_type(999) ==
            pr::colour_unresolved,
        "a unit type the package does not hold wears no colour"
    );
}

// The joins a package could not carry before: what a cell draws as, and which
// archetype a unit type wears. Both are resolved by the compiler, because a
// cell's identity is a hash of the authored name and no client can recover the
// name to match a keyword against it.
void carries_the_content_joins() {
    const auto compiled = gc::compile(source());
    expect(static_cast<bool>(compiled), "a project with terrain compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "its package loads");

    const auto result = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(result), "the content joins decode");
    const pr::Presentation& shown = result.presentation;

    expect(
        shown.kind_of_terrain(900) == gc::terrain_kind_index("water"),
        "a map named river draws as water"
    );
    expect(
        shown.kind_of_terrain(901) == gc::terrain_kind_index("grass"),
        "a name the library matches by a word inside it draws as that kind"
    );
    expect(
        shown.kind_of_terrain(902) == gc::terrain_kind_unknown,
        "a name the library matches nowhere is carried as unknown, not as "
        "unresolved"
    );
    expect(
        shown.kind_of_terrain(903) == pr::terrain_kind_unresolved,
        "an identity the package does not carry is unresolved"
    );
    expect(
        shown.terrain.size() == 3,
        "the join holds one entry per distinct identity, not one per cell"
    );
    for (std::size_t index = 1; index < shown.terrain.size(); ++index) {
        expect(
            shown.terrain[index - 1].terrain_id <
                shown.terrain[index].terrain_id,
            "the terrain join is sorted so a client can search it"
        );
    }

    expect(
        shown.archetype_of_unit_type(63) == gc::archetype_index("archer"),
        "a unit type whose class spells an archetype wears it"
    );
    expect(
        shown.archetype_of_unit_type(64) == gc::archetype_index("rogue"),
        "a unit type spells its own archetype when its class does not"
    );
    expect(
        shown.archetype_of_unit_type(60) == gc::archetype_default,
        "a unit type spelling no archetype wears the roster's first"
    );
    expect(
        shown.archetype_of_unit_type(999) == pr::archetype_unresolved,
        "a unit type the package does not hold wears no archetype"
    );
    expect(
        shown.archetypes.size() == 5,
        "every unit type the package holds is in the archetype join"
    );
}

// The style join, which exists only when a character asks for one. The three
// things worth proving are the three the shape rests on: that a game where
// nobody overrides writes no record at all and every unit type therefore reads
// as "unchanged"; that a game where somebody does writes every unit type
// resolved, so a client needs nothing else to answer; and that the record's
// presence is the only difference: a project that authors no override still
// compiles to a package with three presentation records, which is what keeps
// every game written before this field byte-identical.
void carries_a_characters_own_style() {
    const auto plain = gc::compile(source());
    expect(static_cast<bool>(plain), "a project with no override compiles");
    const auto plain_loaded = load(plain.package);
    const auto plain_shown = pr::load_presentation(plain_loaded.package);
    expect(static_cast<bool>(plain_shown), "and its presentation decodes");
    expect(
        plain_shown.presentation.character_styles.empty(),
        "a game every character of which follows the game's style carries no "
        "style join at all"
    );
    expect(
        plain_shown.presentation.character_style_of_unit_type(60) ==
            pr::character_style_unresolved,
        "so every unit type in it reads as 'draw it the way you were going to '"
        "draw the game', which is what a client did before this record existed"
    );
    expect(
        plain_shown.presentation.character_figures.empty() &&
            plain_shown.presentation.character_figure_of_unit_type(60) ==
                pr::character_figure_unresolved,
        "and the same of the figure axis beside it"
    );
    expect(
        plain_loaded.package.find(
            pf::SectionType::presentation,
            pf::presentation_style_record_id
        ) == nullptr &&
            plain_loaded.package.find(
                pf::SectionType::presentation,
                pf::presentation_figure_record_id
            ) == nullptr,
        "and the records are absent rather than empty, so the package costs "
        "not one byte for a choice nobody made"
    );

    // One character raised in another setting's style, in a game that names
    // none of its own. The rest of the roster follows the game.
    gc::GameSource raised = source();
    raised.unit_types[1].character_style = gc::character_style_index("undead");
    const auto compiled = gc::compile(raised);
    expect(static_cast<bool>(compiled), "a mixed-style project compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "its package loads");
    const auto result = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(result), "the style join decodes");
    const pr::Presentation& shown = result.presentation;

    expect(
        shown.character_style_of_unit_type(61) ==
            gc::character_style_index("undead"),
        "a character that names a style is drawn in it"
    );
    expect(
        shown.character_style_of_unit_type(60) == gc::default_character_style,
        "and the character beside it is drawn in the game's, resolved rather "
        "than left for a client to work out"
    );
    expect(
        shown.character_style_of_unit_type(999) ==
            pr::character_style_unresolved,
        "a unit type the package does not hold wears no style"
    );
    expect(
        shown.character_styles.size() == raised.unit_types.size(),
        "the join holds every unit type, so the answer never depends on a "
        "byte the package does not carry"
    );
    for (std::size_t index = 1; index < shown.character_styles.size(); ++index) {
        expect(
            shown.character_styles[index - 1].unit_type_id <
                shown.character_styles[index].unit_type_id,
            "the style join is sorted so a client can search it"
        );
    }
    expect(
        shown.archetype_of_unit_type(61) ==
            plain_shown.presentation.archetype_of_unit_type(61),
        "and a character's style changes no archetype: the roster is closed, "
        "so the same role is drawn by another hand rather than becoming "
        "another role"
    );

    // The two axes are independent, and the records are the proof: a game that
    // authors a style and no figure carries the style join alone, so an author
    // pays for the axis they used and not for the one they did not.
    expect(
        loaded.package.find(
            pf::SectionType::presentation,
            pf::presentation_figure_record_id
        ) == nullptr,
        "dressing a character in another setting's style writes no figure join"
    );

    // The figure axis, authored on its own. One character drawn at the second
    // build in a game that names no figure, which is the female mage and the
    // animal archers case: the roster is otherwise untouched.
    gc::GameSource narrowed = source();
    narrowed.unit_types[2].character_figure =
        gc::character_figure_index("second");
    const auto figured = pr::load_presentation(
        load(gc::compile(narrowed).package).package
    );
    expect(
        figured.presentation.character_figure_of_unit_type(62) ==
            gc::character_figure_index("second"),
        "a character that names a figure is drawn with it"
    );
    expect(
        figured.presentation.character_figure_of_unit_type(60) ==
            gc::default_character_figure,
        "and the character beside it is drawn with the game's"
    );
    expect(
        figured.presentation.character_styles.empty(),
        "and choosing a figure writes no style join: the two axes are chosen "
        "independently and are paid for independently"
    );

    // Both axes on one character, which is what a mixed roster needs: a mage at
    // the second build in one setting's style, beside archers in another.
    gc::GameSource both = source();
    both.character_figure = gc::character_figure_index("second");
    both.unit_types[3].character_style = gc::character_style_index("nature");
    both.unit_types[3].character_figure = gc::character_figure_index("first");
    const auto paired = pr::load_presentation(
        load(gc::compile(both).package).package
    );
    expect(
        paired.presentation.character_style_of_unit_type(63) ==
                gc::character_style_index("nature") &&
            paired.presentation.character_figure_of_unit_type(63) ==
                gc::character_figure_index("first") &&
            paired.presentation.character_figure_of_unit_type(60) ==
                gc::character_figure_index("second"),
        "a character may step out of the game on both axes at once, and the "
        "characters beside it follow the game on both"
    );

    // A game that names a style of its own, with one character stepping out of
    // it. The record then carries two different styles and neither is the
    // library's default, which is the case that would expose a reader assuming
    // the project's style is medieval.
    gc::GameSource sci_fi = raised;
    sci_fi.character_style = gc::character_style_index("scifi");
    const auto mixed = pr::load_presentation(
        load(gc::compile(sci_fi).package).package
    );
    expect(
        mixed.presentation.character_style_of_unit_type(60) ==
                gc::character_style_index("scifi") &&
            mixed.presentation.character_style_of_unit_type(61) ==
                gc::character_style_index("undead"),
        "a game's style is a default the characters that name none follow, and "
        "never a gate on the one that names another"
    );
}

void an_unthemed_project_is_the_default() {
    const auto compiled = gc::compile(source());
    const auto loaded = load(compiled.package);
    const auto presentation = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(presentation), "an unthemed package decodes");
    expect(
        presentation.presentation.theme == pr::default_theme,
        "a project naming no theme is drawn in the default theme"
    );
}

// A package with no presentation section at all: what every package written
// before this section existed looks like. It must load and resolve to what a
// client drew then, not to an error.
void an_absent_section_is_not_an_error() {
    pf::PackageSource package;
    package.content_revision = 1;
    package.required_engine = {{0, 1, 0}, {0, 1, 99}};
    package.sections = {
        pf::SectionSource{
            pf::SectionType::manifest,
            1,
            0,
            pf::section_flag_required,
            {pf::RecordSource{1, {0, 0}}}
        }
    };
    const auto loaded = load(pf::write_mock_package(package));
    expect(
        static_cast<bool>(loaded),
        "a package with no presentation section still loads"
    );

    const auto presentation = pr::load_presentation(loaded.package);
    expect(
        static_cast<bool>(presentation),
        "an absent presentation section is not an error"
    );
    expect(
        presentation.presentation.theme == pr::default_theme,
        "an absent section resolves to the default theme"
    );
    expect(
        presentation.presentation.factions.empty() &&
            presentation.presentation.unit_types.empty(),
        "an absent section claims no colours"
    );
    expect(
        presentation.presentation.terrain.empty() &&
            presentation.presentation.archetypes.empty(),
        "an absent section carries no content joins"
    );
    expect(
        presentation.presentation.kind_of_terrain(900) ==
                pr::terrain_kind_unresolved &&
            presentation.presentation.archetype_of_unit_type(60) ==
                pr::archetype_unresolved,
        "an absent section resolves every identity to unresolved"
    );
}

// Builds a package whose presentation section holds exactly the records given,
// so a damaged payload can be handed to the reader without damaging anything
// else.
pf::LoadResult package_with_records(
    std::vector<pf::RecordSource> records
) {
    pf::PackageSource package;
    package.content_revision = 1;
    package.required_engine = {{0, 1, 0}, {0, 1, 99}};
    package.sections = {
        pf::SectionSource{
            pf::SectionType::manifest,
            1,
            0,
            pf::section_flag_required,
            {pf::RecordSource{1, {0, 0}}}
        },
        pf::SectionSource{
            pf::SectionType::presentation,
            1,
            0,
            0,
            std::move(records)
        }
    };
    return load(pf::write_mock_package(package));
}

pf::LoadResult package_with_presentation(
    const std::vector<std::uint8_t>& payload
) {
    return package_with_records(
        {pf::RecordSource{pf::presentation_record_id, payload}}
    );
}

// A project record with no factions, so a join record can be damaged without
// the record beside it also being wrong.
const std::vector<std::uint8_t> empty_project{0, 0, 0};

// The reader's verdict on a join record holding exactly `payload`.
pr::PresentationError join_verdict(
    std::uint64_t record_id,
    const std::vector<std::uint8_t>& payload
) {
    const auto loaded = package_with_records(
        {
            pf::RecordSource{pf::presentation_record_id, empty_project},
            pf::RecordSource{record_id, payload}
        }
    );
    return pr::load_presentation(loaded.package).error;
}

// A package written after the presentation section existed and before the
// content joins did: the section is there and holds the project record alone.
// Every such package must keep loading, and must resolve to what its client
// drew then rather than to an error.
void a_package_written_before_the_joins() {
    const auto compiled = gc::compile(source());
    const auto complete = load(compiled.package);
    expect(static_cast<bool>(complete), "the current package loads");

    // The same project record, alone in the section, exactly as the compiler
    // wrote it before the joins were appended beside it.
    const pf::RecordView* project = complete.package.find(
        pf::SectionType::presentation, pf::presentation_record_id
    );
    expect(project != nullptr, "the project record is there to copy");
    if (project == nullptr) return;
    const std::vector<std::uint8_t> payload(
        complete.package.bytes.begin() + project->payload_offset,
        complete.package.bytes.begin() + project->payload_offset +
            project->payload_size
    );

    const auto older = package_with_presentation(payload);
    expect(
        static_cast<bool>(older),
        "a package carrying only the project record loads"
    );
    const auto result = pr::load_presentation(older.package);
    expect(
        static_cast<bool>(result),
        "a presentation section with no content joins is not an error"
    );
    expect(
        result.presentation.theme == pr::default_theme &&
            result.presentation.colour_of_faction(80) ==
                gc::faction_colour_index("amber"),
        "the choices such a package did carry are still read"
    );
    expect(
        result.presentation.kind_of_terrain(900) ==
                pr::terrain_kind_unresolved &&
            result.presentation.archetype_of_unit_type(63) ==
                pr::archetype_unresolved,
        "an older package resolves every content identity to unresolved"
    );
}

void a_damaged_section_is_refused() {
    // A header that claims one faction, and a faction entry that stops after
    // six of its eight identity bytes.
    const std::vector<std::uint8_t> truncated{
        0, 1, 0, 1, 2, 3, 4, 5, 6
    };
    const auto short_load = package_with_presentation(truncated);
    expect(
        static_cast<bool>(short_load),
        "the container still accepts a package with a short presentation record"
    );
    const auto short_result = pr::load_presentation(short_load.package);
    expect(
        short_result.error == pr::PresentationError::malformed_payload,
        "a truncated presentation payload is refused"
    );
    expect(
        short_result.presentation.theme == pr::default_theme &&
            short_result.presentation.factions.empty(),
        "a refused payload publishes nothing"
    );

    // A header that claims no factions, followed by a byte nothing declares.
    const std::vector<std::uint8_t> trailing{2, 0, 0, 0xFF};
    const auto trailing_result =
        pr::load_presentation(package_with_presentation(trailing).package);
    expect(
        trailing_result.error == pr::PresentationError::malformed_payload,
        "a presentation payload with unexplained trailing bytes is refused"
    );

    // A header alone, with no room for the theme's own byte.
    const auto empty_result =
        pr::load_presentation(package_with_presentation({}).package);
    expect(
        empty_result.error == pr::PresentationError::malformed_payload,
        "an empty presentation payload is refused"
    );

    // A count far larger than the payload could hold, which must be rejected
    // on the bytes present rather than believed and reserved for.
    const std::vector<std::uint8_t> overlong{0, 0xFF, 0xFF};
    const auto overlong_result =
        pr::load_presentation(package_with_presentation(overlong).package);
    expect(
        overlong_result.error == pr::PresentationError::malformed_payload,
        "a faction count the payload cannot hold is refused"
    );

    // Two entries for one faction: which colour would win is not a question a
    // reader should have to answer.
    std::vector<std::uint8_t> duplicated{0, 2, 0};
    for (int entry = 0; entry < 2; ++entry) {
        for (int byte = 0; byte < 8; ++byte) {
            duplicated.push_back(byte == 0 ? 7 : 0);
        }
        duplicated.push_back(3);
    }
    const auto duplicated_result =
        pr::load_presentation(package_with_presentation(duplicated).package);
    expect(
        duplicated_result.error == pr::PresentationError::malformed_payload,
        "two entries for one faction are refused"
    );
}

// The same four failures, for each content join. They share a decoder, so this
// proves the decoder rather than two copies of it. It is asked of both records
// because a record that silently decoded to nothing would look exactly like a
// package written before the join existed.
void a_damaged_content_join_is_refused() {
    const std::uint64_t records[2] = {
        pf::presentation_terrain_record_id,
        pf::presentation_archetype_record_id,
    };
    for (const std::uint64_t record : records) {
        expect(
            join_verdict(record, {}) ==
                pr::PresentationError::malformed_payload,
            "a join payload with no room for its count is refused"
        );

        // One entry declared, and an entry that stops after six of its eight
        // identity bytes.
        expect(
            join_verdict(record, {1, 0, 0, 0, 1, 2, 3, 4, 5, 6}) ==
                pr::PresentationError::malformed_payload,
            "a truncated join entry is refused"
        );

        // No entries declared, and a byte nothing accounts for.
        expect(
            join_verdict(record, {0, 0, 0, 0, 0xFF}) ==
                pr::PresentationError::malformed_payload,
            "a join payload with unexplained trailing bytes is refused"
        );

        // A count far larger than the payload could hold, which must be
        // rejected on the bytes present rather than reserved for.
        expect(
            join_verdict(record, {0xFF, 0xFF, 0xFF, 0xFF}) ==
                pr::PresentationError::malformed_payload,
            "a join count the payload cannot hold is refused"
        );

        // Two entries for one identity: which value wins is not a question a
        // reader should have to answer.
        std::vector<std::uint8_t> duplicated{2, 0, 0, 0};
        for (int entry = 0; entry < 2; ++entry) {
            for (int byte = 0; byte < 8; ++byte) {
                duplicated.push_back(byte == 0 ? 9 : 0);
            }
            duplicated.push_back(static_cast<std::uint8_t>(entry));
        }
        expect(
            join_verdict(record, duplicated) ==
                pr::PresentationError::malformed_payload,
            "two entries for one identity are refused"
        );

        // And the shape the compiler writes, so the cases above are refusals
        // of damage rather than of the encoding itself.
        expect(
            join_verdict(record, {1, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 3}) ==
                pr::PresentationError::none,
            "one well-formed entry is accepted"
        );
    }

    // A damaged join publishes nothing, not even the project record beside it.
    const auto loaded = package_with_records(
        {
            pf::RecordSource{pf::presentation_record_id, empty_project},
            pf::RecordSource{
                pf::presentation_terrain_record_id, {0, 0, 0, 0, 0xFF}
            }
        }
    );
    const auto result = pr::load_presentation(loaded.package);
    expect(
        result.presentation.theme == pr::default_theme &&
            result.presentation.factions.empty() &&
            result.presentation.terrain.empty(),
        "a refused join publishes no presentation at all"
    );
}

// Every section a rule reads, byte for byte the same in two packages.
void expect_gameplay_sections_match(
    const pf::LoadedPackage& left_package,
    const pf::LoadedPackage& right_package,
    std::string_view message
) {
    for (const pf::SectionView& section : left_package.sections) {
        if (section.type == pf::SectionType::presentation) continue;
        const pf::SectionView* other = right_package.find(section.type);
        expect(other != nullptr, "the same sections are present");
        if (other == nullptr) continue;
        bool identical = section.records.size() == other->records.size();
        for (std::size_t index = 0;
             identical && index < section.records.size(); ++index) {
            const pf::RecordView& left = section.records[index];
            const pf::RecordView& right = other->records[index];
            identical = left.stable_id == right.stable_id &&
                        left.payload_size == right.payload_size &&
                        std::equal(
                            left_package.bytes.begin() + left.payload_offset,
                            left_package.bytes.begin() + left.payload_offset +
                                left.payload_size,
                            right_package.bytes.begin() + right.payload_offset
                        );
        }
        expect(identical, message);
    }
}

// Rewrites a loaded package with its presentation section dropped and nothing
// else touched, which is the only way to build "the same project, carrying no
// presentation data at all" from a compiler that always writes some.
std::vector<std::uint8_t> without_presentation(
    const pf::LoadedPackage& package
) {
    pf::PackageSource rebuilt;
    rebuilt.game_id = package.game_id;
    rebuilt.content_revision = package.content_revision;
    rebuilt.required_engine = package.required_engine;
    rebuilt.target = package.target;
    rebuilt.required_features = package.required_features;
    for (const pf::SectionView& section : package.sections) {
        if (section.type == pf::SectionType::presentation) continue;
        pf::SectionSource source_section;
        source_section.type = section.type;
        source_section.schema_major = section.schema_major;
        source_section.schema_minor = section.schema_minor;
        source_section.flags = section.flags;
        for (const pf::RecordView& record : section.records) {
            source_section.records.push_back(
                pf::RecordSource{
                    record.stable_id,
                    std::vector<std::uint8_t>(
                        package.bytes.begin() + record.payload_offset,
                        package.bytes.begin() + record.payload_offset +
                            record.payload_size
                    )
                }
            );
        }
        rebuilt.sections.push_back(std::move(source_section));
    }
    return pf::write_mock_package(rebuilt);
}

// Presentation is not gameplay. Changing it must not move a byte of any
// section a rule reads, and neither must carrying it at all.
void presentation_does_not_touch_gameplay_sections() {
    auto plain = source();
    auto decorated = source();
    decorated.theme = gc::theme_index("ashland");
    decorated.factions[1].colour = gc::faction_colour_index("bone");

    const auto plain_package = load(gc::compile(plain).package);
    const auto decorated_package = load(gc::compile(decorated).package);
    expect(
        static_cast<bool>(plain_package) &&
            static_cast<bool>(decorated_package),
        "both packages load"
    );
    expect_gameplay_sections_match(
        plain_package.package,
        decorated_package.package,
        "a gameplay section is untouched by presentation"
    );

    // And the stronger claim the content joins make necessary: they are
    // derived from gameplay data (terrain identities, class names), so the
    // question is not only whether changing them moves a gameplay byte but
    // whether carrying them does. Compare against the same package with the
    // whole presentation section removed.
    const auto stripped = load(without_presentation(plain_package.package));
    expect(
        static_cast<bool>(stripped),
        "the same package with no presentation section loads"
    );
    expect_gameplay_sections_match(
        plain_package.package,
        stripped.package,
        "a gameplay section is untouched by carrying presentation at all"
    );
    const auto absent = pr::load_presentation(stripped.package);
    expect(
        static_cast<bool>(absent) && absent.presentation.terrain.empty() &&
            absent.presentation.archetypes.empty(),
        "the stripped package really carries no content joins"
    );
}

}  // namespace

int main() {
    carries_authored_choices();
    carries_the_content_joins();
    carries_a_characters_own_style();
    an_unthemed_project_is_the_default();
    an_absent_section_is_not_an_error();
    a_package_written_before_the_joins();
    a_damaged_section_is_refused();
    a_damaged_content_join_is_refused();
    presentation_does_not_touch_gameplay_sections();
    return failures == 0 ? 0 : 1;
}
