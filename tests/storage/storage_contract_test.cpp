// SPDX-License-Identifier: MIT
#include <grandleon/campaign/outcome.hpp>
#include <grandleon/campaign/save.hpp>
#include <grandleon/storage/byte_window_storage.hpp>
#include <grandleon/storage/filesystem_storage.hpp>
#include <grandleon/storage/memory_storage.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace campaign = grandleon::campaign;
namespace core = grandleon::core;
namespace storage = grandleon::storage;

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::vector<std::uint8_t> payload(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(seed + index);
    }
    return bytes;
}

// ---------------------------------------------------------------------------
// The contract. One function, run against every implementation, because a
// promise only one adapter keeps is not a contract.
// ---------------------------------------------------------------------------

void the_slot_contract(storage::SlotStorage& device, const std::string& label) {
    const std::string where = label + ": ";

    expect(device.slots().empty(), where + "a fresh device holds nothing");
    expect(!device.contains("chapter_one"), where + "and contains nothing");
    expect(
        device.read("chapter_one").error == storage::StorageError::not_found,
        where + "and reading a slot that was never written is not_found"
    );
    expect(
        device.erase("chapter_one") == storage::StorageError::not_found,
        where + "and so is erasing one"
    );

    const std::vector<std::uint8_t> first = payload(300, 7);
    expect(
        device.write("chapter_one", first) == storage::StorageError::none,
        where + "a slot can be written"
    );
    expect(device.contains("chapter_one"), where + "and is then there");
    const storage::StorageRead back = device.read("chapter_one");
    expect(static_cast<bool>(back), where + "and reads back");
    expect(back.bytes == first, where + "byte for byte");

    // Opaque means opaque: the device may not care what is in the bytes, so
    // every byte value has to survive, including the ones a text-mode write
    // would mangle.
    std::vector<std::uint8_t> every_byte(256);
    for (std::size_t index = 0; index < every_byte.size(); ++index) {
        every_byte[index] = static_cast<std::uint8_t>(index);
    }
    expect(
        device.write("every-byte", every_byte) == storage::StorageError::none,
        where + "a slot holding every byte value can be written"
    );
    expect(
        device.read("every-byte").bytes == every_byte,
        where + "and comes back unaltered, nulls and newlines and all"
    );

    const std::vector<std::uint8_t> second = payload(40, 200);
    expect(
        device.write("chapter_one", second) == storage::StorageError::none,
        where + "a slot can be overwritten"
    );
    expect(
        device.read("chapter_one").bytes == second,
        where + "and holds all of the new save and none of the old one"
    );

    expect(
        device.write("empty", {}) == storage::StorageError::none,
        where + "an empty slot is a slot"
    );
    const storage::StorageRead nothing = device.read("empty");
    expect(
        static_cast<bool>(nothing) && nothing.bytes.empty(),
        where + "and reads back as present and empty rather than absent"
    );

    const std::vector<std::string> names = device.slots();
    expect(
        names.size() == 3U && names[0] == "chapter_one" && names[1] == "empty" &&
            names[2] == "every-byte",
        where + "the slot list is exactly what was written, in ascending order"
    );
    expect(
        storage::used_bytes(device) == second.size() + every_byte.size(),
        where + "and the device knows what it is holding"
    );

    expect(
        device.erase("empty") == storage::StorageError::none,
        where + "a slot can be erased"
    );
    expect(!device.contains("empty"), where + "and is then gone");
    expect(device.slots().size() == 2U, where + "and gone from the listing");
    expect(
        device.read("chapter_one").bytes == second,
        where + "while its neighbours are untouched"
    );

    // Names, refused identically everywhere. A filesystem would have taken
    // several of these; the shared vocabulary is what stops it.
    const std::string bad_names[] = {
        "",
        "../escape",
        "save/one",
        "Chapter",
        "chapter one",
        "chapter.one",
        std::string(storage::maximum_slot_name_length + 1U, 'a'),
    };
    for (const std::string& name : bad_names) {
        expect(
            device.write(name, first) == storage::StorageError::invalid_slot_name,
            where + "writing '" + name + "' is refused as a name"
        );
        expect(
            device.read(name).error == storage::StorageError::invalid_slot_name,
            where + "and so is reading it"
        );
        expect(
            device.erase(name) == storage::StorageError::invalid_slot_name,
            where + "and erasing it"
        );
        expect(!device.contains(name), where + "and it is never contained");
    }
    expect(
        device.slots().size() == 2U,
        where + "and none of that wrote anything"
    );

    expect(
        device.write(std::string(storage::maximum_slot_name_length, 'z'), first) ==
            storage::StorageError::none,
        where + "a name at the length limit is fine"
    );
    expect(device.erase(std::string(storage::maximum_slot_name_length, 'z')) ==
               storage::StorageError::none,
           where + "and can be erased again");

    for (const std::string& name : device.slots()) {
        expect(
            device.erase(name) == storage::StorageError::none,
            where + "the device can be emptied"
        );
    }
    expect(device.slots().empty(), where + "and is then empty again");
}

// A device of a stated size, made fresh. The budget tests need three of them,
// each constrained in a different direction, so that "out of space" is never
// ambiguous about which limit it ran into.
using DeviceFactory = std::function<
    std::unique_ptr<storage::SlotStorage>(storage::StorageBudget, const std::string&)>;

// The budget, run against devices deliberately the size of a console's. This is
// the shape of the constrained-storage risk the design names, exercised on a
// host years before there is a cartridge to exercise it on.
void the_budget_contract(const DeviceFactory& make, const std::string& label) {
    const std::string where = label + " budget: ";

    const std::unique_ptr<storage::SlotStorage> per_slot =
        make(storage::StorageBudget{64U, 4096U, 8U}, "budget_slot");
    expect(
        per_slot->budget().maximum_slot_bytes == 64U,
        where + "the device states how large one slot may be"
    );
    expect(
        per_slot->write("big", payload(65, 1)) == storage::StorageError::too_large,
        where + "a payload over one slot is too_large"
    );
    expect(!per_slot->contains("big"), where + "and nothing was written");
    expect(
        per_slot->write("big", payload(64, 1)) == storage::StorageError::none,
        where + "a payload exactly at the slot size fits"
    );

    const std::unique_ptr<storage::SlotStorage> per_device =
        make(storage::StorageBudget{64U, 100U, 8U}, "budget_total");
    expect(
        per_device->write("one", payload(64, 1)) == storage::StorageError::none,
        where + "one save fits"
    );
    expect(
        per_device->write("two", payload(37, 2)) == storage::StorageError::out_of_space,
        where + "a second that would push the device past its total is out_of_space"
    );
    expect(!per_device->contains("two"), where + "and is not there");
    expect(
        per_device->read("one").bytes == payload(64, 1),
        where + "and the first is untouched"
    );
    expect(
        per_device->write("two", payload(36, 2)) == storage::StorageError::none,
        where + "one that fills the device exactly is written"
    );
    expect(
        per_device->write("two", payload(37, 3)) == storage::StorageError::out_of_space,
        where + "growing a slot past the total is refused"
    );
    expect(
        per_device->read("two").bytes == payload(36, 2),
        where + "and the slot still holds the save it held before"
    );
    // The budget is measured over what the device holds *after* the write. An
    // implementation that summed every existing slot and then added the new
    // bytes would refuse this, and a player would be unable to overwrite a full
    // save file with a smaller one.
    expect(
        per_device->write("one", payload(10, 4)) == storage::StorageError::none,
        where + "and shrinking a slot on a full device never fails for space"
    );
    expect(
        storage::used_bytes(*per_device) == 46U,
        where + "leaving the device holding what it should"
    );

    const std::unique_ptr<storage::SlotStorage> per_count =
        make(storage::StorageBudget{64U, 4096U, 2U}, "budget_count");
    expect(
        per_count->write("one", payload(4, 1)) == storage::StorageError::none &&
            per_count->write("two", payload(4, 2)) == storage::StorageError::none,
        where + "a two-slot device takes two saves"
    );
    expect(
        per_count->write("three", payload(4, 3)) == storage::StorageError::out_of_space,
        where + "and refuses a third with room to spare in bytes"
    );
    expect(
        per_count->slots().size() == 2U,
        where + "and still holds the two it had"
    );
    expect(
        per_count->write("two", payload(8, 4)) == storage::StorageError::none,
        where + "while overwriting an existing slot is not a third slot"
    );
}

// ---------------------------------------------------------------------------

std::string test_root(const char* leaf) {
    return std::string(GRANDLEON_STORAGE_TEST_ROOT) + "/" + leaf;
}

void a_fresh_directory(const std::string& path) {
    std::error_code code;
    std::filesystem::remove_all(std::filesystem::path(path), code);
}

void the_memory_device_keeps_the_contract() {
    storage::MemorySlotStorage device;
    the_slot_contract(device, "memory");

    the_budget_contract(
        [](storage::StorageBudget budget, const std::string&) {
            return std::unique_ptr<storage::SlotStorage>(
                new storage::MemorySlotStorage(budget)
            );
        },
        "memory"
    );
}

void the_desktop_directory_keeps_the_same_contract() {
    const std::string root = test_root("contract");
    a_fresh_directory(root);
    storage::FilesystemSlotStorage device(root);
    expect(device.available(), "the save directory is created if it is not there");
    the_slot_contract(device, "filesystem");

    the_budget_contract(
        [](storage::StorageBudget budget, const std::string& name) {
            const std::string path = test_root(name.c_str());
            a_fresh_directory(path);
            return std::unique_ptr<storage::SlotStorage>(
                new storage::FilesystemSlotStorage(path, budget)
            );
        },
        "filesystem"
    );
}

// ---------------------------------------------------------------------------
// The cartridge, on a host.
//
// `ByteWindowSlotStorage` is what a Nintendo 64 SRAM cartridge and any other
// battery window both are once the directory is written: a flat region of bytes
// with no names in it. The device runs here against ordinary memory, so its
// layout, its ordering and its out-of-space paths are pinned by `ctest` on
// every commit, and an emulator only has to prove the part a host cannot: that
// the region survives the power switch.
// ---------------------------------------------------------------------------

// The Nintendo 64 cartridge's own numbers: 32,768 bytes of SRAM, four slots.
constexpr std::size_t sram_bytes = 32U * 1024U;
constexpr std::size_t sram_slots = 4U;

void the_byte_window_device_keeps_the_contract() {
    // A window at exactly the size the default budget asks for, so the device
    // is not quietly running with room to spare.
    const storage::StorageBudget budget{4096U, 16384U, 8U};
    storage::VectorByteWindow window(
        storage::ByteWindowSlotStorage::bytes_required(budget)
    );
    storage::ByteWindowSlotStorage device(window, budget);
    expect(device.available(), "a window large enough for its budget is available");
    expect(
        !device.was_formatted(),
        "and a window nobody has written is an empty device, not a broken one"
    );
    the_slot_contract(device, "byte window");

    the_budget_contract(
        [](storage::StorageBudget scoped, const std::string&) {
            // The window outlives the device it is handed to, which is the
            // ownership the console adapter has: the cartridge is there before
            // the program is.
            static std::vector<std::unique_ptr<storage::VectorByteWindow>> kept;
            kept.push_back(
                std::unique_ptr<storage::VectorByteWindow>(
                    new storage::VectorByteWindow(
                        storage::ByteWindowSlotStorage::bytes_required(scoped)
                    )
                )
            );
            return std::unique_ptr<storage::SlotStorage>(
                new storage::ByteWindowSlotStorage(*kept.back(), scoped)
            );
        },
        "byte window"
    );
}

// The property a cartridge exists for, proved against the layout rather than
// against the hardware: a second device over the same region finds what the
// first one left.
void a_byte_window_survives_the_device_that_wrote_it() {
    const storage::StorageBudget budget =
        storage::ByteWindowSlotStorage::budget_for(sram_bytes, sram_slots);
    expect(
        budget.maximum_slots == sram_slots,
        "a 32 KiB cartridge carries four slots"
    );
    expect(
        budget.maximum_total_bytes == sram_bytes - 16U - 36U * sram_slots,
        "and everything the header and directory do not take"
    );
    // The measured campaign save is 2,868 bytes and the largest this
    // repository has produced is 3,128, both pinned by
    // `tests/campaign/save_test.cpp`. The figure below is what the cartridge
    // actually offers, and the assertion is that it is comfortably more.
    expect(
        budget.maximum_total_bytes >= 4U * 3128U,
        "which is room for four of the largest campaign save measured"
    );

    storage::VectorByteWindow window(sram_bytes, 0xFF);
    {
        storage::ByteWindowSlotStorage first(window, budget);
        expect(
            !first.was_formatted(),
            "a cartridge full of erase-state bytes reads as an empty device"
        );
        expect(
            first.write("chapter_one", payload(2628, 3)) ==
                storage::StorageError::none,
            "a campaign-sized save fits a cartridge"
        );
        expect(
            first.write("chapter_two", payload(2888, 9)) ==
                storage::StorageError::none,
            "and so does a second beside it"
        );
    }

    storage::ByteWindowSlotStorage second(window, budget);
    expect(second.was_formatted(), "the region carries a directory afterwards");
    expect(
        second.read("chapter_one").bytes == payload(2628, 3),
        "and a later device finds the first save byte for byte"
    );
    expect(
        second.read("chapter_two").bytes == payload(2888, 9),
        "and the second"
    );
    expect(
        second.slots().size() == 2U && second.slots().front() == "chapter_one",
        "and lists exactly the two, in order"
    );

    // A cartridge nobody has saved to holds whatever it holds. Every one of
    // these must read as an empty device rather than as a directory, because
    // the alternative is a save menu built out of noise.
    const std::uint8_t junk_fills[] = {0x00, 0xFF, 0x55, 0xAA};
    for (const std::uint8_t fill : junk_fills) {
        storage::VectorByteWindow noise(sram_bytes, fill);
        storage::ByteWindowSlotStorage device(noise, budget);
        expect(
            device.available() && !device.was_formatted() &&
                device.slots().empty(),
            "an unwritten cartridge is an empty device"
        );
    }

    // One flipped byte anywhere in the image is a refusal, not a mangled save.
    // The image a single 2,628-byte save writes is bytes 0 to 2,679: sixteen
    // of header, thirty-six of directory, then the save. Each offset below is
    // a different part of it: the magic, the slot count, a name, a length,
    // the middle of the payload, and its last byte.
    for (const std::size_t at : {2U, 6U, 20U, 60U, 2000U, 2679U}) {
        storage::VectorByteWindow damaged(sram_bytes, 0xFF);
        {
            storage::ByteWindowSlotStorage writer(damaged, budget);
            expect(
                writer.write("chapter_one", payload(2628, 3)) ==
                    storage::StorageError::none,
                "a save is written before it is damaged"
            );
        }
        damaged.bytes()[at] = static_cast<std::uint8_t>(
            damaged.bytes()[at] ^ 0x01U
        );
        storage::ByteWindowSlotStorage device(damaged, budget);
        expect(
            !device.was_formatted() && device.slots().empty(),
            "a single flipped byte is refused rather than half-read"
        );
    }

    // A window too small for the budget it is given is a device that is not
    // there, and says so instead of writing off the end of the cartridge.
    storage::VectorByteWindow undersized(sram_bytes / 2U);
    storage::ByteWindowSlotStorage absent(undersized, budget);
    expect(!absent.available(), "a window smaller than its budget is unavailable");
    expect(
        absent.write("chapter_one", payload(4, 1)) ==
            storage::StorageError::unavailable,
        "and writing to it says unavailable"
    );
    expect(
        absent.read("chapter_one").error == storage::StorageError::unavailable,
        "and so does reading"
    );
    expect(absent.slots().empty(), "and it lists nothing");
    expect(
        absent.write("save/one", payload(4, 1)) ==
            storage::StorageError::invalid_slot_name,
        "and a bad name is still a bad name before the device is consulted"
    );
}

// What only a device with a disk behind it can be wrong about.
void the_desktop_directory_is_a_directory() {
    const std::string root = test_root("desktop");
    a_fresh_directory(root);

    {
        storage::FilesystemSlotStorage device(root);
        expect(
            device.write("chapter_one", payload(128, 3)) == storage::StorageError::none,
            "a save is written"
        );
    }

    // The whole reason this adapter exists: a second run of the program finds
    // what the first one left.
    storage::FilesystemSlotStorage reopened(root);
    expect(
        reopened.read("chapter_one").bytes == payload(128, 3),
        "and a later session finds it exactly where it was left"
    );
    expect(
        reopened.slots().size() == 1U && reopened.slots().front() == "chapter_one",
        "and lists it once"
    );

    // The directory belongs to this adapter, but it is still a directory.
    std::ofstream(std::filesystem::path(root) / "notes.txt") << "not a save";
    std::ofstream(std::filesystem::path(root) / "chapter_one.gls.tmp") << "half a save";
    std::ofstream(std::filesystem::path(root) / "Chapter.gls") << "not a slot name";
    expect(
        reopened.slots().size() == 1U,
        "a file that is not a slot is not listed as one"
    );
    expect(
        reopened.read("chapter_one").bytes == payload(128, 3),
        "and the real slot is unaffected by what is sitting next to it"
    );

    // A completed write leaves nothing staged behind.
    expect(
        reopened.write("chapter_one", payload(64, 5)) == storage::StorageError::none,
        "the slot is rewritten"
    );
    std::error_code code;
    expect(
        !std::filesystem::exists(
            std::filesystem::path(root) / "chapter_one.gls.tmp", code
        ),
        "and the staged file it was written through is gone"
    );
    expect(
        reopened.read("chapter_one").bytes == payload(64, 5),
        "and the slot holds the new save whole"
    );

    // A save slot is a file on somebody's disk, and a file on somebody's disk
    // is untrusted input. `write` refuses a payload larger than one slot, so a
    // slot larger than one slot did not come from this adapter: it was
    // dropped in, grown by another program, or is not a save at all. Reading
    // it means allocating whatever the directory entry says, so the size is
    // checked before a byte is read rather than after.
    {
        const std::string capped_root = test_root("capped");
        a_fresh_directory(capped_root);
        const storage::StorageBudget small{64U, 4096U, 8U};
        storage::FilesystemSlotStorage capped(capped_root, small);
        expect(
            capped.write("chapter_one", payload(64, 3)) ==
                storage::StorageError::none,
            "a slot at the device's limit is written"
        );
        {
            std::ofstream oversized(
                std::filesystem::path(capped_root) / "chapter_two.gls",
                std::ios::binary
            );
            const std::vector<std::uint8_t> too_much = payload(65, 9);
            oversized.write(
                reinterpret_cast<const char*>(too_much.data()),
                static_cast<std::streamsize>(too_much.size())
            );
        }
        const storage::StorageRead refused = capped.read("chapter_two");
        expect(
            refused.error == storage::StorageError::too_large,
            "a slot file larger than the device's slot size is too_large"
        );
        expect(
            refused.bytes.empty(),
            "and not one byte of it was read into memory"
        );
        expect(
            capped.read("chapter_one").bytes == payload(64, 3),
            "while the slot beside it still reads back"
        );
    }

    // A root that cannot be a directory is a device that is not present, and
    // every operation says so instead of throwing.
    std::ofstream(std::filesystem::path(root) / "occupied") << "a file";
    storage::FilesystemSlotStorage absent(
        (std::filesystem::path(root) / "occupied").string()
    );
    expect(!absent.available(), "a root that cannot be a directory is unavailable");
    expect(
        absent.write("chapter_one", payload(4, 1)) == storage::StorageError::unavailable,
        "and writing to it says unavailable"
    );
    expect(
        absent.read("chapter_one").error == storage::StorageError::unavailable,
        "and so does reading"
    );
    expect(
        absent.erase("chapter_one") == storage::StorageError::unavailable,
        "and erasing"
    );
    expect(!absent.contains("chapter_one"), "and it contains nothing");
    expect(absent.slots().empty(), "and lists nothing");
    expect(
        absent.write("../escape", payload(4, 1)) ==
            storage::StorageError::invalid_slot_name,
        "and a bad name is still a bad name before the device is even consulted"
    );
}

// The seam this whole suite is for: a campaign, through the save layer, through
// a device, and back to the same campaign.
void a_campaign_survives_a_device() {
    campaign::CampaignState state;
    const auto batch = campaign::make_outcome_batch(
        {{core::PackageId{}, core::ContentCategory::encounter, 11U}, 0xabcdULL, 0U},
        {
            campaign::recruit_unit(
                campaign::PersistentEntityId{1},
                {core::PackageId{}, core::ContentCategory::unit_type, 5U}
            ),
            campaign::add_item(
                campaign::PersistentEntityId{},
                {core::PackageId{}, core::ContentCategory::item, 9U},
                6U
            ),
        }
    );
    expect(
        static_cast<bool>(campaign::apply_outcome(state, batch)),
        "a campaign is told something"
    );
    const campaign::CampaignSave save = campaign::make_campaign_save(state, {});
    const std::vector<std::uint8_t> bytes = campaign::save_campaign(save);

    const std::string root = test_root("round_trip");
    a_fresh_directory(root);
    storage::FilesystemSlotStorage disk(root);
    storage::MemorySlotStorage memory;
    // The cartridge in the same list as the disk, because the whole claim of
    // this layer is that a campaign does not know which one it landed on.
    storage::VectorByteWindow cartridge_window(sram_bytes, 0xFF);
    storage::ByteWindowSlotStorage cartridge(
        cartridge_window,
        storage::ByteWindowSlotStorage::budget_for(sram_bytes, sram_slots)
    );

    storage::SlotStorage* devices[] = {&disk, &memory, &cartridge};
    for (storage::SlotStorage* device : devices) {
        expect(
            device->write("chapter_one", bytes) == storage::StorageError::none,
            "the save is written to a device"
        );
        const storage::StorageRead stored = device->read("chapter_one");
        expect(static_cast<bool>(stored), "and read back");
        expect(stored.bytes == bytes, "as the same opaque bytes");
        const campaign::SaveLoadResult loaded =
            campaign::load_campaign(stored.bytes, campaign::SaveLoadOptions{});
        expect(
            static_cast<bool>(loaded),
            std::string(campaign::save_error_name(loaded.error))
        );
        expect(loaded.save == save, "and the same campaign it started as");
    }
}

}  // namespace

int main() {
    the_memory_device_keeps_the_contract();
    the_desktop_directory_keeps_the_same_contract();
    the_byte_window_device_keeps_the_contract();
    a_byte_window_survives_the_device_that_wrote_it();
    the_desktop_directory_is_a_directory();
    a_campaign_survives_a_device();
    return failures == 0 ? 0 : 1;
}
