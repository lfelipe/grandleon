// SPDX-License-Identifier: MIT
// A controller on a machine with no library to read one.
//
// The Nintendo 64 polls a joypad through libdragon's API. There is nothing to
// reuse here: Nugget's `psyqo` is inline assembly throughout and a translation
// unit that saw one of its
// headers could not be held to this repository's `-Wpedantic -Wconversion
// -Werror` discipline. So SIO0 is bit-banged, exactly as the panning scratch
// bit-banged it, and this file is that code with the measurement it produced
// written down beside it rather than left in a document.
//
// ---------------------------------------------------------------------------
// The transfer
//
// Port one, five bytes out (0x01, 0x42 and three pad bytes) and five back:
// 0xFF, the device identifier (0x41 is a digital pad), 0x5A, and two button
// halves in which a *clear* bit is a pressed button.
//
// ---------------------------------------------------------------------------
// The acknowledgement budget, which is a measurement and not a preference
//
// Measured on this emulator: a poll costs **257 µs with a 128-spin
// acknowledgement budget and 3,572 µs with a
// 2,000-spin one**, because PCSX-Redux delivers each byte and never raises the
// acknowledgement level at all, seen on 0 of 4 waits, every poll. A generous
// budget is therefore not a safety margin, it is four timeouts burned every
// frame, and it was more than twice the whole rest of a frame.
//
// So the budget is 128: long enough for real hardware, where a pad raises /ACK
// about ten microseconds after each byte, and cheap enough for an emulator that
// never raises it. `acknowledgements()` counts how often the level was actually
// seen, so the number is never taken on trust: a machine that does raise it
// says so in the report rather than being assumed.
//
// The same measurement left a note for whoever wrote this: *read the pad from
// a vertical-retrace handler, not synchronously in the frame*. That advice is
// declined here, deliberately and with the reason recorded in `turn_exe.cpp`:
// this executable has no interrupt handler at all, its frame is a fifth of a
// millisecond of work against 16,667 µs of budget, and a handler would buy
// latency this client cannot spend: the client blocks on a press, it does not
// sample one.
//
// Nothing in this file is in `engine/`, and nothing in `engine/` knows it
// exists.

#ifndef GRANDLEON_PLATFORM_PLAYSTATION_PSX_PAD_H
#define GRANDLEON_PLATFORM_PLAYSTATION_PSX_PAD_H

#include <cstdint>

namespace grandleon::playstation::pad {

// Prepares the port. Cheap, and idempotent; `read()` reconfigures the port on
// every poll anyway, because a transfer that assumed the port was still as it
// left it would be a transfer that broke the first time anything else touched
// it.
void begin() noexcept;

// Every button held right now, in `grandleon::client::turn`'s vocabulary. Zero
// when nothing is held, when nothing is plugged in, and when the port did not
// answer. All three are "no press" to a client, and are told apart by
// `answered()` and `identifier()` in the report rather than in the state
// machine.
[[nodiscard]] std::uint16_t read() noexcept;

// Every button that went down since the last call to this function. Edge
// triggered, so one press is one step and a script's meaning never depends on
// how long a thumb rested.
//
// A held direction repeating is built on top of this rather than into it: the
// poll loop in `turn_exe.cpp` counts frames against `read()` and decides,
// because the repeat must not reach a client that is being handed a recorded
// list of presses. See `view::repeat_due`.
[[nodiscard]] std::uint16_t pressed() noexcept;

// ---------------------------------------------------------------------------
// What the polling cost, measured here rather than quoted from anywhere. All
// of these are over the run so far.
// ---------------------------------------------------------------------------

// How many polls have been made, and what they cost in microseconds.
[[nodiscard]] std::uint32_t polls() noexcept;
[[nodiscard]] std::uint32_t total_microseconds() noexcept;
[[nodiscard]] std::uint32_t shortest_microseconds() noexcept;
[[nodiscard]] std::uint32_t longest_microseconds() noexcept;

// How many polls got the device identifier a digital pad answers with.
[[nodiscard]] std::uint32_t answers() noexcept;

// The last identifier byte seen, whatever it was, so a port that answered
// something else is reported rather than silently treated as empty.
[[nodiscard]] std::uint8_t identifier() noexcept;

// How many acknowledgement waits were made and how many of them saw the level.
// Under PCSX-Redux this came out at 0 of 4 per poll; a machine that differs
// says so here.
[[nodiscard]] std::uint32_t acknowledgement_waits() noexcept;
[[nodiscard]] std::uint32_t acknowledgements() noexcept;

}  // namespace grandleon::playstation::pad

#endif  // GRANDLEON_PLATFORM_PLAYSTATION_PSX_PAD_H
