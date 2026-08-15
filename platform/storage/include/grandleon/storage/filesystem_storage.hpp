// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/storage/slot_storage.hpp>

#include <string>

// Slots as files in one directory, for the desktop.
//
// This is the first thing in the repository below the tools layer that touches
// a filesystem, and it is deliberately the only one. It is its own library,
// `grandleon_storage_filesystem`, and nothing in `engine/` links it. The
// engine libraries do not know a filesystem exists; the portable
// `grandleon_storage` interface does not either; a desktop binary is the thing
// that chooses this implementation and passes it in. `platform/storage/README.md`
// says why that separation is worth two CMake targets.
//
// One file per slot, named `<slot>.gls` in the directory handed to the
// constructor. Nothing is nested, because the interface has no nesting: a slot
// name is validated against `is_valid_slot_name` before it is ever turned into
// a path, so `../` and an absolute path are refused as names rather than
// escaped as paths.
//
// Writes are staged and renamed. A save written directly into its final file
// would leave a truncated save behind if the process died mid-write, and the
// player would find out the next time they loaded. Writing `<slot>.gls.tmp` and
// renaming it means the slot holds the old save or the new one and never half
// of each: the same all-or-nothing promise the interface makes everywhere.
//
// Reads are bounded by the same budget writes are. This is the one adapter
// whose slots another program can reach: a file in the save directory can be
// any size somebody makes it, and reading it means allocating whatever the
// directory says it holds. A slot larger than `maximum_slot_bytes` is
// therefore `too_large` on the way back as well as on the way out, decided on
// the size before anything is read.

namespace grandleon::storage {

class FilesystemSlotStorage final : public SlotStorage {
public:
    // The directory is created if it is not there. If it cannot be, every
    // operation answers `unavailable` rather than throwing: a missing save
    // directory is a device that is not present, which is a case every platform
    // has and every caller must already handle.
    explicit FilesystemSlotStorage(std::string root);
    FilesystemSlotStorage(std::string root, StorageBudget budget);

    [[nodiscard]] StorageError write(
        std::string_view slot,
        const std::vector<std::uint8_t>& bytes
    ) override;
    [[nodiscard]] StorageRead read(std::string_view slot) const override;
    [[nodiscard]] bool contains(std::string_view slot) const override;
    [[nodiscard]] StorageError erase(std::string_view slot) override;
    [[nodiscard]] std::vector<std::string> slots() const override;
    [[nodiscard]] StorageBudget budget() const noexcept override { return budget_; }

    [[nodiscard]] const std::string& root() const noexcept { return root_; }
    [[nodiscard]] bool available() const noexcept { return available_; }

    // The suffix a slot's file carries. Public so a tool that has to explain
    // the save directory to a player does not have to guess it.
    static constexpr std::string_view file_suffix = ".gls";

private:
    std::string root_;
    StorageBudget budget_{1U << 20U, 64U << 20U, 256U};
    bool available_{false};
};

}  // namespace grandleon::storage
