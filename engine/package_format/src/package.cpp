// SPDX-License-Identifier: MIT
#include <grandleon/package_format/package.hpp>

#include <grandleon/core/bounds.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace grandleon::package_format {
namespace {

constexpr std::array<std::uint8_t, 4> magic{'G', 'L', 'P', 'K'};
constexpr std::uint32_t header_size = 72;
constexpr std::uint32_t directory_entry_size = 32;
constexpr std::uint32_t record_header_size = 12;
constexpr std::uint32_t alignment = 4;

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void patch_u32(
    std::vector<std::uint8_t>& out,
    std::size_t offset,
    std::uint32_t value
) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        out[offset++] =
            static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void align(std::vector<std::uint8_t>& out) {
    while ((out.size() % alignment) != 0U) {
        out.push_back(0);
    }
}

std::uint32_t checksum(
    PackageBytes bytes,
    std::size_t offset,
    std::size_t size
) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = offset; index < offset + size; ++index) {
        hash ^= bytes.data[index];
        hash *= 16777619U;
    }
    return hash;
}

std::uint32_t checksum(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset,
    std::size_t size
) {
    return checksum(PackageBytes{bytes.data(), bytes.size()}, offset, size);
}

std::uint32_t envelope_checksum(
    PackageBytes bytes,
    std::size_t directory_end
) {
    std::uint32_t hash = 2166136261U;
    for (std::size_t index = 0; index < directory_end; ++index) {
        const std::uint8_t value =
            index >= 68U && index < 72U ? 0U : bytes.data[index];
        hash ^= value;
        hash *= 16777619U;
    }
    return hash;
}

std::uint32_t envelope_checksum(
    const std::vector<std::uint8_t>& bytes,
    std::size_t directory_end
) {
    return envelope_checksum(
        PackageBytes{bytes.data(), bytes.size()}, directory_end
    );
}

using core::checked_region;

bool get_u16(
    PackageBytes bytes,
    std::size_t offset,
    std::uint16_t& value
) {
    if (!checked_region(bytes.size, offset, 2)) {
        return false;
    }
    value = static_cast<std::uint16_t>(
        bytes.data[offset] |
        (static_cast<std::uint16_t>(bytes.data[offset + 1]) << 8U)
    );
    return true;
}

bool get_u32(
    PackageBytes bytes,
    std::size_t offset,
    std::uint32_t& value
) {
    if (!checked_region(bytes.size, offset, 4)) {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes.data[offset++]) << shift;
    }
    return true;
}

bool get_u64(
    PackageBytes bytes,
    std::size_t offset,
    std::uint64_t& value
) {
    if (!checked_region(bytes.size, offset, 8)) {
        return false;
    }
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes.data[offset++]) << shift;
    }
    return true;
}

void put_version(std::vector<std::uint8_t>& out, Version version) {
    put_u16(out, version.major);
    put_u16(out, version.minor);
    put_u16(out, version.patch);
}

bool get_version(
    PackageBytes bytes,
    std::size_t offset,
    Version& version
) {
    return get_u16(bytes, offset, version.major) &&
           get_u16(bytes, offset + 2, version.minor) &&
           get_u16(bytes, offset + 4, version.patch);
}

struct DirectoryEntry final {
    SectionType type{};
    std::uint16_t schema_major{};
    std::uint16_t schema_minor{};
    std::uint32_t flags{};
    std::uint32_t record_count{};
    std::uint32_t offset{};
    std::uint32_t stored_size{};
    std::uint32_t unpacked_size{};
    std::uint32_t checksum{};
    bool supported{true};
};

// Enumerated rather than bounded by its ends. A range test says "known" about
// every value between the first section type and the last, which is a promise
// about numbers nobody has assigned yet: a value withdrawn from the middle of
// the list would keep loading as a section this build claims to understand.
// Listing them also means `-Wswitch` names this function when a type is
// appended, which is the moment the list has to be extended.
bool is_known_section(std::uint32_t type) {
    switch (static_cast<SectionType>(type)) {
        case SectionType::manifest:
        case SectionType::classes:
        case SectionType::unit_types:
        case SectionType::weapons:
        case SectionType::items:
        case SectionType::maps:
        case SectionType::dialogue:
        case SectionType::presentation:
        case SectionType::weapon_types:
        case SectionType::item_types:
        case SectionType::factions:
        case SectionType::objectives:
        case SectionType::encounters:
        case SectionType::campaigns:
        case SectionType::abilities:
        case SectionType::talks:
        case SectionType::arrivals:
        case SectionType::placement_names:
        case SectionType::moments:
            return true;
    }
    return false;
}

}  // namespace

std::string_view error_name(Error error) noexcept {
    switch (error) {
        case Error::none: return "none";
        case Error::truncated: return "truncated";
        case Error::invalid_magic: return "invalid_magic";
        case Error::unsupported_container: return "unsupported_container";
        case Error::incompatible_engine: return "incompatible_engine";
        case Error::incompatible_target: return "incompatible_target";
        case Error::unsupported_feature: return "unsupported_feature";
        case Error::unsupported_required_section:
            return "unsupported_required_section";
        case Error::unsupported_schema: return "unsupported_schema";
        case Error::invalid_directory: return "invalid_directory";
        case Error::duplicate_section: return "duplicate_section";
        case Error::invalid_section: return "invalid_section";
        case Error::checksum_mismatch: return "checksum_mismatch";
        case Error::invalid_record: return "invalid_record";
        case Error::duplicate_record: return "duplicate_record";
    }
    return "unknown";
}

const SectionView* LoadedPackage::find(SectionType type) const noexcept {
    const auto found = std::find_if(
        sections.begin(),
        sections.end(),
        [type](const SectionView& section) { return section.type == type; }
    );
    return found == sections.end() ? nullptr : &*found;
}

const RecordView* LoadedPackage::find(
    SectionType type,
    std::uint64_t stable_id
) const noexcept {
    const SectionView* section = find(type);
    if (section == nullptr) {
        return nullptr;
    }
    const auto found = std::lower_bound(
        section->records.begin(),
        section->records.end(),
        stable_id,
        [](const RecordView& record, std::uint64_t id) {
            return record.stable_id < id;
        }
    );
    return found == section->records.end() || found->stable_id != stable_id
               ? nullptr
               : &*found;
}

namespace {

// Which section holds records of a category, for the categories that have one.
//
// The two enumerations agree on the fourteen kinds a package stores records of
// and then part: `campaign_node` and `world_flag` are identities with no
// section of their own, while 15 and 16 in the directory are `abilities` and
// `talks`. Carrying the number across would answer a question about a campaign
// node with an ability's bytes: a record of the wrong shape, handed to a
// caller that has already been told what shape it asked for.
//
// Written out rather than cast, so the compiler is the thing that notices. The
// switch is exhaustive and has no `default`, which makes a category added to
// `core::ContentCategory` tomorrow a build failure here instead of a silent
// landing on whatever section shares its number.
[[nodiscard]] bool section_of_category(
    core::ContentCategory category,
    SectionType& type
) noexcept {
    switch (category) {
        case core::ContentCategory::manifest:
            type = SectionType::manifest;
            return true;
        case core::ContentCategory::unit_class:
            type = SectionType::classes;
            return true;
        case core::ContentCategory::unit_type:
            type = SectionType::unit_types;
            return true;
        case core::ContentCategory::weapon:
            type = SectionType::weapons;
            return true;
        case core::ContentCategory::item:
            type = SectionType::items;
            return true;
        case core::ContentCategory::map:
            type = SectionType::maps;
            return true;
        case core::ContentCategory::dialogue:
            type = SectionType::dialogue;
            return true;
        case core::ContentCategory::presentation:
            type = SectionType::presentation;
            return true;
        case core::ContentCategory::weapon_type:
            type = SectionType::weapon_types;
            return true;
        case core::ContentCategory::item_type:
            type = SectionType::item_types;
            return true;
        case core::ContentCategory::faction:
            type = SectionType::factions;
            return true;
        case core::ContentCategory::objective:
            type = SectionType::objectives;
            return true;
        case core::ContentCategory::encounter:
            type = SectionType::encounters;
            return true;
        case core::ContentCategory::campaign:
            type = SectionType::campaigns;
            return true;
        // A node lives inside its campaign's record and a flag is named rather
        // than stored. Both are real identities and neither is a record, so
        // the honest answer to "which section holds one" is that none does.
        case core::ContentCategory::campaign_node:
        case core::ContentCategory::world_flag:
            return false;
    }
    // A value no enumerator names, which a package's bytes can produce.
    return false;
}

}  // namespace

const RecordView* LoadedPackage::find(
    const core::ContentRef& reference
) const noexcept {
    if (reference.package_id != game_id) {
        return nullptr;
    }
    SectionType type{};
    if (!section_of_category(reference.category, type)) {
        return nullptr;
    }
    return find(type, reference.stable_id);
}

std::vector<std::uint8_t> write_mock_package(const PackageSource& source) {
    std::vector<std::uint8_t> out;
    out.reserve(header_size + source.sections.size() * directory_entry_size);
    out.insert(out.end(), magic.begin(), magic.end());
    put_u16(out, container_major);
    put_u16(out, container_minor);
    put_u32(out, header_size);
    const std::size_t total_size_offset = out.size();
    put_u32(out, 0);
    out.insert(out.end(), source.game_id.begin(), source.game_id.end());
    put_u32(out, source.content_revision);
    put_version(out, source.required_engine.minimum);
    put_version(out, source.required_engine.maximum);
    put_u32(out, static_cast<std::uint32_t>(source.target));
    put_u64(out, source.required_features);
    put_u32(out, static_cast<std::uint32_t>(source.sections.size()));
    put_u32(out, header_size);
    const std::size_t envelope_checksum_offset = out.size();
    put_u32(out, 0);

    const std::size_t directory_offset = out.size();
    out.resize(
        out.size() + source.sections.size() * directory_entry_size,
        0
    );

    for (std::size_t index = 0; index < source.sections.size(); ++index) {
        align(out);
        const SectionSource& source_section = source.sections[index];
        const std::uint32_t section_offset =
            static_cast<std::uint32_t>(out.size());
        for (const RecordSource& record : source_section.records) {
            put_u64(out, record.stable_id);
            put_u32(out, static_cast<std::uint32_t>(record.payload.size()));
            out.insert(out.end(), record.payload.begin(), record.payload.end());
            align(out);
        }
        const std::uint32_t section_size =
            static_cast<std::uint32_t>(out.size()) - section_offset;
        const std::size_t entry = directory_offset + index * directory_entry_size;
        patch_u32(out, entry, static_cast<std::uint32_t>(source_section.type));
        out[entry + 4] =
            static_cast<std::uint8_t>(source_section.schema_major & 0xffU);
        out[entry + 5] =
            static_cast<std::uint8_t>(source_section.schema_major >> 8U);
        out[entry + 6] =
            static_cast<std::uint8_t>(source_section.schema_minor & 0xffU);
        out[entry + 7] =
            static_cast<std::uint8_t>(source_section.schema_minor >> 8U);
        patch_u32(out, entry + 8, source_section.flags);
        patch_u32(
            out,
            entry + 12,
            static_cast<std::uint32_t>(source_section.records.size())
        );
        patch_u32(out, entry + 16, section_offset);
        patch_u32(out, entry + 20, section_size);
        patch_u32(out, entry + 24, section_size);
        patch_u32(
            out,
            entry + 28,
            checksum(out, section_offset, section_size)
        );
    }
    patch_u32(out, total_size_offset, static_cast<std::uint32_t>(out.size()));
    patch_u32(
        out,
        envelope_checksum_offset,
        envelope_checksum(
            out,
            directory_offset +
                source.sections.size() * directory_entry_size
        )
    );
    return out;
}

LoadResult load_mock_package_in_place(
    PackageBytes bytes,
    const LoadOptions& options
) {
    auto fail = [](Error error) {
        LoadResult result;
        result.error = error;
        return result;
    };
    if (bytes.data == nullptr || bytes.size < header_size) {
        return fail(Error::truncated);
    }
    if (!std::equal(magic.begin(), magic.end(), bytes.data)) {
        return fail(Error::invalid_magic);
    }

    std::uint16_t format_major = 0;
    std::uint16_t format_minor = 0;
    std::uint32_t found_header_size = 0;
    std::uint32_t total_size = 0;
    if (!get_u16(bytes, 4, format_major) ||
        !get_u16(bytes, 6, format_minor) ||
        !get_u32(bytes, 8, found_header_size) ||
        !get_u32(bytes, 12, total_size)) {
        return fail(Error::truncated);
    }
    if (format_major != container_major || format_minor > container_minor) {
        return fail(Error::unsupported_container);
    }
    if (found_header_size < header_size || total_size != bytes.size) {
        return fail(Error::invalid_directory);
    }

    LoadResult result;
    std::copy_n(bytes.data + 16, 16, result.package.game_id.begin());
    if (!get_u32(bytes, 32, result.package.content_revision) ||
        !get_version(bytes, 36, result.package.required_engine.minimum) ||
        !get_version(bytes, 42, result.package.required_engine.maximum)) {
        return fail(Error::truncated);
    }
    if (result.package.required_engine.maximum <
            result.package.required_engine.minimum ||
        !result.package.required_engine.contains(options.engine_version)) {
        return fail(Error::incompatible_engine);
    }

    std::uint32_t target = 0;
    std::uint32_t section_count = 0;
    std::uint32_t directory_offset = 0;
    std::uint32_t expected_envelope_checksum = 0;
    if (!get_u32(bytes, 48, target) ||
        !get_u64(bytes, 52, result.package.required_features) ||
        !get_u32(bytes, 60, section_count) ||
        !get_u32(bytes, 64, directory_offset) ||
        !get_u32(bytes, 68, expected_envelope_checksum)) {
        return fail(Error::truncated);
    }
    result.package.target = static_cast<TargetProfile>(target);
    if (result.package.target != TargetProfile::portable &&
        result.package.target != options.target) {
        return fail(Error::incompatible_target);
    }
    if ((result.package.required_features & ~options.supported_features) != 0U) {
        return fail(Error::unsupported_feature);
    }
    if (section_count > options.maximum_sections ||
        section_count >
            std::numeric_limits<std::uint32_t>::max() / directory_entry_size ||
        directory_offset < found_header_size ||
        !checked_region(
            bytes.size,
            directory_offset,
            section_count * directory_entry_size
        )) {
        return fail(Error::invalid_directory);
    }
    const std::size_t directory_end =
        static_cast<std::size_t>(directory_offset) +
        static_cast<std::size_t>(section_count) * directory_entry_size;
    if (envelope_checksum(bytes, directory_end) !=
        expected_envelope_checksum) {
        return fail(Error::checksum_mismatch);
    }

    std::vector<DirectoryEntry> entries;
    entries.reserve(section_count);
    std::set<std::uint32_t> section_types;
    for (std::uint32_t index = 0; index < section_count; ++index) {
        const std::size_t offset =
            static_cast<std::size_t>(directory_offset) +
            static_cast<std::size_t>(index) * directory_entry_size;
        DirectoryEntry entry;
        std::uint32_t type = 0;
        if (!get_u32(bytes, offset, type) ||
            !get_u16(bytes, offset + 4, entry.schema_major) ||
            !get_u16(bytes, offset + 6, entry.schema_minor) ||
            !get_u32(bytes, offset + 8, entry.flags) ||
            !get_u32(bytes, offset + 12, entry.record_count) ||
            !get_u32(bytes, offset + 16, entry.offset) ||
            !get_u32(bytes, offset + 20, entry.stored_size) ||
            !get_u32(bytes, offset + 24, entry.unpacked_size) ||
            !get_u32(bytes, offset + 28, entry.checksum)) {
            return fail(Error::invalid_directory);
        }
        entry.type = static_cast<SectionType>(type);
        if (!section_types.insert(type).second) {
            return fail(Error::duplicate_section);
        }
        if (entry.record_count > options.maximum_records_per_section ||
            entry.stored_size != entry.unpacked_size ||
            (entry.offset % alignment) != 0U ||
            entry.offset < directory_end ||
            !checked_region(bytes.size, entry.offset, entry.stored_size)) {
            return fail(Error::invalid_section);
        }
        if (checksum(bytes, entry.offset, entry.stored_size) != entry.checksum) {
            return fail(Error::checksum_mismatch);
        }
        const bool required = (entry.flags & section_flag_required) != 0U;
        if (!is_known_section(type)) {
            if (required) {
                return fail(Error::unsupported_required_section);
            }
            entry.supported = false;
        }
        if (entry.schema_major != 1U) {
            if (required) {
                return fail(Error::unsupported_schema);
            }
            entry.supported = false;
        }
        entries.push_back(entry);
    }

    std::vector<const DirectoryEntry*> ordered_entries;
    ordered_entries.reserve(entries.size());
    for (const DirectoryEntry& entry : entries) {
        ordered_entries.push_back(&entry);
    }
    std::sort(
        ordered_entries.begin(),
        ordered_entries.end(),
        [](const DirectoryEntry* lhs, const DirectoryEntry* rhs) {
            return lhs->offset < rhs->offset;
        }
    );
    // Sections in file order, with nothing unaccounted for between them and
    // nothing after the last.
    //
    // Every byte of a package is covered by exactly one of three things: the
    // envelope hash covers the header and the directory, each section's hash
    // covers its own extent, and the alignment padding in between is covered
    // by neither. So the only way two files that decode to the same package
    // can be required to be the same file is to fix what the padding holds.
    // Zero is what the writer puts there, and requiring it is what makes a
    // package's bytes an identity rather than one of many spellings.
    std::size_t previous_end = directory_end;
    for (const DirectoryEntry* entry : ordered_entries) {
        if (entry->offset < previous_end) {
            return fail(Error::invalid_section);
        }
        for (std::size_t index = previous_end; index < entry->offset; ++index) {
            if (bytes.data[index] != 0U) return fail(Error::invalid_section);
        }
        previous_end =
            static_cast<std::size_t>(entry->offset) + entry->stored_size;
    }
    if (previous_end != bytes.size) {
        return fail(Error::invalid_section);
    }

    result.package.sections.reserve(section_count);
    for (const DirectoryEntry& entry : entries) {
        if (!entry.supported) {
            continue;
        }
        SectionView section{
            entry.type,
            entry.schema_major,
            entry.schema_minor,
            entry.flags,
            {}
        };
        // A record costs at least its own header, so the count is checked
        // against the bytes the section actually has before anything is
        // reserved: a lying count must not be able to ask for an allocation
        // the section could never fill. Divided rather than multiplied,
        // because the product of a hostile count and twelve is not
        // representable everywhere the quotient is.
        if (entry.record_count > entry.stored_size / record_header_size) {
            return fail(Error::invalid_record);
        }
        section.records.reserve(entry.record_count);
        std::size_t cursor = entry.offset;
        const std::size_t end = entry.offset + entry.stored_size;
        std::set<std::uint64_t> ids;
        for (std::uint32_t index = 0; index < entry.record_count; ++index) {
            std::uint64_t stable_id = 0;
            std::uint32_t payload_size = 0;
            if (cursor > end || end - cursor < record_header_size ||
                !get_u64(bytes, cursor, stable_id) ||
                !get_u32(bytes, cursor + 8, payload_size) ||
                payload_size > end - cursor - record_header_size) {
                return fail(Error::invalid_record);
            }
            if (!ids.insert(stable_id).second) {
                return fail(Error::duplicate_record);
            }
            section.records.push_back(
                RecordView{
                    stable_id,
                    static_cast<std::uint32_t>(cursor + record_header_size),
                    payload_size
                }
            );
            cursor += record_header_size + payload_size;
            // Rounded up in the cursor's own width. A mask built from an
            // `unsigned int` widens with zeros in its upper half, which turns
            // the round-up into a truncation for any cursor above four
            // gigabytes and restarts record parsing at the file header.
            cursor = (cursor + alignment - 1U) &
                     ~static_cast<std::size_t>(alignment - 1U);
        }
        if (cursor != end) {
            return fail(Error::invalid_record);
        }
        std::sort(
            section.records.begin(),
            section.records.end(),
            [](const RecordView& lhs, const RecordView& rhs) {
                return lhs.stable_id < rhs.stable_id;
            }
        );
        result.package.sections.push_back(std::move(section));
    }

    result.package.borrowed = bytes;
    return result;
}

LoadResult load_mock_package(
    const std::vector<std::uint8_t>& bytes,
    const LoadOptions& options
) {
    // One validator, two ownerships. The copy is taken after the load rather
    // than before it so that a package this build refuses costs nothing, and
    // `borrowed` is cleared with it so that `byte_data()` answers the copy.
    // The caller's vector is allowed to go away the moment this returns.
    LoadResult result =
        load_mock_package_in_place({bytes.data(), bytes.size()}, options);
    result.package.borrowed = {};
    if (result.error == Error::none) {
        result.package.bytes = bytes;
    }
    return result;
}

}  // namespace grandleon::package_format
