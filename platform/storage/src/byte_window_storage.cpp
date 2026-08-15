// SPDX-License-Identifier: MIT
#include <grandleon/storage/byte_window_storage.hpp>

#include <algorithm>
#include <cstring>

namespace grandleon::storage {

namespace {

constexpr std::uint8_t magic[4] = {'G', 'L', 'S', 'D'};

// FNV-1a, 32 bits, spelled out here rather than borrowed from `core`.
// `grandleon_storage` links nothing at all, which is the property that lets a
// cartridge link it, and four lines of arithmetic is a smaller price than the
// edge that would buy them.
[[nodiscard]] std::uint32_t checksum(
    const std::uint8_t* bytes,
    std::size_t length
) noexcept {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < length; ++index) {
        hash ^= bytes[index];
        hash *= 16777619U;
    }
    return hash;
}

void put16(std::uint8_t* into, std::uint16_t value) noexcept {
    into[0] = static_cast<std::uint8_t>(value >> 8U);
    into[1] = static_cast<std::uint8_t>(value & 0xFFU);
}

void put32(std::uint8_t* into, std::uint32_t value) noexcept {
    into[0] = static_cast<std::uint8_t>(value >> 24U);
    into[1] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    into[2] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    into[3] = static_cast<std::uint8_t>(value & 0xFFU);
}

[[nodiscard]] std::uint16_t get16(const std::uint8_t* from) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(from[0]) << 8U) |
        static_cast<std::uint16_t>(from[1])
    );
}

[[nodiscard]] std::uint32_t get32(const std::uint8_t* from) noexcept {
    return (static_cast<std::uint32_t>(from[0]) << 24U) |
           (static_cast<std::uint32_t>(from[1]) << 16U) |
           (static_cast<std::uint32_t>(from[2]) << 8U) |
           static_cast<std::uint32_t>(from[3]);
}

}  // namespace

bool VectorByteWindow::read(
    std::size_t offset,
    std::uint8_t* into,
    std::size_t length
) const {
    if (length == 0) return offset <= bytes_.size();
    if (offset > bytes_.size() || length > bytes_.size() - offset) return false;
    std::memcpy(into, bytes_.data() + offset, length);
    return true;
}

bool VectorByteWindow::write(
    std::size_t offset,
    const std::uint8_t* from,
    std::size_t length
) {
    if (length == 0) return offset <= bytes_.size();
    if (offset > bytes_.size() || length > bytes_.size() - offset) return false;
    std::memcpy(bytes_.data() + offset, from, length);
    return true;
}

std::size_t ByteWindowSlotStorage::bytes_required(
    const StorageBudget& budget
) noexcept {
    return byte_window_header_bytes +
           byte_window_directory_entry_bytes * budget.maximum_slots +
           budget.maximum_total_bytes;
}

StorageBudget ByteWindowSlotStorage::budget_for(
    std::size_t bytes,
    std::size_t slots
) noexcept {
    const std::size_t overhead =
        byte_window_header_bytes + byte_window_directory_entry_bytes * slots;
    const std::size_t payload = bytes > overhead ? bytes - overhead : 0U;
    // One slot may be the whole payload area. A device with room for four
    // saves is not a device that refuses one large one.
    return StorageBudget{payload, payload, payload == 0U ? 0U : slots};
}

ByteWindowSlotStorage::ByteWindowSlotStorage(
    ByteWindow& window,
    StorageBudget budget
)
    : window_(&window), budget_(budget) {
    available_ = window.size() >= bytes_required(budget);
    if (available_) {
        adopt_window();
    }
}

void ByteWindowSlotStorage::adopt_window() {
    slots_.clear();
    formatted_ = false;

    std::vector<std::uint8_t> header(byte_window_header_bytes);
    if (!window_->read(0, header.data(), header.size())) {
        // The window is there and would not read. That is a device fault
        // rather than an unwritten card, and it is reported as one.
        available_ = false;
        return;
    }
    if (std::memcmp(header.data(), magic, sizeof(magic)) != 0) return;
    if (get16(header.data() + 4) != byte_window_format_version) return;

    const std::size_t count = get16(header.data() + 6);
    const std::size_t payload_bytes = get32(header.data() + 8);
    const std::uint32_t stated = get32(header.data() + 12);
    if (count > budget_.maximum_slots) return;
    if (payload_bytes > budget_.maximum_total_bytes) return;

    const std::size_t body_bytes =
        byte_window_directory_entry_bytes * count + payload_bytes;
    if (body_bytes > window_->size() - byte_window_header_bytes) return;

    std::vector<std::uint8_t> body(body_bytes);
    if (!window_->read(byte_window_header_bytes, body.data(), body.size())) {
        available_ = false;
        return;
    }
    if (checksum(body.data(), body.size()) != stated) return;

    // Every field below is now known to be what was written, so the parse can
    // trust the lengths. It still refuses a name the shared vocabulary would
    // not accept: a directory is not a licence to hold a slot no other device
    // could.
    std::vector<Slot> parsed;
    parsed.reserve(count);
    std::size_t payload_at = byte_window_directory_entry_bytes * count;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t* entry =
            body.data() + byte_window_directory_entry_bytes * index;
        std::size_t name_length = 0;
        while (name_length <= maximum_slot_name_length &&
               entry[name_length] != 0) {
            ++name_length;
        }
        const std::string name(
            reinterpret_cast<const char*>(entry), name_length
        );
        if (!is_valid_slot_name(name)) return;
        const std::size_t length =
            get32(entry + maximum_slot_name_length + 1U);
        if (length > budget_.maximum_slot_bytes) return;
        if (length > body.size() - payload_at) return;
        Slot slot;
        slot.name = name;
        slot.bytes.assign(
            body.begin() + static_cast<std::ptrdiff_t>(payload_at),
            body.begin() + static_cast<std::ptrdiff_t>(payload_at + length)
        );
        payload_at += length;
        parsed.push_back(std::move(slot));
    }
    if (payload_at != body.size()) return;

    // The directory is written in ascending order, so a directory that is not
    // is one this build did not write.
    for (std::size_t index = 1; index < parsed.size(); ++index) {
        if (!(parsed[index - 1].name < parsed[index].name)) return;
    }

    slots_ = std::move(parsed);
    formatted_ = true;
}

bool ByteWindowSlotStorage::publish() {
    const std::size_t count = slots_.size();
    std::size_t payload_bytes = 0;
    for (const Slot& slot : slots_) payload_bytes += slot.bytes.size();

    std::vector<std::uint8_t> body(
        byte_window_directory_entry_bytes * count + payload_bytes, 0
    );
    std::size_t payload_at = byte_window_directory_entry_bytes * count;
    for (std::size_t index = 0; index < count; ++index) {
        std::uint8_t* entry =
            body.data() + byte_window_directory_entry_bytes * index;
        const Slot& slot = slots_[index];
        std::memcpy(entry, slot.name.data(), slot.name.size());
        put32(
            entry + maximum_slot_name_length + 1U,
            static_cast<std::uint32_t>(slot.bytes.size())
        );
        if (!slot.bytes.empty()) {
            std::memcpy(
                body.data() + payload_at, slot.bytes.data(), slot.bytes.size()
            );
        }
        payload_at += slot.bytes.size();
    }

    std::uint8_t header[byte_window_header_bytes] = {};
    std::memcpy(header, magic, sizeof(magic));
    put16(header + 4, byte_window_format_version);
    put16(header + 6, static_cast<std::uint16_t>(count));
    put32(header + 8, static_cast<std::uint32_t>(payload_bytes));
    put32(header + 12, checksum(body.data(), body.size()));

    // The body first and the header last. A device that dies between them
    // leaves a header describing the image that was there before, which the
    // checksum then rejects: an empty card rather than a plausible wrong one.
    if (!window_->write(byte_window_header_bytes, body.data(), body.size())) {
        return false;
    }
    if (!window_->write(0, header, sizeof(header))) return false;
    return window_->commit();
}

const ByteWindowSlotStorage::Slot* ByteWindowSlotStorage::find(
    std::string_view name
) const noexcept {
    for (const Slot& slot : slots_) {
        if (slot.name == name) return &slot;
    }
    return nullptr;
}

StorageError ByteWindowSlotStorage::write(
    std::string_view slot,
    const std::vector<std::uint8_t>& bytes
) {
    if (!is_valid_slot_name(slot)) return StorageError::invalid_slot_name;
    if (!available_) return StorageError::unavailable;
    if (bytes.size() > budget_.maximum_slot_bytes) return StorageError::too_large;

    // Ascending by name, so the insertion point and the existing slot are the
    // same lookup. `slots()` is required to be ordered, and keeping the vector
    // ordered is what makes that free rather than a sort per call.
    const auto at = std::lower_bound(
        slots_.begin(),
        slots_.end(),
        slot,
        [](const Slot& held, std::string_view name) {
            return held.name < name;
        }
    );
    const bool existing = at != slots_.end() && at->name == slot;
    if (!existing && slots_.size() >= budget_.maximum_slots) {
        return StorageError::out_of_space;
    }

    // Measured over what the device holds *after* the write, so overwriting a
    // save with a smaller one on a full device is never refused for space.
    std::size_t total = bytes.size();
    for (const Slot& held : slots_) {
        if (held.name != slot) total += held.bytes.size();
    }
    if (total > budget_.maximum_total_bytes) return StorageError::out_of_space;

    // Nothing above this line has moved a byte, which is what makes every
    // refusal above leave the device exactly as it was.
    const std::vector<Slot> before = slots_;
    if (existing) {
        at->bytes = bytes;
    } else {
        Slot added;
        added.name = std::string(slot);
        added.bytes = bytes;
        slots_.insert(at, std::move(added));
    }

    if (!publish()) {
        slots_ = before;
        // Put the device back to what it was holding, so a failed write leaves
        // the slot with its old save rather than with the image this call had
        // already started laying down. A device that fails that too is beyond
        // anything this layer can promise, and the error says so either way.
        (void)publish();
        return StorageError::io_failure;
    }
    return StorageError::none;
}

StorageRead ByteWindowSlotStorage::read(std::string_view slot) const {
    StorageRead result;
    if (!is_valid_slot_name(slot)) {
        result.error = StorageError::invalid_slot_name;
        return result;
    }
    if (!available_) {
        result.error = StorageError::unavailable;
        return result;
    }
    const Slot* held = find(slot);
    if (held == nullptr) {
        result.error = StorageError::not_found;
        return result;
    }
    result.bytes = held->bytes;
    return result;
}

bool ByteWindowSlotStorage::contains(std::string_view slot) const {
    if (!is_valid_slot_name(slot) || !available_) return false;
    return find(slot) != nullptr;
}

StorageError ByteWindowSlotStorage::erase(std::string_view slot) {
    if (!is_valid_slot_name(slot)) return StorageError::invalid_slot_name;
    if (!available_) return StorageError::unavailable;
    const auto at = std::lower_bound(
        slots_.begin(),
        slots_.end(),
        slot,
        [](const Slot& held, std::string_view name) {
            return held.name < name;
        }
    );
    if (at == slots_.end() || at->name != slot) return StorageError::not_found;

    const std::vector<Slot> before = slots_;
    slots_.erase(at);
    if (!publish()) {
        slots_ = before;
        (void)publish();
        return StorageError::io_failure;
    }
    return StorageError::none;
}

std::vector<std::string> ByteWindowSlotStorage::slots() const {
    std::vector<std::string> names;
    if (!available_) return names;
    names.reserve(slots_.size());
    for (const Slot& slot : slots_) names.push_back(slot.name);
    return names;
}

}  // namespace grandleon::storage
