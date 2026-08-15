// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/starting_kit.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace grandleon::package_runtime {
namespace {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

// The same untrusted-input discipline every other decoder in this module
// keeps: nothing is read that the record does not still have bytes for.
class Reader final {
public:
    Reader(const LoadedPackage& package, const RecordView& record)
        : bytes_(package.byte_data()),
          cursor_(record.payload_offset),
          end_(static_cast<std::size_t>(record.payload_offset) +
               record.payload_size) {}

    bool u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(
            bytes_[cursor_] |
            (static_cast<std::uint16_t>(bytes_[cursor_ + 1]) << 8U)
        );
        cursor_ += 2;
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(bytes_[cursor_ + index])
                     << (8U * index);
        }
        cursor_ += 8;
        return true;
    }

    bool skip(std::size_t count) {
        if (remaining() < count) return false;
        cursor_ += count;
        return true;
    }

    bool skip_string() {
        std::uint16_t size = 0;
        return u16(size) && skip(size);
    }

    bool skip_ids() {
        std::uint16_t count = 0;
        if (!u16(count) || static_cast<std::size_t>(count) > remaining() / 8U) {
            return false;
        }
        return skip(static_cast<std::size_t>(count) * 8U);
    }

    bool ids(std::vector<std::uint64_t>& values) {
        std::uint16_t count = 0;
        if (!u16(count) || static_cast<std::size_t>(count) > remaining() / 8U) {
            return false;
        }
        values.clear();
        values.reserve(count);
        for (std::uint16_t index = 0; index < count; ++index) {
            std::uint64_t value = 0;
            if (!u64(value)) return false;
            values.push_back(value);
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return cursor_ <= end_ ? end_ - cursor_ : 0;
    }

private:
    const std::uint8_t* bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

}  // namespace

UnitStartingItemsLoad load_unit_starting_items(
    const LoadedPackage& package,
    std::uint64_t unit_type_id
) {
    UnitStartingItemsLoad result;
    const RecordView* const record =
        package.find(SectionType::unit_types, unit_type_id);
    if (record == nullptr) {
        return result;
    }
    Reader reader(package, *record);
    // Name, class, faction, starting weapons: everything written before the
    // list this decoder is for. Everything after it (the abilities, the growth
    // block, the drop) is somebody else's business and is never reached, which
    // is why this decoder needs no opinion about how long a record's tail is.
    if (!(reader.skip_string() && reader.skip(16) && reader.skip_ids() &&
          reader.ids(result.items))) {
        return result;
    }
    result.found = true;
    return result;
}

}  // namespace grandleon::package_runtime
