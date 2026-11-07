// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/localization.hpp>

#include <cstddef>
#include <cstdint>

namespace grandleon::package_runtime {
namespace {

class Reader final {
public:
    Reader(
        const package_format::LoadedPackage& package,
        const package_format::RecordView& record
    ) noexcept
        : bytes_(package.byte_data() + record.payload_offset),
          size_(record.payload_size) {}

    bool u16(std::uint16_t& value) noexcept {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(bytes_[at_]) |
                static_cast<std::uint16_t>(bytes_[at_ + 1]) << 8U;
        at_ += 2;
        return true;
    }

    bool u32(std::uint32_t& value) noexcept {
        if (remaining() < 4) return false;
        value = 0;
        for (unsigned byte = 0; byte < 4; ++byte) {
            value |= static_cast<std::uint32_t>(bytes_[at_ + byte])
                     << (byte * 8U);
        }
        at_ += 4;
        return true;
    }

    bool text(std::string_view& value) noexcept {
        std::uint16_t length = 0;
        if (!u16(length) || remaining() < length) return false;
        value = std::string_view(
            reinterpret_cast<const char*>(bytes_ + at_), length
        );
        at_ += length;
        return true;
    }

private:
    [[nodiscard]] std::size_t remaining() const noexcept {
        return at_ <= size_ ? size_ - at_ : 0;
    }

    const std::uint8_t* bytes_{};
    std::size_t size_{};
    std::size_t at_{};
};

const package_format::RecordView* record(
    const package_format::LoadedPackage& package
) noexcept {
    return package.find(
        package_format::SectionType::localizations,
        package_format::localization_record_id
    );
}

}  // namespace

std::string_view source_language(
    const package_format::LoadedPackage& package
) noexcept {
    const auto* held = record(package);
    if (held == nullptr) return {};
    Reader reader(package, *held);
    std::string_view source;
    return reader.text(source) ? source : std::string_view{};
}

std::string_view localized_text(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::string_view message_id,
    std::string_view current_source
) noexcept {
    const auto* held = record(package);
    if (held == nullptr) return current_source;
    Reader reader(package, *held);
    std::string_view authored_language;
    std::uint16_t catalog_count = 0;
    if (!reader.text(authored_language) || !reader.u16(catalog_count)) {
        return current_source;
    }
    if (language.empty() || language == authored_language) {
        return current_source;
    }
    for (std::uint16_t catalog_index = 0;
         catalog_index < catalog_count; ++catalog_index) {
        std::string_view catalog_language;
        std::uint32_t entry_count = 0;
        if (!reader.text(catalog_language) || !reader.u32(entry_count)) {
            return current_source;
        }
        for (std::uint32_t entry_index = 0;
             entry_index < entry_count; ++entry_index) {
            std::string_view id;
            std::string_view source;
            std::string_view text;
            if (!reader.text(id) || !reader.text(source) ||
                !reader.text(text)) {
                return current_source;
            }
            if (catalog_language == language && id == message_id) {
                return source == current_source ? text : current_source;
            }
        }
    }
    return current_source;
}

}  // namespace grandleon::package_runtime
