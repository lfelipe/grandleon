// SPDX-License-Identifier: MIT
// See psx_card.h. Nothing here is in `engine/`.

#include "psx_card.h"

#include "psx_runtime.h"

#include <cstring>

namespace grandleon::playstation::card {
namespace {

// SIO0, uncached through KSEG1, at the same addresses and the same widths
// `psx_pad.cpp` uses. The two files touch the same port and neither leaves it
// configured: every transfer here reconfigures it on the way in and clears the
// select on the way out, so a pad poll that follows a save finds the port the
// way it always finds it.
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

// A card is slower to answer than a pad, and a frame is a hundred and forty
// exchanges rather than five, so the per-byte budget is the pad's and the
// acknowledgement budget is not: a card on real hardware raises the level
// within a few tens of microseconds, and this emulator never raises it at all
// (`psx_pad.h` measured that and says so). The wait is therefore a floor on
// how fast the bytes are pushed rather than a wait for anything.
constexpr int transfer_spins = 2000;
constexpr int ack_spins = 128;

// The device address that selects the memory card rather than the controller.
constexpr std::uint8_t card_device = 0x81;
constexpr std::uint8_t command_read = 0x52;
constexpr std::uint8_t command_write = 0x57;

// The two bytes a card names itself with.
constexpr std::uint8_t card_id_first = 0x5A;
constexpr std::uint8_t card_id_second = 0x5D;
// The two it acknowledges a command with.
constexpr std::uint8_t card_ack_first = 0x5C;
constexpr std::uint8_t card_ack_second = 0x5D;

constexpr std::uint8_t end_good = 0x47;
constexpr std::uint8_t end_bad_checksum = 0x4E;
constexpr std::uint8_t end_bad_frame = 0xFF;

bool lost = false;

[[nodiscard]] std::uint8_t exchange(std::uint8_t out) noexcept {
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
    const std::uint8_t in = *joy_data;
    // The card raises its acknowledgement between bytes. Waited for rather
    // than required, for the reason above.
    spins = ack_spins;
    while ((*joy_stat & stat_ack) == 0) {
        if (--spins <= 0) break;
    }
    return in;
}

void open_port() noexcept {
    lost = false;
    *joy_ctrl = 0x0040u;  // reset the port
    for (int i = 0; i < 16; ++i) (void)*joy_stat;
    *joy_mode = 0x000Du;  // eight bits, no parity, baud reload factor 1
    *joy_baud = 0x0088u;  // 250 kHz, which is the card's rate
    *joy_ctrl = 0x0003u;  // transmit enable, port one selected
    for (int i = 0; i < 64; ++i) (void)*joy_stat;
}

void close_port() noexcept { *joy_ctrl = 0; }

std::uint32_t transfer_microseconds = 0;

// The opening five exchanges both commands share: address the card, name the
// command, take the card's identifier, and hand over the frame number. `why`
// comes back naming what stopped it when it did not get that far.
[[nodiscard]] bool address_frame(
    std::uint8_t command, std::size_t frame, Fault& why
) noexcept {
    why = Fault::none;
    (void)exchange(card_device);
    (void)exchange(command);
    const std::uint8_t id_first = exchange(0x00);
    const std::uint8_t id_second = exchange(0x00);
    if (lost) {
        why = Fault::absent;
        return false;
    }
    if (id_first != card_id_first || id_second != card_id_second) {
        // 0xFF on every byte is an empty slot; anything else is a device that
        // is not a card. Both stop the transfer and only one is worth a
        // different word on the screen.
        why = (id_first == 0xFF && id_second == 0xFF) ? Fault::absent
                                                      : Fault::not_a_card;
        return false;
    }
    (void)exchange(static_cast<std::uint8_t>((frame >> 8) & 0xFFU));
    (void)exchange(static_cast<std::uint8_t>(frame & 0xFFU));
    if (lost) why = Fault::absent;
    return !lost;
}

[[nodiscard]] Fault end_fault(std::uint8_t end) noexcept {
    if (end == end_good) return Fault::none;
    if (end == end_bad_checksum) return Fault::bad_checksum;
    if (end == end_bad_frame) return Fault::bad_frame;
    return Fault::unknown_reply;
}

}  // namespace

const char* fault_name(Fault fault) noexcept {
    switch (fault) {
        case Fault::none: return "none";
        case Fault::absent: return "absent";
        case Fault::not_a_card: return "not-a-card";
        case Fault::bad_frame: return "bad-frame";
        case Fault::bad_checksum: return "bad-checksum";
        case Fault::unknown_reply: return "unknown-reply";
        case Fault::unformatted: return "unformatted";
        case Fault::foreign_file: return "foreign-file";
        case Fault::card_full: return "card-full";
    }
    return "unknown";
}

Fault read_frame(std::size_t frame, std::uint8_t* into) noexcept {
    if (frame >= card_frames || into == nullptr) return Fault::bad_frame;
    const std::uint32_t opened = clock_ticks();
    open_port();
    Fault why = Fault::none;
    if (!address_frame(command_read, frame, why)) {
        close_port();
        transfer_microseconds +=
            (clock_ticks() - opened) * 1000u / ticks_per_1000_microseconds;
        return why;
    }
    const std::uint8_t ack_first = exchange(0x00);
    const std::uint8_t ack_second = exchange(0x00);
    const std::uint8_t confirm_high = exchange(0x00);
    const std::uint8_t confirm_low = exchange(0x00);
    std::uint8_t checksum = static_cast<std::uint8_t>(confirm_high ^ confirm_low);
    for (std::size_t i = 0; i < frame_bytes; ++i) {
        into[i] = exchange(0x00);
        checksum = static_cast<std::uint8_t>(checksum ^ into[i]);
    }
    const std::uint8_t claimed = exchange(0x00);
    const std::uint8_t end = exchange(0x00);
    close_port();
    transfer_microseconds +=
        (clock_ticks() - opened) * 1000u / ticks_per_1000_microseconds;

    if (lost) return Fault::absent;
    if (ack_first != card_ack_first || ack_second != card_ack_second) {
        return Fault::unknown_reply;
    }
    // The card echoes the frame it is about to send. A card that echoed a
    // different one has been asked for a frame it does not have, whatever it
    // says at the end.
    if (confirm_high != static_cast<std::uint8_t>((frame >> 8) & 0xFFU) ||
        confirm_low != static_cast<std::uint8_t>(frame & 0xFFU)) {
        return Fault::bad_frame;
    }
    if (claimed != checksum) return Fault::bad_checksum;
    return end_fault(end);
}

Fault write_frame(std::size_t frame, const std::uint8_t* from) noexcept {
    if (frame >= card_frames || from == nullptr) return Fault::bad_frame;
    const std::uint32_t opened = clock_ticks();
    open_port();
    Fault why = Fault::none;
    if (!address_frame(command_write, frame, why)) {
        close_port();
        transfer_microseconds +=
            (clock_ticks() - opened) * 1000u / ticks_per_1000_microseconds;
        return why;
    }
    std::uint8_t checksum = static_cast<std::uint8_t>(
        ((frame >> 8) & 0xFFU) ^ (frame & 0xFFU)
    );
    for (std::size_t i = 0; i < frame_bytes; ++i) {
        (void)exchange(from[i]);
        checksum = static_cast<std::uint8_t>(checksum ^ from[i]);
    }
    (void)exchange(checksum);
    const std::uint8_t ack_first = exchange(0x00);
    const std::uint8_t ack_second = exchange(0x00);
    const std::uint8_t end = exchange(0x00);
    close_port();
    transfer_microseconds +=
        (clock_ticks() - opened) * 1000u / ticks_per_1000_microseconds;

    if (lost) return Fault::absent;
    if (ack_first != card_ack_first || ack_second != card_ack_second) {
        return Fault::unknown_reply;
    }
    return end_fault(end);
}

// ---------------------------------------------------------------------------
// The card's own directory, so its file manager can see this save
//
// Block zero is the directory. Frame zero is its header, `MC` and a checksum;
// frames one to fifteen describe blocks one to fifteen. An entry is 128 bytes:
//
//   0..3    state         0x51 first block of a file, 0x52 a middle one,
//                         0x53 the last, 0xA0..0xA3 a free one
//   4..7    file size in bytes, over the whole file, little-endian
//   8..9    the next block of the file, or 0xFFFF for the last
//   10..29  the file name, nul-padded
//   127     the exclusive-or of the 127 bytes before it
//
// Frame zero of the file's own first block is its title frame: `SC`, how many
// blocks the file is, the title the manager prints and the icon's palette.
// Frame one is the icon itself.
//
// A card is shared. Nothing here formats one, nothing here touches a block this
// file does not own, and nothing here writes frame zero of block zero: a game
// that could not make sense of a card and reformatted it would be erasing
// somebody's afternoon. An unformatted card is reported and left alone.
// ---------------------------------------------------------------------------
namespace {

void put_checksum(std::uint8_t* frame) noexcept {
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < frame_bytes - 1U; ++i) {
        sum = static_cast<std::uint8_t>(sum ^ frame[i]);
    }
    frame[frame_bytes - 1U] = sum;
}

// Whether the entry's own last byte still describes the 127 before it. The
// card's manager maintains this byte and so does `put_checksum` above, so an
// entry that fails it is an entry nothing on this card wrote intact.
[[nodiscard]] bool entry_checksum_holds(const std::uint8_t* entry) noexcept {
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < frame_bytes - 1U; ++i) {
        sum = static_cast<std::uint8_t>(sum ^ entry[i]);
    }
    return sum == entry[frame_bytes - 1U];
}

[[nodiscard]] bool entry_is_free(const std::uint8_t* entry) noexcept {
    return (entry[0] & 0xF0U) == 0xA0U;
}

[[nodiscard]] bool entry_is_first(const std::uint8_t* entry) noexcept {
    return entry[0] == 0x51U;
}

// The state byte the block at `step` of this file's chain must carry: first,
// last, or one in the middle. Written once because both halves of this file
// need it and they must not be able to disagree. `publish_directory` composes
// exactly these and `claim_blocks` requires exactly these, so a chain that
// this build did not write is refused rather than followed.
[[nodiscard]] std::uint8_t chain_state(std::size_t step) noexcept {
    if (step == 0U) return 0x51U;
    return step + 1U == save_blocks ? 0x53U : 0x52U;
}

[[nodiscard]] bool entry_is_ours(const std::uint8_t* entry) noexcept {
    for (std::size_t i = 0; i < file_name_bytes; ++i) {
        if (entry[10U + i] != static_cast<std::uint8_t>(save_file_name[i])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint16_t entry_next(const std::uint8_t* entry) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(entry[8]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(entry[9]) << 8)
    );
}

// One directory entry for one of the file's blocks.
void compose_directory_entry(
    std::uint8_t* frame, std::uint8_t state, std::uint32_t bytes,
    std::uint16_t next
) noexcept {
    std::memset(frame, 0, frame_bytes);
    frame[0] = state;
    frame[4] = static_cast<std::uint8_t>(bytes & 0xFFU);
    frame[5] = static_cast<std::uint8_t>((bytes >> 8) & 0xFFU);
    frame[6] = static_cast<std::uint8_t>((bytes >> 16) & 0xFFU);
    frame[7] = static_cast<std::uint8_t>((bytes >> 24) & 0xFFU);
    frame[8] = static_cast<std::uint8_t>(next & 0xFFU);
    frame[9] = static_cast<std::uint8_t>((next >> 8) & 0xFFU);
    for (std::size_t i = 0; i < file_name_bytes; ++i) {
        frame[10U + i] = static_cast<std::uint8_t>(save_file_name[i]);
    }
    put_checksum(frame);
}

// The title frame, at the head of the file's first block. The palette lives
// here rather than beside the icon, which is where the format puts it.
void compose_title_frame(std::uint8_t* frame) noexcept {
    std::memset(frame, 0, frame_bytes);
    frame[0] = 'S';
    frame[1] = 'C';
    // One icon frame: this save's icon does not animate.
    frame[2] = 0x11;
    // How many blocks the file is, which is what the manager subtracts from
    // the card when it shows how much room is left.
    frame[3] = static_cast<std::uint8_t>(save_blocks);
    // The title field is sixty-four bytes. The bound is tested before the
    // character is read rather than after it, so the loop cannot walk off the
    // end of the title on the way to noticing that it has.
    for (std::size_t i = 0; i < 64U && save_file_title[i] != '\0'; ++i) {
        frame[4U + i] = static_cast<std::uint8_t>(save_file_title[i]);
    }
    // Sixteen colours at 0x60, five-bit channels with the mask bit set so the
    // manager draws them opaque. Only three are used: nothing, the amber the
    // game is drawn in, and a darker edge under it. Index zero is left at zero,
    // which is this format's transparent.
    static constexpr std::uint16_t palette[3] = {0x0000, 0x929F, 0x84AA};
    for (std::size_t i = 0; i < 3U; ++i) {
        frame[96U + i * 2U] = static_cast<std::uint8_t>(palette[i] & 0xFFU);
        frame[97U + i * 2U] =
            static_cast<std::uint8_t>((palette[i] >> 8) & 0xFFU);
    }
}

// The icon: sixteen rows of sixteen pixels, one nibble each, low nibble first,
// which is 128 bytes and so exactly one frame. A shield, because the manager
// shows it at the size of a fingernail and a shape is all that survives.
constexpr char icon_rows[16][17] = {
    "                ",
    "  1111111111 1  ",
    " 122222222222 1 ",
    " 1222222222221  ",
    " 1222222222221  ",
    " 1222222222221  ",
    " 1222222222221  ",
    " 1222222222221  ",
    "  12222222221   ",
    "  12222222221   ",
    "   122222221    ",
    "    1222221     ",
    "     12221      ",
    "      121       ",
    "       1        ",
    "                ",
};

void compose_icon_frame(std::uint8_t* frame) noexcept {
    std::memset(frame, 0, frame_bytes);
    for (std::size_t row = 0; row < 16U; ++row) {
        for (std::size_t column = 0; column < 16U; ++column) {
            const char cell = icon_rows[row][column];
            const std::uint8_t index = cell == '1'   ? 2U
                                       : cell == '2' ? 1U
                                                     : 0U;
            const std::size_t at = row * 8U + column / 2U;
            const int shift = (column % 2U) == 0U ? 0 : 4;
            frame[at] = static_cast<std::uint8_t>(
                frame[at] | static_cast<std::uint8_t>(index << shift)
            );
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------

MemoryCardWindow::MemoryCardWindow() {
    transfer_microseconds = 0;
    present_ = claim_blocks() && fill();
    microseconds_ = transfer_microseconds;
}

// Where on the card the file is, or where it is going to go.
//
// The card's directory is read, never guessed at. If this game's file is
// already on it, its own chain of blocks is followed and adopted, including
// the case where the player's card manager moved it, which is a thing card
// managers do. If it is not, the first free blocks are taken, and if there are
// not enough the answer is `card_full` rather than somebody else's save.
bool MemoryCardWindow::claim_blocks() {
    std::uint8_t header[frame_bytes];
    if (const Fault fault = read_frame(0, header); fault != Fault::none) {
        fault_ = fault;
        return false;
    }
    ++frames_read_;
    if (header[0] != 'M' || header[1] != 'C') {
        // Not formatted. Formatting it here would be erasing it, and this game
        // has no business doing that to a card it did not recognise.
        fault_ = Fault::unformatted;
        return false;
    }

    std::uint8_t entries[directory_blocks][frame_bytes];
    for (std::size_t block = 0; block < directory_blocks; ++block) {
        const Fault fault =
            read_frame(first_data_block + block, entries[block]);
        if (fault != Fault::none) {
            fault_ = fault;
            return false;
        }
        ++frames_read_;
    }

    // Which entries the card still describes intact. An entry whose own
    // checksum does not hold is read as neither free nor this file's, and both
    // halves of that matter: following a chain out of a damaged entry would be
    // trusting a pointer the card itself says is broken, and handing the block
    // out as free would be giving away one that may still belong to the file
    // whose entry was the thing that got damaged. Either way the answer is to
    // leave it alone, which is what the whole of this section promises.
    bool intact[directory_blocks]{};
    for (std::size_t block = 0; block < directory_blocks; ++block) {
        intact[block] = entry_checksum_holds(entries[block]);
    }

    // This game's own file, if the card is holding one.
    for (std::size_t block = 0; block < directory_blocks; ++block) {
        if (!intact[block] || !entry_is_first(entries[block]) ||
            !entry_is_ours(entries[block])) {
            continue;
        }
        std::size_t at = block;
        for (std::size_t step = 0; step < save_blocks; ++step) {
            // Every block of the chain has to be the block this build would
            // have written there, and being inside the directory is not that.
            // A card is shared: a `next` that is in range but names a block
            // belonging to somebody else's file (their own first block, or a
            // free one) would otherwise be adopted here and overwritten by
            // the first commit, which is the one thing this file promises
            // never to do. A block already taken by an earlier step is refused
            // on the same terms, so a chain that loops back on itself cannot
            // hand out one block twice.
            if (!intact[at] || entries[at][0] != chain_state(step)) {
                fault_ = Fault::foreign_file;
                return false;
            }
            const std::size_t claimed = first_data_block + at;
            for (std::size_t earlier = 0; earlier < step; ++earlier) {
                if (blocks_[earlier] == claimed) {
                    fault_ = Fault::foreign_file;
                    return false;
                }
            }
            blocks_[step] = claimed;
            const std::uint16_t next = entry_next(entries[at]);
            const bool last = step + 1U == save_blocks;
            if (last) {
                // The chain has to end exactly where this build's file ends.
                // A longer or shorter one was written by something else and is
                // not this window's to reinterpret.
                if (next != 0xFFFFU) {
                    fault_ = Fault::foreign_file;
                    return false;
                }
                break;
            }
            if (next >= directory_blocks) {
                fault_ = Fault::foreign_file;
                return false;
            }
            at = next;
        }
        adopted_ = true;
        // Already on the card, so its directory entries are already right and
        // rewriting them would be two more writes for no change.
        directory_written_ = true;
        return true;
    }

    // Not there. The first free blocks, in the order the card lists them.
    std::size_t taken = 0;
    for (std::size_t block = 0; block < directory_blocks && taken < save_blocks;
         ++block) {
        if (!intact[block] || !entry_is_free(entries[block])) continue;
        blocks_[taken++] = first_data_block + block;
    }
    if (taken < save_blocks) {
        fault_ = Fault::card_full;
        return false;
    }
    return true;
}

// Which frame of the card a frame of the window is. The blocks need not be
// next to each other, and the first of them opens with the title and the icon.
std::size_t MemoryCardWindow::frame_of(std::size_t index) const noexcept {
    const std::size_t within = index + reserved_frames;
    const std::size_t block = within / block_frames;
    return blocks_[block] * block_frames + within % block_frames;
}

std::size_t MemoryCardWindow::size() const noexcept { return sizeof shadow_; }

bool MemoryCardWindow::fill() {
    for (std::size_t index = 0; index < window_frames; ++index) {
        const Fault fault = read_frame(
            frame_of(index), shadow_ + index * frame_bytes
        );
        if (fault != Fault::none) {
            fault_ = fault;
            return false;
        }
        ++frames_read_;
    }
    // What the card holds is now what the shadow holds, which is what makes
    // the first commit write only the frames a save actually changed.
    std::memcpy(landed_, shadow_, sizeof shadow_);
    return true;
}

void MemoryCardWindow::reload() {
    // Scribble first, and this is the whole point of the function: a card that
    // answers nothing leaves the shadow holding whatever it held, so a reload
    // that did not scribble would compare the copy it was about to fetch
    // against itself and report a pass on a machine with no card in it.
    std::memset(shadow_, 0x5A, sizeof shadow_);
    transfer_microseconds = 0;
    present_ = present_ && fill();
    microseconds_ += transfer_microseconds;
}

bool MemoryCardWindow::read(
    std::size_t offset, std::uint8_t* into, std::size_t length
) const {
    if (!present_) return false;
    if (length == 0) return offset <= sizeof shadow_;
    if (offset > sizeof shadow_ || length > sizeof shadow_ - offset) {
        return false;
    }
    std::memcpy(into, shadow_ + offset, length);
    return true;
}

bool MemoryCardWindow::write(
    std::size_t offset, const std::uint8_t* from, std::size_t length
) {
    if (!present_) return false;
    if (length == 0) return offset <= sizeof shadow_;
    if (offset > sizeof shadow_ || length > sizeof shadow_ - offset) {
        return false;
    }
    std::memcpy(shadow_ + offset, from, length);
    return true;
}

bool MemoryCardWindow::publish_directory() {
    std::uint8_t frame[frame_bytes];
    // The file's blocks, front to back: the first names the whole size and
    // points at the second, and the last points at nothing. The pointer is a
    // block index counted from the first data block, which is why it is one
    // less than the block number it names.
    for (std::size_t block = 0; block < save_blocks; ++block) {
        const bool last = block + 1U == save_blocks;
        const std::uint8_t state = chain_state(block);
        const std::uint16_t next =
            last ? static_cast<std::uint16_t>(0xFFFFU)
                 : static_cast<std::uint16_t>(
                       blocks_[block + 1U] - first_data_block
                   );
        compose_directory_entry(
            frame, state, static_cast<std::uint32_t>(save_bytes), next
        );
        const Fault fault = write_frame(blocks_[block], frame);
        if (fault != Fault::none) {
            fault_ = fault;
            return false;
        }
        ++frames_written_;
    }
    // And the file's own head, which is the card's rather than the campaign's:
    // what the manager prints, and the picture it prints beside it.
    compose_title_frame(frame);
    Fault fault =
        write_frame(blocks_[0] * block_frames + title_frame_index, frame);
    if (fault != Fault::none) {
        fault_ = fault;
        return false;
    }
    ++frames_written_;
    compose_icon_frame(frame);
    fault = write_frame(blocks_[0] * block_frames + icon_frame_index, frame);
    if (fault != Fault::none) {
        fault_ = fault;
        return false;
    }
    ++frames_written_;
    return true;
}

bool MemoryCardWindow::commit() {
    if (!present_) return false;
    transfer_microseconds = 0;
    ++commits_;

    // The card's own directory, once, and before the payload: a manager that
    // saw a block in use with no entry pointing at it would call the card
    // corrupt, and a player who pulled the power between the two would rather
    // lose a save than a card.
    if (!directory_written_) {
        if (!publish_directory()) {
            microseconds_ += transfer_microseconds;
            return false;
        }
        directory_written_ = true;
    }

    bool landed = true;
    for (std::size_t index = 0; index < window_frames; ++index) {
        const std::size_t offset = index * frame_bytes;
        if (std::memcmp(shadow_ + offset, landed_ + offset, frame_bytes) == 0) {
            continue;
        }
        const Fault fault = write_frame(frame_of(index), shadow_ + offset);
        if (fault != Fault::none) {
            fault_ = fault;
            landed = false;
            continue;
        }
        std::memcpy(landed_ + offset, shadow_ + offset, frame_bytes);
        ++frames_written_;
    }
    microseconds_ += transfer_microseconds;
    return landed;
}

}  // namespace grandleon::playstation::card
