// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/localization.hpp>

#include <grandleon/core/content_identity.hpp>

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

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

bool remove_edges(
    std::string_view value,
    std::string_view prefix,
    std::string_view suffix,
    std::string_view& middle
) noexcept {
    if (value.size() <= prefix.size() + suffix.size() ||
        value.compare(0, prefix.size(), prefix) != 0 ||
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) !=
            0) {
        return false;
    }
    middle = value.substr(
        prefix.size(), value.size() - prefix.size() - suffix.size()
    );
    return !middle.empty();
}

std::string_view section_prefix(package_format::SectionType section) noexcept {
    using package_format::SectionType;
    switch (section) {
        case SectionType::classes: return "classes.";
        case SectionType::weapon_types: return "weapon_types.";
        case SectionType::item_types: return "item_types.";
        case SectionType::unit_types: return "unit_types.";
        case SectionType::weapons: return "weapons.";
        case SectionType::items: return "items.";
        case SectionType::maps: return "maps.";
        case SectionType::factions: return "factions.";
        case SectionType::abilities: return "abilities.";
        case SectionType::objectives: return "objectives.";
        default: return {};
    }
}

template <typename Match>
std::string_view localized_matching(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::string_view current_source,
    Match&& matches
) noexcept {
    const auto* held = record(package);
    if (held == nullptr) return current_source;
    Reader reader(package, *held);
    std::string_view authored_language;
    std::uint16_t catalog_count = 0;
    if (!reader.text(authored_language) || !reader.u16(catalog_count) ||
        language.empty() || language == authored_language) {
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
            std::string_view translated;
            if (!reader.text(id) || !reader.text(source) ||
                !reader.text(translated)) {
                return current_source;
            }
            if (catalog_language == language && matches(id)) {
                return source == current_source ? translated : current_source;
            }
        }
    }
    return current_source;
}

bool stable_named_id(
    std::string_view message_id,
    std::string_view prefix,
    std::uint64_t stable_id
) noexcept {
    std::string_view source_id;
    return remove_edges(message_id, prefix, ".name", source_id) &&
           core::stable_content_id_v1(source_id) == stable_id;
}

bool nested_named_ids(
    std::string_view message_id,
    std::string_view relation,
    std::uint64_t first_id,
    std::uint64_t second_id
) noexcept {
    std::string_view middle;
    if (!remove_edges(message_id, "campaigns.", ".name", middle)) {
        return false;
    }
    const std::size_t split = middle.find(relation);
    if (split == std::string_view::npos || split == 0 ||
        split + relation.size() >= middle.size()) {
        return false;
    }
    return core::stable_content_id_v1(middle.substr(0, split)) == first_id &&
           core::stable_content_id_v1(middle.substr(split + relation.size())) ==
               second_id;
}

std::uint64_t scoped_placement_id(
    std::string_view campaign,
    std::string_view node,
    std::string_view placement
) noexcept {
    std::uint64_t value = 14695981039346656037ULL;
    const auto append = [&value](std::string_view text) {
        for (const char character : text) {
            value ^= static_cast<std::uint8_t>(character);
            value *= 1099511628211ULL;
        }
    };
    append(campaign);
    append("/");
    append(node);
    append("/");
    append(placement);
    return value;
}

bool placement_named_id(
    std::string_view message_id,
    std::uint64_t placement_id
) noexcept {
    std::string_view middle;
    if (!remove_edges(message_id, "campaigns.", ".name", middle)) {
        return false;
    }
    constexpr std::string_view node_separator = ".nodes.";
    constexpr std::string_view placement_separator = ".placements.";
    const std::size_t node_at = middle.find(node_separator);
    if (node_at == std::string_view::npos || node_at == 0) return false;
    const std::size_t placement_at = middle.find(
        placement_separator, node_at + node_separator.size()
    );
    if (placement_at == std::string_view::npos ||
        placement_at == node_at + node_separator.size() ||
        placement_at + placement_separator.size() >= middle.size()) {
        return false;
    }
    const std::string_view campaign = middle.substr(0, node_at);
    const std::string_view node = middle.substr(
        node_at + node_separator.size(),
        placement_at - node_at - node_separator.size()
    );
    const std::string_view placement = middle.substr(
        placement_at + placement_separator.size()
    );
    return scoped_placement_id(campaign, node, placement) == placement_id;
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

std::string_view localized_project_title(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::string_view current_source
) noexcept {
    return localized_text(
        package, language, "project.title", current_source
    );
}

std::vector<std::string_view> available_languages(
    const package_format::LoadedPackage& package
) {
    std::vector<std::string_view> result;
    const auto* held = record(package);
    if (held == nullptr) return result;
    Reader reader(package, *held);
    std::string_view source;
    std::uint16_t catalog_count = 0;
    if (!reader.text(source) || source.empty() || !reader.u16(catalog_count)) {
        return {};
    }
    result.push_back(source);
    for (std::uint16_t catalog_index = 0;
         catalog_index < catalog_count; ++catalog_index) {
        std::string_view language;
        std::uint32_t entry_count = 0;
        if (!reader.text(language) || !reader.u32(entry_count)) return {};
        if (!language.empty() && language != source) result.push_back(language);
        for (std::uint32_t entry_index = 0;
             entry_index < entry_count; ++entry_index) {
            std::string_view ignored;
            if (!reader.text(ignored) || !reader.text(ignored) ||
                !reader.text(ignored)) {
                return {};
            }
        }
    }
    return result;
}

std::string_view localized_content_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    package_format::SectionType section,
    std::uint64_t content_id,
    std::string_view current_source
) noexcept {
    const std::string_view prefix = section_prefix(section);
    if (prefix.empty()) return current_source;
    return localized_matching(
        package, language, current_source,
        [prefix, content_id](std::string_view message_id) {
            return stable_named_id(message_id, prefix, content_id);
        }
    );
}

std::string_view localized_objective_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t objective_id,
    std::string_view current_source
) noexcept {
    return localized_content_name(
        package, language, package_format::SectionType::objectives,
        objective_id, current_source
    );
}

std::string_view localized_placement_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t placement_id,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [placement_id](std::string_view message_id) {
            return placement_named_id(message_id, placement_id);
        }
    );
}

std::string_view localized_dialogue_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t dialogue_id,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [dialogue_id](std::string_view message_id) {
            return stable_named_id(message_id, "dialogues.", dialogue_id);
        }
    );
}

std::string_view localized_dialogue_line(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t dialogue_id,
    std::size_t line_index,
    std::string_view field,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [dialogue_id, line_index, field](std::string_view message_id) {
            std::string_view middle;
            const std::string_view suffix = field == "speaker"
                                                ? ".speaker"
                                            : field == "text" ? ".text"
                                                              : std::string_view{};
            if (suffix.empty() ||
                !remove_edges(message_id, "dialogues.", suffix, middle)) {
                return false;
            }
            constexpr std::string_view separator = ".lines.";
            const std::size_t split = middle.find(separator);
            if (split == std::string_view::npos || split == 0) return false;
            std::size_t parsed_index = 0;
            const std::string_view number = middle.substr(
                split + separator.size()
            );
            const auto parsed = std::from_chars(
                number.data(), number.data() + number.size(), parsed_index
            );
            return parsed.ec == std::errc{} &&
                   parsed.ptr == number.data() + number.size() &&
                   parsed_index == line_index &&
                   core::stable_content_id_v1(middle.substr(0, split)) ==
                       dialogue_id;
        }
    );
}

std::string_view localized_campaign_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [campaign_id](std::string_view message_id) {
            return stable_named_id(message_id, "campaigns.", campaign_id);
        }
    );
}

std::string_view localized_campaign_node_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::uint64_t node_id,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [campaign_id, node_id](std::string_view message_id) {
            return nested_named_ids(
                message_id, ".nodes.", campaign_id, node_id
            );
        }
    );
}

std::string_view localized_campaign_member_name(
    const package_format::LoadedPackage& package,
    std::string_view language,
    std::uint64_t campaign_id,
    std::uint64_t member_id,
    std::string_view current_source
) noexcept {
    return localized_matching(
        package, language, current_source,
        [campaign_id, member_id](std::string_view message_id) {
            return nested_named_ids(
                message_id, ".members.", campaign_id, member_id
            );
        }
    );
}

}  // namespace grandleon::package_runtime
