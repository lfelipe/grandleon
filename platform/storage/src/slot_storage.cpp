// SPDX-License-Identifier: MIT
#include <grandleon/storage/slot_storage.hpp>

namespace grandleon::storage {

bool is_valid_slot_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > maximum_slot_name_length) {
        return false;
    }
    for (const char character : name) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !digit && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

std::string_view storage_error_name(StorageError error) noexcept {
    switch (error) {
        case StorageError::none: return "none";
        case StorageError::invalid_slot_name: return "invalid_slot_name";
        case StorageError::not_found: return "not_found";
        case StorageError::too_large: return "too_large";
        case StorageError::out_of_space: return "out_of_space";
        case StorageError::unavailable: return "unavailable";
        case StorageError::io_failure: return "io_failure";
    }
    return "unknown";
}

std::size_t used_bytes(const SlotStorage& storage) {
    std::size_t total = 0;
    for (const std::string& name : storage.slots()) {
        const StorageRead slot = storage.read(name);
        if (slot) {
            total += slot.bytes.size();
        }
    }
    return total;
}

}  // namespace grandleon::storage
