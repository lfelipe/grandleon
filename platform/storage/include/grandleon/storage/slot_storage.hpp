// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Named slots of opaque bytes, and nothing else.
//
// This is the whole platform surface a save needs. The engine hands over a
// vector of bytes and a name; a device puts them somewhere and gives them back.
// Nothing here knows what a campaign is, what a section is, or that the bytes
// have a magic number at all, which is the point.
// `engine/campaign/include/grandleon/campaign/save.hpp` says a save is
// "opaque bytes handed to platform storage", and an interface that could read
// the payload would eventually be asked to.
//
// The vocabulary is deliberately smaller than a filesystem's. There are no
// directories, no paths, no seeking, no partial reads, no handles, no times.
// A Nintendo 64 has four kilobytes of EEPROM addressed in sixty-four-bit
// blocks, or a battery-backed byte-wide SRAM window; a browser has a key-value
// store with a string-shaped value. The intersection of those is "a short name,
// a blob, all of it at once", so that is the interface, and the desktop adapter
// is the one that has to give something up rather than the consoles having to
// invent something.
//
// Every adapter implements this interface: the desktop filesystem one, the
// in-memory one, and the byte window a console's battery-backed region is.
// Each passes the one contract suite, against a measured byte budget from
// `tests/campaign/save_test.cpp`.

namespace grandleon::storage {

// What a slot may be called, on every platform at once.
//
// Lowercase ASCII letters, digits, `_` and `-`, between one and thirty-one
// characters. The rule is the intersection again: a console has no notion of a
// path and a desktop does, so a name that could contain `/`, `.`, or a
// backslash would be a directory traversal on exactly one target. Refusing them
// in the shared vocabulary means the filesystem adapter is not the only thing
// standing between a slot name and the rest of the disk.
inline constexpr std::size_t maximum_slot_name_length = 31;

[[nodiscard]] bool is_valid_slot_name(std::string_view name) noexcept;

// Why a device said no. Append only.
enum class StorageError : std::uint8_t {
    none = 0,
    // The name is not one every platform could carry.
    invalid_slot_name,
    // No such slot. Reading or erasing something that was never written.
    not_found,
    // This payload is larger than one slot on this device. On the way out it
    // means the caller offered too many bytes; on the way back it means the
    // device is holding a slot larger than one slot may be, which is a slot
    // nothing here wrote and is refused rather than allocated for.
    too_large,
    // The device has room for slots but not for this many bytes, or holds as
    // many slots as it can.
    out_of_space,
    // The device is not there, or would not open. A memory card absent from
    // the port, a directory that cannot be created.
    unavailable,
    // The device took the request and then failed it. A short write, a read
    // that returned fewer bytes than the slot holds.
    io_failure,
};

[[nodiscard]] std::string_view storage_error_name(StorageError error) noexcept;

struct StorageRead final {
    StorageError error{StorageError::none};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == StorageError::none;
    }
};

// What a device can hold. Stated rather than discovered, because a console's
// answer is a hardware constant and a desktop's is a policy, and a caller that
// wants to know whether a save will fit should get the same kind of answer from
// both.
struct StorageBudget final {
    std::size_t maximum_slot_bytes{};
    std::size_t maximum_total_bytes{};
    std::size_t maximum_slots{};
};

// The interface every platform implements.
//
// Every operation is complete or refused; none is partial. `write` either
// leaves the slot holding all of the new bytes or leaves it holding what it
// held before, because the failure mode a save cannot survive is a slot that
// holds half of one save and half of another.
class SlotStorage {
public:
    SlotStorage() = default;
    virtual ~SlotStorage() = default;

    SlotStorage(const SlotStorage&) = delete;
    SlotStorage& operator=(const SlotStorage&) = delete;
    SlotStorage(SlotStorage&&) = delete;
    SlotStorage& operator=(SlotStorage&&) = delete;

    // Replace the slot's contents, creating it if it is not there.
    [[nodiscard]] virtual StorageError write(
        std::string_view slot,
        const std::vector<std::uint8_t>& bytes
    ) = 0;

    [[nodiscard]] virtual StorageRead read(std::string_view slot) const = 0;

    [[nodiscard]] virtual bool contains(std::string_view slot) const = 0;

    [[nodiscard]] virtual StorageError erase(std::string_view slot) = 0;

    // Every slot the device holds, in ascending name order. Ordered because a
    // save-slot menu built from an unordered list would rearrange itself
    // between runs on some targets and not on others.
    [[nodiscard]] virtual std::vector<std::string> slots() const = 0;

    [[nodiscard]] virtual StorageBudget budget() const noexcept = 0;
};

// How many bytes the device is currently holding across every slot.
[[nodiscard]] std::size_t used_bytes(const SlotStorage& storage);

}  // namespace grandleon::storage
