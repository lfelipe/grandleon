// SPDX-License-Identifier: MIT
// The freestanding seam between the portable engine and a PlayStation with no
// operating system. Everything here exists because a `-nostdlib` R3000A link
// has to be given what a hosted C++ program is handed for free: somewhere to
// print, somewhere to allocate, and a clock.
//
// Nothing in this file is in `engine/`, and nothing in `engine/` knows it
// exists.

#pragma once

#include <cstddef>
#include <cstdint>

namespace grandleon::playstation {

// One report line, assembled in a fixed buffer and pushed out in a single
// flush.
//
// The channel is the BIOS teletype: the kernel's `putchar` entry point, which
// PCSX-Redux intercepts and writes to its own stdout. It is deliberately
// character-at-a-time rather than the kernel's `printf`, because a conformance
// report should not depend on a reimplemented varargs formatter to say whether
// the engine is correct. `reaches_the_teletype_through_printf()` in the
// conformance executable exercises the kernel `printf` separately, so the
// question the evaluation asked about it is still answered.
//
// The buffer is a member rather than a heap allocation so that a line can be
// printed while the allocator is being measured, or after it has been filled.
class Line {
  public:
    Line& text(const char* value);
    Line& decimal(std::uint32_t value);
    Line& signed_decimal(int value);
    Line& hex64(std::uint64_t value);
    void flush();

    // The line so far, for a caller assembling a label to pass to something
    // that prints. The buffer is always terminated, so this is safe before a
    // flush and meaningless after one.
    [[nodiscard]] const char* c_str() const noexcept { return buffer_; }

  private:
    void push(char value);

    static constexpr std::size_t capacity = 220;
    char buffer_[capacity]{};
    std::size_t length_ = 0;
};

void print(const char* message);

// Hands control to the host, synchronously, from inside the store instruction
// that does it: PCSX-Redux runs `PCSX.execSlots[slot]()` on a byte write to
// 0x1f802081 and resumes the CPU when it returns. That is what lets an observer
// look at the machine at an instant the program names, rather than at whatever
// instant it happened to poll.
//
// `slot` must not be zero. Zero is `pcsx_debugbreak`, which *pauses* the
// emulator and waits for a debugger that is not there. README.md records the
// same trap for Nugget's `abort()`, and in a script it is a hang rather than a
// result.
//
// A slot with no function behind it is ignored rather than an error, so an
// executable that calls this runs identically with no observer attached. The
// emulator checks `isfunction` before the call; nothing here depends on that
// being true.
void signal_host(int slot);

// What the heap has been asked for, and what it gave back. The peak is the
// console-side counterpart to the ~159 KiB the host-side instrumented allocator
// measured for the content path.
struct HeapCensus {
    std::uint32_t free_bytes;
    std::uint32_t allocated_bytes;
    std::uint32_t peak_allocated_bytes;
    std::uint32_t largest_free_block;
    std::uint32_t allocations;
    std::uint32_t live_allocations;
};

HeapCensus heap_census();
void reset_heap_peak();
std::uint32_t heap_capacity();

// Root counter 2 in divide-by-eight mode, extended past its sixteen bits by
// polling. The unit is the counter's own tick: one eighth of the 33.868800 MHz
// system clock, so 4.2336 ticks per microsecond.
void start_clock();
std::uint32_t clock_ticks();

constexpr std::uint32_t ticks_per_1000_microseconds = 4234;
constexpr std::uint32_t cpu_cycles_per_1000_ticks = 8000;

// ---------------------------------------------------------------------------
// The frame
//
// A board that is steered has a frame; a board drawn once does not need one.
// What a frame is on this machine, with no operating system and no interrupt
// handler installed, is a question with two answers, and this takes both:
//
//   * **The vertical-blanking request.** Bit 0 of I_STAT is raised once a
//     display frame whether or not the interrupt is unmasked, so it can be
//     polled and acknowledged without an exception handler ever running. This
//     is the real retrace, and it is what a moving picture wants.
//
//   * **Root counter 2, bounded.** The wait above is given a budget of a little
//     over two frames' worth of ticks and returns when it expires. So a machine
//     whose I_STAT is being acknowledged by somebody else (a BIOS handler, a
//     debugger) still gets a frame-paced loop instead of a hang, and says so.
//
// Which of the two answered is counted rather than assumed: `vblank_waits` and
// `vblank_retraces` are reported, and a run where the two differ is a run where
// the retrace was not there.
// ---------------------------------------------------------------------------

// Returns once per display frame. Never blocks longer than the budget above.
void wait_vblank();

// How many times `wait_vblank` has been called, and how many of those saw the
// vertical-blanking request rather than timing out against the counter.
[[nodiscard]] std::uint32_t vblank_waits();
[[nodiscard]] std::uint32_t vblank_retraces();

}  // namespace grandleon::playstation
