// SPDX-License-Identifier: MIT
// A memory card, as a byte window.
//
// This is the whole PlayStation half of the save path. Everything a slot is
// belongs to `grandleon::storage::ByteWindowSlotStorage`: the names, the
// directory, the budget, the ordering, every refusal. It links nothing and is
// proved on a host by `tests/storage/storage_contract_test.cpp`. What is left
// for a console is moving bytes, and that is all this file does. The Nintendo
// 64's `sram_window.h` is the same file for a cartridge; the two consoles
// differ in the bus and in nothing above it.
//
// ---------------------------------------------------------------------------
// Why the card is spoken to directly, and not through the BIOS
//
// The kernel offers two ways in: `_card_read`/`_card_write`, which are
// asynchronous and deliver their completion as an event, and `open`/`read`/
// `write` over `bu00:`, which walks the card's directory for you. Both would
// mean installing the BIOS's own SIO0 interrupt handling underneath a program
// that already bit-bangs SIO0 to read its pad: two owners of one port, one of
// them invisible.
//
// So the card is spoken to the way the pad is: the documented protocol, on the
// documented registers, in this repository's own code. `psx_pad.h` made the
// argument first and it applies unchanged here. The protocol is two commands
// and a checksum, and speaking it directly is what makes the transfer
// something a reader of this file can check against a hardware reference
// rather than against a kernel nobody has the source of.
//
// ---------------------------------------------------------------------------
// The protocol, in full, because everything below is an encoding of it
//
// The card is device 0x81 on port one. Every exchange is a byte out and a byte
// in at once, and the interesting byte usually arrives one position after the
// byte that asked for it.
//
//   read a frame        write a frame
//   ----------------    -----------------
//   81  -               81  -
//   52  flag            57  flag
//   00  5A              00  5A            the card's identifier, two bytes
//   00  5D              00  5D
//   MSB 00              MSB 00            the frame number, big-endian
//   LSB 00              LSB 00
//   00  5C              128 data bytes out
//   00  5D              checksum out
//   00  MSB             00  5C
//   00  LSB             00  5D
//   128 data bytes in   00  end
//   00  checksum
//   00  end
//
// The checksum is the exclusive-or of the two frame-number bytes and all 128
// data bytes. The end byte is 0x47 for a frame the card took, 0x4E for a
// checksum it did not believe and 0xFF for a frame number it does not have.
// A card that is not plugged in answers 0xFF to everything and acknowledges
// nothing, which is how absence is told from refusal.
//
// ---------------------------------------------------------------------------
// Why a shadow, and why only the changed frames go back
//
// A frame is 128 bytes and a card is 1,024 of them. The slot directory above
// reads and rewrites its whole image on every publish, so a window that spoke
// to the card per call would spend a hundred and twenty-eight transfers on a
// save that changed one. The window therefore holds its region in `.bss`,
// fills it once at boot, and on commit writes back exactly the frames whose
// bytes are different from what the card was last known to hold.
//
// That is not an optimisation of a measurement, it is the difference between a
// save the player waits through and one they do not. The counters below are
// what makes the claim checkable rather than asserted: every executable that
// holds a window prints the frames it read, the frames it wrote and the
// microseconds they cost, and a resuming run's written count is zero.

#ifndef GRANDLEON_PLATFORM_PLAYSTATION_PSX_CARD_H
#define GRANDLEON_PLATFORM_PLAYSTATION_PSX_CARD_H

#include <grandleon/storage/byte_window_storage.hpp>

#include <cstddef>
#include <cstdint>

namespace grandleon::playstation::card {

// One frame, and how many of them a card holds. Both are the hardware's.
inline constexpr std::size_t frame_bytes = 128U;
inline constexpr std::size_t card_frames = 1024U;

// A block is what the card's own directory allocates in, and what its file
// manager shows the player one of.
inline constexpr std::size_t block_frames = 64U;
inline constexpr std::size_t block_bytes = frame_bytes * block_frames;

// The save is one file of two blocks, and *which* two is the card's answer
// rather than this file's: a card is shared with every other game the player
// owns, so the blocks are found in the card's own directory: the file's own if
// it is already there, otherwise the first free ones. A game that wrote to a
// fixed block would overwrite whatever was in it.
//
// Two blocks rather than one because of what has to fit. A campaign save is 864
// bytes for Tarnholt and 3,128 for the largest this repository produces
// (`tests/campaign/save_test.cpp`), and four slots of the larger is 12,512. One
// block is 8,192 bytes and leaves 7,776 after the two reserved frames and the
// slot directory: enough for Tarnholt four times over and not enough for the
// general case. Two blocks leave **15,968**, which holds four of the largest
// with room to spare, and cost an eighth of the card the player might rather
// spend on another game.
//
//   2 blocks x 64 frames x 128 bytes            16,384
//   less the title and icon frames                -256
//   less the slot directory's header and four
//   entries (16 + 4 x 36)                         -160
//   = what a campaign may write                 15,968
inline constexpr std::size_t save_blocks = 2U;
inline constexpr std::size_t save_bytes = save_blocks * block_bytes;

// The directory sits in block zero and describes the fifteen after it.
inline constexpr std::size_t directory_blocks = 15U;
inline constexpr std::size_t first_data_block = 1U;

// The head of the file is the card's, not the campaign's. Frame zero of the
// first block is the title frame every card manager reads to find out what to
// print beside the entry, and frame one is the icon it prints there. That icon
// is a sixteen-by-sixteen image at four bits a pixel, which is 128 bytes and
// so exactly one frame. So the window handed to the slot directory starts two
// frames in, and those 256 bytes are never storage's to see.
inline constexpr std::size_t title_frame_index = 0U;
inline constexpr std::size_t icon_frame_index = 1U;
inline constexpr std::size_t reserved_frames = 2U;
inline constexpr std::size_t window_frames =
    save_blocks * block_frames - reserved_frames;
inline constexpr std::size_t window_bytes = window_frames * frame_bytes;

// How many slots the directory inside the file reserves room for. Four, which
// is what the save menu offers and what the Nintendo 64 reserves, so that the
// two consoles present a player with the same choice.
inline constexpr std::size_t save_slots = 4U;

// What the card's own file manager calls this save, and what the campaign
// recognises its own file by when it finds one.
//
// The convention is two bytes of region, ten of product code and eight the game
// chooses, and the whole name is exactly twenty bytes. It is not enforced,
// because the card does not parse it, but a name that does not follow it is a
// name every card manager in the world displays oddly, and a name *longer*
// than twenty is a name silently cut in the directory, which is how a game
// stops recognising its own save. The length is asserted below so it can only
// be got wrong once.
inline constexpr char save_file_name[] = "BESLES-00000CAMPAIGN";
inline constexpr std::size_t file_name_bytes = 20U;
static_assert(
    sizeof save_file_name == file_name_bytes + 1U,
    "a card file name is exactly twenty bytes and the directory cuts a longer "
    "one"
);

// What the manager shows beside it, in the card's own title frame. That field
// is sixty-four bytes and a longer title is one the card cuts, so the length is
// asserted here for the same reason the file name's is.
inline constexpr char save_file_title[] = "GRANDLEON";
static_assert(
    sizeof save_file_title <= 65U,
    "a card file title is at most sixty-four bytes and the title frame cuts a "
    "longer one"
);

// Reasons a transfer did not happen. Reported rather than thrown, because
// there are no exceptions on this machine and because a save that could not be
// written is a thing the *screen* has to say.
enum class Fault : std::uint8_t {
    none = 0,
    // Nothing answered on the port. No card, or no slot.
    absent,
    // The card answered but not with its own identifier.
    not_a_card,
    // The card refused the frame number.
    bad_frame,
    // The checksum did not survive the wire.
    bad_checksum,
    // The card ended a transfer with a byte that is none of the three the
    // protocol defines.
    unknown_reply,
    // A card with no directory on it. Not formatted here: formatting is
    // erasing, and erasing somebody's card because this game did not
    // understand it is the worst thing this file could do.
    unformatted,
    // A card whose directory this save's file is on, in a shape that is not
    // the shape this build writes.
    foreign_file,
    // A card with no room left for the file.
    card_full,
};

[[nodiscard]] const char* fault_name(Fault fault) noexcept;

// The region of the card the campaign keeps its slots in, as a byte window.
class MemoryCardWindow final : public grandleon::storage::ByteWindow {
public:
    // Reads the region off the card. A card that is not there leaves the
    // window unavailable rather than empty, which is the distinction
    // `ByteWindowSlotStorage` needs to tell "no save yet" from "no device".
    MemoryCardWindow();

    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] bool read(
        std::size_t offset, std::uint8_t* into, std::size_t length
    ) const override;
    [[nodiscard]] bool write(
        std::size_t offset, const std::uint8_t* from, std::size_t length
    ) override;
    // Writes back every frame whose bytes differ from what the card holds, and
    // the card's own directory entry the first time there is anything to point
    // it at. False if any frame did not land.
    [[nodiscard]] bool commit() override;

    // Whether a card answered at all. Distinct from `was_formatted` above it:
    // this is about the hardware, that is about what was found on it.
    [[nodiscard]] bool present() const noexcept { return present_; }
    [[nodiscard]] Fault fault() const noexcept { return fault_; }

    // Throw the shadow away and read the card again.
    //
    // The proof that bytes reached the device, and the same argument
    // `sram_window.h` makes: every commit's answer is about a transfer rather
    // than about persistence, so the only way to strengthen it without
    // switching the machine off is to scribble over the copy that was written
    // from and fetch the region back. The scribble is what stops a card that
    // silently answers nothing from returning the very bytes it never took.
    void reload();

    // Counters, so what the save cost is reported rather than assumed.
    [[nodiscard]] std::uint32_t frames_read() const noexcept {
        return frames_read_;
    }
    [[nodiscard]] std::uint32_t frames_written() const noexcept {
        return frames_written_;
    }
    [[nodiscard]] std::uint32_t microseconds() const noexcept {
        return microseconds_;
    }
    [[nodiscard]] std::uint32_t commits() const noexcept { return commits_; }
    // Whether the file was already on the card when this window opened, and
    // which block it starts in. Reported so a run says where it put itself.
    [[nodiscard]] bool adopted() const noexcept { return adopted_; }
    [[nodiscard]] std::size_t first_block() const noexcept {
        return blocks_[0];
    }

private:
    [[nodiscard]] bool claim_blocks();
    [[nodiscard]] bool fill();
    [[nodiscard]] bool publish_directory();
    [[nodiscard]] std::size_t frame_of(std::size_t index) const noexcept;

    // Which of the card's blocks hold the file. Found in the card's own
    // directory rather than assumed, because a card is shared: a game that
    // wrote to a fixed block would overwrite whatever else was in it.
    std::size_t blocks_[save_blocks]{};

    std::uint8_t shadow_[window_bytes]{};
    // What the card was last known to hold, frame for frame. `commit` writes
    // the frames where the two disagree and nothing else.
    std::uint8_t landed_[window_bytes]{};
    bool present_{false};
    bool adopted_{false};
    bool directory_written_{false};
    Fault fault_{Fault::none};
    std::uint32_t frames_read_{0};
    std::uint32_t frames_written_{0};
    std::uint32_t microseconds_{0};
    std::uint32_t commits_{0};
};

// The budget this region carries. Stated by the hardware, worked out by the
// directory, and never guessed at here.
[[nodiscard]] inline grandleon::storage::StorageBudget card_budget() {
    return grandleon::storage::ByteWindowSlotStorage::budget_for(
        window_bytes, save_slots
    );
}

// One frame off the card and one frame onto it, for a caller that wants the
// device rather than the window: the check that formats a card and the check
// that reads one back. Both answer a fault rather than a bool so that a
// failure can be reported by name.
[[nodiscard]] Fault read_frame(std::size_t frame, std::uint8_t* into) noexcept;
[[nodiscard]] Fault write_frame(
    std::size_t frame, const std::uint8_t* from
) noexcept;

}  // namespace grandleon::playstation::card

#endif  // GRANDLEON_PLATFORM_PLAYSTATION_PSX_CARD_H
