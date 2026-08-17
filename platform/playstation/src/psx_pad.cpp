// SPDX-License-Identifier: MIT
// See psx_pad.h. Nothing here is in `engine/`.

#include "psx_pad.h"

#include "psx_runtime.h"

#include <grandleon/client/turn_client.hpp>

namespace grandleon::playstation::pad {
namespace {

namespace turn = grandleon::client::turn;

// SIO0, uncached through KSEG1. `JOY_DATA` is byte-wide; everything else is
// read and written at the width the hardware decodes.
volatile std::uint8_t* const joy_data =
    reinterpret_cast<volatile std::uint8_t*>(0xbf801040);
volatile std::uint32_t* const joy_stat =
    reinterpret_cast<volatile std::uint32_t*>(0xbf801044);
volatile std::uint16_t* const joy_mode =
    reinterpret_cast<volatile std::uint16_t*>(0xbf801048);
volatile std::uint16_t* const joy_ctrl =
    reinterpret_cast<volatile std::uint16_t*>(0xbf80104a);
volatile std::uint16_t* const joy_baud =
    reinterpret_cast<volatile std::uint16_t*>(0xbf80104e);

constexpr std::uint32_t stat_tx_ready = 1u << 0;
constexpr std::uint32_t stat_rx_not_empty = 1u << 1;
constexpr std::uint32_t stat_ack = 1u << 7;

// One byte at 250 kHz is 32 microseconds, about 1,100 cycles of a 33.8688 MHz
// R3000A, so a few thousand turns of a four-instruction spin is generous for a
// pad that is answering and cheap for one that is not.
constexpr int transfer_spins = 2000;

// The measured budget. psx_pad.h says why it is this and not the other.
constexpr int ack_spins = 128;

// The device identifier a digital pad answers with. A dual-shock in digital
// mode answers the same, which is why this is the only one worth naming.
constexpr std::uint8_t digital_pad = 0x41;

// The two button halves, as they arrive, inverted to active-high. Byte three
// is the first half and byte four the second, so bit 8 upwards is the second.
//
//   half one   select . . start  up right down left
//   half two   L2 R2 L1 R1  triangle circle cross square
constexpr std::uint16_t raw_select = 1u << 0;
constexpr std::uint16_t raw_start = 1u << 3;
constexpr std::uint16_t raw_up = 1u << 4;
constexpr std::uint16_t raw_right = 1u << 5;
constexpr std::uint16_t raw_down = 1u << 6;
constexpr std::uint16_t raw_left = 1u << 7;
constexpr std::uint16_t raw_triangle = 1u << 12;
constexpr std::uint16_t raw_circle = 1u << 13;
constexpr std::uint16_t raw_cross = 1u << 14;
constexpr std::uint16_t raw_square = 1u << 15;

std::uint16_t held_previously = 0;
std::uint32_t poll_count = 0;
std::uint32_t microseconds_total = 0;
std::uint32_t microseconds_shortest = 0xFFFFFFFFu;
std::uint32_t microseconds_longest = 0;
std::uint32_t answered_count = 0;
std::uint8_t last_identifier = 0;
std::uint32_t ack_waited = 0;
std::uint32_t ack_seen = 0;

[[nodiscard]] std::uint8_t exchange(std::uint8_t out, bool& lost) noexcept {
    int spins = transfer_spins;
    while ((*joy_stat & stat_tx_ready) == 0) {
        if (--spins <= 0) {
            lost = true;
            return 0xFF;
        }
    }
    *joy_data = out;
    spins = transfer_spins;
    while ((*joy_stat & stat_rx_not_empty) == 0) {
        if (--spins <= 0) {
            lost = true;
            return 0xFF;
        }
    }
    return *joy_data;
}

// The client's vocabulary, and the whole of the button mapping.
//
//   d-pad      the cursor, and a menu caret
//   cross      pad_a      confirm
//   circle     pad_b      back out
//   triangle   pad_c      the unit action menu
//   start      pad_start  "I am done"
//
// Cross confirms and circle cancels, which is the Western PlayStation
// convention. `grandleon/client/turn_client.hpp` records the choice where a
// reader of a script will find it, and square, select and the shoulders are
// deliberately unmapped: a button that does something undocumented is worse
// than a button that does nothing.
[[nodiscard]] constexpr std::uint16_t translate(std::uint16_t raw) noexcept {
    std::uint16_t buttons = 0;
    if ((raw & raw_up) != 0) buttons |= turn::pad_up;
    if ((raw & raw_down) != 0) buttons |= turn::pad_down;
    if ((raw & raw_left) != 0) buttons |= turn::pad_left;
    if ((raw & raw_right) != 0) buttons |= turn::pad_right;
    if ((raw & raw_cross) != 0) buttons |= turn::pad_a;
    if ((raw & raw_circle) != 0) buttons |= turn::pad_b;
    if ((raw & raw_triangle) != 0) buttons |= turn::pad_c;
    if ((raw & raw_start) != 0) buttons |= turn::pad_start;
    // Select and square are read and dropped on purpose; naming them here is
    // what stops a later reader assuming the halves were decoded wrongly.
    static_cast<void>(raw_select);
    static_cast<void>(raw_square);
    return buttons;
}

}  // namespace

void begin() noexcept {
    held_previously = 0;
    (void)read();
    held_previously = 0;
}

std::uint16_t read() noexcept {
    const std::uint32_t opened = clock_ticks();

    *joy_ctrl = 0x0040u;  // reset the port
    for (int i = 0; i < 16; ++i) (void)*joy_stat;
    *joy_mode = 0x000Du;  // eight bits, no parity, baud reload factor 1
    *joy_baud = 0x0088u;  // 250 kHz, which is the pad's rate
    *joy_ctrl = 0x0003u;  // transmit enable, controller port one selected
    for (int i = 0; i < 64; ++i) (void)*joy_stat;

    static const std::uint8_t request[5] = {0x01, 0x42, 0x00, 0x00, 0x00};
    std::uint8_t reply[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool lost = false;
    for (int i = 0; i < 5; ++i) {
        reply[i] = exchange(request[i], lost);
        if (lost) break;
        if (i == 4) break;
        ++ack_waited;
        int spins = ack_spins;
        while ((*joy_stat & stat_ack) == 0) {
            if (--spins <= 0) break;
        }
        if (spins > 0) ++ack_seen;
    }
    *joy_ctrl = 0;

    const std::uint32_t elapsed = clock_ticks() - opened;
    const std::uint32_t microseconds =
        elapsed * 1000u / ticks_per_1000_microseconds;
    ++poll_count;
    microseconds_total += microseconds;
    if (microseconds < microseconds_shortest) microseconds_shortest = microseconds;
    if (microseconds > microseconds_longest) microseconds_longest = microseconds;

    last_identifier = reply[1];
    if (lost || reply[1] != digital_pad) return 0;
    ++answered_count;

    const std::uint16_t raw = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(reply[3]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(reply[4]) << 8)
    );
    // A clear bit is a pressed button, so the halves are inverted before
    // anything reads them.
    return translate(static_cast<std::uint16_t>(~raw));
}

std::uint16_t pressed() noexcept {
    const std::uint16_t now = read();
    const std::uint16_t edges =
        static_cast<std::uint16_t>(now & static_cast<std::uint16_t>(~held_previously));
    held_previously = now;
    return edges;
}

std::uint32_t polls() noexcept { return poll_count; }
std::uint32_t total_microseconds() noexcept { return microseconds_total; }
std::uint32_t shortest_microseconds() noexcept {
    return poll_count == 0 ? 0 : microseconds_shortest;
}
std::uint32_t longest_microseconds() noexcept { return microseconds_longest; }
std::uint32_t answers() noexcept { return answered_count; }
std::uint8_t identifier() noexcept { return last_identifier; }
std::uint32_t acknowledgement_waits() noexcept { return ack_waited; }
std::uint32_t acknowledgements() noexcept { return ack_seen; }

}  // namespace grandleon::playstation::pad
