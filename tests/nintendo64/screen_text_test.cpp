// SPDX-License-Identifier: MIT
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "screen_text.h"

// Where the Nintendo 64's rows of text are allowed to land, checked against
// the frame the ROM opens.
//
// This is a host test about a console because the thing being checked is
// arithmetic, and because the failure it exists to prevent cannot be seen on
// the console at all. libdragon's `graphics_draw_character` computes
// `buffer + y * stride + x` and stores the glyph; it compares nothing against
// the surface's width or height. A row drawn below scanline 239 is written
// into the memory after the framebuffer (the second buffer, and then the heap
// the campaign session allocates out of), and the machine carries on. Wrong
// pixels first, and then somebody else's bytes.
//
// So each screen's band is declared once in `screen_text.h` and the ROM draws
// from it, and this is where the bands are held to the frame. Every number
// below is derived from the band or from the schema that bounds the authored
// text, and none of it is a recording of what a run happened to produce.

namespace screen = grandleon::n64screen;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The bounds the source schemas put on authored text, which are the numbers a
// screen has to survive rather than the numbers the shipped game happens to
// use. `schemas/source/v1/dialogue.schema.json` caps a line at 4,096
// characters and `common.schema.json` caps a `displayName` at 160;
// `campaign.schema.json` caps a node's `recruits` at 64.
constexpr std::size_t schema_dialogue_line = 4096;
constexpr std::size_t schema_display_name = 160;
constexpr int schema_recruits = 64;

std::string repeated_words(std::size_t characters) {
    // Ordinary prose rather than one unbroken token: a paragraph of five
    // letter words is what an author writes, and it is what the wrap has to
    // walk. `WORD ` is five characters, so the length is exact.
    std::string out;
    while (out.size() < characters) out += "WORD ";
    out.resize(characters);
    return out;
}

// -------------------------------------------------------------------------
// The bands, against the frame
// -------------------------------------------------------------------------

void a_band_stops_before_its_floor() {
    // Eight rows of eleven pixels from 124 is the last row that ends above the
    // button prompt at 210: 124 + 7*11 = 201, and 201 + 8 = 209.
    expect(screen::dialogue_band.top == 124, "the dialogue band starts at 124");
    expect(screen::dialogue_band.rows() == 8, "and holds eight rows");
    expect(
        screen::dialogue_band.y_of(screen::dialogue_band.rows() - 1) ==
            screen::dialogue_band.last,
        "and its last row is the last scanline it names"
    );
    expect(
        screen::dialogue_band.bottom() <= screen::dialogue_prompt_y,
        "and the bottom of its last row is above the prompt"
    );
    expect(
        !screen::dialogue_band.holds(screen::dialogue_band.last +
                                     screen::dialogue_band.step),
        "and one row further down is outside it"
    );

    // A floor with no room for even the first row is a band that draws
    // nothing, rather than one that draws a row above its own top.
    const screen::TextBand nothing = screen::TextBand::above(200, 11, 204);
    expect(nothing.rows() == 0, "a band with no room holds no rows");
    expect(!nothing.holds(200), "and holds not even its own first scanline");
    expect(
        nothing.bottom() <= 204, "and its bottom is still above its floor"
    );
}

// The Stage picker's band is the one whose list is *expected* to be longer than
// the band: a picker exists to reach a late Stage, and a long game is the game
// somebody wants it for. So the band bounds a window that scrolls, and what has
// to hold is that the window has room to be a window and that the prompt under
// it is not one of the rows.
void the_stage_band_is_a_window_with_room_to_scroll() {
    expect(
        screen::stage_band.rows() >= 4,
        "the Stage picker shows enough rows at once to be read as a list rather "
        "than as a caret with a neighbour"
    );
    expect(
        screen::stage_band.bottom() <= screen::stage_prompt_y,
        "and its last row ends above the prompt, which is not a Stage and must "
        "not be drawn over by one"
    );
}

void every_band_ends_inside_the_frame() {
    const struct {
        const char* name;
        screen::TextBand band;
    } bands[] = {
        {"the dialogue band", screen::dialogue_band},
        {"the joined band", screen::joined_band},
        {"the aftermath's fallen band", screen::aftermath_fallen_band},
        {"the aftermath's roster band", screen::aftermath_roster_band},
        {"the aftermath's store band", screen::aftermath_store_band},
        {"the Stage picker's band", screen::stage_band},
    };
    for (const auto& entry : bands) {
        expect(
            entry.band.bottom() <= screen::frame_height,
            std::string(entry.name) + " ends inside the framebuffer"
        );
        expect(
            entry.band.rows() >= 1,
            std::string(entry.name) + " has room for at least one row"
        );
    }
}

// -------------------------------------------------------------------------
// The dialogue screen
// -------------------------------------------------------------------------

// The overrun, stated as the arithmetic a screen with no band does.
//
// This is the measurement the bands exist for: an authored line this long,
// laid out one row after another from the top of the band with no cap, reaches
// below the last scanline of the framebuffer.
int unbounded_bottom(const screen::TextBand& band, int rows) {
    return band.top + (rows - 1) * band.step + screen::font_height;
}

void an_authored_paragraph_would_have_left_the_buffer() {
    // The shortest authored line that reaches past the framebuffer, found
    // rather than guessed: ordinary prose, wrapped the way the screen wraps
    // it, then walked row after row from the top of the band with no cap.
    std::size_t overruns_at = 0;
    for (std::size_t length = 1; length <= schema_dialogue_line; ++length) {
        const std::vector<std::string> rows =
            screen::wrap_text(repeated_words(length), screen::safe_columns);
        if (unbounded_bottom(
                screen::dialogue_band, static_cast<int>(rows.size())
            ) > screen::frame_height) {
            overruns_at = length;
            break;
        }
    }
    expect(overruns_at > 0, "some authored line reaches past the frame");
    std::cerr << "  derived: an authored line of " << overruns_at
              << " characters is the shortest that would have been drawn "
                 "below the last scanline of the framebuffer\n";
    // One ordinary paragraph, and nothing like the ceiling the schema allows.
    // Tarnholt's longest authored line is 108 characters, so this is three of
    // them written as one speech.
    expect(
        overruns_at < 500,
        "and it is one ordinary paragraph rather than an abusive input"
    );
    expect(
        overruns_at < schema_dialogue_line / 8,
        "far inside what the dialogue schema allows an author to write"
    );
    // A line that long is already more than one screenful, which is the fact
    // the paging below rests on.
    expect(
        static_cast<int>(
            screen::wrap_text(repeated_words(overruns_at),
                              screen::safe_columns).size()
        ) > screen::dialogue_band.rows(),
        "and more than the band holds"
    );

    // The schema's own ceiling, which is where the writing ends up rather than
    // merely a little past the bottom of the screen.
    const std::vector<std::string> longest = screen::wrap_text(
        repeated_words(schema_dialogue_line), screen::safe_columns
    );
    const int overshoot =
        unbounded_bottom(
            screen::dialogue_band, static_cast<int>(longest.size())
        ) -
        screen::frame_height;
    expect(
        overshoot > 1000,
        "and the longest line the schema allows would reach more than a "
        "thousand scanlines past it, which at 640 bytes a scanline is past "
        "the second framebuffer and into the heap"
    );
    std::cerr << "  derived: the longest authored line the schema allows "
                 "wraps to "
              << longest.size() << " rows, whose last would start at scanline "
              << screen::dialogue_band.y_of(
                     static_cast<int>(longest.size()) - 1
                 )
              << " — " << overshoot << " scanlines past the frame\n";
}

void the_dialogue_screen_pages_rather_than_overruns() {
    const int per_page = screen::dialogue_band.rows();
    for (const std::size_t length :
         {std::size_t{0}, std::size_t{108}, std::size_t{350},
          schema_dialogue_line}) {
        const std::vector<std::string> rows =
            screen::wrap_text(repeated_words(length), screen::safe_columns);
        const int pages =
            screen::pages_of(static_cast<int>(rows.size()), per_page);
        expect(pages >= 1, "every authored line gets at least one screen");

        // Every row the screen draws lands inside the band, and every row the
        // wrap produced is drawn on exactly one page. Paged, never truncated:
        // the number bounds a page and not a story.
        std::size_t drawn = 0;
        for (int page = 0; page < pages; ++page) {
            const screen::Page shown =
                screen::page_of(rows.size(), per_page, page);
            expect(
                shown.first == drawn,
                "each page starts where the last one stopped"
            );
            for (std::size_t index = shown.first; index < shown.last;
                 ++index) {
                expect(
                    screen::dialogue_band.holds(
                        screen::dialogue_band.y_of(
                            static_cast<int>(index - shown.first)
                        )
                    ),
                    "a drawn row stands inside the dialogue band"
                );
            }
            drawn += shown.count();
        }
        expect(
            drawn == rows.size(),
            "and every wrapped row of the line was drawn on some page"
        );
    }
}

void a_wrapped_row_stays_inside_the_safe_area() {
    // The wrap is what keeps a row from running off the right edge, and a word
    // longer than the line is broken rather than allowed to.
    const std::string unbroken(schema_display_name, 'M');
    const std::vector<std::string> rows =
        screen::wrap_text(unbroken, screen::safe_columns);
    for (const std::string& row : rows) {
        expect(
            row.size() <= screen::safe_columns,
            "no wrapped row is wider than the safe area"
        );
        expect(
            screen::safe_left +
                    static_cast<int>(row.size()) * screen::font_width <=
                screen::safe_right,
            "and none of them reaches past the right margin"
        );
    }
    expect(!rows.empty(), "an unbroken word still produces rows");
}

void a_speaker_name_is_cut_to_the_room_beside_the_portrait() {
    const std::size_t columns =
        static_cast<std::size_t>(screen::columns_from(100));
    const std::string name =
        screen::clipped(std::string(schema_display_name, 'A'), columns);
    expect(name.size() == columns, "the longest name is cut to the columns");
    expect(
        100 + static_cast<int>(name.size()) * screen::font_width <=
            screen::safe_right,
        "and what is left ends inside the safe area"
    );
}

// -------------------------------------------------------------------------
// The screen that names who joined
// -------------------------------------------------------------------------

void an_intake_of_twelve_would_have_left_the_buffer() {
    // Twelve is where it starts. The band begins at 92 and steps 14, so the
    // twelfth row starts at 246, below the last scanline of a 240-line frame.
    expect(
        screen::joined_band.y_of(11) >= screen::frame_height,
        "the twelfth name, drawn without a cap, starts past the framebuffer"
    );
    // And the schema's ceiling is not twelve.
    expect(
        unbounded_bottom(screen::joined_band, schema_recruits) -
                screen::frame_height >
            500,
        "and the 64 recruits a node may author would reach hundreds of "
        "scanlines past it"
    );
    std::cerr << "  derived: 64 recruits drawn without a cap would end at "
                 "scanline "
              << unbounded_bottom(screen::joined_band, schema_recruits)
              << " on a 240-line frame\n";
}

void the_joined_screen_names_who_it_can_and_counts_the_rest() {
    const int rows = screen::joined_band.rows();
    for (int arrivals = 0; arrivals <= schema_recruits; ++arrivals) {
        const int named = screen::named_of(arrivals, rows);
        expect(named <= arrivals, "the screen never names more than arrived");
        expect(
            arrivals <= rows ? named == arrivals : named < arrivals,
            "an intake the band holds is named in full, and a larger one "
            "leaves a row to count the rest on"
        );
        // The last row written is the overflow count when there is one and the
        // last name otherwise, and either way it stands inside the band.
        const int last_row = named < arrivals ? named : named - 1;
        if (last_row >= 0) {
            expect(
                screen::joined_band.holds(screen::joined_band.y_of(last_row)),
                "and the last row the screen writes is inside the band"
            );
        }
    }
    // The shipped game's largest intake is two, so it is drawn whole and the
    // screen is the screen it always was.
    expect(
        screen::named_of(2, rows) == 2,
        "Tarnholt's largest intake is named in full"
    );
    expect(
        screen::joined_band.y_of(0) == 92 &&
            screen::joined_band.y_of(1) == 106,
        "on the two scanlines it always used"
    );
}

void a_joined_name_is_cut_to_the_room_it_has() {
    const std::string name = screen::clipped(
        std::string(schema_display_name, 'W'), screen::joined_columns
    );
    expect(
        screen::joined_left +
                static_cast<int>(name.size()) * screen::font_width <=
            screen::safe_right,
        "the longest authored name ends inside the safe area"
    );
    // Undtruncated, the same name is four screens wide, which on a machine
    // that does not clip is four scanlines of smear rather than one long row.
    expect(
        screen::joined_left +
                static_cast<int>(schema_display_name) * screen::font_width >
            screen::frame_width * 3,
        "where the authored one would have been more than three screens wide"
    );
}

// -------------------------------------------------------------------------
// The title screen, which caps by ellipsis rather than by paging
// -------------------------------------------------------------------------

void a_long_title_is_cut_and_says_so() {
    std::vector<std::string> rows = screen::wrap_text(
        repeated_words(schema_display_name), screen::safe_columns
    );
    expect(rows.size() > 3U, "the longest title is more than three rows");
    screen::clip_rows(rows, 3, screen::safe_columns);
    expect(rows.size() == 3U, "and is cut to the three the screen has");
    expect(
        rows.back().size() <= screen::safe_columns,
        "and the ellipsised row still fits the safe area"
    );
    expect(
        rows.back().size() >= 3U &&
            rows.back().compare(rows.back().size() - 3U, 3U, "...") == 0,
        "and says it was cut"
    );

    std::vector<std::string> fits = screen::wrap_text("TARNHOLT", 34);
    const std::vector<std::string> before = fits;
    screen::clip_rows(fits, 3, screen::safe_columns);
    expect(fits == before, "a title that fits is untouched");
}

// -------------------------------------------------------------------------
// The controls screen, whose rows are fixed and therefore checkable exactly
// -------------------------------------------------------------------------

// Every other screen here is bounded against text an author might write, so
// the most it can claim is that the bound holds. This one is the ROM's own
// words, so the claim can be exact: every row that will actually be drawn,
// walked, measured, and held to the same margins the rest of the console uses.
//
// The rows are read from `screen_text.h` rather than copied, which is the
// whole point of the table being there. A copy would pass forever.
void every_line_of_the_controls_screen_fits_the_safe_area() {
    expect(screen::controls_line_count > 0U, "the screen has lines to draw");
    for (std::size_t index = 0; index < screen::controls_line_count; ++index) {
        const std::string_view line{screen::controls_lines[index]};
        const int right = screen::controls_left + screen::text_width(line);
        expect(
            right <= screen::safe_right,
            "controls line " + std::to_string(index) + " (\"" +
                std::string(line) + "\", " + std::to_string(line.size()) +
                " characters) ends at x=" + std::to_string(right) +
                ", past the safe area at " +
                std::to_string(screen::safe_right)
        );
        // And the margin is not one glyph of luck: a row that stopped exactly
        // at the physical edge would be a row one word away from the bug this
        // exists to catch, so the check is against the safe area every other
        // screen writes into rather than against the last column of the frame.
        expect(
            right + screen::font_width <= screen::frame_width,
            "controls line " + std::to_string(index) +
                " has at least one more glyph of room before the frame ends"
        );
        expect(
            line.size() <= screen::controls_columns,
            "controls line " + std::to_string(index) +
                " is inside the columns its margin leaves"
        );
    }
    std::cerr << "  derived: the longest of "
              << screen::controls_line_count << " controls lines ends at x="
              << [] {
                     int widest = 0;
                     for (std::size_t index = 0;
                          index < screen::controls_line_count; ++index) {
                         const int right =
                             screen::controls_left +
                             screen::text_width(
                                 std::string_view{screen::controls_lines[index]}
                             );
                         if (right > widest) widest = right;
                     }
                     return widest;
                 }()
              << ", inside a safe area that ends at " << screen::safe_right
              << " on a " << screen::frame_width << "-pixel display\n";
}

void the_controls_screen_ends_above_its_own_prompt() {
    // The band has to hold every row, and the last of them has to finish above
    // the button prompt: the two failures are different, and a screen that
    // wrote over its own prompt would look like a font bug rather than like a
    // row count nobody re-checked.
    expect(
        static_cast<std::size_t>(screen::controls_band.rows()) >=
            screen::controls_line_count,
        "the band holds every line the screen draws"
    );
    const int last = screen::controls_band.y_of(
        static_cast<int>(screen::controls_line_count) - 1
    );
    expect(
        screen::controls_band.holds(last),
        "the last line starts inside the band"
    );
    expect(
        last + screen::font_height <= screen::controls_prompt_y,
        "and finishes above the prompt at y=" +
            std::to_string(screen::controls_prompt_y) + ", where it ends at " +
            std::to_string(last + screen::font_height)
    );
    expect(
        screen::controls_prompt_y + screen::font_height <=
            screen::frame_height,
        "and the prompt itself is inside the frame"
    );
}

void the_heading_and_the_prompt_are_centred_by_their_own_length() {
    const struct {
        const char* what;
        std::string_view text;
    } centred[] = {
        {"the heading", std::string_view{screen::controls_title}},
        {"the prompt", std::string_view{screen::controls_prompt}},
    };
    for (const auto& entry : centred) {
        const int x = screen::centred_x(entry.text);
        const int width = screen::text_width(entry.text);
        expect(
            x >= screen::safe_left && x + width <= screen::safe_right,
            std::string(entry.what) + " sits inside the safe area"
        );
        // Centred means centred: the room left of it and the room right of it
        // differ by less than one glyph. Deriving the column is what makes this
        // true of whatever the words become, rather than of these words.
        const int left_room = x;
        const int right_room = screen::frame_width - (x + width);
        expect(
            left_room - right_room <= 1 && right_room - left_room <= 1,
            std::string(entry.what) + " is centred on the display"
        );
    }
}

}  // namespace

int main() {
    a_band_stops_before_its_floor();
    every_band_ends_inside_the_frame();
    the_stage_band_is_a_window_with_room_to_scroll();
    an_authored_paragraph_would_have_left_the_buffer();
    the_dialogue_screen_pages_rather_than_overruns();
    a_wrapped_row_stays_inside_the_safe_area();
    a_speaker_name_is_cut_to_the_room_beside_the_portrait();
    an_intake_of_twelve_would_have_left_the_buffer();
    the_joined_screen_names_who_it_can_and_counts_the_rest();
    a_joined_name_is_cut_to_the_room_it_has();
    a_long_title_is_cut_and_says_so();
    every_line_of_the_controls_screen_fits_the_safe_area();
    the_controls_screen_ends_above_its_own_prompt();
    the_heading_and_the_prompt_are_centred_by_their_own_length();
    if (failures == 0) {
        std::cerr << "every Nintendo 64 text band ends inside the frame\n";
    }
    return failures == 0 ? 0 : 1;
}
