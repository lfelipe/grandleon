// SPDX-License-Identifier: MIT
// Host-side test for the shared console font.
//
// Two machines have no font of their own and now read one table. The Mega
// Drive expands each glyph into a VDP tile in whichever two palette entries the
// board left unclaimed; the PlayStation expands the whole sheet into a 4bpp
// texture page. Neither expansion is checkable off the hardware, but the
// *shape* of a letter is, and it is the part both machines share, so the shape
// is pinned here.
//
// What matters most is the box. Both expansions walk a fixed rectangle per
// glyph and place letters on a fixed grid, and the PlayStation's pixel claims
// name a particular ink pixel of a particular letter. So a glyph that set a
// pixel outside the five-by-seven box the font documents would be a message bar
// whose letters ran into each other on one console and a wrong pixel claim on
// the other. That is asserted for all sixty-four rather than described.

#include "grandleon/view/glyphs.hpp"

#include <iostream>
#include <string_view>

namespace view = grandleon::view;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    // The range the font claims to cover, which is every character the clients'
    // own strings use: space through underscore.
    expect(view::first_glyph == 0x20, "the font starts at space");
    expect(view::glyph_count == 0x40, "the font holds sixty-four glyphs");
    expect(view::glyph_width == 8 && view::glyph_rows == 8,
           "a glyph is eight by eight");

    // Space is blank and underscore is not, so the ends of the range are the
    // characters they are meant to be rather than an off-by-one into the table.
    bool space_blank = true;
    for (int row = 0; row < view::glyph_rows; ++row) {
        if (view::glyph_row(' ', row) != 0) space_blank = false;
    }
    expect(space_blank, "space is blank");

    bool underscore_marked = false;
    for (int row = 0; row < view::glyph_rows; ++row) {
        if (view::glyph_row('_', row) != 0) underscore_marked = true;
    }
    expect(underscore_marked, "underscore is not blank");

    // Every printable glyph except space has ink somewhere. A blank letter is
    // what a message bar with a hole in it looks like, and it would pass every
    // "solid paper" pixel claim there is.
    bool all_inked = true;
    for (int index = 1; index < view::glyph_count; ++index) {
        bool inked = false;
        for (int row = 0; row < view::glyph_rows; ++row) {
            if (view::glyph_row(view::first_glyph + index, row) != 0) inked = true;
        }
        if (!inked) {
            std::cerr << "  glyph " << (view::first_glyph + index)
                      << " has no ink\n";
            all_inked = false;
        }
    }
    expect(all_inked, "every glyph but space has ink");

    // The box: five pixels wide in an eight-pixel cell and seven rows tall in
    // eight, so a line of text has a column of tracking and a pixel of leading
    // without either being spelled out at a call site.
    bool inside_box = true;
    for (int index = 0; index < view::glyph_count; ++index) {
        const int character = view::first_glyph + index;
        if (view::glyph_row(character, view::glyph_rows - 1) != 0) {
            std::cerr << "  glyph " << character << " sets its last row\n";
            inside_box = false;
        }
        for (int row = 0; row < view::glyph_rows; ++row) {
            for (int x = 5; x < view::glyph_width; ++x) {
                if (view::glyph_pixel(character, x, row)) {
                    std::cerr << "  glyph " << character << " sets column " << x
                              << '\n';
                    inside_box = false;
                }
            }
        }
    }
    expect(inside_box, "every glyph fits the five-by-seven box");

    // `glyph_pixel` and `glyph_row` are two views of one table, and the
    // consoles use both: a tile-based machine expands rows, the PlayStation
    // asks about pixels. They must not be able to disagree.
    bool views_agree = true;
    for (int index = 0; index < view::glyph_count; ++index) {
        const int character = view::first_glyph + index;
        for (int row = 0; row < view::glyph_rows; ++row) {
            const int bits = view::glyph_row(character, row);
            for (int x = 0; x < view::glyph_width; ++x) {
                const bool from_row = (bits & (0x80 >> (x + 1))) != 0;
                if (from_row != view::glyph_pixel(character, x, row)) {
                    views_agree = false;
                }
            }
        }
    }
    expect(views_agree, "the row and the pixel say the same thing");

    // Out of range is blank rather than whatever is next in memory, which is
    // what lets a caller walk a fixed rectangle over arbitrary text.
    expect(view::glyph_row(0x1F, 0) == 0, "below the range is blank");
    expect(view::glyph_row(0x60, 0) == 0, "above the range is blank");
    expect(view::glyph_row('A', -1) == 0, "a row above the glyph is blank");
    expect(view::glyph_row('A', view::glyph_rows) == 0,
           "a row below the glyph is blank");
    expect(!view::glyph_pixel('A', -1, 0), "a column left of the glyph is blank");
    expect(!view::glyph_pixel('A', view::glyph_width, 0),
           "a column right of the glyph is blank");

    if (failures == 0) {
        std::cout << "glyph_test: all checks passed\n";
    }
    return failures == 0 ? 0 : 1;
}
