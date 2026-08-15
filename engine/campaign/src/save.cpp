// SPDX-License-Identifier: MIT
#include <grandleon/campaign/save.hpp>

#include <grandleon/core/bounds.hpp>
#include <grandleon/core/random.hpp>

#include <algorithm>
#include <array>
#include <utility>

namespace grandleon::campaign {
namespace {

constexpr std::array<std::uint8_t, 4> magic{'G', 'L', 'S', 'V'};

// Where the envelope checksum sits, so the writer can leave a hole and the
// reader can read the same hole as zero.
constexpr std::size_t envelope_checksum_offset = 48;
constexpr std::size_t envelope_checksum_size = 8;

// The fixed part of one record in each variable-length section. A count is
// refused before allocation when the bytes that remain could not hold that many
// fixed parts, which is the cheap half of the bound; the variable tails are
// checked as they are read.
constexpr std::size_t definition_ref_size = 28;
constexpr std::size_t stack_record_size = definition_ref_size + 4;
constexpr std::size_t unit_record_size =
    8 + definition_ref_size + 1 + 1 + 2 + 4 + (2 * growable_stat_count) + 4;
constexpr std::size_t objective_record_size = definition_ref_size + 4;
constexpr std::size_t world_record_size = definition_ref_size + 4 + 8;
constexpr std::size_t outcome_record_size = 8;
constexpr std::size_t progression_record_size = definition_ref_size + 8;

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

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
        out[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void patch_u64(
    std::vector<std::uint8_t>& out,
    std::size_t offset,
    std::uint64_t value
) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out[offset++] = static_cast<std::uint8_t>((value >> shift) & 0xffU);
    }
}

void put_definition_ref(
    std::vector<std::uint8_t>& out,
    const DefinitionRef& reference
) {
    out.insert(out.end(), reference.package_id.begin(), reference.package_id.end());
    put_u32(out, static_cast<std::uint32_t>(reference.category));
    put_u64(out, reference.stable_id);
}

void put_stack(std::vector<std::uint8_t>& out, const InventoryStack& stack) {
    put_definition_ref(out, stack.item);
    put_u32(out, stack.quantity);
}

void align_sections(std::vector<std::uint8_t>& out) {
    while ((out.size() % save_section_alignment) != 0U) {
        out.push_back(0);
    }
}

// ---------------------------------------------------------------------------
// Integrity
// ---------------------------------------------------------------------------

std::uint64_t fold(
    const std::uint8_t* data,
    std::size_t size,
    std::uint64_t hash
) noexcept {
    for (std::size_t index = 0; index < size; ++index) {
        hash = core::fnv1a64_step(hash, data[index]);
    }
    return hash;
}

std::uint64_t section_checksum(const std::uint8_t* data, std::size_t size) noexcept {
    return fold(data, size, core::fnv1a64_offset_basis);
}

// Over the header, the package table and the directory, with the eight bytes
// that hold the answer read as zero.
std::uint64_t envelope_checksum(
    const std::vector<std::uint8_t>& bytes,
    std::size_t metadata_end
) noexcept {
    std::uint64_t hash = core::fnv1a64_offset_basis;
    for (std::size_t index = 0; index < metadata_end; ++index) {
        const bool hole = index >= envelope_checksum_offset &&
                          index < envelope_checksum_offset + envelope_checksum_size;
        hash = core::fnv1a64_step(hash, hole ? std::uint8_t{0} : bytes[index]);
    }
    return hash;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// A cursor that cannot walk off the end. Every accessor answers false rather
// than reading, so a decode is a chain of `&&` and a truncated save falls out
// of it without a single unchecked index.
class Reader final {
public:
    Reader(const std::uint8_t* data, std::size_t size) noexcept
        : data_(data), size_(size) {}

    // Spelled the way the six readers in `engine/package_runtime` spell it.
    // Every mutation of `cursor_` below is gated by a `remaining()` test, so
    // the subtraction cannot wrap as the class stands. But that is a property
    // of all of its methods at once, and this is a bound the whole decode
    // rests on.
    [[nodiscard]] std::size_t remaining() const noexcept {
        return cursor_ <= size_ ? size_ - cursor_ : 0;
    }
    [[nodiscard]] bool done() const noexcept { return cursor_ == size_; }

    [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) {
            return false;
        }
        value = data_[cursor_++];
        return true;
    }

    [[nodiscard]] bool u16(std::uint16_t& value) noexcept {
        if (remaining() < 2U) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            data_[cursor_] | (static_cast<std::uint16_t>(data_[cursor_ + 1]) << 8U)
        );
        cursor_ += 2U;
        return true;
    }

    [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
        if (remaining() < 4U) {
            return false;
        }
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(data_[cursor_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool u64(std::uint64_t& value) noexcept {
        if (remaining() < 8U) {
            return false;
        }
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(data_[cursor_++]) << shift;
        }
        return true;
    }

    [[nodiscard]] bool definition_ref(DefinitionRef& reference) noexcept {
        if (remaining() < definition_ref_size) {
            return false;
        }
        for (std::size_t index = 0; index < reference.package_id.size(); ++index) {
            reference.package_id[index] = data_[cursor_ + index];
        }
        cursor_ += reference.package_id.size();
        std::uint32_t category = 0;
        std::uint64_t stable = 0;
        if (!u32(category) || !u64(stable)) {
            return false;
        }
        // The category is a namespace tag this module treats as opaque. It is
        // deliberately not range-checked: `core::ContentCategory` is
        // append-only, and a save written by a build that knows one more
        // category should fail the engine-compatibility check with a clear
        // answer rather than fail here with "invalid section".
        reference.category = static_cast<core::ContentCategory>(category);
        reference.stable_id = stable;
        return true;
    }

    [[nodiscard]] bool stack(InventoryStack& value) noexcept {
        return definition_ref(value.item) && u32(value.quantity);
    }

private:
    const std::uint8_t* data_{};
    std::size_t size_{};
    std::size_t cursor_{};
};

// The bound that matters: refuse a count before reserving for it. Both halves
// are needed: the cap keeps a merely large save from becoming a large
// allocation, and the remaining-bytes check keeps a small save from claiming to
// hold a million records.
[[nodiscard]] bool bounded_count(
    const Reader& reader,
    std::uint32_t count,
    std::size_t record_size,
    std::uint32_t cap
) noexcept {
    return count <= cap && static_cast<std::size_t>(count) <= reader.remaining() / record_size;
}

[[nodiscard]] bool version_below(core::Version lhs, core::Version rhs) noexcept {
    if (lhs.major != rhs.major) {
        return lhs.major < rhs.major;
    }
    if (lhs.minor != rhs.minor) {
        return lhs.minor < rhs.minor;
    }
    return lhs.patch < rhs.patch;
}

[[nodiscard]] bool package_below(
    const core::PackageId& lhs,
    const core::PackageId& rhs
) noexcept {
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return lhs[index] < rhs[index];
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Section bodies
// ---------------------------------------------------------------------------

std::vector<std::uint8_t> encode_roster(const CampaignState& state) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(state.units.size()));
    for (const PersistentUnit& unit : state.units) {
        put_u64(out, unit.id.value);
        put_definition_ref(out, unit.definition);
        put_u8(out, static_cast<std::uint8_t>(unit.availability));
        put_u8(out, 0);
        put_u16(out, unit.progression.level);
        put_u32(out, unit.progression.experience);
        // What the levels gave, in `GrowableStat` order. Written before the
        // carried stacks so that everything fixed-width in a member record is
        // in one run and only the variable-length tail is variable.
        for (std::uint16_t gain : unit.progression.gained) {
            put_u16(out, gain);
        }
        put_u32(out, static_cast<std::uint32_t>(unit.carried.size()));
        for (const InventoryStack& stack : unit.carried) {
            put_stack(out, stack);
        }
    }
    return out;
}

std::vector<std::uint8_t> encode_stacks(const std::vector<InventoryStack>& stacks) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(stacks.size()));
    for (const InventoryStack& stack : stacks) {
        put_stack(out, stack);
    }
    return out;
}

std::vector<std::uint8_t> encode_objectives(const CampaignState& state) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(state.objectives.size()));
    for (const ObjectiveRecord& record : state.objectives) {
        put_definition_ref(out, record.objective);
        put_u8(out, static_cast<std::uint8_t>(record.result));
        put_u8(out, 0);
        put_u16(out, 0);
    }
    return out;
}

std::vector<std::uint8_t> encode_world(const CampaignState& state) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(state.world.size()));
    for (const WorldFlag& flag : state.world) {
        put_definition_ref(out, flag.key);
        put_u8(out, static_cast<std::uint8_t>(flag.value.type));
        put_u8(out, 0);
        put_u16(out, 0);
        put_u64(out, static_cast<std::uint64_t>(flag.value.value));
    }
    return out;
}

std::vector<std::uint8_t> encode_outcomes(const CampaignState& state) {
    std::vector<std::uint8_t> out;
    put_u32(out, static_cast<std::uint32_t>(state.applied_outcomes.size()));
    for (const OutcomeId id : state.applied_outcomes) {
        put_u64(out, id.value);
    }
    return out;
}

// The route, in the order it was walked. `validate` has already proved that an
// active progression has at least one step and that its last step is the
// active node, so nothing here has a second opinion about either.
std::vector<std::uint8_t> encode_progression(const CampaignState& state) {
    std::vector<std::uint8_t> out;
    put_definition_ref(out, state.progress.campaign);
    put_definition_ref(out, state.progress.active_node);
    put_u32(out, static_cast<std::uint32_t>(state.progress.history.size()));
    for (const ProgressionEntry& entry : state.progress.history) {
        put_definition_ref(out, entry.node);
        put_u64(out, entry.cause.value);
    }
    return out;
}

std::vector<std::uint8_t> encode_section(
    SaveSectionType type,
    const CampaignState& state
) {
    switch (type) {
        case SaveSectionType::roster: return encode_roster(state);
        case SaveSectionType::store: return encode_stacks(state.store);
        case SaveSectionType::objectives: return encode_objectives(state);
        case SaveSectionType::world: return encode_world(state);
        case SaveSectionType::outcome_history: return encode_outcomes(state);
        case SaveSectionType::progression: return encode_progression(state);
    }
    return {};
}

// Whether this campaign has anything to say in this section. Only the
// progression section can answer no, and it answers no exactly when the
// campaign has not entered a graph, which is what makes the section's
// presence and the campaign's `active` flag one fact with one encoding.
[[nodiscard]] bool section_is_written(
    SaveSectionType type,
    const CampaignState& state
) noexcept {
    return type != SaveSectionType::progression || state.progress.active;
}

[[nodiscard]] bool known_availability(std::uint8_t value) noexcept {
    return value >= static_cast<std::uint8_t>(Availability::unrecruited) &&
           value <= static_cast<std::uint8_t>(Availability::dead);
}

[[nodiscard]] bool decode_stacks(
    Reader& reader,
    std::uint32_t cap,
    std::vector<InventoryStack>& out
) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || !bounded_count(reader, count, stack_record_size, cap)) {
        return false;
    }
    out.resize(count);
    for (InventoryStack& stack : out) {
        if (!reader.stack(stack)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_roster(
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || !bounded_count(reader, count, unit_record_size, limits.maximum_units)) {
        return false;
    }
    state.units.resize(count);
    for (PersistentUnit& unit : state.units) {
        std::uint8_t availability = 0;
        std::uint8_t reserved = 0;
        if (!reader.u64(unit.id.value) || !reader.definition_ref(unit.definition) ||
            !reader.u8(availability) || !reader.u8(reserved) ||
            !reader.u16(unit.progression.level) ||
            !reader.u32(unit.progression.experience)) {
            return false;
        }
        for (std::uint16_t& gain : unit.progression.gained) {
            if (!reader.u16(gain)) {
                return false;
            }
        }
        if (reserved != 0U || !known_availability(availability)) {
            return false;
        }
        unit.availability = static_cast<Availability>(availability);
        if (!decode_stacks(reader, limits.maximum_stacks, unit.carried)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_objectives(
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    std::uint32_t count = 0;
    if (!reader.u32(count) ||
        !bounded_count(reader, count, objective_record_size, limits.maximum_objectives)) {
        return false;
    }
    state.objectives.resize(count);
    for (ObjectiveRecord& record : state.objectives) {
        std::uint8_t result = 0;
        std::uint8_t reserved8 = 0;
        std::uint16_t reserved16 = 0;
        if (!reader.definition_ref(record.objective) || !reader.u8(result) ||
            !reader.u8(reserved8) || !reader.u16(reserved16)) {
            return false;
        }
        if (reserved8 != 0U || reserved16 != 0U ||
            result < static_cast<std::uint8_t>(ObjectiveOutcome::satisfied) ||
            result > static_cast<std::uint8_t>(ObjectiveOutcome::failed)) {
            return false;
        }
        record.result = static_cast<ObjectiveOutcome>(result);
    }
    return true;
}

[[nodiscard]] bool decode_world(
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    std::uint32_t count = 0;
    if (!reader.u32(count) ||
        !bounded_count(reader, count, world_record_size, limits.maximum_world_values)) {
        return false;
    }
    state.world.resize(count);
    for (WorldFlag& flag : state.world) {
        std::uint8_t type = 0;
        std::uint8_t reserved8 = 0;
        std::uint16_t reserved16 = 0;
        std::uint64_t value = 0;
        if (!reader.definition_ref(flag.key) || !reader.u8(type) ||
            !reader.u8(reserved8) || !reader.u16(reserved16) || !reader.u64(value)) {
            return false;
        }
        if (reserved8 != 0U || reserved16 != 0U ||
            type < static_cast<std::uint8_t>(WorldValueType::boolean) ||
            type > static_cast<std::uint8_t>(WorldValueType::integer)) {
            return false;
        }
        flag.value.type = static_cast<WorldValueType>(type);
        flag.value.value = static_cast<std::int64_t>(value);
    }
    return true;
}

[[nodiscard]] bool decode_outcomes(
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    std::uint32_t count = 0;
    if (!reader.u32(count) ||
        !bounded_count(reader, count, outcome_record_size, limits.maximum_outcomes)) {
        return false;
    }
    state.applied_outcomes.resize(count);
    for (OutcomeId& id : state.applied_outcomes) {
        if (!reader.u64(id.value)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool decode_progression(
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    CampaignProgress progress;
    std::uint32_t count = 0;
    if (!reader.definition_ref(progress.campaign) ||
        !reader.definition_ref(progress.active_node) || !reader.u32(count) ||
        !bounded_count(
            reader, count, progression_record_size,
            limits.maximum_progression_entries
        )) {
        return false;
    }
    // A section that is here at all is a campaign that entered a graph, so a
    // route of no steps is a contradiction rather than an empty case.
    if (count == 0U) {
        return false;
    }
    progress.active = true;
    progress.history.resize(count);
    for (ProgressionEntry& entry : progress.history) {
        if (!reader.definition_ref(entry.node) || !reader.u64(entry.cause.value)) {
            return false;
        }
    }
    state.progress = std::move(progress);
    return true;
}

[[nodiscard]] bool decode_section(
    SaveSectionType type,
    Reader& reader,
    const SaveLimits& limits,
    CampaignState& state
) {
    switch (type) {
        case SaveSectionType::roster: return decode_roster(reader, limits, state);
        case SaveSectionType::store:
            return decode_stacks(reader, limits.maximum_stacks, state.store);
        case SaveSectionType::objectives: return decode_objectives(reader, limits, state);
        case SaveSectionType::world: return decode_world(reader, limits, state);
        case SaveSectionType::outcome_history: return decode_outcomes(reader, limits, state);
        case SaveSectionType::progression:
            return decode_progression(reader, limits, state);
    }
    return false;
}

constexpr std::array<SaveSectionType, 6> known_sections{
    SaveSectionType::roster,
    SaveSectionType::store,
    SaveSectionType::objectives,
    SaveSectionType::world,
    SaveSectionType::outcome_history,
    SaveSectionType::progression,
};

// Field-by-field campaign equality.
//
// Deliberately not `canonical_hash`. The property that has to hold is that
// `load(save(state))` *is* the state, and a hash comparison proves that two
// campaigns fold to the same number, which is a weaker claim and the exact
// claim a serialization bug would be able to satisfy by accident. Every field
// the encoder writes is compared here, so a field the encoder forgets is a
// field this notices.
[[nodiscard]] bool stacks_equal(
    const std::vector<InventoryStack>& lhs,
    const std::vector<InventoryStack>& rhs
) noexcept {
    return lhs.size() == rhs.size() &&
           std::equal(
               lhs.begin(),
               lhs.end(),
               rhs.begin(),
               [](const InventoryStack& left, const InventoryStack& right) {
                   return left.item == right.item && left.quantity == right.quantity;
               }
           );
}

[[nodiscard]] bool states_equal(
    const CampaignState& lhs,
    const CampaignState& rhs
) noexcept {
    return lhs.units.size() == rhs.units.size() &&
           std::equal(
               lhs.units.begin(),
               lhs.units.end(),
               rhs.units.begin(),
               [](const PersistentUnit& left, const PersistentUnit& right) {
                   return left.id == right.id && left.definition == right.definition &&
                          left.availability == right.availability &&
                          left.progression == right.progression &&
                          stacks_equal(left.carried, right.carried);
               }
           ) &&
           stacks_equal(lhs.store, rhs.store) &&
           lhs.objectives.size() == rhs.objectives.size() &&
           std::equal(
               lhs.objectives.begin(),
               lhs.objectives.end(),
               rhs.objectives.begin(),
               [](const ObjectiveRecord& left, const ObjectiveRecord& right) {
                   return left.objective == right.objective &&
                          left.result == right.result;
               }
           ) &&
           lhs.world.size() == rhs.world.size() &&
           std::equal(
               lhs.world.begin(),
               lhs.world.end(),
               rhs.world.begin(),
               [](const WorldFlag& left, const WorldFlag& right) {
                   return left.key == right.key && left.value == right.value;
               }
           ) &&
           lhs.applied_outcomes.size() == rhs.applied_outcomes.size() &&
           std::equal(
               lhs.applied_outcomes.begin(),
               lhs.applied_outcomes.end(),
               rhs.applied_outcomes.begin()
           ) &&
           lhs.progress.active == rhs.progress.active &&
           lhs.progress.campaign == rhs.progress.campaign &&
           lhs.progress.active_node == rhs.progress.active_node &&
           lhs.progress.history.size() == rhs.progress.history.size() &&
           std::equal(
               lhs.progress.history.begin(),
               lhs.progress.history.end(),
               rhs.progress.history.begin()
           );
}

SaveDecodeResult decode_failure(SaveError error, std::uint32_t section = 0) {
    SaveDecodeResult result;
    result.error = error;
    result.section = section;
    return result;
}

SaveLoadResult load_failure(SaveError error, std::uint32_t section = 0) {
    SaveLoadResult result;
    result.error = error;
    result.section = section;
    return result;
}

}  // namespace

std::string_view save_section_name(std::uint32_t type) noexcept {
    switch (static_cast<SaveSectionType>(type)) {
        case SaveSectionType::roster: return "roster";
        case SaveSectionType::store: return "store";
        case SaveSectionType::objectives: return "objectives";
        case SaveSectionType::world: return "world";
        case SaveSectionType::outcome_history: return "outcome_history";
        case SaveSectionType::progression: return "progression";
    }
    return "unknown";
}

bool is_known_save_section(std::uint32_t type) noexcept {
    return std::find(
               known_sections.begin(),
               known_sections.end(),
               static_cast<SaveSectionType>(type)
           ) != known_sections.end();
}

SaveSectionSchema save_section_schema(SaveSectionType type) noexcept {
    switch (type) {
        // Schema 3 widened those points from six stats to ten, when skill,
        // luck, evasion and magic became growable. Schema 2 gave a member the
        // points their level-ups actually granted; schema 1 gave them
        // experience and their own inventory; schema 0 had neither. Every step
        // is registered in `standard_save_migrations`.
        case SaveSectionType::roster: return {3, 0};
        case SaveSectionType::store: return {1, 0};
        case SaveSectionType::objectives: return {1, 0};
        case SaveSectionType::world: return {1, 0};
        case SaveSectionType::outcome_history: return {1, 0};
        case SaveSectionType::progression: return {1, 0};
    }
    return {1, 0};
}

bool save_section_required(SaveSectionType type) noexcept {
    return type != SaveSectionType::progression;
}

std::string_view save_error_name(SaveError error) noexcept {
    switch (error) {
        case SaveError::none: return "none";
        case SaveError::oversized: return "oversized";
        case SaveError::truncated: return "truncated";
        case SaveError::invalid_magic: return "invalid_magic";
        case SaveError::unsupported_envelope: return "unsupported_envelope";
        case SaveError::invalid_header: return "invalid_header";
        case SaveError::incompatible_engine: return "incompatible_engine";
        case SaveError::incompatible_rules: return "incompatible_rules";
        case SaveError::invalid_package_table: return "invalid_package_table";
        case SaveError::duplicate_package: return "duplicate_package";
        case SaveError::unordered_packages: return "unordered_packages";
        case SaveError::invalid_directory: return "invalid_directory";
        case SaveError::duplicate_section: return "duplicate_section";
        case SaveError::unordered_directory: return "unordered_directory";
        case SaveError::checksum_mismatch: return "checksum_mismatch";
        case SaveError::unknown_required_section: return "unknown_required_section";
        case SaveError::unsupported_schema: return "unsupported_schema";
        case SaveError::invalid_section: return "invalid_section";
        case SaveError::missing_required_section: return "missing_required_section";
        case SaveError::invalid_state: return "invalid_state";
    }
    return "unknown";
}

bool operator==(
    const SavePackageRequirement& lhs,
    const SavePackageRequirement& rhs
) noexcept {
    return lhs.package == rhs.package &&
           lhs.content_revision == rhs.content_revision &&
           lhs.integrity == rhs.integrity;
}

bool operator==(const RetainedSection& lhs, const RetainedSection& rhs) noexcept {
    return lhs.type == rhs.type && lhs.schema_major == rhs.schema_major &&
           lhs.schema_minor == rhs.schema_minor && lhs.flags == rhs.flags &&
           lhs.bytes == rhs.bytes;
}

bool operator==(const SaveHeader& lhs, const SaveHeader& rhs) noexcept {
    return lhs.envelope_major == rhs.envelope_major &&
           lhs.envelope_minor == rhs.envelope_minor && lhs.engine == rhs.engine &&
           lhs.rules_contract == rhs.rules_contract;
}

bool operator==(const CampaignSave& lhs, const CampaignSave& rhs) noexcept {
    return lhs.header == rhs.header && lhs.packages == rhs.packages &&
           states_equal(lhs.state, rhs.state) && lhs.retained == rhs.retained;
}

CampaignSave make_campaign_save(
    CampaignState state,
    std::vector<SavePackageRequirement> packages
) {
    CampaignSave save;
    save.header.engine = core::engine_version();
    std::sort(
        packages.begin(),
        packages.end(),
        [](const SavePackageRequirement& lhs, const SavePackageRequirement& rhs) {
            return package_below(lhs.package, rhs.package);
        }
    );
    save.packages = std::move(packages);
    save.state = std::move(state);
    return save;
}

std::vector<std::uint8_t> save_campaign(const CampaignSave& save) {
    // One entry per section that will be written, in ascending type. Known
    // sections come from the campaign; retained ones come through untouched.
    struct Pending final {
        std::uint32_t type{};
        std::uint16_t schema_major{};
        std::uint16_t schema_minor{};
        std::uint32_t flags{};
        std::vector<std::uint8_t> bytes;
    };

    std::vector<Pending> pending;
    pending.reserve(known_sections.size() + save.retained.size());
    for (const SaveSectionType type : known_sections) {
        if (!section_is_written(type, save.state)) {
            continue;
        }
        const SaveSectionSchema schema = save_section_schema(type);
        pending.push_back(
            {static_cast<std::uint32_t>(type),
             schema.major,
             schema.minor,
             save_section_required(type) ? save_section_flag_required : 0U,
             encode_section(type, save.state)}
        );
    }
    for (const RetainedSection& section : save.retained) {
        // A retained section that collides with a section this build owns, or
        // with another retained one, is dropped rather than written: the writer
        // has no error channel, and a save that would not load is worse than a
        // section that was never ours.
        if (is_known_save_section(section.type)) {
            continue;
        }
        const bool duplicate = std::any_of(
            pending.begin(),
            pending.end(),
            [&section](const Pending& other) { return other.type == section.type; }
        );
        if (duplicate) {
            continue;
        }
        pending.push_back(
            {section.type,
             section.schema_major,
             section.schema_minor,
             section.flags,
             section.bytes}
        );
    }
    std::sort(
        pending.begin(),
        pending.end(),
        [](const Pending& lhs, const Pending& rhs) { return lhs.type < rhs.type; }
    );

    const std::size_t package_count = save.packages.size();
    const std::size_t section_count = pending.size();
    const std::size_t package_table_offset = save_header_size;
    const std::size_t directory_offset =
        package_table_offset + package_count * save_package_entry_size;
    const std::size_t metadata_end =
        directory_offset + section_count * save_directory_entry_size;

    std::vector<std::uint8_t> out;
    out.reserve(metadata_end);
    out.insert(out.end(), magic.begin(), magic.end());
    put_u16(out, save.header.envelope_major);
    put_u16(out, save.header.envelope_minor);
    put_u32(out, static_cast<std::uint32_t>(save_header_size));
    const std::size_t total_size_offset = out.size();
    put_u32(out, 0);
    put_u16(out, save.header.engine.major);
    put_u16(out, save.header.engine.minor);
    put_u16(out, save.header.engine.patch);
    put_u16(out, 0);
    put_u32(out, save.header.rules_contract);
    put_u32(out, static_cast<std::uint32_t>(package_count));
    put_u32(out, static_cast<std::uint32_t>(package_table_offset));
    put_u32(out, static_cast<std::uint32_t>(section_count));
    put_u32(out, static_cast<std::uint32_t>(directory_offset));
    put_u32(out, 0);
    put_u64(out, 0);
    put_u64(out, 0);

    for (const SavePackageRequirement& requirement : save.packages) {
        out.insert(
            out.end(),
            requirement.package.begin(),
            requirement.package.end()
        );
        put_u32(out, requirement.content_revision);
        put_u32(out, 0);
        put_u64(out, requirement.integrity);
    }

    out.resize(metadata_end, 0);
    for (std::size_t index = 0; index < section_count; ++index) {
        align_sections(out);
        const Pending& section = pending[index];
        const std::size_t offset = out.size();
        out.insert(out.end(), section.bytes.begin(), section.bytes.end());

        const std::size_t entry = directory_offset + index * save_directory_entry_size;
        patch_u32(out, entry, section.type);
        out[entry + 4] = static_cast<std::uint8_t>(section.schema_major & 0xffU);
        out[entry + 5] = static_cast<std::uint8_t>(section.schema_major >> 8U);
        out[entry + 6] = static_cast<std::uint8_t>(section.schema_minor & 0xffU);
        out[entry + 7] = static_cast<std::uint8_t>(section.schema_minor >> 8U);
        patch_u32(out, entry + 8, section.flags);
        patch_u32(out, entry + 12, static_cast<std::uint32_t>(offset));
        patch_u32(out, entry + 16, static_cast<std::uint32_t>(section.bytes.size()));
        patch_u32(out, entry + 20, 0);
        patch_u64(
            out,
            entry + 24,
            section_checksum(out.data() + offset, section.bytes.size())
        );
    }

    patch_u32(out, total_size_offset, static_cast<std::uint32_t>(out.size()));
    patch_u64(out, envelope_checksum_offset, envelope_checksum(out, metadata_end));
    return out;
}

SaveDecodeResult decode_save_envelope(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
) {
    // The cap is the first thing checked and the only thing checked before a
    // byte is read: an oversized save costs a comparison, not a copy.
    if (bytes.size() > options.limits.maximum_bytes) {
        return decode_failure(SaveError::oversized);
    }
    if (bytes.size() < save_header_size) {
        return decode_failure(SaveError::truncated);
    }
    if (!std::equal(magic.begin(), magic.end(), bytes.begin())) {
        return decode_failure(SaveError::invalid_magic);
    }

    // Past the magic, which `std::equal` above has already accepted.
    Reader header(bytes.data() + magic.size(), save_header_size - magic.size());
    SaveHeader meta;
    std::uint32_t found_header_size = 0;
    std::uint32_t total_size = 0;
    std::uint16_t reserved16 = 0;
    std::uint32_t package_count = 0;
    std::uint32_t package_table_offset = 0;
    std::uint32_t section_count = 0;
    std::uint32_t directory_offset = 0;
    std::uint32_t reserved32 = 0;
    std::uint64_t stored_checksum = 0;
    std::uint64_t reserved64 = 0;
    if (!header.u16(meta.envelope_major) ||
        !header.u16(meta.envelope_minor) || !header.u32(found_header_size) ||
        !header.u32(total_size) || !header.u16(meta.engine.major) ||
        !header.u16(meta.engine.minor) || !header.u16(meta.engine.patch) ||
        !header.u16(reserved16) || !header.u32(meta.rules_contract) ||
        !header.u32(package_count) || !header.u32(package_table_offset) ||
        !header.u32(section_count) || !header.u32(directory_offset) ||
        !header.u32(reserved32) || !header.u64(stored_checksum) ||
        !header.u64(reserved64)) {
        return decode_failure(SaveError::truncated);
    }

    // The envelope version gates everything after it: a different major means
    // the field offsets read above are not where this build thinks they are,
    // and every later diagnostic would be a guess.
    if (meta.envelope_major != save_envelope_major ||
        meta.envelope_minor > save_envelope_minor) {
        return decode_failure(SaveError::unsupported_envelope);
    }
    if (found_header_size != save_header_size) {
        return decode_failure(SaveError::invalid_header);
    }
    if (total_size > bytes.size()) {
        return decode_failure(SaveError::truncated);
    }
    if (total_size != bytes.size()) {
        return decode_failure(SaveError::invalid_header);
    }
    if (reserved16 != 0U || reserved32 != 0U || reserved64 != 0U) {
        return decode_failure(SaveError::invalid_header);
    }

    // The metadata is contiguous and its offsets follow from its counts. They
    // are written anyway and checked here rather than trusted, because a save
    // that disagrees with its own arithmetic is a save whose layout nothing can
    // reason about, and because it leaves exactly one byte encoding per save.
    if (package_count > options.limits.maximum_packages ||
        package_table_offset != save_header_size) {
        return decode_failure(SaveError::invalid_package_table);
    }
    const std::size_t package_table_end =
        static_cast<std::size_t>(package_table_offset) +
        static_cast<std::size_t>(package_count) * save_package_entry_size;
    if (package_table_end > bytes.size()) {
        return decode_failure(SaveError::truncated);
    }
    if (section_count > options.limits.maximum_sections ||
        directory_offset != package_table_end) {
        return decode_failure(SaveError::invalid_directory);
    }
    const std::size_t metadata_end =
        static_cast<std::size_t>(directory_offset) +
        static_cast<std::size_t>(section_count) * save_directory_entry_size;
    if (metadata_end > bytes.size()) {
        return decode_failure(SaveError::truncated);
    }

    // Integrity before meaning: from here on the header, the table and the
    // directory are known to be the bytes that were written.
    if (envelope_checksum(bytes, metadata_end) != stored_checksum) {
        return decode_failure(SaveError::checksum_mismatch);
    }

    if (version_below(meta.engine, options.minimum_engine) ||
        version_below(options.engine, meta.engine)) {
        return decode_failure(SaveError::incompatible_engine);
    }
    if (meta.rules_contract != options.rules_contract) {
        return decode_failure(SaveError::incompatible_rules);
    }

    SaveDecodeResult result;
    result.decoded.header = meta;
    result.decoded.packages.reserve(package_count);
    Reader packages(
        bytes.data() + package_table_offset,
        package_table_end - package_table_offset
    );
    for (std::uint32_t index = 0; index < package_count; ++index) {
        SavePackageRequirement requirement;
        std::uint32_t reserved = 0;
        for (std::size_t byte = 0; byte < requirement.package.size(); ++byte) {
            if (!packages.u8(requirement.package[byte])) {
                return decode_failure(SaveError::invalid_package_table);
            }
        }
        if (!packages.u32(requirement.content_revision) || !packages.u32(reserved) ||
            !packages.u64(requirement.integrity) || reserved != 0U) {
            return decode_failure(SaveError::invalid_package_table);
        }
        if (index > 0) {
            const core::PackageId& previous = result.decoded.packages.back().package;
            if (previous == requirement.package) {
                return decode_failure(SaveError::duplicate_package);
            }
            if (!package_below(previous, requirement.package)) {
                return decode_failure(SaveError::unordered_packages);
            }
        }
        result.decoded.packages.push_back(requirement);
    }

    result.decoded.sections.reserve(section_count);
    Reader directory(
        bytes.data() + directory_offset,
        metadata_end - directory_offset
    );
    std::size_t previous_end = metadata_end;
    for (std::uint32_t index = 0; index < section_count; ++index) {
        SaveSectionView view;
        std::uint32_t reserved = 0;
        std::uint64_t checksum = 0;
        if (!directory.u32(view.type) || !directory.u16(view.schema_major) ||
            !directory.u16(view.schema_minor) || !directory.u32(view.flags) ||
            !directory.u32(view.offset) || !directory.u32(view.size) ||
            !directory.u32(reserved) || !directory.u64(checksum) || reserved != 0U) {
            return decode_failure(SaveError::invalid_directory);
        }
        if (index > 0) {
            const SaveSectionView& previous = result.decoded.sections.back();
            if (previous.type == view.type) {
                return decode_failure(SaveError::duplicate_section, view.type);
            }
            if (previous.type > view.type) {
                return decode_failure(SaveError::unordered_directory, view.type);
            }
        }
        // Sections follow the metadata in directory order, each starting at the
        // next four-byte boundary after the last. Nothing overlaps, nothing
        // hides between them, and nothing trails the final one.
        const std::size_t expected = (previous_end + save_section_alignment - 1U) /
                                     save_section_alignment * save_section_alignment;
        if (view.offset != expected) {
            return decode_failure(SaveError::invalid_directory, view.type);
        }
        // Asked as a region rather than as a subtraction, because `expected`
        // is `previous_end` rounded *up*: a section whose size is not a
        // multiple of four puts the next one's mandatory start as much as
        // three bytes past a file that ends where it ends. `bytes.size() -
        // view.offset` would wrap there and admit the read below.
        if (!core::checked_region(bytes.size(), view.offset, view.size)) {
            return decode_failure(SaveError::truncated, view.type);
        }
        previous_end = static_cast<std::size_t>(view.offset) + view.size;
        result.decoded.sections.push_back(view);

        if (section_checksum(bytes.data() + view.offset, view.size) != checksum) {
            return decode_failure(SaveError::checksum_mismatch, view.type);
        }
    }
    if (previous_end != bytes.size()) {
        return decode_failure(SaveError::invalid_header);
    }

    result.decoded.bytes = bytes;
    return result;
}

SaveLoadResult interpret_save(DecodedSave decoded, const SaveLoadOptions& options) {
    SaveLoadResult result;
    result.save.header = decoded.header;
    result.save.packages = std::move(decoded.packages);

    std::array<bool, known_sections.size()> seen{};
    for (const SaveSectionView& view : decoded.sections) {
        if (!is_known_save_section(view.type)) {
            // The forward-compatibility rule, in three lines. A required
            // section this build cannot read is a promise it cannot keep.
            if (view.required()) {
                return load_failure(SaveError::unknown_required_section, view.type);
            }
            if (options.retain_unknown_sections) {
                RetainedSection retained;
                retained.type = view.type;
                retained.schema_major = view.schema_major;
                retained.schema_minor = view.schema_minor;
                retained.flags = view.flags;
                const auto begin = decoded.bytes.begin() +
                                   static_cast<std::ptrdiff_t>(view.offset);
                retained.bytes.assign(
                    begin,
                    begin + static_cast<std::ptrdiff_t>(view.size)
                );
                result.save.retained.push_back(std::move(retained));
            }
            continue;
        }

        const auto type = static_cast<SaveSectionType>(view.type);
        // A schema major this build does not read is what a migration is for.
        // The registry runs before this call; reaching here with a mismatch
        // means no migration claimed it.
        if (view.schema_major != save_section_schema(type).major) {
            return load_failure(SaveError::unsupported_schema, view.type);
        }
        Reader reader(decoded.bytes.data() + view.offset, view.size);
        if (!decode_section(type, reader, options.limits, result.save.state) ||
            !reader.done()) {
            return load_failure(SaveError::invalid_section, view.type);
        }
        // The known section values are 1..6 and contiguous, which the array
        // below relies on; `known_sections` is the list that keeps them so.
        seen[static_cast<std::size_t>(view.type) - 1U] = true;
    }

    for (std::size_t index = 0; index < known_sections.size(); ++index) {
        // An absent optional section is not a missing one. The progression
        // section is absent from every campaign that has not entered a graph,
        // which includes every save written before the section existed, and
        // absence means exactly that: unstarted.
        if (!seen[index] && save_section_required(known_sections[index])) {
            return load_failure(
                SaveError::missing_required_section,
                static_cast<std::uint32_t>(known_sections[index])
            );
        }
    }

    // The last gate, and the one that makes a decoded save a campaign: the
    // whole candidate is checked with the same function a commit is checked
    // with, so bytes cannot smuggle in an arrangement no operation could reach.
    const StateError state_error = validate(result.save.state);
    if (state_error != StateError::none) {
        result.error = SaveError::invalid_state;
        result.state_error = state_error;
        result.save = CampaignSave{};
        return result;
    }
    return result;
}

SaveLoadResult load_campaign(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
) {
    SaveDecodeResult decoded = decode_save_envelope(bytes, options);
    if (!decoded) {
        return load_failure(decoded.error, decoded.section);
    }
    return interpret_save(std::move(decoded.decoded), options);
}

SaveLoadResult load_campaign_into(
    CampaignSave& live,
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
) {
    SaveLoadResult result = load_campaign(bytes, options);
    if (!result) {
        return result;
    }
    live = result.save;
    return result;
}

}  // namespace grandleon::campaign
