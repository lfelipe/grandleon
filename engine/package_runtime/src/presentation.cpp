// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/presentation.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace grandleon::package_runtime {
namespace {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

// The same bounded cursor every other reader in this module uses. It never
// reads past the record it was given, and every payload here is required to be
// consumed exactly: a declared count is measured against the bytes actually
// present before anything is reserved, and `finished()` is asked afterwards,
// so a record with unexplained trailing bytes is a refusal rather than a
// partial decode. Whoever adds a record to this section owes both.
class Reader final {
public:
    Reader(const LoadedPackage& package, const RecordView& record)
        : bytes_(package.byte_data()),
          cursor_(record.payload_offset),
          end_(static_cast<std::size_t>(record.payload_offset) +
               record.payload_size) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = bytes_[cursor_++];
        return true;
    }

    bool u16(std::uint16_t& value) {
        if (remaining() < 2) return false;
        value = static_cast<std::uint16_t>(
            bytes_[cursor_] |
            (static_cast<std::uint16_t>(bytes_[cursor_ + 1]) << 8U)
        );
        cursor_ += 2;
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(bytes_[cursor_++]) << shift;
        }
        return true;
    }

    bool skip_string() {
        std::uint16_t size = 0;
        if (!u16(size) || remaining() < size) return false;
        cursor_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return cursor_ <= end_ ? end_ - cursor_ : 0;
    }

    [[nodiscard]] bool finished() const noexcept { return cursor_ == end_; }

private:
    const std::uint8_t* bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

// A unit type record carries its faction identity in its third field. Reading
// only as far as that field is deliberate: the encounter loader owns the rest
// of the record, and presentation must not start validating gameplay data.
bool read_unit_type_faction(
    const LoadedPackage& package,
    const RecordView& record,
    std::uint64_t& faction_id
) {
    Reader reader(package, record);
    std::uint64_t class_id = 0;
    return reader.skip_string() && reader.u64(class_id) &&
           reader.u64(faction_id);
}

// One identity, one already-resolved art-library index.
using JoinEntry = std::pair<std::uint64_t, std::uint8_t>;

// Both content joins have the same shape (a count, then that many identity
// and value pairs) because both are tables a client binary searches. Decoded
// once here so the two cannot drift apart.
//
// Refuses a record whose payload does not decode exactly: a count the bytes
// present could not hold, bytes left over past the entries it declared, or one
// identity named twice. Sorted on the way out, so a writer's order is its own
// business and a reader's lookup is a binary search either way.
bool read_join(
    const LoadedPackage& package,
    const RecordView& record,
    std::vector<JoinEntry>& out
) {
    Reader reader(package, record);
    std::uint32_t count = 0;
    if (!reader.u32(count)) return false;
    // Nine bytes an entry, checked against the bytes actually present before
    // anything is reserved, so a lying count cannot ask for an allocation the
    // payload could never fill. Divided rather than multiplied: the product of
    // a hostile count and nine is not representable everywhere the quotient is.
    const std::size_t declared = reader.remaining();
    if (declared % 9U != 0 || declared / 9U != count) return false;

    out.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        std::uint64_t identity = 0;
        std::uint8_t value = 0;
        if (!reader.u64(identity) || !reader.u8(value)) return false;
        out.push_back({identity, value});
    }
    std::sort(
        out.begin(),
        out.end(),
        [](const JoinEntry& lhs, const JoinEntry& rhs) {
            return lhs.first < rhs.first;
        }
    );
    for (std::size_t index = 1; index < out.size(); ++index) {
        if (out[index].first == out[index - 1].first) return false;
    }
    return reader.finished();
}

// A sorted join's value for one identity, or `unresolved` when the table does
// not hold it, which is every identity when the package carries no join.
template<typename Entry, typename Identity, typename Value>
std::uint8_t lookup(
    const std::vector<Entry>& entries,
    Identity Entry::*identity,
    Value Entry::*value,
    std::uint64_t wanted,
    std::uint8_t unresolved
) noexcept {
    const auto found = std::lower_bound(
        entries.begin(),
        entries.end(),
        wanted,
        [identity](const Entry& entry, std::uint64_t id) {
            return entry.*identity < id;
        }
    );
    if (found == entries.end() || (*found).*identity != wanted) {
        return unresolved;
    }
    return (*found).*value;
}

}  // namespace

std::string_view error_name(PresentationError error) noexcept {
    switch (error) {
        case PresentationError::none: return "none";
        case PresentationError::malformed_payload: return "malformed_payload";
    }
    return "unknown";
}

std::uint8_t Presentation::colour_of_faction(
    std::uint64_t faction_id
) const noexcept {
    const auto found = std::lower_bound(
        factions.begin(),
        factions.end(),
        faction_id,
        [](const FactionColour& entry, std::uint64_t id) {
            return entry.faction_id < id;
        }
    );
    if (found == factions.end() || found->faction_id != faction_id) {
        return colour_unresolved;
    }
    return found->colour;
}

std::uint8_t Presentation::colour_of_unit_type(
    std::uint64_t unit_type_id
) const noexcept {
    const auto found = std::lower_bound(
        unit_types.begin(),
        unit_types.end(),
        unit_type_id,
        [](const UnitTypeColour& entry, std::uint64_t id) {
            return entry.unit_type_id < id;
        }
    );
    if (found == unit_types.end() || found->unit_type_id != unit_type_id) {
        return colour_unresolved;
    }
    return found->colour;
}

std::uint8_t Presentation::kind_of_terrain(
    std::uint64_t terrain_id
) const noexcept {
    return lookup(
        terrain,
        &TerrainKind::terrain_id,
        &TerrainKind::kind,
        terrain_id,
        terrain_kind_unresolved
    );
}

std::uint8_t Presentation::archetype_of_unit_type(
    std::uint64_t unit_type_id
) const noexcept {
    return lookup(
        archetypes,
        &UnitTypeArchetype::unit_type_id,
        &UnitTypeArchetype::archetype,
        unit_type_id,
        archetype_unresolved
    );
}

std::uint8_t Presentation::character_style_of_unit_type(
    std::uint64_t unit_type_id
) const noexcept {
    return lookup(
        character_styles,
        &UnitTypeCharacterStyle::unit_type_id,
        &UnitTypeCharacterStyle::style,
        unit_type_id,
        character_style_unresolved
    );
}

std::uint8_t Presentation::character_figure_of_unit_type(
    std::uint64_t unit_type_id
) const noexcept {
    return lookup(
        character_figures,
        &UnitTypeCharacterFigure::unit_type_id,
        &UnitTypeCharacterFigure::figure,
        unit_type_id,
        character_figure_unresolved
    );
}

PresentationLoadResult load_presentation(const LoadedPackage& package) {
    PresentationLoadResult result;
    auto fail = [&result] {
        result.error = PresentationError::malformed_payload;
        result.presentation = Presentation{};
        return result;
    };

    const RecordView* record = package.find(
        SectionType::presentation, package_format::presentation_record_id
    );
    // No section, or a section that does not hold the project record: a
    // package written before presentation data was carried. The defaults are
    // exactly what a client drew then.
    if (record == nullptr) return result;

    Reader reader(package, *record);
    std::uint8_t theme = default_theme;
    std::uint16_t faction_count = 0;
    if (!reader.u8(theme) || !reader.u16(faction_count)) return fail();
    // Nine bytes an entry, checked before reserving so a lying count cannot
    // ask for an allocation the payload could never fill.
    if (reader.remaining() != static_cast<std::size_t>(faction_count) * 9U) {
        return fail();
    }

    Presentation presentation;
    presentation.theme = theme;
    presentation.factions.reserve(faction_count);
    for (std::uint16_t index = 0; index < faction_count; ++index) {
        FactionColour entry;
        if (!reader.u64(entry.faction_id) || !reader.u8(entry.colour)) {
            return fail();
        }
        presentation.factions.push_back(entry);
    }
    std::sort(
        presentation.factions.begin(),
        presentation.factions.end(),
        [](const FactionColour& lhs, const FactionColour& rhs) {
            return lhs.faction_id < rhs.faction_id;
        }
    );
    if (!reader.finished()) return fail();
    for (std::size_t index = 1; index < presentation.factions.size(); ++index) {
        if (presentation.factions[index].faction_id ==
            presentation.factions[index - 1].faction_id) {
            return fail();
        }
    }

    // The join. A unit type naming no faction, or naming one this section does
    // not hold, stays unresolved, and the client falls back to the unit's side.
    const package_format::SectionView* unit_types =
        package.find(SectionType::unit_types);
    if (unit_types != nullptr) {
        presentation.unit_types.reserve(unit_types->records.size());
        for (const RecordView& unit_type : unit_types->records) {
            std::uint64_t faction_id = 0;
            if (!read_unit_type_faction(package, unit_type, faction_id)) {
                return fail();
            }
            presentation.unit_types.push_back(
                UnitTypeColour{
                    unit_type.stable_id,
                    presentation.colour_of_faction(faction_id)
                }
            );
        }
        std::sort(
            presentation.unit_types.begin(),
            presentation.unit_types.end(),
            [](const UnitTypeColour& lhs, const UnitTypeColour& rhs) {
                return lhs.unit_type_id < rhs.unit_type_id;
            }
        );
    }

    // The content joins, each in a record of its own. Absent is not an error
    // and is what every package written before they existed looks like: the
    // table stays empty and every identity resolves to "unresolved", which is
    // the fallback a client already draws.
    const RecordView* terrain = package.find(
        SectionType::presentation,
        package_format::presentation_terrain_record_id
    );
    if (terrain != nullptr) {
        std::vector<JoinEntry> entries;
        if (!read_join(package, *terrain, entries)) return fail();
        presentation.terrain.reserve(entries.size());
        for (const JoinEntry& entry : entries) {
            presentation.terrain.push_back(
                TerrainKind{entry.first, entry.second}
            );
        }
    }

    const RecordView* archetypes = package.find(
        SectionType::presentation,
        package_format::presentation_archetype_record_id
    );
    if (archetypes != nullptr) {
        std::vector<JoinEntry> entries;
        if (!read_join(package, *archetypes, entries)) return fail();
        presentation.archetypes.reserve(entries.size());
        for (const JoinEntry& entry : entries) {
            presentation.archetypes.push_back(
                UnitTypeArchetype{entry.first, entry.second}
            );
        }
    }

    // Which style draws each of those archetypes. Absent is not an error and
    // is the ordinary case: a game every character of which follows the game's
    // own style writes no record here at all, and every unit type then
    // resolves to `character_style_unresolved`: "draw it the way you were
    // going to draw the game", which is what every client did before a
    // character could name a style.
    const RecordView* character_styles = package.find(
        SectionType::presentation,
        package_format::presentation_style_record_id
    );
    if (character_styles != nullptr) {
        std::vector<JoinEntry> entries;
        if (!read_join(package, *character_styles, entries)) return fail();
        presentation.character_styles.reserve(entries.size());
        for (const JoinEntry& entry : entries) {
            presentation.character_styles.push_back(
                UnitTypeCharacterStyle{entry.first, entry.second}
            );
        }
    }

    const RecordView* character_figures = package.find(
        SectionType::presentation,
        package_format::presentation_figure_record_id
    );
    if (character_figures != nullptr) {
        std::vector<JoinEntry> entries;
        if (!read_join(package, *character_figures, entries)) return fail();
        presentation.character_figures.reserve(entries.size());
        for (const JoinEntry& entry : entries) {
            presentation.character_figures.push_back(
                UnitTypeCharacterFigure{entry.first, entry.second}
            );
        }
    }

    result.presentation = std::move(presentation);
    return result;
}

}  // namespace grandleon::package_runtime
