// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/names.hpp>

#include <cstddef>
#include <cstdint>

namespace grandleon::package_runtime {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

namespace {

std::string_view leading_string(
    const LoadedPackage& package, const RecordView& record
) noexcept {
    const std::uint8_t* bytes = package.byte_data();
    if (bytes == nullptr) return {};

    // Length-prefixed and little-endian, exactly as the compiler's
    // `put_string` wrote it. Two refusals rather than one, because they are
    // two different damages: a record too short to hold its own length, and a
    // length that runs past the end of the record it was read from. Neither is
    // clamped: a name truncated to whatever happened to fit would be a name
    // this reader invented.
    const std::size_t begin = record.payload_offset;
    const std::size_t size = record.payload_size;
    if (size < 2) return {};
    const std::size_t length =
        static_cast<std::size_t>(bytes[begin]) |
        (static_cast<std::size_t>(bytes[begin + 1]) << 8U);
    if (length > size - 2) return {};
    return std::string_view(
        reinterpret_cast<const char*>(bytes + begin + 2), length
    );
}

}  // namespace

std::string_view content_name(
    const LoadedPackage& package,
    SectionType section,
    std::uint64_t stable_id
) noexcept {
    const RecordView* record = package.find(section, stable_id);
    if (record == nullptr) return {};
    return leading_string(package, *record);
}

std::uint64_t unit_type_class(
    const LoadedPackage& package, std::uint64_t unit_type_id
) noexcept {
    const std::uint8_t* bytes = package.byte_data();
    if (bytes == nullptr) return 0;
    const RecordView* record =
        package.find(SectionType::unit_types, unit_type_id);
    if (record == nullptr) return 0;
    const std::size_t begin = record->payload_offset;
    const std::size_t size = record->payload_size;
    if (size < 2) return 0;
    const std::size_t length =
        static_cast<std::size_t>(bytes[begin]) |
        (static_cast<std::size_t>(bytes[begin + 1]) << 8U);
    // The name, then the class. Both bounds are checked against the record's
    // own size rather than against the package's, because a record that ran
    // into its neighbour would otherwise read the neighbour's first field and
    // return a class identity that belongs to somebody else.
    if (length > size - 2 || size - 2 - length < 8) return 0;
    const std::size_t at = begin + 2 + length;
    std::uint64_t value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes[at + shift / 8U]) << shift;
    }
    return value;
}

}  // namespace grandleon::package_runtime
