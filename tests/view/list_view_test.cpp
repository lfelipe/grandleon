// SPDX-License-Identifier: MIT
// Host-side test for the shared list window. The header is pure arithmetic
// precisely so this suite can pin the edges, the last row above all, without
// a console in the loop.
//
// The bug this exists to make impossible is silent: a list longer than its
// window can drop the rows past the end with nothing on screen to say so,
// so "the last row is reachable" is the assertion that matters most here and
// it is made three ways: by walking a caret to the end, by asking the window
// whether it is visible, and by the legend agreeing about which rows are up.

#include "grandleon/view/list_view.hpp"

#include <cstring>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// The legend, as a comparable string.
std::string_view legend_of(const grandleon::view::ListWindow& window,
                           char (&buffer)[grandleon::view::scroll_legend_size]) {
    grandleon::view::scroll_legend(window, buffer, sizeof buffer);
    return buffer;
}

// Walks a caret from the first item to the last, one row at a time, following
// with the given margin, which is what a player pressing down does. Answers
// whether every item was visible at the moment the caret stood on it.
bool every_item_reachable(int rows, int total, int margin) {
    grandleon::view::ListWindow window{0, rows, total};
    for (int cursor = 0; cursor < total; ++cursor) {
        window.follow(cursor, margin);
        if (!window.visible(cursor)) return false;
        if (window.row_of(cursor) < 0) return false;
        if (window.row_of(cursor) >= rows) return false;
        if (window.top < 0) return false;
        if (window.end() > total) return false;
    }
    // And back up again, because a window that only clamps in one direction
    // strands the first row instead of the last.
    for (int cursor = total - 1; cursor >= 0; --cursor) {
        window.follow(cursor, margin);
        if (!window.visible(cursor)) return false;
    }
    return true;
}

}  // namespace

int main() {
    using grandleon::view::ListWindow;
    using grandleon::view::scroll_legend;
    using grandleon::view::scroll_legend_size;

    char buffer[scroll_legend_size];

    // A list that fits never scrolls, and a renderer asking it questions gets
    // the answers it got before there was a window at all. This is the case
    // every shipped company is in, and it is why nothing on screen moved.
    {
        ListWindow window{0, 7, 4};
        window.follow(3, 2);
        expect(window.top == 0, "a fitting list pins the window to the top");
        expect(!window.scrolls(), "a fitting list does not scroll");
        expect(window.shown() == 4, "a fitting list shows all of itself");
        expect(window.end() == 4, "and ends where the list ends");
        expect(window.row_of(3) == 3, "an item's row is its index");
        expect(legend_of(window, buffer).empty(),
               "a list that fits says nothing about scrolling");
    }

    // A list exactly the size of its window is the boundary, and it is on the
    // "fits" side: seven of seven scrolls nowhere.
    {
        ListWindow window{0, 7, 7};
        window.follow(6, 2);
        expect(window.top == 0, "a list the size of its window does not move");
        expect(!window.scrolls(), "and does not claim to scroll");
        expect(window.visible(6), "its last row is on screen");
        expect(legend_of(window, buffer).empty(),
               "and it says nothing either");
    }

    // One more than fits is where truncation would begin. The last row is
    // reachable and the window says where it is.
    {
        ListWindow window{0, 7, 8};
        expect(window.scrolls(), "eight rows in seven scrolls");
        window.follow(7, 1);
        expect(window.visible(7), "the eighth row is reachable");
        expect(window.top == 1, "and the window moved exactly one row");
        expect(window.end() == 8, "showing the end of the list");
        expect(legend_of(window, buffer) == "2-8 OF 8",
               "the legend counts one-based and inclusive");
    }

    // The representative twelve-member company, in the seven rows the consoles
    // draw. Every member is reachable, in both directions, at every margin the
    // clients could pick.
    {
        for (int margin = 0; margin <= 3; ++margin) {
            expect(every_item_reachable(7, 12, margin),
                   "twelve members are all reachable in seven rows");
        }
        // And in the narrowest window a screen could offer.
        expect(every_item_reachable(1, 12, 0),
               "a one-row window still reaches every member");
        expect(every_item_reachable(2, 40, 1),
               "a two-row window still reaches every row of forty");
    }

    // The margin keeps the caret off the edge while there is list to spare,
    // and gives that up at the ends rather than scrolling past them.
    {
        ListWindow window{0, 7, 12};
        window.follow(5, 2);
        expect(window.top == 1, "crossing the lower margin scrolls just enough");
        expect(window.row_of(5) == 4, "and the caret lands inside the window");
        window.follow(11, 2);
        expect(window.top == 5, "the far end pins rather than overscrolling");
        expect(window.visible(11), "and the last row is on screen");
        expect(legend_of(window, buffer) == "6-12 OF 12",
               "the legend names the last page");
        window.follow(0, 2);
        expect(window.top == 0, "walking back pins to the first row");
        expect(legend_of(window, buffer) == "1-7 OF 12",
               "and the legend names the first page");
    }

    // Degenerate inputs stay inside the list rather than off it.
    {
        ListWindow window{50, 7, 12};
        window.clamp();
        expect(window.top == 5, "a window past the end clamps back to it");
        ListWindow negative{-4, 7, 12};
        negative.clamp();
        expect(negative.top == 0, "a window above the list clamps to zero");
        ListWindow empty{3, 7, 0};
        empty.clamp();
        expect(empty.top == 0, "an empty list has nothing to scroll");
        expect(empty.shown() == 0 && empty.end() == 0,
               "and shows nothing");
        expect(!empty.visible(0), "with no visible row");
        expect(legend_of(empty, buffer).empty(), "and no legend");
        ListWindow zero_rows{0, 0, 12};
        zero_rows.follow(11, 0);
        expect(zero_rows.rows >= 1, "a zero-row window is widened to one");
        expect(zero_rows.visible(11), "and still reaches the last row");
    }

    // A margin wider than the window cannot be satisfied on both sides at
    // once; what it must not do is leave the caret off the screen.
    {
        ListWindow window{0, 3, 20};
        for (int cursor = 0; cursor < 20; ++cursor) {
            window.follow(cursor, 9);
            expect(window.visible(cursor),
                   "an oversized margin still keeps the caret visible");
        }
    }

    // The legend writes into caller-owned storage and never past it.
    {
        ListWindow window{5, 7, 120};
        char exact[scroll_legend_size];
        std::memset(exact, 0x7F, sizeof exact);
        const int written = scroll_legend(window, exact, sizeof exact);
        expect(std::string_view(exact) == "6-12 OF 120",
               "the legend reads as its window");
        expect(written == 11, "and reports what it wrote");

        char cramped[6];
        std::memset(cramped, 0x7F, sizeof cramped);
        const int short_write = scroll_legend(window, cramped, sizeof cramped);
        expect(short_write <= 5, "a cramped buffer is not overrun");
        expect(cramped[short_write] == '\0',
               "and is terminated exactly where it stopped");
        expect(std::string_view(cramped) == std::string_view("6-12 OF 120", 5),
               "having written as much of the legend as it had room for");

        char one[1];
        one[0] = 0x7F;
        expect(scroll_legend(window, one, sizeof one) == 0,
               "a one-byte buffer holds only the terminator");
        expect(one[0] == '\0', "which is written");
    }

    if (failures == 0) {
        std::cout << "RESULT list_view PASS\n";
        return 0;
    }
    std::cout << "RESULT list_view FAIL " << failures << '\n';
    return 1;
}
