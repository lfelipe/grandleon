// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/storage/slot_storage.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Slots in a flat region of battery-backed bytes.
//
// This is the shape a console save device actually has. A Nintendo 64
// cartridge with SRAM is thirty-two kilobytes on the peripheral bus, with no
// name, no length, and no second slot: it has addresses. So the thing a
// console adapter is missing is not a device but a *directory*, and a
// directory written twice is a directory that can disagree with itself.
//
// So it is written once, here, above nothing. `ByteWindowSlotStorage` keeps the
// whole `SlotStorage` contract against an abstract region of bytes: the names,
// the budget, the ordering, the refusals. A platform supplies the region.
// What the Nintendo 64 adds is forty lines that move bytes between RDRAM and
// the cartridge bus (`platform/nintendo64/src/sram_window.h`), and it adds
// nothing about what a slot is.
//
// The split is what lets the console device be tested on a host. The contract
// tests in `tests/storage/storage_contract_test.cpp` run the same
// `the_slot_contract` and `the_budget_contract` against this device over a
// `VectorByteWindow`, including at a cartridge's own budget, so the layout,
// the ordering, the refusals and the out-of-space paths are proved by the host
// suite on every commit, and what an emulator run has to prove is only the part
// a host cannot: that the bytes survive the power switch.

namespace grandleon::storage {

// A fixed-size region of bytes somebody else owns.
//
// Deliberately not a pointer. On a Nintendo 64 the cartridge bus is not
// addressable by the CPU in a way `memcpy` may use: a read is a DMA and a
// write is a DMA, so an interface that handed out a `std::uint8_t*` would be
// one no console could implement. Both calls are all-or-nothing over the whole
// range they name, and both answer whether the hardware took it.
class ByteWindow {
public:
    ByteWindow() = default;
    ByteWindow(const ByteWindow&) = delete;
    ByteWindow& operator=(const ByteWindow&) = delete;
    virtual ~ByteWindow() = default;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;

    [[nodiscard]] virtual bool read(
        std::size_t offset,
        std::uint8_t* into,
        std::size_t length
    ) const = 0;

    [[nodiscard]] virtual bool write(
        std::size_t offset,
        const std::uint8_t* from,
        std::size_t length
    ) = 0;

    // Everything written since the last commit is now on the device. A window
    // that writes straight through says so by not overriding this; a window
    // that shadows the device in RAM pushes the shadow here. That is what a
    // cartridge wants, because a bus transfer per field would be absurd.
    [[nodiscard]] virtual bool commit() { return true; }
};

// A window backed by ordinary memory: the host stand-in for a cartridge.
//
// It exists so the contract tests can run the real device rather than a
// simulation of it. Give it `ByteWindowSlotStorage::bytes_required` for a
// console's budget and the console's layout arithmetic, its ordering, and its
// out-of-space paths are exercised by `ctest`.
class VectorByteWindow final : public ByteWindow {
public:
    explicit VectorByteWindow(std::size_t size, std::uint8_t fill = 0)
        : bytes_(size, fill) {}

    [[nodiscard]] std::size_t size() const noexcept override {
        return bytes_.size();
    }

    [[nodiscard]] bool read(
        std::size_t offset,
        std::uint8_t* into,
        std::size_t length
    ) const override;

    [[nodiscard]] bool write(
        std::size_t offset,
        const std::uint8_t* from,
        std::size_t length
    ) override;

    // What the window is holding, for a test that wants to corrupt it.
    [[nodiscard]] std::vector<std::uint8_t>& bytes() noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// The layout the window holds. Big-endian throughout, deliberately: the
// Nintendo 64 is a big-endian machine and the host suite is not, and an image a
// host wrote must be an image a console reads. A field whose byte order is a
// property of whoever wrote it is a save that only round-trips on one machine.
inline constexpr std::uint16_t byte_window_format_version = 1;

// Bytes of header, before the directory.
inline constexpr std::size_t byte_window_header_bytes = 16;

// Bytes of directory per slot: a nul-padded name and a length.
inline constexpr std::size_t byte_window_directory_entry_bytes =
    maximum_slot_name_length + 1U + 4U;

class ByteWindowSlotStorage final : public SlotStorage {
public:
    // How large a window a budget needs. A device is configured the other way
    // round in practice, since the hardware states its size and the budget
    // follows, but the arithmetic is the same either way and stating it once
    // is what lets a caller check rather than assume.
    [[nodiscard]] static std::size_t bytes_required(
        const StorageBudget& budget
    ) noexcept;

    // The budget a window of `bytes` can carry with `slots` slots: everything
    // the header and directory do not take, in one slot or spread over all of
    // them. This is what a console adapter calls, because a cartridge states
    // its size and nothing else.
    [[nodiscard]] static StorageBudget budget_for(
        std::size_t bytes,
        std::size_t slots
    ) noexcept;

    // The window is not owned and must outlive the device. It is read once,
    // here: a device that cannot read its window, or whose window is too small
    // for its budget, is `available() == false` and refuses every operation
    // with `unavailable` rather than pretending to be an empty card.
    ByteWindowSlotStorage(ByteWindow& window, StorageBudget budget);

    [[nodiscard]] bool available() const noexcept { return available_; }

    // Whether the window held a directory this build could read. False for a
    // window that has never been written, which is what a fresh cartridge
    // looks like, and is an empty device rather than a broken one.
    [[nodiscard]] bool was_formatted() const noexcept { return formatted_; }

    [[nodiscard]] StorageError write(
        std::string_view slot,
        const std::vector<std::uint8_t>& bytes
    ) override;
    [[nodiscard]] StorageRead read(std::string_view slot) const override;
    [[nodiscard]] bool contains(std::string_view slot) const override;
    [[nodiscard]] StorageError erase(std::string_view slot) override;
    [[nodiscard]] std::vector<std::string> slots() const override;
    [[nodiscard]] StorageBudget budget() const noexcept override {
        return budget_;
    }

private:
    struct Slot final {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };

    [[nodiscard]] const Slot* find(std::string_view name) const noexcept;

    // Read the window and replace `slots_` with what it held. A window that
    // does not carry this build's image leaves `slots_` empty and `formatted_`
    // false, because that is what a cartridge nobody has saved to looks like
    // and it is not an error.
    void adopt_window();

    // Write `slots_` over the whole window and push it at the device. The one
    // place bytes leave this class, so the only way a slot can be half-written
    // is a device that failed mid-commit, and a device that says so gets its
    // previous contents put back by the caller.
    [[nodiscard]] bool publish();

    ByteWindow* window_;
    StorageBudget budget_{};
    // Kept sorted by name, so `slots()` is the order the contract requires
    // rather than the order the window happened to hold.
    std::vector<Slot> slots_;
    bool available_{false};
    bool formatted_{false};
};

}  // namespace grandleon::storage
