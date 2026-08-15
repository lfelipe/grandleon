// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/storage/slot_storage.hpp>

#include <string>
#include <utility>
#include <vector>

// Slots that live only as long as the program does.
//
// Two jobs. It is the reference implementation, the one the contract tests
// pin the meaning of every error against, so that a device adapter has
// something to be wrong compared to. And it is the storage a target without a
// device uses: a browser tab before persistence is granted, a headless test, a
// console emulator with no memory card in the port. A game that saves into it
// works exactly as it does anywhere else and forgets everything on exit, which
// is a better answer than a save path that only exists on some platforms.
//
// Its budget is a parameter rather than a constant precisely so it can stand in
// for a device that is much smaller than a desktop: give it a Nintendo 64's
// thirty-two kilobytes and the out-of-space paths are exercised on a host.

namespace grandleon::storage {

class MemorySlotStorage final : public SlotStorage {
public:
    MemorySlotStorage() = default;
    explicit MemorySlotStorage(StorageBudget budget) noexcept
        : budget_(budget) {}

    [[nodiscard]] StorageError write(
        std::string_view slot,
        const std::vector<std::uint8_t>& bytes
    ) override;
    [[nodiscard]] StorageRead read(std::string_view slot) const override;
    [[nodiscard]] bool contains(std::string_view slot) const override;
    [[nodiscard]] StorageError erase(std::string_view slot) override;
    [[nodiscard]] std::vector<std::string> slots() const override;
    [[nodiscard]] StorageBudget budget() const noexcept override { return budget_; }

private:
    struct Slot final {
        std::string name;
        std::vector<std::uint8_t> bytes;
    };

    [[nodiscard]] const Slot* find(std::string_view name) const noexcept;

    // Kept sorted by name, so `slots()` is the order it is required to be in
    // rather than the order things happened to be written.
    std::vector<Slot> slots_;
    StorageBudget budget_{1U << 20U, 64U << 20U, 256U};
};

}  // namespace grandleon::storage
