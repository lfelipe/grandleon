// SPDX-License-Identifier: MIT
// A list longer than the screen, and the caret that walks it.
//
// The same problem `Camera` solves for a board, in one dimension and over rows
// rather than cells. It is here rather than inside a client for exactly the
// reason the camera is: two consoles draw the company, both of them have to
// scroll it the same way, and a scrolling rule written twice is a rule that can
// disagree with itself, and silently, because the failure of a list that does
// not scroll is a row that is simply not drawn.
//
// Pure integer arithmetic, no allocation, no engine header. `tests/view/`
// compiles it and pins the edges, so what a console run has to prove is only
// that its renderer asked.
//
// The rule, stated once:
//
//   * A list that fits does not scroll, and its window is byte-identical to
//     what a renderer with no window at all would have drawn. This is what
//     makes the shipped four-member company unchanged by any of it.
//   * A list that does not fit keeps the caret at least `margin` rows from
//     the window's edge, where the list allows it, and pins to the ends
//     rather than scrolling past them.
//   * A window that is scrolling says so, in one legend both renderers place
//     rather than word.

#pragma once

#include <cstddef>

namespace grandleon::view {

// ---------------------------------------------------------------------------
// ListWindow
// ---------------------------------------------------------------------------

struct ListWindow final {
    // First visible item.
    int top{0};
    // How many items fit on the screen at once. At least one.
    int rows{1};
    // How many items the list holds.
    int total{0};

    // Keeps the window inside the list. A window larger than the list pins to
    // zero rather than centring, so a fitting list renders exactly as it did
    // before there was a window.
    void clamp() noexcept {
        const int limit = total > rows ? total - rows : 0;
        if (top < 0) top = 0;
        if (top > limit) top = limit;
    }

    // Scrolls so the caret stays at least `margin` rows from the window's
    // edge, where the list allows it.
    //
    // A margin wider than half the window is asking for the caret to be kept
    // away from two edges that are the same edge, and `Camera` would answer by
    // scrolling the cursor off the screen. `Camera` is never handed one,
    // because a board's viewport is always wider than its margin. A list
    // *is* handed one: a company window is a few rows and a caller is entitled
    // to say "keep two rows of context" without knowing how few. So the margin
    // is taken as the most the window can honour, and the caret staying
    // visible is the invariant that outranks it.
    void follow(int cursor, int margin) noexcept {
        if (rows < 1) rows = 1;
        if (margin < 0) margin = 0;
        const int most = (rows - 1) / 2;
        if (margin > most) margin = most;
        if (cursor < top + margin) top = cursor - margin;
        if (cursor > top + rows - 1 - margin) {
            top = cursor - (rows - 1 - margin);
        }
        clamp();
    }

    // Whether any of the list is off the screen. A window that answers false
    // is a window nothing has to say anything about.
    [[nodiscard]] bool scrolls() const noexcept { return total > rows; }

    // How many rows the window actually occupies: the whole list when it fits.
    [[nodiscard]] int shown() const noexcept {
        if (total < 0) return 0;
        return total < rows ? total : rows;
    }

    // One past the last visible item.
    [[nodiscard]] int end() const noexcept { return top + shown(); }

    [[nodiscard]] bool visible(int item) const noexcept {
        return item >= top && item < end();
    }

    // Where an item lands within the window, or -1 when it is off it.
    [[nodiscard]] int row_of(int item) const noexcept {
        return visible(item) ? item - top : -1;
    }
};

// ---------------------------------------------------------------------------
// The legend
// ---------------------------------------------------------------------------

// What a scrolling window says about itself: `4-10 OF 12`, one-based and
// inclusive, so a player reading it counts the way the rows in front of them
// are counted.
//
// Here rather than in either renderer because it is the wording, and the
// wording is the part a player would notice differing between two consoles.
// A window that does not scroll writes an empty string and says nothing,
// which is what keeps a company that fits looking exactly as it always did.
//
// Writes into caller-owned storage and returns how many characters it wrote,
// excluding the terminator. Nothing here allocates, formats through `printf`,
// or touches a locale: this header is compiled for a machine whose global
// constructors do not run.
inline constexpr std::size_t scroll_legend_size = 24;

namespace detail {

// Digits of a non-negative number, most significant first. Returns how many.
inline int write_number(int value, char* into, std::size_t room) noexcept {
    if (room == 0) return 0;
    if (value < 0) value = 0;
    char reversed[12];
    int digits = 0;
    do {
        reversed[digits++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && digits < 12);
    int written = 0;
    while (digits > 0 && static_cast<std::size_t>(written) < room) {
        into[written++] = reversed[--digits];
    }
    return written;
}

inline int write_text(const char* text, char* into, std::size_t room) noexcept {
    int written = 0;
    while (text[written] != '\0' && static_cast<std::size_t>(written) < room) {
        into[written] = text[written];
        ++written;
    }
    return written;
}

}  // namespace detail

inline int scroll_legend(
    const ListWindow& window, char* into, std::size_t size
) noexcept {
    if (into == nullptr || size == 0) return 0;
    into[0] = '\0';
    if (!window.scrolls() || window.shown() <= 0) return 0;
    // One less than `size` throughout, so the terminator always has a place.
    const std::size_t room = size - 1U;
    std::size_t at = 0;
    const auto left = [&]() noexcept { return room > at ? room - at : 0U; };
    at += static_cast<std::size_t>(
        detail::write_number(window.top + 1, into + at, left())
    );
    at += static_cast<std::size_t>(detail::write_text("-", into + at, left()));
    at += static_cast<std::size_t>(
        detail::write_number(window.end(), into + at, left())
    );
    at += static_cast<std::size_t>(
        detail::write_text(" OF ", into + at, left())
    );
    at += static_cast<std::size_t>(
        detail::write_number(window.total, into + at, left())
    );
    into[at] = '\0';
    return static_cast<int>(at);
}

}  // namespace grandleon::view
