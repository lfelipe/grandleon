// SPDX-License-Identifier: MIT
// A dialogue record, from the compiler that writes it to the reader a client
// asks. Specifically the tail of it, which now holds two optional fields where
// it held one.
//
// The tail is the interesting part because its optionality is the record's own
// length rather than a flag. Three lengths mean three different scenes, and
// getting that wrong would not be a crash: it would be a scene loading with a
// backdrop it never named or a cast it never had. So the cases are asserted
// separately, in both directions: what each scene writes, and what a reader
// makes of a record that is none of the three.

#include <grandleon/game_content/compiler.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/package_runtime/presentation.hpp>

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

// Two characters drawn by different hands, so that a portrait resolved through
// the presentation records lands somewhere a portrait resolved from a display
// name could not: neither speaker's name spells its own archetype, and the two
// wear different colours, different styles and different figures.
gc::GameSource source() {
    gc::GameSource value;
    value.game_id[0] = 0x51U;
    value.title = "Dialogue slice";
    value.content_revision = 1;
    value.required_engine = {{0, 1, 0}, {0, 1, 99}};
    value.weapon_types = {{10, "Blade"}};
    value.item_types = {{20, "Consumable"}};
    value.classes = {
        {30, "Vanguard", {6, 4, 1, 0, 3}, {10}},
        {31, "Mage", {5, 3, 1, 0, 4}, {10}},
    };
    value.weapons = {{40, "Sword", 10, 3, 1, 1}};
    value.items = {{50, "Tonic", 20, 1}};
    value.unit_types = {
        {60, "Halvard", 30, 80, {40}, {50}},
        {61, "Briar", 31, 81, {40}, {}},
    };
    value.factions = {
        {80, "Blue Company", gc::faction_colour_index("blue")},
        {81, "Amber Company", gc::faction_colour_index("amber")},
    };
    // Briar is drawn by another hand at the other build. The board already
    // honours this; the point of the cast is that a portrait does too.
    value.unit_types[1].character_style = gc::character_style_index("nature");
    value.unit_types[1].character_figure = gc::character_figure_index("second");
    return value;
}

pf::LoadResult load(const std::vector<std::uint8_t>& bytes) {
    return pf::load_mock_package(
        bytes,
        {{0, 1, 0}, pf::TargetProfile::desktop, 0, 32, 10'000}
    );
}

gc::Dialogue scene() {
    gc::Dialogue value;
    value.id = 90;
    value.name = "Before the Gate";
    value.lines = {
        {"Halvard", "The gate is barred."},
        {"Briar", "Then we will ask it not to be."},
        {"Halvard", "That is not how gates work."},
        {"A Voice", "It is how this one works."},
    };
    return value;
}

// A scene that casts somebody: every line lands on the entry its speaker
// names, a speaker nobody cast lands on none, and the identity a line lands on
// is the one the presentation records answer all four questions about.
void carries_a_cast() {
    auto authored = source();
    gc::Dialogue staged = scene();
    staged.cast = {{"Halvard", 60}, {"Briar", 61}};
    staged.lines[0].cast_entry = 1;
    staged.lines[1].cast_entry = 2;
    staged.lines[2].cast_entry = 1;
    authored.dialogues = {staged};

    const auto compiled = gc::compile(authored);
    expect(static_cast<bool>(compiled), "a cast scene compiles");
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "a cast package loads");

    const auto read = pr::load_dialogue(loaded.package, 90);
    expect(static_cast<bool>(read), "the dialogue record decodes");
    const pr::Dialogue& dialogue = read.dialogue;
    expect(
        dialogue.cast.size() == 2 && dialogue.cast[0] == 60 &&
            dialogue.cast[1] == 61,
        "the cast survives compilation, in authored order"
    );
    expect(
        dialogue.lines.size() == 4 &&
            dialogue.speaker_unit_type(dialogue.lines[0]) != nullptr &&
            *dialogue.speaker_unit_type(dialogue.lines[0]) == 60 &&
            *dialogue.speaker_unit_type(dialogue.lines[1]) == 61 &&
            *dialogue.speaker_unit_type(dialogue.lines[2]) == 60,
        "every line resolves to the character its speaker was cast as"
    );
    expect(
        dialogue.speaker_unit_type(dialogue.lines[3]) == nullptr,
        "a speaker the scene cast nobody for resolves to nobody rather than "
        "to the first entry"
    );
    expect(
        dialogue.lines[0].speaker == "Halvard" &&
            dialogue.lines[3].speaker == "A Voice",
        "and the words are untouched: a cast says who speaks, never what is "
        "written"
    );

    // The whole point: the four questions a portrait asks are the four the
    // board asks, of the same records, about the identity the line landed on.
    const auto shown = pr::load_presentation(loaded.package);
    expect(static_cast<bool>(shown), "the presentation section decodes");
    const pr::Presentation& presentation = shown.presentation;
    const std::uint64_t briar = *dialogue.speaker_unit_type(dialogue.lines[1]);
    expect(
        presentation.archetype_of_unit_type(briar) ==
            gc::archetype_index("mage"),
        "the archetype a portrait would draw is the class's, not the name's"
    );
    expect(
        presentation.colour_of_unit_type(briar) ==
            gc::faction_colour_index("amber"),
        "the colour is the character's declared faction, not a substring"
    );
    expect(
        presentation.character_style_of_unit_type(briar) ==
            gc::character_style_index("nature"),
        "the style is the one the character names"
    );
    expect(
        presentation.character_figure_of_unit_type(briar) ==
            gc::character_figure_index("second"),
        "and so is the figure"
    );
}

// The three tail lengths, and the proof that the two older ones still mean
// what they meant. A scene casting nobody must write exactly the bytes it
// wrote before a cast existed. That is what keeps every shipped package
// byte-identical, so the assertion is on the encoded record itself and not
// only on what comes back out of it.
void the_tail_still_means_what_it_meant() {
    const auto record_of = [](const gc::Dialogue& staged) {
        auto authored = source();
        authored.dialogues = {staged};
        const auto compiled = gc::compile(authored);
        expect(static_cast<bool>(compiled), "the scene compiles");
        const auto loaded = load(compiled.package);
        expect(static_cast<bool>(loaded), "the package loads");
        const pf::RecordView* view =
            loaded.package.find(pf::SectionType::dialogue, 90);
        return view == nullptr ? std::size_t{0} : view->payload_size;
    };

    const gc::Dialogue plain = scene();
    gc::Dialogue with_backdrop = scene();
    with_backdrop.backdrop = gc::backdrop_index("throne_hall") + 1;
    gc::Dialogue with_cast = scene();
    with_cast.cast = {{"Halvard", 60}};
    with_cast.lines[0].cast_entry = 1;
    with_cast.lines[2].cast_entry = 1;

    const std::size_t bare = record_of(plain);
    expect(
        record_of(with_backdrop) == bare + 1,
        "a backdrop is still exactly one byte on the end of the record it was "
        "one byte on the end of before a cast existed"
    );
    expect(
        record_of(with_cast) == bare + 2 + 8 + plain.lines.size(),
        "a cast is the backdrop byte it must now always write, the cast size, "
        "one identity per entry, and one byte per line"
    );

    // And each of the three reads back as itself.
    auto authored = source();
    authored.dialogues = {plain, with_backdrop, with_cast};
    authored.dialogues[1].id = 91;
    authored.dialogues[2].id = 92;
    const auto compiled = gc::compile(authored);
    const auto loaded = load(compiled.package);
    expect(static_cast<bool>(loaded), "the three scenes load together");
    const auto bare_read = pr::load_dialogue(loaded.package, 90);
    expect(
        static_cast<bool>(bare_read) && bare_read.dialogue.backdrop == 0 &&
            bare_read.dialogue.cast.empty(),
        "a record with no tail is a scene naming no backdrop and casting "
        "nobody"
    );
    const auto backdrop_read = pr::load_dialogue(loaded.package, 91);
    expect(
        static_cast<bool>(backdrop_read) &&
            backdrop_read.dialogue.backdrop ==
                gc::backdrop_index("throne_hall") + 1 &&
            backdrop_read.dialogue.cast.empty(),
        "a record with one trailing byte is still a backdrop and no cast"
    );
    const auto cast_read = pr::load_dialogue(loaded.package, 92);
    expect(
        static_cast<bool>(cast_read) && cast_read.dialogue.backdrop == 0 &&
            cast_read.dialogue.cast.size() == 1,
        "a record with a longer tail is a cast, and a zero backdrop byte in "
        "it is a scene naming none rather than naming the first on the menu"
    );
}

// Builds a well-formed container holding exactly one dialogue record with
// exactly this payload. The container is written properly, so the payload is
// the only thing under test. Patching a compiled image instead would be caught
// by the envelope's own checks and would prove nothing about the reader.
pr::DialogueError dialogue_verdict(const std::vector<std::uint8_t>& payload) {
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
            pf::SectionType::dialogue,
            1,
            0,
            0,
            {pf::RecordSource{90, payload}}
        }
    };
    const auto loaded = load(pf::write_mock_package(package));
    expect(
        static_cast<bool>(loaded),
        "the container accepts a package holding this dialogue payload"
    );
    if (!loaded) return pr::DialogueError::missing_section;
    return pr::load_dialogue(loaded.package, 90).error;
}

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void put_string(std::vector<std::uint8_t>& out, std::string_view value) {
    put_u16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

// Two lines and a name, and nothing after them. Every case below appends its
// own tail to this, so the cases differ only in the thing under test.
std::vector<std::uint8_t> two_lines() {
    std::vector<std::uint8_t> payload;
    put_string(payload, "Before the Gate");
    put_u16(payload, 2);
    put_string(payload, "Halvard");
    put_string(payload, "One");
    put_string(payload, "Briar");
    put_string(payload, "Two");
    return payload;
}

// A record whose tail does not describe itself is refused, not half-believed.
void refuses_a_tail_it_cannot_read() {
    expect(
        dialogue_verdict(two_lines()) == pr::DialogueError::none,
        "the payload every case below is built from is itself readable"
    );

    {
        // A tail claiming a cast of nobody. An empty cast is written as no
        // tail at all, so this is a record describing a shape no writer emits.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);  // backdrop: names none
        payload.push_back(0);  // cast size: nobody
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a tail claiming a cast of nobody is refused"
        );
    }
    {
        // A tail claiming more entries than the record holds.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);
        payload.push_back(200);
        for (int index = 0; index < 8; ++index) payload.push_back(0);
        payload.push_back(1);
        payload.push_back(1);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a tail claiming more entries than it holds is refused rather "
            "than read past the end of the record"
        );
    }
    {
        // A line naming an entry past the end of the cast.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);
        payload.push_back(1);
        for (int index = 0; index < 8; ++index) payload.push_back(0);
        payload.push_back(1);
        payload.push_back(9);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a line naming a cast entry the scene does not hold is refused "
            "rather than resolved to a neighbour"
        );
    }
    {
        // One byte too few for the per-line bytes.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);
        payload.push_back(1);
        for (int index = 0; index < 8; ++index) payload.push_back(0);
        payload.push_back(1);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a tail one line short of naming every line is refused"
        );
    }
    {
        // One byte too many after them.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);
        payload.push_back(1);
        for (int index = 0; index < 8; ++index) payload.push_back(0);
        payload.push_back(1);
        payload.push_back(1);
        payload.push_back(0);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a tail with anything after the per-line bytes is refused, as a "
            "record longer than it describes always was"
        );
    }
    {
        // And the two older shapes still mean what they meant: a zero backdrop
        // byte on its own is not a scene naming the first backdrop on the menu.
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(0);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::malformed_payload,
            "a lone zero trailing byte is still refused rather than read as "
            "the first backdrop on the menu"
        );
    }
    {
        std::vector<std::uint8_t> payload = two_lines();
        payload.push_back(1);
        expect(
            dialogue_verdict(payload) == pr::DialogueError::none,
            "and a lone non-zero trailing byte is still a backdrop"
        );
    }
}

}  // namespace

int main() {
    carries_a_cast();
    the_tail_still_means_what_it_meant();
    refuses_a_tail_it_cannot_read();
    if (failures == 0) std::cout << "package_runtime dialogue tests passed\n";
    return failures == 0 ? 0 : 1;
}
