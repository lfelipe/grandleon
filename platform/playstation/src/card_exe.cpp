// SPDX-License-Identifier: MIT
// The memory card, proved.
//
// A save is the one claim a console cannot make from inside a single run: an
// executable that writes bytes and reads them back has proved a buffer. So
// this executable is run twice against the same card image, and what it does
// depends on what it finds:
//
//   the card is blank        write a campaign-shaped payload into every slot
//                            the device offers, then read the card back over
//                            the top of the copy it was written from and check
//                            the bytes again
//   the card holds a save    read it and check that it is byte-for-byte what
//                            the run before wrote
//   the card is refused      check that not one frame went to it, and on what
//                            it was refused. A card with no directory this
//                            game can read, and a card whose directory holds
//                            a file under this game's name in a shape this
//                            build did not write, are both cards that belong
//                            to somebody else until proved otherwise
//
// The second run is the whole point, and it is a *different emulator process*
// against the same file. Nothing in this executable's memory survives between
// them, so a pass on the second run is the card holding the bytes and cannot
// be anything else. `docs/ARES_VALIDATION.md` imposes exactly this obligation
// on the Nintendo 64 and it is not weaker here.
//
// What is under test is the seam and not a model of it: the bytes go through
// `grandleon::storage::ByteWindowSlotStorage`, which is the same device the
// campaign saves into and the same one `tests/storage/storage_contract_test.cpp`
// proves the contract of on a host. This file adds the wire and nothing else.
//
// There is no video output: a card is not a picture, and a renderer is not
// part of this gate.

#include "psx_card.h"
#include "psx_runtime.h"

#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/storage/slot_storage.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace psx = grandleon::playstation;
namespace card = grandleon::playstation::card;
namespace storage = grandleon::storage;

namespace {

int checks = 0;
int failures = 0;

void expect(bool condition, const char* message) {
    ++checks;
    psx::Line line;
    line.text(condition ? "ok   " : "FAIL ").text(message).flush();
    if (!condition) ++failures;
}

bool emulator_present() {
    return *reinterpret_cast<volatile std::uint32_t*>(0x1f802080) == 0x58534350U;
}

// What the slots are called, and what goes in them. The names are the ones
// `view::slot_name_at` composes for a campaign, so this exercises the very
// strings a save screen produces rather than names invented here.
constexpr const char* slot_names[] = {
    "grandleon", "grandleon-2", "grandleon-3", "grandleon-4"
};
constexpr std::size_t slot_count =
    sizeof slot_names / sizeof slot_names[0];

// A payload shaped like a campaign save: 864 bytes is Tarnholt's, and the
// pattern is a function of the slot so that a device which mixed two slots up
// fails rather than passes.
constexpr std::size_t payload_bytes = 864U;

std::vector<std::uint8_t> payload_for(std::size_t slot) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(payload_bytes);
    for (std::size_t i = 0; i < payload_bytes; ++i) {
        bytes.push_back(static_cast<std::uint8_t>(
            (i * 31U + slot * 199U + 7U) & 0xFFU
        ));
    }
    return bytes;
}

void report_window(const card::MemoryCardWindow& window) {
    psx::Line line;
    line.text("CARD present ")
        .signed_decimal(window.present() ? 1 : 0)
        .text(" fault ")
        .text(card::fault_name(window.fault()))
        .text(" bytes ")
        .signed_decimal(static_cast<std::int32_t>(window.size()))
        .text(" block ")
        .signed_decimal(static_cast<std::int32_t>(window.first_block()))
        .text(" adopted ")
        .signed_decimal(window.adopted() ? 1 : 0)
        .flush();
}

void report_transfers(const card::MemoryCardWindow& window) {
    psx::Line line;
    line.text("CARD frames read ")
        .signed_decimal(static_cast<std::int32_t>(window.frames_read()))
        .text(" written ")
        .signed_decimal(static_cast<std::int32_t>(window.frames_written()))
        .text(" commits ")
        .signed_decimal(static_cast<std::int32_t>(window.commits()))
        .text(" us ")
        .signed_decimal(static_cast<std::int32_t>(window.microseconds()))
        .flush();
}

void report_budget(const storage::StorageBudget& budget) {
    psx::Line line;
    line.text("CARD budget slot ")
        .signed_decimal(static_cast<std::int32_t>(budget.maximum_slot_bytes))
        .text(" total ")
        .signed_decimal(static_cast<std::int32_t>(budget.maximum_total_bytes))
        .text(" slots ")
        .signed_decimal(static_cast<std::int32_t>(budget.maximum_slots))
        .flush();
}

// Every slot holds what this executable would have written into it.
void slots_hold_what_was_written(storage::SlotStorage& device) {
    for (std::size_t slot = 0; slot < slot_count; ++slot) {
        const storage::StorageRead held = device.read(slot_names[slot]);
        psx::Line line;
        line.text("CARD slot ").text(slot_names[slot]).text(" error ")
            .text(std::string(storage::storage_error_name(held.error)).c_str())
            .text(" bytes ")
            .signed_decimal(static_cast<std::int32_t>(held.bytes.size()))
            .flush();
        expect(static_cast<bool>(held), "the slot reads back");
        expect(held.bytes == payload_for(slot), "byte for byte");
    }
}

}  // namespace

int main() {
    psx::start_clock();
    psx::print("grandleon playstation card");

    expect(emulator_present(), "PCSX-Redux's control port is present");

    // Static, not on `main`'s frame: the window shadows sixteen kilobytes of
    // the card, and the stack reserve is thirty-two.
    static card::MemoryCardWindow window;
    report_window(window);

    // Four things can be in the slot, and each of them has its own right
    // answer. Which one it is decides what this run proves.
    if (!window.present()) {
        // A card this game will not touch. The only correct thing to do with
        // one is nothing: it is somebody's card and it holds somebody's
        // afternoon. The run script checks the image is byte-for-byte what it
        // was, which is the half this executable cannot check about itself.
        expect(
            window.frames_written() == 0,
            "a card this game refuses is not written to"
        );
        if (window.fault() == card::Fault::unformatted) {
            // Nothing on it this game can read at all.
            expect(
                window.frames_read() == 1,
                "and an unrecognised header is refused without a survey"
            );
        } else {
            // A directory this game read and would not take blocks out of: a
            // file under this game's name whose chain runs on into a block
            // another game owns, or a card with no free block whose entry the
            // card itself can vouch for. Both would mean a commit landing on
            // somebody else's save, and which one it is is on the line below,
            // where the run script reads it.
            expect(
                window.fault() == card::Fault::foreign_file ||
                    window.fault() == card::Fault::card_full,
                "and it is the card's directory that refused it, not the wire"
            );
            expect(
                window.frames_read() == 1U + card::directory_blocks,
                "and the whole directory was read to decide that"
            );
        }
        report_transfers(window);
        psx::Line refusal;
        refusal.text("CARD REFUSED ")
            .text(card::fault_name(window.fault()))
            .flush();
        psx::Line refused;
        refused.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
            .signed_decimal(checks - failures)
            .text("/")
            .signed_decimal(checks)
            .flush();
        return failures == 0 ? 0 : 1;
    }

    expect(window.present(), "a memory card answered on port one");
    expect(window.size() == card::window_bytes, "and offers its whole region");

    const storage::StorageBudget budget = card::card_budget();
    report_budget(budget);
    expect(
        budget.maximum_slots == card::save_slots,
        "the budget offers the slots the save menu does"
    );
    expect(
        budget.maximum_total_bytes >= slot_count * payload_bytes,
        "and holds every slot's save at once"
    );

    storage::ByteWindowSlotStorage device(window, budget);
    expect(device.available(), "the slot device accepted the window");

    const bool formatted = device.was_formatted();
    psx::Line found;
    found.text("CARD formatted ").signed_decimal(formatted ? 1 : 0).flush();

    if (!formatted) {
        expect(!window.adopted(), "a card with no file on it had none found");
        // Write every slot, then prove the bytes left the machine by fetching
        // the region back over the copy they were written from -- which is what
        // `reload` scribbles first to make honest.
        for (std::size_t slot = 0; slot < slot_count; ++slot) {
            const storage::StorageError error =
                device.write(slot_names[slot], payload_for(slot));
            expect(
                error == storage::StorageError::none, "the card took the save"
            );
        }
        report_transfers(window);
        slots_hold_what_was_written(device);

        window.reload();
        expect(window.present(), "and answers again after a reload");
        storage::ByteWindowSlotStorage after(window, budget);
        expect(
            after.was_formatted(), "the region read back off the card parses"
        );
        expect(
            storage::used_bytes(after) == slot_count * payload_bytes,
            "and holds every byte that was written into it"
        );
        slots_hold_what_was_written(after);
        psx::print("CARD wrote a fresh card");
    } else {
        // A card written by a previous *process*. Nothing in this one's memory
        // was there when it was written.
        expect(
            window.adopted(), "the file was found in the card's own directory"
        );
        expect(
            window.frames_written() == 0, "and nothing was written to find it"
        );
        expect(
            storage::used_bytes(device) == slot_count * payload_bytes,
            "the card kept every byte across a power cycle"
        );
        slots_hold_what_was_written(device);
        psx::print("CARD read a card written before this process started");
    }

    report_transfers(window);

    psx::Line result;
    result.text(failures == 0 ? "RESULT PASS " : "RESULT FAIL ")
        .signed_decimal(checks - failures)
        .text("/")
        .signed_decimal(checks)
        .flush();
    return failures == 0 ? 0 : 1;
}
