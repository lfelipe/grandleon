// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/campaign/state.hpp>
#include <grandleon/core/version.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

// The bytes a campaign becomes when nobody is playing it.
//
// The accepted design asks for one shape and one procedure. The shape is "a
// sectioned versioned save envelope": compatibility metadata and integrity for
// independently versioned sections, fixed-width portable encoding rather than a
// C++ ABI layout, opaque bytes handed to platform storage, unknown optional
// sections retained or skipped and unknown required sections refused. The
// procedure is "migrate into temporary state": decode, validate, build a
// complete candidate, and only then replace what the session is holding.
//
// Both are here, and the seam between them is deliberate. `decode_save_envelope`
// answers "are these bytes a save, and are they intact?" without deciding what
// any section means. `interpret_save` answers "does this build understand what
// they say?" and produces a candidate campaign. The migration registry in
// `migration.hpp` goes between the two: it rewrites section bytes and
// section schema versions, and everything downstream of it re-runs unchanged.
//
// ## The envelope, byte for byte
//
// Every field is little-endian and fixed-width. Nothing here writes a struct's
// memory, a pointer, a container, or anything whose size a host ABI chose.
//
// ```
// header, 64 bytes
//   0  4   magic 'G' 'L' 'S' 'V'
//   4  2   envelope major
//   6  2   envelope minor
//   8  4   header size, 64
//  12  4   total size, which must equal the byte count exactly
//  16  2   writing engine major
//  18  2   writing engine minor
//  20  2   writing engine patch
//  22  2   reserved, zero
//  24  4   rules contract
//  28  4   package requirement count
//  32  4   package table offset
//  36  4   section count
//  40  4   section directory offset
//  44  4   reserved, zero
//  48  8   envelope checksum
//  56  8   reserved, zero
//
// package requirement, 32 bytes each, ascending package id, no duplicates
//   0 16   package id
//  16  4   content revision
//  20  4   reserved, zero
//  24  8   content integrity value
//
// section directory entry, 32 bytes each, ascending type, no duplicates
//   0  4   section type
//   4  2   section schema major
//   6  2   section schema minor
//   8  4   flags; bit 0 is `save_section_flag_required`
//  12  4   offset of the section's bytes, four-byte aligned
//  16  4   size of the section's bytes
//  20  4   reserved, zero
//  24  8   checksum of the section's bytes
//
// roster section body, type 1, schema 3.0
//   0  4   member count
//  then one variable-length member each, in ascending persistent id
//   0  8   persistent id
//   8 28   definition reference
//  36  1   availability
//  37  1   reserved, zero
//  38  2   level
//  40  4   lifetime experience
//  44 20   points gained per growable stat, ten 2-byte values in
//          `GrowableStat` order: health, strength, defense, resistance,
//          movement, action points, skill, luck, evasion, magic
//  64  4   carried stack count, then one stack each
//
// progression section body, type 6, schema 1.0, written only when the campaign
// has entered a graph
//   0 28   the campaign the position belongs to
//  28 28   the active node
//  56  4   route step count, at least one
//  then one 36-byte step each, in the order they were walked
//   0 28   the node entered
//  28  8   the outcome id whose commit entered it; zero for the first step
// ```
//
// ## What the two checksums are, and are not
//
// Both are FNV-1a-64, the arithmetic
// `engine/core/include/grandleon/core/random.hpp` names once and this
// repository has already proved bit-for-bit on a VR4300 and an R3000A, so a
// save written on a desktop and a save written on a console agree about their
// own integrity. Sixty-four bits is affordable here because a save is a host
// concern first, and both consoles do 64-bit arithmetic in software already.
//
// They detect **corruption, not tampering**. A flipped bit in a memory card, a
// truncated write, a file copied through something that mangled it: those are
// caught, and that is the whole claim. An attacker who edits a save can
// recompute both checksums with the constants printed above, and nothing here
// pretends otherwise: authenticity needs a key, a key needs somewhere to keep
// it, and a single-player save has no threat model that a keyless MAC would
// improve. A save is the player's file to edit.
//
// The envelope checksum covers the header, the package table and the directory,
// with its own eight bytes read as zero. The per-section checksums cover each
// section's bytes. Two levels rather than one because the sections are
// independently versioned: a save whose directory is intact but whose roster
// section is damaged must name the roster section in its diagnostic, and one
// checksum over everything could only say "something".
//
// ## Untrusted input
//
// Save bytes come from a filesystem, a memory card, or a browser's local
// storage, and none of those is a promise. Every count and every length is
// checked against the bytes that remain *and* against a hard cap from
// `SaveLimits` before anything is reserved or resized, exactly as
// `engine/package_format` checks a package. A save claiming four billion roster
// members is refused in constant time and constant memory.

namespace grandleon::campaign {

// The envelope's own version, which is separate from any section's. It changes
// when the header, the package table, or the directory changes shape, never
// because a section learned a field.
inline constexpr std::uint16_t save_envelope_major = 1;
inline constexpr std::uint16_t save_envelope_minor = 0;

// The rules contract a save was written against: the meaning of the campaign
// vocabulary rather than the layout of its bytes. Two builds with the same
// envelope version and different contracts disagree about what an availability
// value or an outcome operation *does*, which no amount of intact bytes fixes.
inline constexpr std::uint32_t save_rules_contract = 1;

inline constexpr std::size_t save_header_size = 64;
inline constexpr std::size_t save_package_entry_size = 32;
inline constexpr std::size_t save_directory_entry_size = 32;
inline constexpr std::size_t save_section_alignment = 4;

// A section this build does not know is refused when this bit is set and
// retained or dropped when it is not. That is the whole of the forward
// compatibility story, and it is the writer of the *newer* save that decides:
// a section a future build could do without is written optional, and one it
// could not is written required.
inline constexpr std::uint32_t save_section_flag_required = 1U;

// The sections a campaign is cut into. Values are persisted, so append only.
//
// The cut follows `CampaignState`'s own fields rather than convenience. A
// roster and an outcome history change at different rates and will want to
// change shape at different times; giving each its own schema version is what
// lets one of them move without a migration for all five.
enum class SaveSectionType : std::uint32_t {
    roster = 1,
    store = 2,
    objectives = 3,
    world = 4,
    outcome_history = 5,
    // Where the campaign stands in its authored graph, and the route it
    // walked to get there. The one section that is optional in both
    // directions: it is written only by a campaign that has entered a graph,
    // and a save without it is a campaign that has not. See
    // `save_section_required` below for why that is a decision rather than an
    // oversight.
    progression = 6,
};

[[nodiscard]] std::string_view save_section_name(std::uint32_t type) noexcept;

[[nodiscard]] bool is_known_save_section(std::uint32_t type) noexcept;

// One section's schema version, independent of the envelope's and of the other
// sections'. Five read 1.0 and the roster reads 2.0, which is the whole point
// of stating them per section rather than once: the day the roster moved was
// not the day the other five did, and no save had to be rewritten for them.
struct SaveSectionSchema final {
    std::uint16_t major{1};
    std::uint16_t minor{};
};

[[nodiscard]] SaveSectionSchema save_section_schema(SaveSectionType type) noexcept;

// Whether a save must carry this section for this build to load it, and
// whether this build marks it required when it writes one.
//
// Four of the five original sections and the roster are required, because a
// campaign without a roster is not a campaign that was saved: it is a save
// that lost something. `progression` is the exception, in both directions:
//
// * **Absent means unstarted.** A campaign that has not entered a graph has no
//   position to write, so it writes no section, and one byte encoding per
//   campaign is preserved. Every save written before this section existed is
//   exactly such a save, so it still loads and means what it always meant.
// * **Written optional.** A build that predates this section retains its bytes
//   rather than refusing the load, and writes them back out untouched. An
//   older build cannot advance a graph it does not know about, so retaining
//   the position is the whole of what it owes a newer one.
[[nodiscard]] bool save_section_required(SaveSectionType type) noexcept;

// One package the save's contents refer to, and what the save believed about
// it. The spec requires a save to name "every required package identity,
// content revision, and integrity value before the payload is interpreted",
// which is why this table sits between the header and the directory rather
// than inside a section: it is readable before a single campaign byte is.
//
// Nothing here resolves these against mounted content. That is the load
// path's job once packages and saves meet, and the resolution machinery lives
// in `identity.hpp`. What the format owes them is a place in the bytes, which
// this is.
struct SavePackageRequirement final {
    core::PackageId package{};
    std::uint32_t content_revision{};
    // Whatever the package layer computes over a mounted package. Recorded
    // rather than checked here: this module must not link the package format,
    // and a value it cannot verify is still a value it must not lose.
    std::uint64_t integrity{};
};

[[nodiscard]] bool operator==(
    const SavePackageRequirement& lhs,
    const SavePackageRequirement& rhs
) noexcept;

// An optional section this build did not recognise, carried through untouched.
//
// Retention is what keeps an older build from quietly deleting a newer build's
// data. Load a save written by a version that knows about, say, a relationship
// section; play; save. Without retention the relationships are gone, and the
// player is never told. With it the bytes come back out exactly as they went
// in, which is also the reason `save(load(bytes)) == bytes` can be asked for at
// all.
struct RetainedSection final {
    std::uint32_t type{};
    std::uint16_t schema_major{};
    std::uint16_t schema_minor{};
    std::uint32_t flags{};
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] bool operator==(
    const RetainedSection& lhs,
    const RetainedSection& rhs
) noexcept;

// What the header says about the save as a whole.
struct SaveHeader final {
    std::uint16_t envelope_major{save_envelope_major};
    std::uint16_t envelope_minor{save_envelope_minor};
    // The engine that wrote these bytes, not the one reading them.
    core::Version engine{};
    std::uint32_t rules_contract{save_rules_contract};
};

[[nodiscard]] bool operator==(const SaveHeader& lhs, const SaveHeader& rhs) noexcept;

// A campaign and everything the envelope carries beside it.
//
// This is what a save *is* as far as the rest of the engine is concerned; the
// bytes are an encoding of it. Loading produces one of these and saving
// consumes one, and the two are inverses.
struct CampaignSave final {
    SaveHeader header{};
    // Ascending package id, no duplicates.
    std::vector<SavePackageRequirement> packages;
    CampaignState state;
    // Ascending type, no duplicates, and never a type this build knows.
    std::vector<RetainedSection> retained;
};

[[nodiscard]] bool operator==(const CampaignSave& lhs, const CampaignSave& rhs) noexcept;

[[nodiscard]] inline bool operator!=(
    const CampaignSave& lhs,
    const CampaignSave& rhs
) noexcept {
    return !(lhs == rhs);
}

// A save of `state`, stamped with this build's engine version and rules
// contract, with `packages` put into canonical order.
[[nodiscard]] CampaignSave make_campaign_save(
    CampaignState state,
    std::vector<SavePackageRequirement> packages
);

// The hard caps a hostile save is measured against, before any allocation.
//
// Every one of them is also a budget. `maximum_bytes` is the one a constrained
// platform cares about: a Nintendo 64 save is thirty-two kilobytes of SRAM or
// two kilobytes of EEPROM, so the default megabyte is a desktop ceiling and a
// console adapter is expected to lower it and to say so. `tests/campaign`
// measures a representative campaign against these numbers so that those
// adapters have a figure to lower it against rather than a guess.
struct SaveLimits final {
    std::uint32_t maximum_bytes{1U << 20U};
    std::uint32_t maximum_sections{64};
    std::uint32_t maximum_packages{64};
    std::uint32_t maximum_units{4096};
    // Per owner, and separately for the shared store.
    std::uint32_t maximum_stacks{1024};
    std::uint32_t maximum_objectives{4096};
    std::uint32_t maximum_world_values{4096};
    std::uint32_t maximum_outcomes{65536};
    // Steps of one campaign's route. A cycle walked many times is the case
    // that makes this a cap rather than a node count.
    std::uint32_t maximum_progression_entries{4096};
};

struct SaveLoadOptions final {
    // The build doing the reading.
    core::Version engine{core::engine_version()};
    std::uint32_t rules_contract{save_rules_contract};
    // The oldest writing engine whose saves this build reads. A save older than
    // this needs a migration, and until the registry exists it is refused
    // rather than guessed at.
    core::Version minimum_engine{};
    // Whether an unrecognised optional section is carried through or dropped.
    // Retention is the default because losing a player's data silently is the
    // worse failure; a tool that deliberately rewrites a save to this build's
    // shape turns it off.
    bool retain_unknown_sections{true};
    SaveLimits limits{};
};

// Why a save was refused. Persisted in diagnostics, so append only.
enum class SaveError : std::uint8_t {
    none = 0,
    // More bytes than the caller's cap allows. Checked first and before any
    // copy, because the point of a cap is that the bytes past it are never
    // touched.
    oversized,
    // The bytes end inside something the format says is there.
    truncated,
    // Not a save.
    invalid_magic,
    // A save envelope this build does not read.
    unsupported_envelope,
    // The header contradicts itself: a size that is not the size, a table that
    // starts inside the header, a reserved field that is not zero.
    invalid_header,
    // Written by an engine outside the range this build reads.
    incompatible_engine,
    // Written against a different meaning of the campaign vocabulary.
    incompatible_rules,
    // The package table is out of bounds, over its cap, or malformed.
    invalid_package_table,
    // Two requirements name one package.
    duplicate_package,
    // The package table is not in ascending package order.
    unordered_packages,
    // The directory is out of bounds, over its cap, or names a section that
    // overlaps the metadata or runs off the end.
    invalid_directory,
    // Two directory entries name one section type.
    duplicate_section,
    // The directory is not in ascending section order.
    unordered_directory,
    // The envelope or a section does not match its recorded checksum.
    checksum_mismatch,
    // A section marked required is one this build does not know. The save may
    // be perfectly intact; this build simply cannot honour what it promises.
    unknown_required_section,
    // A known section is at a schema major this build does not read. This is
    // the error a migration exists to prevent, and it is raised after the
    // migration seam rather than before it.
    unsupported_schema,
    // A known section's bytes are the wrong length, or hold a value that is
    // not a member of an enumeration, or a count that does not fit.
    invalid_section,
    // A section this build requires is absent.
    missing_required_section,
    // The decoded campaign is not a campaign any sequence of legal operations
    // could have produced. `SaveLoadResult::state_error` says which invariant.
    invalid_state,
};

[[nodiscard]] std::string_view save_error_name(SaveError error) noexcept;

// One section as the envelope describes it, with its bytes located but not yet
// understood. This is the migration registry's working surface.
struct SaveSectionView final {
    std::uint32_t type{};
    std::uint16_t schema_major{};
    std::uint16_t schema_minor{};
    std::uint32_t flags{};
    // Into `DecodedSave::bytes`.
    std::uint32_t offset{};
    std::uint32_t size{};

    [[nodiscard]] bool required() const noexcept {
        return (flags & save_section_flag_required) != 0U;
    }
};

// A save that has been proved to be a save: bounded, in order, and matching
// every checksum it carries. Nothing in it has been interpreted.
struct DecodedSave final {
    SaveHeader header{};
    std::vector<SavePackageRequirement> packages;
    // The whole save, so that a section view stays a range rather than a copy.
    std::vector<std::uint8_t> bytes;
    // Ascending type, no duplicates.
    std::vector<SaveSectionView> sections;
};

struct SaveDecodeResult final {
    SaveError error{SaveError::none};
    // The raw section type the rejection is about, or zero when it is about the
    // envelope as a whole.
    std::uint32_t section{};
    DecodedSave decoded;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == SaveError::none;
    }
};

struct SaveLoadResult final {
    SaveError error{SaveError::none};
    std::uint32_t section{};
    // Set when `error` is `invalid_state`.
    StateError state_error{StateError::none};
    CampaignSave save;

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == SaveError::none;
    }
};

// Encode. Deterministic by construction: every collection in `CampaignState`
// has a stated canonical order, sections are written in ascending type, and
// nothing consults a clock, a pointer, or a hash-table iteration order. The
// same campaign encodes to the same bytes on every platform and in every run.
//
// Total, on purpose: it has no error channel, so a caller cannot be tempted to
// ignore one. A `CampaignSave` assembled by hand with a retained section that
// duplicates a known type, or duplicates another retained one, has that section
// dropped rather than written into a save that would not load.
[[nodiscard]] std::vector<std::uint8_t> save_campaign(const CampaignSave& save);

// Decode: bounds, order, integrity, and envelope-level compatibility. Allocates
// nothing whose size a hostile save chose.
[[nodiscard]] SaveDecodeResult decode_save_envelope(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
);

// Interpret a decoded save into a candidate campaign, and validate the whole of
// it. Unknown optional sections are retained or dropped here; unknown required
// sections and unreadable schema versions are refused here.
//
// `migration.hpp`'s registry runs between `decode_save_envelope` and this
// call: a migration reads `DecodedSave::sections`, rewrites the bytes and the
// schema versions it upgrades, and hands the result here. Nothing in this
// function needs to know a migration ran.
[[nodiscard]] SaveLoadResult interpret_save(
    DecodedSave decoded,
    const SaveLoadOptions& options
);

// Decode and interpret in one call.
[[nodiscard]] SaveLoadResult load_campaign(
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
);

// Load into a live session, or leave it exactly as it was.
//
// This is the design's "migrate into temporary state" in one signature. The
// candidate is decoded, interpreted, and fully validated before `live` is
// touched at all; a save that fails for any reason at any stage leaves the
// session holding the campaign it was already holding.
[[nodiscard]] SaveLoadResult load_campaign_into(
    CampaignSave& live,
    const std::vector<std::uint8_t>& bytes,
    const SaveLoadOptions& options
);

}  // namespace grandleon::campaign
