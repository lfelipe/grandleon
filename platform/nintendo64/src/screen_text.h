// SPDX-License-Identifier: MIT
#pragma once

// Where this console's rows of text are allowed to land, and what happens to
// the ones that do not fit.
//
// libdragon does not clip. `graphics_draw_text` walks the string and hands
// every character to `graphics_draw_character`, whose only guard is a null
// surface: it computes `buffer + y * stride + x` and stores, with no
// comparison against the surface's width or height anywhere in the loop. A row
// drawn below the last scanline is not quietly dropped. It is a write into
// whatever follows the framebuffer, which on this machine is the second buffer
// and then the heap the campaign session is allocating out of.
//
// So every screen here has to know its own last row, and one number written
// out by hand at each call site is a number a new screen can simply not
// write. The bound is a type instead, declared once per screen and drawn
// from, so a list has something underneath it by construction.
//
// Nothing in this header touches libdragon or names a machine, so
// `tests/nintendo64/screen_text_test.cpp` compiles the very arithmetic the ROM
// draws with: the bands below are the bands the screens use, and the host
// checks them against the frame the ROM opens rather than against a copy of
// it.

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace grandleon::n64screen {

// ---------------------------------------------------------------------------
// The frame, and the font drawn into it
// ---------------------------------------------------------------------------

// The frame this ROM opens:
// `display_init(RESOLUTION_320x240, DEPTH_16_BPP, …)`.
// 640 bytes a scanline, 153,600 bytes a buffer, and one pixel past the last of
// them belongs to something else.
inline constexpr int frame_width = 320;
inline constexpr int frame_height = 240;

// libdragon's built-in font, which is what `graphics_draw_text` draws with:
// eight pixels across and eight down per character. The width was already
// spelled into this ROM at every site that sizes a box (`strlen(text) * 8`);
// the height is what makes a row's *bottom* knowable, and without it no screen
// can say where its last row ends.
inline constexpr int font_width = 8;
inline constexpr int font_height = 8;

// The margins the screens share: the column the body text starts in, and the
// first column past the right of the safe area. Thirty-four columns from x=24
// ends at 296, which is the width the title screen and the dialogue screen
// were both already wrapping to.
inline constexpr int safe_left = 24;
inline constexpr int safe_right = 296;

// How many characters fit between `x` and the right of the safe area.
[[nodiscard]] constexpr int columns_from(int x) noexcept {
    const int room = safe_right - x;
    return room > 0 ? room / font_width : 0;
}

// The wrap width of the safe area itself, which is what a screen writing from
// the left margin gets.
inline constexpr std::size_t safe_columns =
    static_cast<std::size_t>(columns_from(safe_left));

// How wide a run of glyphs is, and where it starts if it is to sit in the
// middle of the display.
//
// A centred row's left edge is a consequence of its own length, so deriving it
// is the difference between a row that is centred and a row that was centred
// once, by hand, against words nobody has changed since. Counting glyphs is
// also the only way the arithmetic below can be checked at all: a hand-written
// x says nothing about how long the string it is under is allowed to get.
[[nodiscard]] constexpr int text_width(std::string_view text) noexcept {
    return static_cast<int>(text.size()) * font_width;
}

[[nodiscard]] constexpr int centred_x(std::string_view text) noexcept {
    const int width = text_width(text);
    return width >= frame_width ? 0 : (frame_width - width) / 2;
}

// Whether every one of these rows, drawn from `left`, ends before `right`.
//
// The horizontal counterpart of `TextBand`, and it exists for the same reason:
// libdragon clips nothing in either direction. A row drawn past the right edge
// does not stop there: `buffer + y * stride + x` keeps walking, so the tail of
// a too-long line reappears at the left of the scanlines below it, and a line
// long enough runs off the last scanline into the heap. The rule cannot be
// enforced by looking at the words, because the words are the thing that
// changes; it is enforced by counting them, at the build that draws them.
[[nodiscard]] constexpr bool rows_fit(
    const char* const* rows,
    std::size_t count,
    int left,
    int right
) noexcept {
    for (std::size_t index = 0; index < count; ++index) {
        if (left + text_width(std::string_view{rows[index]}) > right) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// TextBand
// ---------------------------------------------------------------------------

// A band of scanlines a screen may draw rows of text into: where the first row
// starts, how far apart rows are, and the last scanline a row may start on.
//
// `last` rather than a row count because `if (y > 170) break;` is the shape the
// screens which got this right already had, so moving them onto this moves
// which line the number is written on and nothing else.
struct TextBand final {
    int top{0};
    int step{1};
    // The last scanline a row may start on. Below `top` when the band has no
    // room for even its first row, which is a band that must draw nothing.
    int last{0};

    // The band that starts at `top` and stops before `floor`. The floor is the
    // first scanline the band must not touch, where whatever is drawn under it
    // begins. A row occupies `font_height` scanlines, so the last row is the
    // last one whose bottom is still above the floor.
    [[nodiscard]] static constexpr TextBand above(
        int top, int step, int floor
    ) noexcept {
        const int stride = step < 1 ? 1 : step;
        const int room = floor - font_height - top;
        if (room < 0) return TextBand{top, stride, top - stride};
        return TextBand{top, stride, top + (room / stride) * stride};
    }

    // How many rows fit, counting from `top`.
    [[nodiscard]] constexpr int rows() const noexcept {
        return last < top ? 0 : (last - top) / step + 1;
    }

    // Whether a row starting on this scanline is inside the band.
    [[nodiscard]] constexpr bool holds(int y) const noexcept {
        return y >= top && y <= last;
    }

    [[nodiscard]] constexpr int y_of(int row) const noexcept {
        return top + row * step;
    }

    // The first scanline below everything this band can draw. What a host test
    // compares against `frame_height`.
    [[nodiscard]] constexpr int bottom() const noexcept {
        return last < top ? top : last + font_height;
    }
};

// ---------------------------------------------------------------------------
// The bands, one per screen that draws a list of authored strings
// ---------------------------------------------------------------------------

// The cutscene's body text. It starts under the portrait frame and stops above
// the button prompt, so eight rows of thirty-four columns is one screenful and
// a longer speech is paged rather than written off the bottom of the buffer.
inline constexpr int dialogue_prompt_y = 210;
inline constexpr TextBand dialogue_band =
    TextBand::above(124, 11, dialogue_prompt_y);

// The names of the people who just joined the company, under the heading and
// above the same prompt.
inline constexpr int joined_prompt_y = 210;
inline constexpr int joined_left = 60;
inline constexpr TextBand joined_band =
    TextBand::above(92, 14, joined_prompt_y);
inline constexpr std::size_t joined_columns =
    static_cast<std::size_t>(columns_from(joined_left));

// The aftermath's three lists, whose limits this screen has always known and
// stated as bare scanlines at the three loops that read them.
//
// The fallen start under their own heading and stop with room for the "AND N
// MORE" line the seventh name would otherwise have pushed off. The roster
// follows the fallen, so its top floats: 48 is where it begins on a battle
// nobody was lost in, and `rows()` is therefore the most it can ever draw
// rather than what it draws today. The store is the last thing above the
// prompt and has room for exactly one line, which is what it has always drawn.
inline constexpr TextBand aftermath_fallen_band{61, 13, 148};
inline constexpr TextBand aftermath_roster_band{48, 13, 170};
inline constexpr TextBand aftermath_store_band{198, 12, 208};

// The Stage picker, on a ROM built with it. One row per Stage,
// under the heading and the line saying what an unseen Stage costs, and above
// the button prompt.
//
// A campaign may hold more Stages than this band has rows, and unlike every
// other list here that is *ordinary* rather than exotic: a picker exists to
// reach a late Stage, and a long game is exactly the game somebody wants it
// for. So the rows here bound a window and never the list, and the screen
// scrolls the window under the caret. What the band is for is the same thing it
// is for everywhere else: libdragon clips nothing, so a window one row taller
// than the display would be written into the heap the campaign session is
// allocating out of, and the machine would carry on.
inline constexpr int stage_prompt_y = 218;
inline constexpr TextBand stage_band = TextBand::above(56, 13, stage_prompt_y);

// ---------------------------------------------------------------------------
// The controls screen, whose words are this ROM's own
// ---------------------------------------------------------------------------

// Every other list here is authored text of unknown length, bounded by a band
// and cut or paged to fit. This one is the opposite case and needs the
// opposite treatment: the words are written in this repository, they are
// short, and the only thing that can go wrong is somebody writing a longer
// one. So the rows live here as data rather than at the call site, and the
// build refuses a row that would not fit. A screen made of string literals
// inside a function cannot be asked that.
//
// The heading and the button prompt take their column from their own length
// rather than from a number somebody centred by hand.
// They are `const char*` rather than `std::string_view` because libdragon
// draws from a null-terminated pointer and this ROM allocates out of a heap it
// counts in kilobytes: a view would have to be copied into a `std::string` at
// every draw to be handed over. The lengths are still counted at compile time,
// through a view built where the counting happens.
inline constexpr int controls_title_y = 30;
inline constexpr int controls_prompt_y = 210;
inline constexpr const char* controls_title = "HOW TO PLAY";
inline constexpr const char* controls_prompt = "PRESS A TO BEGIN";

// The body starts in from the safe margin, so that the button names form a
// column of their own and what each one does lines up beside it.
inline constexpr int controls_left = 56;
inline constexpr std::size_t controls_columns =
    static_cast<std::size_t>(columns_from(controls_left));
inline constexpr TextBand controls_band =
    TextBand::above(58, 12, controls_prompt_y);

inline constexpr const char* controls_lines[] = {
    "D-PAD  MOVE THE CURSOR",
    "A      PICK ONE UP. ITS MENU",
    "       WALKS, ATTACKS, USES AN",
    "       ITEM, OR ENDS ITS TURN.",
    "       Z OPENS IT AGAIN.",
    "START  BOARD MENU. END THE",
    "       SIDE'S TURN, OR LEAVE.",
    "B      BACK OUT ONE STEP",
    "R      SHOW ALL THEY THREATEN",
    "",
    "BLUE IS WHERE THIS ONE CAN GO.",
    "RED: IT CAN GO, AND BE HIT.",
    "YOU MOVE YOUR SIDE, NOT THEM.",
};
inline constexpr std::size_t controls_line_count =
    std::size(controls_lines);

static_assert(
    rows_fit(controls_lines, controls_line_count, controls_left, safe_right),
    "a line of the controls screen would be drawn past the safe area, which on "
    "a machine that does not clip means the rest of it lands on the scanlines "
    "below"
);
static_assert(
    static_cast<std::size_t>(controls_band.rows()) >= controls_line_count,
    "the controls screen has more lines than fit above its button prompt"
);
static_assert(
    controls_band.bottom() <= controls_prompt_y &&
        controls_prompt_y + font_height <= frame_height,
    "the controls screen's last line would be drawn over its own prompt"
);
static_assert(
    centred_x(std::string_view{controls_title}) >= safe_left &&
        centred_x(std::string_view{controls_title}) +
                text_width(std::string_view{controls_title}) <= safe_right &&
        centred_x(std::string_view{controls_prompt}) >= safe_left &&
        centred_x(std::string_view{controls_prompt}) +
                text_width(std::string_view{controls_prompt}) <= safe_right,
    "the controls screen's heading or prompt is too long to centre in the safe "
    "area"
);

// Every band, held to the frame by the compiler that builds the ROM.
// libdragon draws a row below the last scanline rather than declining to, so
// the bound has to be a fact about the build and not a hope about the content.
static_assert(
    dialogue_band.bottom() <= frame_height,
    "the cutscene's last row would be drawn past the framebuffer"
);
static_assert(
    joined_band.bottom() <= frame_height,
    "the last name of an intake would be drawn past the framebuffer"
);
static_assert(
    aftermath_fallen_band.bottom() <= frame_height &&
        aftermath_roster_band.bottom() <= frame_height &&
        aftermath_store_band.bottom() <= frame_height,
    "an aftermath row would be drawn past the framebuffer"
);
static_assert(
    dialogue_band.rows() >= 1 && joined_band.rows() >= 1,
    "a band a screen draws into has room for no rows at all"
);

// ---------------------------------------------------------------------------
// Text that has to fit
// ---------------------------------------------------------------------------

// The built-in font is ASCII; a UTF-8 sequence (the campaign text carries an
// em dash) renders as one dash rather than several glyphs of noise.
//
// It is here rather than behind the probe guard because the probe ROM is built
// with the campaign compiled in, and the campaign names a unit by the name its
// author wrote, which is authored text like any other.
[[nodiscard]] inline std::string ascii_only(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (byte < 0x80) out.push_back(character);
        else if ((byte & 0xC0) == 0xC0) out.push_back('-');
    }
    return out;
}

// Word wrap at `columns`, breaking a word too long for one line rather than
// letting it run off the right edge.
[[nodiscard]] inline std::vector<std::string> wrap_text(
    const std::string& text,
    std::size_t columns
) {
    std::vector<std::string> rows;
    if (columns == 0) return rows;
    std::string row;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find(' ', start);
        if (end == std::string::npos) end = text.size();
        std::string word = text.substr(start, end - start);
        start = end + 1;
        if (word.empty()) continue;
        while (word.size() > columns) {
            if (!row.empty()) {
                rows.push_back(row);
                row.clear();
            }
            rows.push_back(word.substr(0, columns));
            word.erase(0, columns);
        }
        if (row.empty()) row = word;
        else if (row.size() + 1 + word.size() <= columns) row += ' ' + word;
        else {
            rows.push_back(row);
            row = word;
        }
    }
    if (!row.empty()) rows.push_back(row);
    return rows;
}

// One line's worth of an authored name, for a field that has one row and no
// room to wrap. A `displayName` may be 160 characters
// (schemas/source/v1/common.schema.json), which at eight pixels a character is
// four times the width of this display; cut rather than drawn, because a name
// drawn past the right edge continues onto the scanlines below it.
[[nodiscard]] inline std::string clipped(
    const std::string& text,
    std::size_t columns
) {
    std::string out = text;
    if (out.size() > columns) out.resize(columns);
    return out;
}

// `rows` cut to at most `limit` lines, with the last of them ending in an
// ellipsis so a reader is told that something was cut rather than left to
// believe the sentence ended there. A list that already fits is untouched.
inline void clip_rows(
    std::vector<std::string>& rows,
    int limit,
    std::size_t columns
) {
    if (limit < 0) limit = 0;
    if (rows.size() <= static_cast<std::size_t>(limit)) return;
    rows.resize(static_cast<std::size_t>(limit));
    if (rows.empty()) return;
    std::string& last = rows.back();
    if (columns >= 3U && last.size() > columns - 3U) last.resize(columns - 3U);
    last += "...";
}

// How many screenfuls a wrapped speech takes in a band this many rows tall.
//
// At least one, so a line that wrapped to nothing still gets its screen and
// its press: the cutscene's contract with the player is one press per authored
// line, and a line that drew no screen would swallow the press.
[[nodiscard]] constexpr int pages_of(int rows, int per_page) noexcept {
    if (per_page < 1 || rows <= per_page) return 1;
    return (rows + per_page - 1) / per_page;
}

// The half-open range of wrapped rows one page shows. Clamped to what the
// wrap produced, so the last page is short rather than reading past the end.
struct Page final {
    std::size_t first{0};
    std::size_t last{0};

    [[nodiscard]] constexpr std::size_t count() const noexcept {
        return last - first;
    }
};

[[nodiscard]] constexpr Page page_of(
    std::size_t rows, int per_page, int page
) noexcept {
    if (per_page < 1 || page < 0) return Page{0, 0};
    const std::size_t width = static_cast<std::size_t>(per_page);
    const std::size_t first = static_cast<std::size_t>(page) * width;
    if (first >= rows) return Page{rows, rows};
    const std::size_t last = rows - first < width ? rows : first + width;
    return Page{first, last};
}

// How many of `arrivals` a band of `rows` rows names, given that the ones it
// cannot name have to be counted out loud on a row of the band rather than on
// the row after it. A count drawn below the band is the very write the band
// exists to prevent. A list the band holds whole spends every row on a name.
[[nodiscard]] constexpr int named_of(int arrivals, int rows) noexcept {
    if (rows < 1) return 0;
    if (arrivals <= rows) return arrivals < 0 ? 0 : arrivals;
    return rows - 1;
}

}  // namespace grandleon::n64screen
