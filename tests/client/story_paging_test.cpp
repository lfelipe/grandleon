// SPDX-License-Identifier: MIT
// An authored speech longer than a page, and a word wider than one, on a host.
//
// Two ways a page could lose text without saying so, and both are pinned here
// because both are invisible from the outside: a reader shown a screen that
// stops early has no way to know the sentence went on.
//
//   - A *single* speech taller than sixteen rows. Breaking between speakers is
//     not enough on its own: fifteen rows of thirty-eight columns is about 570
//     characters, and a saying longer than that fits in no page at all. So the
//     pager has to hold and continue *inside* one speaker. `page_capacity`'s
//     header states the invariant that depends on it: "the number bounds a page
//     and never a story".
//
//   - A word wider than the page. There is no space to break it on, so it is
//     broken across rows; a page that laid it on one row would overhang and be
//     cut at column thirty-eight, and would put a blank row above it.
//
// What is under test is the console's own client: this binary compiles
// `platform/client/src/turn_client.cpp` under `GRANDLEON_TURN_CLIENT_CAMPAIGN`,
// which is the translation unit every campaign console compiles, and it reads
// the `ScreenView`s that client paints.
//
// The assertion is deliberately not "the page looks like this". It is that
// every character the author wrote comes back out, in order, once the pages are
// read one after another. That is the property a reader has, and the only one
// that cannot be satisfied by a screen that quietly stops early.
//
// No package, no campaign and no storage: `present_dialogue` is a `Presenter`
// method and takes the dialogue directly, so a scene can be authored here.

#include <grandleon/client/turn_client.hpp>
#include <grandleon/package_runtime/dialogue.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;
namespace turn = grandleon::client::turn;

namespace {

int failures = 0;

void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect(bool condition, std::string_view message) {
    if (!condition) fail(message);
}

class QuietSink final : public turn::ReportSink {
public:
    void line(const char*) override {}
};

// A reader that presses "go on" at every page and keeps what it was shown.
//
// It keeps the rows rather than the pages so that the pages can be read back
// as one continuous text, which is what a player who kept pressing A would
// have read.
class Reader final : public turn::TurnClient {
public:
    explicit Reader(turn::ReportSink& sink) noexcept : TurnClient(sink) {}

    void paint(const sim::EncounterSnapshot&, const turn::Overlay&) override {}

    void paint_screen(const turn::ScreenView& view) override {
        // `hold_page` paints, then reports, then paints again through
        // `after_screen`; only the first of a repeated page is a new page.
        if (!pages_.empty() && pages_.back() == rows_of(view)) return;
        pages_.push_back(rows_of(view));
        backdrops_.push_back(view.backdrop);
        screens_.push_back(view.screen);
    }

    // A runaway guard rather than a script: nothing here needs a particular
    // button, only that the reader keeps going. A pager that never terminated
    // would otherwise hang, and a hang says nothing.
    [[nodiscard]] std::uint16_t next_press() override {
        if (++presses_ > press_ceiling) {
            stalled_ = true;
            return turn::pad_end_of_script;
        }
        return turn::pad_a;
    }

    [[nodiscard]] const std::vector<std::vector<std::string>>& pages()
        const noexcept {
        return pages_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& backdrops() const noexcept {
        return backdrops_;
    }
    [[nodiscard]] const std::vector<turn::Screen>& screens() const noexcept {
        return screens_;
    }
    [[nodiscard]] bool stalled() const noexcept { return stalled_; }

    // Every row of every page, in the order the reader saw them, with the
    // blank separators dropped. This is the text a player read.
    [[nodiscard]] std::string read() const {
        std::string out;
        for (const std::vector<std::string>& page : pages_) {
            for (const std::string& row : page) {
                if (row.empty()) continue;
                if (!out.empty()) out.push_back(' ');
                out += row;
            }
        }
        return out;
    }

private:
    static std::vector<std::string> rows_of(const turn::ScreenView& view) {
        std::vector<std::string> rows;
        if (view.page == nullptr) return rows;
        for (int row = 0; row < view.page->count; ++row) {
            rows.emplace_back(view.page->line(row));
        }
        return rows;
    }

    static constexpr int press_ceiling = 4096;
    std::vector<std::vector<std::string>> pages_{};
    std::vector<std::uint8_t> backdrops_{};
    std::vector<turn::Screen> screens_{};
    int presses_{0};
    bool stalled_{false};
};

// The client's own normalisation, applied to the author's text so that what is
// expected back is what the display can hold: capitals, and a space for
// anything outside the font. Written here rather than reached for from the
// client because the client's copy is private, and a test that shared the
// implementation it is checking would agree with any bug in it.
std::string shouted(std::string_view text) {
    std::string out;
    for (const char raw : text) {
        char value = raw;
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - ('a' - 'A'));
        }
        if (value < 0x20 || value > 0x5F) value = ' ';
        out.push_back(value);
    }
    return out;
}

// The words of a text, in order, normalised the way the display holds them.
// Runs of spaces collapse, because a row's width is the box the renderer draws.
std::vector<std::string> words_of(std::string_view text) {
    std::vector<std::string> words;
    std::string word;
    for (const char value : shouted(text)) {
        if (value == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
            continue;
        }
        word.push_back(value);
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

pr::Dialogue one_speech(std::string speaker, std::string text) {
    pr::Dialogue dialogue;
    dialogue.id = 1;
    dialogue.name = "the speech";
    dialogue.backdrop = 4;
    dialogue.lines.push_back(pr::DialogueLine{std::move(speaker), std::move(text), 0});
    return dialogue;
}

// Every row of every page is at most the display's width, no page is taller
// than a page, and every page is a story page carrying the scene's backdrop.
void the_pages_are_pages(const Reader& reader, std::uint8_t backdrop) {
    expect(!reader.stalled(), "the pager terminates");
    expect(!reader.pages().empty(), "and shows at least one page");
    for (const std::vector<std::string>& page : reader.pages()) {
        expect(
            static_cast<int>(page.size()) <= turn::page_capacity,
            "no page is taller than a page"
        );
        for (const std::string& row : page) {
            expect(
                static_cast<int>(row.size()) <= turn::page_columns,
                "no row is wider than the display"
            );
        }
    }
    for (const turn::Screen screen : reader.screens()) {
        expect(screen == turn::Screen::story, "every page is a story page");
    }
    for (const std::uint8_t held : reader.backdrops()) {
        expect(
            held == backdrop,
            "and every page, including a continuation, keeps the scene's "
            "backdrop"
        );
    }
}

// ---------------------------------------------------------------------------

// A speech far taller than one page. Twelve hundred characters is a little over
// two pages' worth, so a client that stops at sixteen rows loses most of it and
// fails on the word count before it fails on anything subtler.
void a_speech_taller_than_a_page_is_paged_and_not_cut() {
    std::string speech;
    for (int i = 0; i < 150; ++i) {
        if (!speech.empty()) speech.push_back(' ');
        speech += "road";
        speech.push_back(static_cast<char>('a' + (i % 26)));
    }
    expect(speech.size() > 570, "the speech is past the old ceiling");

    QuietSink sink;
    Reader reader(sink);
    const pr::Dialogue dialogue = one_speech("MIREA", speech);
    reader.present_dialogue(dialogue);

    the_pages_are_pages(reader, dialogue.backdrop);
    expect(reader.pages().size() > 1, "a speech past one page is paged");

    // The speaker is named once, at the head of what they said, and the rest
    // is what they said. Read back as words so that where the wrapper chose to
    // break is the wrapper's business and not this test's.
    std::vector<std::string> expected = words_of(speech);
    expected.insert(expected.begin(), "MIREA:");
    expect(
        words_of(reader.read()) == expected,
        "and every word of it is read, in order, with nothing dropped"
    );
}

// A word nobody can break on a space, laid down across as many rows as it
// takes and with no blank row in front of it.
void a_word_wider_than_the_page_is_broken_and_not_cut() {
    // Ninety-five characters: two whole rows and nineteen columns of a third,
    // so both the "fills a row" and the "leaves a remainder" paths run.
    std::string monster;
    for (int i = 0; i < 95; ++i) {
        monster.push_back(static_cast<char>('A' + (i % 26)));
    }
    const std::string speech = "before " + monster + " after";

    QuietSink sink;
    Reader reader(sink);
    const pr::Dialogue dialogue = one_speech("", speech);
    reader.present_dialogue(dialogue);

    the_pages_are_pages(reader, dialogue.backdrop);
    expect(reader.pages().size() == 1, "and it still fits on one page");

    // No blank row anywhere. A page whose speech is four rows long has exactly
    // the separator after it and nothing else empty, and the separator is
    // trimmed off the end of a page rather than left in the middle of one.
    const std::vector<std::string>& page = reader.pages().front();
    std::size_t blanks_inside = 0;
    for (std::size_t row = 0; row + 1 < page.size(); ++row) {
        if (page[row].empty()) ++blanks_inside;
    }
    expect(blanks_inside == 0, "a broken word leaves no blank row above it");

    // Every character of the monster survives, in order. The rows are joined
    // with nothing between them: every break the wrapper made here was inside
    // the word rather than between two, so no space was taken out at one. The
    // space before "after" is on a row of its own accord, because that break
    // was a wrap and not a split.
    std::string laid_down;
    for (const std::string& row : page) laid_down += row;
    const std::string want = shouted("before" + monster + " after");
    expect(
        laid_down == want,
        "and every character of the word is laid down, in order"
    );
}

// The row count the pager is told, against the rows it actually emits. These
// must agree or `present_dialogue` breaks between speakers in the wrong place:
// it asks `wrapped_rows` whether the next speaker would fit before committing
// to a page. A word wider than the page is where two implementations of one
// wrap can most easily disagree, because the count and the wrapper each have to
// know it will be broken.
void the_predicted_height_is_the_emitted_height() {
    const std::string texts[] = {
        "one",
        "two words",
        "a line that is exactly wrapped across the thirty eight columns here",
        std::string(38, 'X'),
        std::string(39, 'X'),
        std::string(76, 'X'),
        std::string(77, 'X'),
        "short " + std::string(80, 'Y'),
        std::string(80, 'Y') + " short",
        "",
    };
    for (const std::string& text : texts) {
        QuietSink sink;
        Reader reader(sink);
        // No speaker, so what lands on the page is the wrapped text and the
        // blank separator after it, and nothing else. A wrapped row is never
        // blank: every one of them is a slice of a word. So counting the
        // non-blank rows counts exactly what the wrapper laid down.
        reader.present_dialogue(one_speech("", text));
        int emitted = 0;
        for (const std::vector<std::string>& page : reader.pages()) {
            for (const std::string& row : page) {
                if (!row.empty()) ++emitted;
            }
        }
        expect(
            turn::wrapped_rows(text.c_str()) == (emitted == 0 ? 1 : emitted),
            "the predicted height is the emitted height for: " + text
        );
    }
}

}  // namespace

int main() {
    a_speech_taller_than_a_page_is_paged_and_not_cut();
    a_word_wider_than_the_page_is_broken_and_not_cut();
    the_predicted_height_is_the_emitted_height();
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "story paging: every page holds, and no story is cut\n";
    return 0;
}
