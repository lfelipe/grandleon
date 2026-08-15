// SPDX-License-Identifier: MIT
#include <grandleon/core/bounds.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace core = grandleon::core;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::size_t maximum = std::numeric_limits<std::size_t>::max();

// The subtraction every parser writes before it learns not to. Spelled out
// here, once, so that the case it gets wrong is a comparison in a test rather
// than a paragraph in a comment.
[[nodiscard]] constexpr bool the_obvious_thing(
    std::size_t total,
    std::size_t offset,
    std::size_t size
) noexcept {
    return size <= total - offset;
}

// What a region is, at the edges. Every one of these is a `static_assert`
// because the guard is `constexpr`: a build that gets one wrong does not
// produce a test that fails, it produces no build at all.
static_assert(core::checked_region(0, 0, 0));
static_assert(core::checked_region(10, 0, 10));
static_assert(core::checked_region(10, 10, 0));
static_assert(core::checked_region(10, 4, 6));
static_assert(!core::checked_region(10, 4, 7));
static_assert(!core::checked_region(10, 11, 0));
static_assert(!core::checked_region(0, 1, 0));
static_assert(!core::checked_region(10, maximum, 0));
static_assert(!core::checked_region(10, 0, maximum));

// The whole reason this function exists rather than being written inline. An
// offset past the end makes the subtraction wrap to a number near the top of
// the address space, and every size on earth is below it. So the guard that
// looks like a bounds check is a guard that accepts everything, precisely in
// the case where accepting is a read of memory the buffer does not own.
static_assert(the_obvious_thing(10, 13, 4));
static_assert(!core::checked_region(10, 13, 4));

// And it is not only huge sizes that slip through. Three bytes past the end,
// which is exactly what rounding an offset up to a four-byte boundary can
// produce, admits any claim at all.
static_assert(the_obvious_thing(97, 100, 64));
static_assert(!core::checked_region(97, 100, 64));

// An offset one past the end with nothing claimed at it. The size test alone
// cannot see this (zero bytes fit anywhere), but the offset is what a walk
// then computes its next step from, so it has to be refused on its own.
static_assert(the_obvious_thing(10, 11, 0));
static_assert(!core::checked_region(10, 11, 0));

// The two agree everywhere the offset is inside the buffer, which is why the
// wrong one survives review: over any input a well-formed file produces, it is
// the right one.
void the_two_agree_on_every_offset_a_buffer_contains() {
    bool agreed = true;
    for (std::size_t total = 0; total <= 64; ++total) {
        for (std::size_t offset = 0; offset <= total; ++offset) {
            for (std::size_t size = 0; size <= 80; ++size) {
                if (core::checked_region(total, offset, size) !=
                    the_obvious_thing(total, offset, size)) {
                    agreed = false;
                }
            }
        }
    }
    expect(
        agreed,
        "inside the buffer the guard and the subtraction decide alike"
    );
}

// Past the end they never agree, at any size. There is no offset beyond the
// buffer at which the subtraction refuses anything.
void past_the_end_the_subtraction_refuses_nothing() {
    bool always_wrong = true;
    bool always_refused = true;
    for (std::size_t total = 0; total <= 64; ++total) {
        for (std::size_t past = 1; past <= 16; ++past) {
            for (std::size_t size = 0; size <= 80; ++size) {
                if (!the_obvious_thing(total, total + past, size)) {
                    always_wrong = false;
                }
                if (core::checked_region(total, total + past, size)) {
                    always_refused = false;
                }
            }
        }
    }
    expect(always_wrong, "the subtraction accepts every region past the end");
    expect(always_refused, "and the guard accepts none of them");
}

// A region can be checked and then indexed with the value that was checked.
// The property the guard is bought for, stated as the loop a caller writes.
void what_the_guard_permits_is_readable() {
    const std::uint8_t buffer[16] = {};
    bool stayed_inside = true;
    for (std::size_t offset = 0; offset <= 24; ++offset) {
        for (std::size_t size = 0; size <= 24; ++size) {
            if (!core::checked_region(sizeof buffer, offset, size)) {
                continue;
            }
            if (offset > sizeof buffer || offset + size > sizeof buffer) {
                stayed_inside = false;
            }
        }
    }
    expect(
        stayed_inside,
        "an accepted region ends inside the buffer and starts inside it too"
    );
}

}  // namespace

int main() {
    the_two_agree_on_every_offset_a_buffer_contains();
    past_the_end_the_subtraction_refuses_nothing();
    what_the_guard_permits_is_readable();
    return failures == 0 ? 0 : 1;
}
