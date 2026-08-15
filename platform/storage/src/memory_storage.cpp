// SPDX-License-Identifier: MIT
#include <grandleon/storage/memory_storage.hpp>

#include <algorithm>

namespace grandleon::storage {

const MemorySlotStorage::Slot* MemorySlotStorage::find(
    std::string_view name
) const noexcept {
    const auto found = std::lower_bound(
        slots_.begin(),
        slots_.end(),
        name,
        [](const Slot& slot, std::string_view wanted) { return slot.name < wanted; }
    );
    return found == slots_.end() || found->name != name ? nullptr : &*found;
}

StorageError MemorySlotStorage::write(
    std::string_view slot,
    const std::vector<std::uint8_t>& bytes
) {
    if (!is_valid_slot_name(slot)) {
        return StorageError::invalid_slot_name;
    }
    if (bytes.size() > budget_.maximum_slot_bytes) {
        return StorageError::too_large;
    }

    const auto found = std::lower_bound(
        slots_.begin(),
        slots_.end(),
        slot,
        [](const Slot& entry, std::string_view wanted) { return entry.name < wanted; }
    );
    const bool replacing = found != slots_.end() && found->name == slot;
    if (!replacing && slots_.size() >= budget_.maximum_slots) {
        return StorageError::out_of_space;
    }

    // The budget is measured over what the device would hold *after* this
    // write, with the slot being replaced taken out first. Overwriting a save
    // with a smaller one must not fail because the old one is still counted.
    std::size_t total = bytes.size();
    for (const Slot& entry : slots_) {
        if (replacing && entry.name == slot) {
            continue;
        }
        total += entry.bytes.size();
    }
    if (total > budget_.maximum_total_bytes) {
        return StorageError::out_of_space;
    }

    if (replacing) {
        found->bytes = bytes;
    } else {
        slots_.insert(found, Slot{std::string(slot), bytes});
    }
    return StorageError::none;
}

StorageRead MemorySlotStorage::read(std::string_view slot) const {
    StorageRead result;
    if (!is_valid_slot_name(slot)) {
        result.error = StorageError::invalid_slot_name;
        return result;
    }
    const Slot* found = find(slot);
    if (found == nullptr) {
        result.error = StorageError::not_found;
        return result;
    }
    result.bytes = found->bytes;
    return result;
}

bool MemorySlotStorage::contains(std::string_view slot) const {
    return is_valid_slot_name(slot) && find(slot) != nullptr;
}

StorageError MemorySlotStorage::erase(std::string_view slot) {
    if (!is_valid_slot_name(slot)) {
        return StorageError::invalid_slot_name;
    }
    const auto found = std::lower_bound(
        slots_.begin(),
        slots_.end(),
        slot,
        [](const Slot& entry, std::string_view wanted) { return entry.name < wanted; }
    );
    if (found == slots_.end() || found->name != slot) {
        return StorageError::not_found;
    }
    slots_.erase(found);
    return StorageError::none;
}

std::vector<std::string> MemorySlotStorage::slots() const {
    std::vector<std::string> names;
    names.reserve(slots_.size());
    for (const Slot& entry : slots_) {
        names.push_back(entry.name);
    }
    return names;
}

}  // namespace grandleon::storage
