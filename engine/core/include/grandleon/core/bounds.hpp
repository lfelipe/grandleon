// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>

// The one bounds question every parser of untrusted bytes asks.
//
// A file says "there is a region of `size` bytes at `offset`". Before anything
// reads it, somebody has to decide whether that region is inside the buffer
// that arrived. Written the obvious way it is a subtraction,
// `size > total - offset`, and on unsigned arithmetic that subtraction wraps
// the moment `offset` exceeds `total`, which turns the guard into a permission
// slip: the comparison passes against a number close to 2^64 and the read that
// follows walks off the end of the buffer.
//
// An offset past the end is not exotic. It does not need a hostile size field,
// only a layout rule that computes one offset from another (an alignment that
// rounds up, a record stride, a header size added to a cursor) over a file
// whose length the rule was never told.
//
// So the question lives here, once, and every parser asks it rather than
// spelling it again. `engine/campaign` and `engine/package_format` both depend
// on `grandleon::core` and neither depends on the other, which is why the
// answer sits in the module below both.
namespace grandleon::core {

// Whether `[offset, offset + size)` lies inside a buffer of `total` bytes.
//
// Both parameters are `std::size_t` because every caller then indexes with the
// value it checked: a narrower parameter would check a truncated offset and
// index with the whole one, which is a passing check over a read the check
// never saw. The subtraction is safe because the first half of the conjunction
// has already established that it cannot wrap.
//
// An empty region still has to clear the first half. Zero bytes are readable
// anywhere, so `size` alone would accept any offset at all and hand the caller
// a pointer it must not form. The offset is usually what the next step of the
// walk is computed from.
[[nodiscard]] constexpr bool checked_region(
    std::size_t total,
    std::size_t offset,
    std::size_t size
) noexcept {
    return offset <= total && size <= total - offset;
}

}  // namespace grandleon::core
