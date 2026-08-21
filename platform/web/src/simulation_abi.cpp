// SPDX-License-Identifier: MIT
// WebAssembly binding surface for the authoritative encounter simulation.
//
// The surface that drives an encounter end to end: create one from a
// definition, apply a command, read a snapshot, read the canonical hash. Then
// the campaign flow around it and the content compiler, which is here because
// the browser otherwise has no package bytes at all. The package container
// format is not bound, and neither is any other engine capability.
//
// The boundary is a flat C ABI over one static little-endian scratch buffer in
// linear memory. Nothing is passed by struct pointer, so the JavaScript side
// never depends on the C++ ABI's padding, alignment, or field ordering; the
// wire format documented in platform/web/README.md is the only contract.
//
// Encounter handles are opaque indices validated against a registry. A caller
// cannot pass an arbitrary integer and have it treated as a pointer.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The scratch-buffer ABI has no Emscripten dependency beyond the export
// annotation, so the native ABI round-trip test in tests/abi compiles this
// translation unit for the host.
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

#include "grandleon/campaign/migration.hpp"
#include "grandleon/campaign/outcome.hpp"
#include "grandleon/campaign/save.hpp"
#include "grandleon/campaign/state.hpp"
#include "grandleon/campaign_runtime/campaign_runtime.hpp"
#include "grandleon/client/campaign_session.hpp"
#include "grandleon/core/content_identity.hpp"
#include "grandleon/game_content/compiler.hpp"
#include "grandleon/game_content/source_project.hpp"
#include "grandleon/package_format/package.hpp"
#include "grandleon/package_runtime/campaign.hpp"
#include "grandleon/package_runtime/dialogue.hpp"
#include "grandleon/simulation/encounter.hpp"
#include "grandleon/storage/memory_storage.hpp"
#include "grandleon/tactics/policy.hpp"

namespace sim = grandleon::simulation;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace campaign = grandleon::campaign;
namespace cr = grandleon::campaign_runtime;
namespace client = grandleon::client;
namespace storage = grandleon::storage;
namespace core = grandleon::core;
namespace gc = grandleon::game_content;

namespace {

constexpr std::size_t io_capacity = 64u * 1024u;

alignas(8) std::uint8_t io_buffer[io_capacity];

// The content compiler's own buffer, and the one place this file departs from
// "one static scratch buffer".
//
// It is a second buffer rather than a bigger first one because the two hold
// different classes of thing. The scratch buffer above carries one encounter,
// one command, one record, payloads whose size the wire format bounds. The
// module-level slot device publishes its budget as `io_capacity` less a
// header, so widening it would quietly relax a refusal that has nothing to do
// with compiling. What travels here is a whole authored project: the shipped
// Tarnholt source is 87,287 bytes on its own, so it does not fit through the
// scratch buffer at all and never will.
//
// Both are uninitialised statics, so neither costs the module a byte of file
// size; the cost is linear memory, which the module reserves up front.
constexpr std::size_t content_capacity = 1024u * 1024u;

alignas(8) std::uint8_t content_buffer[content_capacity];

// Cursor over the scratch buffer. Every read is bounds-checked; a read past the
// end yields zero and latches an overflow flag, so a truncated or hostile
// payload produces a rejected command rather than out-of-bounds access.
class Reader final {
public:
    explicit Reader(std::size_t size) noexcept : size_{size} {}

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

    // How many bytes the payload declared. Read by the count checks, which
    // refuse a declared record count the payload could not possibly hold before
    // anything is reserved for it.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    // Whether every declared byte has been read. A payload that ends exactly
    // here is one written before the tail after this point existed, which is
    // how an optional tail is told from a truncated one.
    [[nodiscard]] bool finished() const noexcept {
        return !overflowed_ && cursor_ >= size_;
    }

    [[nodiscard]] std::uint8_t u8() noexcept {
        if (cursor_ + 1u > size_) {
            overflowed_ = true;
            return 0u;
        }
        return io_buffer[cursor_++];
    }

    [[nodiscard]] std::uint16_t u16() noexcept {
        const std::uint16_t low = u8();
        const std::uint16_t high = u8();
        return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8));
    }

    [[nodiscard]] std::uint32_t u32() noexcept {
        const std::uint32_t low = u16();
        const std::uint32_t high = u16();
        return low | (high << 16);
    }

    [[nodiscard]] std::uint64_t u64() noexcept {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    [[nodiscard]] std::int16_t i16() noexcept {
        return static_cast<std::int16_t>(u16());
    }

    // Copies a raw run of bytes out of the buffer. A short buffer latches the
    // overflow flag and copies nothing, exactly like the scalar reads.
    [[nodiscard]] bool copy(std::uint8_t* destination, std::size_t count) noexcept {
        if (cursor_ + count > size_) {
            overflowed_ = true;
            return false;
        }
        std::memcpy(destination, io_buffer + cursor_, count);
        cursor_ += count;
        return true;
    }

private:
    std::size_t size_{};
    std::size_t cursor_{};
    bool overflowed_{false};
};

// Cursor writing back into the same scratch buffer. Writes past capacity are
// dropped and latch an overflow flag; callers report the overflow instead of
// returning a truncated payload that the reader would silently accept.
class Writer final {
public:
    // Defaulted onto the scratch buffer, because that is what every entry
    // point but the compiler writes into. The compiler's answer is a whole
    // package and goes into the buffer sized for one.
    explicit Writer(
        std::uint8_t* buffer = io_buffer, std::size_t capacity = io_capacity
    ) noexcept
        : buffer_{buffer}, capacity_{capacity} {}

    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }
    [[nodiscard]] std::uint32_t size() const noexcept {
        return static_cast<std::uint32_t>(cursor_);
    }

    void u8(std::uint8_t value) noexcept {
        if (cursor_ + 1u > capacity_) {
            overflowed_ = true;
            return;
        }
        buffer_[cursor_++] = value;
    }

    void u16(std::uint16_t value) noexcept {
        u8(static_cast<std::uint8_t>(value & 0xffu));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    }

    void u32(std::uint32_t value) noexcept {
        u16(static_cast<std::uint16_t>(value & 0xffffu));
        u16(static_cast<std::uint16_t>((value >> 16) & 0xffffu));
    }

    void u64(std::uint64_t value) noexcept {
        u32(static_cast<std::uint32_t>(value & 0xffffffffu));
        u32(static_cast<std::uint32_t>((value >> 32) & 0xffffffffu));
    }

    void i16(std::int16_t value) noexcept {
        u16(static_cast<std::uint16_t>(value));
    }

    void bytes(std::string_view value) noexcept {
        for (const char character : value) {
            u8(static_cast<std::uint8_t>(character));
        }
    }

    // A length-prefixed string, the way every record the compiler writes
    // carries one, so the reader on the other side has one shape to read.
    void string(std::string_view value) noexcept {
        u16(static_cast<std::uint16_t>(value.size()));
        bytes(value);
    }

    // Rewrites one byte already written. A reply whose first bytes report
    // something that is only allowed to happen once the rest of the reply is
    // known to fit reserves a place for it and fills it in here.
    void patch(std::size_t offset, std::uint8_t value) noexcept {
        if (offset >= cursor_) return;
        buffer_[offset] = value;
    }

    // A raw run, for a payload the reader already knows the length of.
    void raw(const std::uint8_t* data, std::size_t count) noexcept {
        if (cursor_ + count > capacity_) {
            overflowed_ = true;
            return;
        }
        if (count != 0u) std::memcpy(buffer_ + cursor_, data, count);
        cursor_ += count;
    }

private:
    std::uint8_t* buffer_{io_buffer};
    std::size_t capacity_{io_capacity};
    std::size_t cursor_{};
    bool overflowed_{false};
};

// Handle registry. Index 0 is never issued so that 0 is an unambiguous failure
// value on every entry point that returns a handle.
std::vector<sim::Encounter*>& registry() {
    static std::vector<sim::Encounter*> instances;
    return instances;
}

[[nodiscard]] sim::Encounter* resolve(std::uint32_t handle) noexcept {
    if (handle == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(handle) - 1u;
    auto& instances = registry();
    if (index >= instances.size()) return nullptr;
    return instances[index];
}

// Where each encounter's events are being collected, parallel to the registry
// and null for every encounter nobody asked to record.
//
// A campaign has to be told what a battle did, and what a battle did is its
// events: `derive_battle_progression` reads the defeat, item-used and
// item-dropped events and derives every campaign consequence from them. The
// encounter itself keeps no log: it answers each command with what that command
// emitted and forgets it, so somebody has to hold the sequence. Holding it here
// rather than in JavaScript is what keeps the whole derivation on this side of
// the boundary.
std::vector<std::vector<sim::Event>*>& event_logs() {
    static std::vector<std::vector<sim::Event>*> logs;
    return logs;
}

// How many events one battle's log may hold.
//
// This is the only structure on this side that grows with how long somebody
// plays rather than with how large their project is, and it is cleared only
// when a new board starts. The module runs on a fixed 16 MB heap with
// `-fno-exceptions`, so the allocation that cannot be served is not an error
// anybody catches: it traps. The module survives that and goes on answering,
// but the log is still there, so every attempt to carry on re-traps: the
// battle is unplayable and nothing said so.
//
// A battle that emits this many events is not a battle; it is a board nobody
// can win being poked at. The shipped campaign finishes in a few hundred.
// So this is a runaway detector, and what it buys is a refusal by name with
// the session still alive.
constexpr std::size_t battle_event_capacity = 65'536u;

[[nodiscard]] std::uint32_t store(sim::Encounter* encounter) {
    auto& instances = registry();
    auto& logs = event_logs();
    for (std::size_t index = 0; index < instances.size(); ++index) {
        if (instances[index] == nullptr) {
            instances[index] = encounter;
            logs[index] = nullptr;
            return static_cast<std::uint32_t>(index + 1u);
        }
    }
    instances.push_back(encounter);
    logs.push_back(nullptr);
    return static_cast<std::uint32_t>(instances.size());
}

// Starts recording an encounter's events into a caller-owned vector. The vector
// must outlive the encounter; the campaign session that owns both is what
// guarantees that.
void record_events(std::uint32_t handle, std::vector<sim::Event>* log) {
    auto& logs = event_logs();
    const std::size_t index = static_cast<std::size_t>(handle) - 1u;
    if (handle == 0u || index >= logs.size()) return;
    logs[index] = log;
}

[[nodiscard]] std::vector<sim::Event>* log_of(std::uint32_t handle) noexcept {
    auto& logs = event_logs();
    const std::size_t index = static_cast<std::size_t>(handle) - 1u;
    if (handle == 0u || index >= logs.size()) return nullptr;
    return logs[index];
}

// A campaign instance binds package_runtime's cursor to the synthetic package
// its dialogue records live in, so gl_campaign_dialogue decodes exactly the
// bytes gl_campaign_add_dialogue attached. The section order is fixed at
// creation: campaigns, encounters, dialogue.
struct CampaignInstance final {
    pf::LoadedPackage package;
    pr::CampaignCursor cursor;
};

constexpr std::size_t campaign_dialogue_section = 2u;

// Total bytes a campaign instance may hold across its record payloads. Each
// call is already bounded by the scratch buffer; this bounds how much a
// caller can accumulate one dialogue at a time.
constexpr std::size_t campaign_bytes_capacity = 4u * 1024u * 1024u;

std::vector<CampaignInstance*>& campaign_registry() {
    static std::vector<CampaignInstance*> instances;
    return instances;
}

[[nodiscard]] CampaignInstance* resolve_campaign(std::uint32_t handle) noexcept {
    if (handle == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(handle) - 1u;
    auto& instances = campaign_registry();
    if (index >= instances.size()) return nullptr;
    return instances[index];
}

[[nodiscard]] std::uint32_t store_campaign(CampaignInstance* instance) {
    auto& instances = campaign_registry();
    for (std::size_t index = 0; index < instances.size(); ++index) {
        if (instances[index] == nullptr) {
            instances[index] = instance;
            return static_cast<std::uint32_t>(index + 1u);
        }
    }
    instances.push_back(instance);
    return static_cast<std::uint32_t>(instances.size());
}

// ---------------------------------------------------------------------------
// The campaign session
// ---------------------------------------------------------------------------

// Boards a caller handed over, rather than boards read out of a package.
//
// `client::CampaignBoards` exists for exactly this caller. The editor plays
// content that has never been compiled, because an author presses Play on a
// project with unsaved edits in it, so there is no package to load an
// encounter from and there never will be one. What there is, is the board
// itself, which the editor already builds to start a bare encounter. So it
// sends that, and every campaign rule above it runs unchanged.
class WireBoards final : public client::CampaignBoards {
public:
    void add(std::uint64_t encounter_id, pr::EncounterLoadResult board) {
        for (Entry& entry : boards_) {
            if (entry.encounter_id == encounter_id) {
                entry.board = std::move(board);
                return;
            }
        }
        boards_.push_back({encounter_id, std::move(board)});
    }

    [[nodiscard]] pr::EncounterLoadResult board(
        std::uint64_t encounter_id
    ) const override {
        for (const Entry& entry : boards_) {
            if (entry.encounter_id == encounter_id) return entry.board;
        }
        pr::EncounterLoadResult missing;
        missing.error = pr::EncounterLoadError::missing_record;
        return missing;
    }

private:
    struct Entry final {
        std::uint64_t encounter_id{};
        pr::EncounterLoadResult board;
    };

    std::vector<Entry> boards_;
};

// How many bytes of a slot the scratch buffer can carry in one piece.
//
// `gl_storage_read` writes a two-byte status ahead of a four-byte length, so
// this is the whole of what is left. It is the device's budget rather than only
// a check at the boundary, so that a campaign too large to mirror is refused by
// `campaign::save_campaign` as `too_large` when it is written, rather than
// saved into a slot nothing outside this module could ever read back.
constexpr std::size_t browser_slot_capacity = io_capacity - 6u;

// The one device every campaign session in this module writes to.
//
// It is module-level rather than per-session on purpose: that is what makes a
// save outlive the session that wrote it, so an author can leave Play, come
// back, and resume the campaign they were in the middle of. `MemorySlotStorage`
// is a tab's memory and nothing more, and `platform/storage/README.md` names a
// browser before persistence is granted as exactly the caller it is for.
//
// A tab's memory is still all it is. What makes a playtest outlive a page is
// the caller mirroring these bytes through `gl_storage_read` and putting them
// back through `gl_storage_write` before a session begins. The slot device
// section at the bottom of this file says why the mirroring is out there rather
// than in here.
storage::MemorySlotStorage& browser_slots() {
    static storage::MemorySlotStorage device{storage::StorageBudget{
        browser_slot_capacity, 4u * 1024u * 1024u, 64u
    }};
    return device;
}

// A campaign session and the synthetic package its content lives in.
//
// The package is synthetic in the same sense the campaign cursor's already is:
// records the caller encoded exactly as tools/game_content encodes them, laid
// out in the sections the decoders binary-search. Two sections are carried, and
// both are read by code that would otherwise have nothing to read: campaigns,
// for the authored flow, and unit types, for the growth block
// `derive_battle_progression` rolls against.
struct SessionInstance final {
    SessionInstance(std::uint64_t campaign, const client::CampaignSessionOptions& options)
        : campaign_id{campaign},
          session{package, campaign, boards, browser_slots(), options} {}

    pf::LoadedPackage package;
    WireBoards boards;
    std::uint64_t campaign_id{};
    client::CampaignSession session;
    // The battle in progress, as an ordinary encounter handle: every existing
    // gl_sim_* entry point drives it, which is why a campaign battle and a bare
    // one are the same battle to everything below this line.
    std::uint32_t battle{0};
    std::vector<sim::Event> events;
};

// The section order a session's synthetic package is built in: campaigns,
// encounters, unit types. Only the last is written to after creation.
constexpr std::size_t session_unit_type_section = 2u;

std::vector<SessionInstance*>& session_registry() {
    static std::vector<SessionInstance*> instances;
    return instances;
}

[[nodiscard]] SessionInstance* resolve_session(std::uint32_t handle) noexcept {
    if (handle == 0u) return nullptr;
    const std::size_t index = static_cast<std::size_t>(handle) - 1u;
    auto& instances = session_registry();
    if (index >= instances.size()) return nullptr;
    return instances[index];
}

[[nodiscard]] std::uint32_t store_session(SessionInstance* instance) {
    auto& instances = session_registry();
    for (std::size_t index = 0; index < instances.size(); ++index) {
        if (instances[index] == nullptr) {
            instances[index] = instance;
            return static_cast<std::uint32_t>(index + 1u);
        }
    }
    instances.push_back(instance);
    return static_cast<std::uint32_t>(instances.size());
}

// A list of inventory stacks: u32 count, then u64 item identity and u32
// quantity each, ascending by identity as the campaign keeps them. One shape
// for both owners a campaign has, because they are the same shape and a reader
// that could tell them apart by their encoding would be a reader that had to.
void write_stacks(
    Writer& writer,
    const std::vector<campaign::InventoryStack>& stacks
) {
    writer.u32(static_cast<std::uint32_t>(stacks.size()));
    for (const campaign::InventoryStack& stack : stacks) {
        writer.u64(stack.item.stable_id);
        writer.u32(stack.quantity);
    }
}

// One roster member, in the layout every session entry point that reports a
// roster shares: u64 member, u64 authored member key, the authored name, u64
// unit type, u8 availability, u16 level, u32 experience, a u16 per growable
// stat, and then what the campaign holds for them. Written in one place so the
// state call, the story call and the commit call cannot come to disagree about
// what a member is.
//
// The kit is on the member rather than derived from a unit type on the far
// side, because that is where it lives: what a campaign character takes onto
// the next board is their own kit, and a front end that read a type's authored
// list would be showing a satchel nobody is going to carry.
void write_roster(Writer& writer, const std::vector<client::RosterEntry>& roster) {
    writer.u32(static_cast<std::uint32_t>(roster.size()));
    for (const client::RosterEntry& entry : roster) {
        writer.u64(entry.member.value);
        writer.u64(entry.placement_source_key);
        writer.string(entry.name);
        writer.u64(entry.unit_type.stable_id);
        writer.u8(static_cast<std::uint8_t>(entry.availability));
        writer.u16(entry.progression.level);
        writer.u32(entry.progression.experience);
        for (const std::uint16_t gained : entry.progression.gained) {
            writer.u16(gained);
        }
        write_stacks(writer, entry.carried);
    }
}

// Status codes for entry points whose failure is a boundary failure rather than
// a simulation-level error. Kept distinct from CreateError and CommandError so
// that a malformed payload can never be mistaken for a game rule outcome.
enum class AbiStatus : std::uint8_t {
    ok = 0,
    malformed_payload = 1,
    unknown_handle = 2,
    buffer_overflow = 3,
    event_log_full = 4,
};

// What `gl_content_compile` did. Kept apart from `AbiStatus` for the reason
// that vocabulary is kept apart from `CreateError`: a boundary condition and a
// refusal by the compiler are different kinds of answer and must not be
// mistakable for one another. The two refusals are distinct because they come
// from different stages with different vocabularies: a project the JSON
// parser would not read at all, and a project it read and the compiler would
// not accept.
enum class ContentStatus : std::uint8_t {
    compiled = 0,
    source_rejected = 1,
    content_rejected = 2,
    source_too_large = 3,
};

// The fixed part of one unit record, written as the sum of what the parse loop
// below reads rather than as a number beside a list of what it reads. A field
// appended to the record and not to this sum is the drift the whole fixed-width
// discipline exists to prevent, and the two cannot be told apart by reading.
//
// Ability and weapon identifiers add 8 bytes each on top, and a carried item
// adds 10: 8 for its identity and 2 for how many of it the unit brings.
// tests/abi holds the total to the parse loop with a zero-slack payload.
constexpr std::uint32_t unit_record_size =
    8u +   // id
    8u +   // unit type
    1u +   // side
    14u +  // x, y, health, strength, power, defense and resistance as i16
    8u +   // skill, luck, evasion and magic as i16
    6u +   // movement, action points, speed, the after-attack flag, and both
           // reach bounds, as u8
    4u +   // ability count
    4u +   // carried weapon count
    1u +   // the terrain it crosses
    1u +   // how often what it holds lands
    4u +   // carried item count
    9u +   // what it leaves behind when it falls, and how often
    1u +   // what it adds to the reach of whatever it is holding
    8u +   // what talking to it records
    5u;    // the round it arrives, the rounds between, and how many it makes

// Writes a tile list into the scratch buffer in the layout the two reachability
// queries share: u8 status, u32 count, then i16 x and i16 y per tile, in the
// engine's own row-major order. Returns bytes written, or 0 when the list does
// not fit, which the caller reports as a boundary failure rather than as an
// empty answer.
// Reads one encounter definition off the scratch buffer, in the wire order
// `platform/web/README.md` documents. False is a malformed payload and the
// definition must not be used; the caller writes the status bytes, because a
// board read for a campaign session and a board read for a bare encounter
// report a refusal in different vocabularies.
//
// Factored out rather than copied: the campaign session reads exactly this
// board, and two parsers of one wire format is the drift this whole surface
// exists to prevent.
[[nodiscard]] bool read_encounter_definition(
    Reader& reader,
    sim::EncounterDefinition& definition
) {
    definition.width = reader.u16();
    definition.height = reader.u16();
    const std::uint32_t declared = reader.u32();

    // Reject a declared count the payload cannot possibly contain before
    // reserving anything, so a bad length cannot drive a large allocation.
    if (reader.overflowed() || declared > (io_capacity / unit_record_size) ||
        (declared * unit_record_size) + 8u > reader.size()) {
        return false;
    }

    definition.units.reserve(declared);
    for (std::uint32_t index = 0; index < declared; ++index) {
        sim::UnitDefinition unit{};
        unit.id = reader.u64();
        unit.unit_type_id = reader.u64();
        unit.side = reader.u8() == 0u ? sim::Side::first : sim::Side::second;
        unit.position.x = reader.i16();
        unit.position.y = reader.i16();
        unit.health = reader.i16();
        unit.strength = reader.i16();
        unit.power = reader.i16();
        unit.defense = reader.i16();
        unit.resistance = reader.i16();
        unit.skill = reader.i16();
        unit.luck = reader.i16();
        unit.evasion = reader.i16();
        unit.magic = reader.i16();
        unit.movement = reader.u8();
        unit.action_points = reader.u8();
        unit.speed = reader.u8();
        unit.acts_after_attacking = reader.u8() != 0u;
        unit.minimum_reach = reader.u8();
        unit.maximum_reach = reader.u8();
        const std::uint32_t ability_count = reader.u32();
        if (reader.overflowed() || ability_count > (io_capacity / 8u)) {
            return false;
        }
        unit.ability_ids.reserve(ability_count);
        for (std::uint32_t ability = 0; ability < ability_count; ++ability) {
            unit.ability_ids.push_back(reader.u64());
        }
        const std::uint32_t carried_count = reader.u32();
        if (reader.overflowed() || carried_count > (io_capacity / 8u)) {
            return false;
        }
        unit.weapon_ids.reserve(carried_count);
        for (std::uint32_t carried = 0; carried < carried_count; ++carried) {
            unit.weapon_ids.push_back(reader.u64());
        }
        unit.crossings = reader.u8();
        unit.accuracy = reader.u8();
        // The pack, appended after everything a unit record already carried.
        // A count of zero is a unit that brings nothing, which is what every
        // caller written before items could be spent sends.
        const std::uint32_t pack_count = reader.u32();
        if (reader.overflowed() || pack_count > (io_capacity / 10u)) {
            return false;
        }
        unit.item_ids.reserve(pack_count);
        unit.item_counts.reserve(pack_count);
        for (std::uint32_t packed = 0; packed < pack_count; ++packed) {
            unit.item_ids.push_back(reader.u64());
            unit.item_counts.push_back(reader.u16());
        }
        // What the unit leaves behind when it falls, appended after the pack.
        // Zero and zero is a unit that leaves nothing; the engine refuses the
        // halves, so nothing here has to.
        unit.drop_item_id = reader.u64();
        unit.drop_chance = reader.u8();
        // What this unit adds to the reach of whatever it is holding, appended
        // after the drop. Zero is a unit whose reach is exactly its weapon's,
        // which is every unit a caller written before specificities existed
        // sends. This file's own convention for a tail is to write the zero
        // rather than leave it off, so that a caller with no bonus to give and
        // a caller whose payload was truncated are not the same bytes.
        unit.reach_bonus = reader.u8();
        // What talking to this character records, appended after the bonus on
        // the same terms: zero is somebody no talk may reach, which is every
        // character a caller written before the gesture existed sends, and the
        // zero is written rather than left off so that a caller with nobody to
        // talk to and a caller whose payload was truncated are not the same
        // bytes.
        unit.talk_record_id = reader.u64();
        // And when this character comes in, appended after the talk record on
        // exactly the same terms: the round of its first arrival, the rounds
        // between arrivals and how many it makes. Three zeroes is a character
        // standing on the board from the opening, which is every character a
        // caller written before waves sends, and the zeroes are written rather
        // than left off for the reason the ones above are.
        //
        // Narrower on the wire than in the engine, and deliberately: these
        // three carry what an *author* wrote, and the source contract bounds a
        // round at 4095, a gap at 255 and a count of arrivals at 64. The wider
        // engine fields hold the rounds the recurrence *expands* to, which are
        // computed on the far side of this boundary and never cross it.
        unit.arrival_round = reader.u16();
        unit.arrival_every = reader.u16();
        unit.arrival_times = reader.u8();
        definition.units.push_back(unit);
    }

    const std::uint32_t ability_count = reader.u32();
    if (reader.overflowed() || ability_count > (io_capacity / 16u)) {
        return false;
    }
    for (std::uint32_t index = 0; index < ability_count; ++index) {
        sim::AbilityDefinition ability;
        ability.id = reader.u64();
        ability.kind = static_cast<sim::AbilityKind>(reader.u8());
        ability.damage_type = static_cast<sim::DamageType>(reader.u8());
        ability.area = static_cast<sim::AreaShape>(reader.u8());
        ability.power = reader.i16();
        ability.minimum_reach = reader.u8();
        ability.maximum_reach = reader.u8();
        ability.radius = reader.u8();
        ability.accuracy = reader.u8();
        definition.abilities.push_back(ability);
    }

    const std::uint32_t weapon_count = reader.u32();
    if (reader.overflowed() || weapon_count > (io_capacity / 12u)) {
        return false;
    }
    for (std::uint32_t index = 0; index < weapon_count; ++index) {
        sim::WeaponDefinition weapon;
        weapon.id = reader.u64();
        weapon.power = reader.i16();
        weapon.minimum_reach = reader.u8();
        weapon.maximum_reach = reader.u8();
        weapon.accuracy = reader.u8();
        weapon.weapon_type = reader.u64();
        definition.weapons.push_back(weapon);
    }

    // Which kinds of weapon beat which, and what beating them is worth. A
    // board with no triangle in it writes a count of zero, which is what every
    // board the browser has ever handed this side wrote before there was one.
    const std::uint32_t kind_count = reader.u32();
    if (reader.overflowed() || kind_count > (io_capacity / 12u)) {
        return false;
    }
    for (std::uint32_t index = 0; index < kind_count; ++index) {
        sim::WeaponTypeDefinition kind;
        kind.id = reader.u64();
        const std::uint32_t beaten = reader.u32();
        if (reader.overflowed() || beaten > (io_capacity / 8u)) return false;
        kind.strong_against.reserve(beaten);
        for (std::uint32_t edge = 0; edge < beaten; ++edge) {
            kind.strong_against.push_back(reader.u64());
        }
        kind.damage = reader.i16();
        kind.accuracy = reader.u8();
        definition.weapon_types.push_back(kind);
    }

    const std::uint32_t item_count = reader.u32();
    if (reader.overflowed() || item_count > (io_capacity / 11u)) {
        return false;
    }
    for (std::uint32_t index = 0; index < item_count; ++index) {
        sim::ItemDefinition item;
        item.id = reader.u64();
        item.kind = static_cast<sim::ItemKind>(reader.u8());
        item.power = reader.i16();
        definition.items.push_back(item);
    }

    const std::uint32_t objective_count = reader.u32();
    if (reader.overflowed() || objective_count > (io_capacity / 22u)) {
        return false;
    }
    for (std::uint32_t index = 0; index < objective_count; ++index) {
        sim::ObjectiveDefinition objective;
        objective.id = reader.u64();
        objective.kind = static_cast<sim::ObjectiveKind>(reader.u8());
        objective.side =
            reader.u8() == 0u ? sim::Side::first : sim::Side::second;
        objective.target_unit_id = reader.u64();
        // How many rounds a survive-rounds objective is about, appended. Zero
        // for every other kind, which is what a caller written before that kind
        // existed sends; the engine refuses the halves, so nothing here has to.
        objective.round_count = reader.u32();
        definition.objectives.push_back(objective);
    }

    const std::uint8_t order = reader.u8();
    if (reader.overflowed() || order > 2u) {
        return false;
    }
    definition.turn_order = static_cast<sim::TurnOrder>(order);

    // The board's passability, one byte per cell. Zero cells is an all-open
    // board, which is what a caller with no terrain to give sends and what the
    // engine reads it as.
    const std::uint32_t terrain_count = reader.u32();
    if (reader.overflowed() || terrain_count > io_capacity) {
        return false;
    }
    definition.terrain.reserve(terrain_count);
    for (std::uint32_t index = 0; index < terrain_count; ++index) {
        definition.terrain.push_back(static_cast<sim::Terrain>(reader.u8()));
    }

    // And the board's price, counted the same way. Zero cells is a board where
    // every step costs one, which is what a caller with no price to give sends.
    // A cell charging nothing is not refused here: the engine refuses it by
    // name as an invalid map, and answering the same refusal in two places
    // would let them disagree.
    const std::uint32_t cost_count = reader.u32();
    if (reader.overflowed() || cost_count > io_capacity) {
        return false;
    }
    definition.movement_cost.reserve(cost_count);
    for (std::uint32_t index = 0; index < cost_count; ++index) {
        definition.movement_cost.push_back(reader.u8());
    }

    // The deployment region, counted rather than optional. A count of zero is
    // an encounter with no phase, which is what every caller with nothing to
    // arrange sends. It has to be *written* rather than left off, because
    // a campaign session appends its own placement table after this record and
    // an absent tail and a present one would be indistinguishable there.
    const std::uint32_t zone_count = reader.u32();
    if (reader.overflowed() || zone_count > (io_capacity / 4u)) {
        return false;
    }
    definition.deployment_tiles.reserve(zone_count);
    for (std::uint32_t index = 0; index < zone_count; ++index) {
        const std::int16_t x = reader.i16();
        const std::int16_t y = reader.i16();
        definition.deployment_tiles.push_back({x, y});
    }

    return !reader.overflowed();
}

std::uint32_t write_tiles(const std::vector<sim::Position>& tiles) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u32(static_cast<std::uint32_t>(tiles.size()));
    for (const sim::Position& tile : tiles) {
        writer.i16(tile.x);
        writer.i16(tile.y);
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// One aim, out of three flat arguments and into the engine's own record.
//
// The three travel as separate arguments rather than through the scratch
// buffer because a client holds exactly these three values between choosing a
// menu row and pressing confirm, and packing them into a payload would make a
// caller write a record to ask a question about state it is already holding.
// A `kind` outside the enum is a boundary failure rather than a gesture the
// engine judges: a caller that sent a fifth number never had an aim to ask
// about, and answering it "unavailable" would look like a rule.
[[nodiscard]] bool read_gesture(
    std::uint32_t kind,
    std::uint64_t weapon_id,
    std::uint64_t ability_id,
    sim::AimedGesture& gesture
) noexcept {
    if (kind > static_cast<std::uint32_t>(sim::Gesture::talk)) return false;
    gesture.kind = static_cast<sim::Gesture>(kind);
    gesture.weapon_id = weapon_id;
    gesture.ability_id = ability_id;
    return true;
}

// A length-prefixed slot name, out of the payload and into a string.
//
// The name is copied before anything is written back, because the reader and
// the writer share one buffer: a response written over a name still being read
// is the one way these two cursors can lie to each other.
[[nodiscard]] bool read_slot_name(Reader& reader, std::string& name) {
    const std::uint16_t size = reader.u16();
    if (reader.overflowed()) return false;
    std::vector<std::uint8_t> bytes(size);
    if (size != 0u && !reader.copy(bytes.data(), size)) return false;
    name.assign(bytes.begin(), bytes.end());
    return true;
}

}  // namespace

extern "C" {

// Pointer to the shared scratch buffer in linear memory. uintptr_t is u32 on
// wasm32, so the export's signature is unchanged there; on a 64-bit host the
// native ABI test gets the full pointer back.
EMSCRIPTEN_KEEPALIVE std::uintptr_t gl_sim_io_buffer(void) {
    return reinterpret_cast<std::uintptr_t>(io_buffer);
}

// Capacity of the shared scratch buffer in bytes.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_io_capacity(void) {
    return static_cast<std::uint32_t>(io_capacity);
}

// Reads an encounter definition from the scratch buffer and creates an
// encounter. Returns a handle, or 0 on failure. The buffer is rewritten with a
// single status byte followed by a CreateError byte.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_create(std::uint32_t payload_size) {
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    sim::EncounterDefinition definition{};
    if (!read_encounter_definition(reader, definition)) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        writer.u8(static_cast<std::uint8_t>(sim::CreateError::none));
        return 0u;
    }

    auto created = sim::create_encounter(definition);
    Writer writer;
    if (!created) {
        writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
        writer.u8(static_cast<std::uint8_t>(created.error));
        return 0u;
    }

    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(sim::CreateError::none));
    return store(new sim::Encounter(created.encounter));
}

// Releases an encounter handle. Releasing an unknown handle is a no-op.
EMSCRIPTEN_KEEPALIVE void gl_sim_destroy(std::uint32_t handle) {
    sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) return;
    registry()[static_cast<std::size_t>(handle) - 1u] = nullptr;
    event_logs()[static_cast<std::size_t>(handle) - 1u] = nullptr;
    delete encounter;
}

// Reads a command from the scratch buffer, applies it, and writes the command
// result back. Returns the number of bytes written, or 0 on a boundary failure
// whose status byte is then the only meaningful content of the buffer.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_apply(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }

    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    sim::Command command{};
    const std::uint8_t type = reader.u8();
    command.unit_id = reader.u64();
    command.destination.x = reader.i16();
    command.destination.y = reader.i16();
    command.target_id = reader.u64();
    command.ability_id = reader.u64();
    command.weapon_id = reader.u64();
    command.item_id = reader.u64();
    if (reader.overflowed()) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }
    // An unrecognised discriminator is passed through rather than rejected
    // here: the engine checks the acting unit, its health, and its side before
    // it looks at the command type, and that precedence is a rule the boundary
    // must not pre-empt. CommandType has a fixed underlying type, so a value
    // outside the enumerators is representable and the engine's fall-through
    // reports invalid_command.
    command.type = static_cast<sim::CommandType>(type);

    // A battle whose log is full is refused *before* the command is applied,
    // so that what the engine holds and what the commit will be told about it
    // can never come apart. The alternative is applying the command and
    // dropping its events, which is a campaign deriving its consequences from
    // a battle that did not happen.
    std::vector<sim::Event>* const log = log_of(handle);
    if (log != nullptr && log->size() >= battle_event_capacity) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::event_log_full));
        return 0u;
    }

    const sim::CommandResult result = encounter->apply(command);

    // A battle a campaign session is watching keeps its events, in the order
    // the engine emitted them. Every other encounter records nothing and costs
    // nothing.
    if (log != nullptr) {
        log->insert(log->end(), result.events.begin(), result.events.end());
    }

    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(result.error));
    writer.u32(static_cast<std::uint32_t>(result.events.size()));
    for (const sim::Event& event : result.events) {
        writer.u8(static_cast<std::uint8_t>(event.type));
        writer.u64(event.unit_id);
        writer.u64(event.related_unit_id);
        writer.i16(event.position.x);
        writer.i16(event.position.y);
        writer.i16(event.amount);
        writer.u8(static_cast<std::uint8_t>(event.outcome));
        // The definition the event is about, appended last. Zero for every
        // event that names none, which is every event but a spent item.
        writer.u64(event.content_id);
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Writes a full encounter snapshot into the scratch buffer and returns the
// number of bytes written, or 0 on a boundary failure.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_snapshot(std::uint32_t handle) {
    sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }

    const sim::EncounterSnapshot snapshot = encounter->snapshot();
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u16(snapshot.width);
    writer.u16(snapshot.height);
    writer.u8(static_cast<std::uint8_t>(snapshot.active_side));
    writer.u8(static_cast<std::uint8_t>(snapshot.outcome));
    writer.u64(snapshot.active_unit_id);
    writer.u8(snapshot.remaining_action_points);
    writer.u32(snapshot.round);
    writer.u64(snapshot.activation_count);
    writer.u32(static_cast<std::uint32_t>(snapshot.units.size()));
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        writer.u64(unit.id);
        writer.u64(unit.unit_type_id);
        writer.u8(static_cast<std::uint8_t>(unit.side));
        writer.i16(unit.position.x);
        writer.i16(unit.position.y);
        writer.i16(unit.health);
        writer.i16(unit.maximum_health);
        writer.i16(unit.strength);
        writer.i16(unit.power);
        writer.i16(unit.defense);
        writer.i16(unit.resistance);
        writer.i16(unit.skill);
        writer.i16(unit.luck);
        writer.i16(unit.evasion);
        writer.i16(unit.magic);
        writer.u8(unit.movement);
        writer.u8(unit.action_points);
        writer.u8(unit.speed);
        writer.u8(unit.acts_after_attacking ? 1u : 0u);
        writer.u8(unit.has_acted ? 1u : 0u);
        writer.u8(unit.minimum_reach);
        writer.u8(unit.maximum_reach);
        writer.u32(static_cast<std::uint32_t>(unit.ability_ids.size()));
        for (const sim::ContentId ability : unit.ability_ids) {
            writer.u64(ability);
        }
        writer.u32(static_cast<std::uint32_t>(unit.weapon_ids.size()));
        for (const sim::ContentId weapon : unit.weapon_ids) {
            writer.u64(weapon);
        }
        // The pack, appended: what the unit carries and how many of each are
        // left. The count changes during a battle, so a client redrawing from
        // a snapshot sees the draught run out.
        writer.u32(static_cast<std::uint32_t>(unit.item_ids.size()));
        for (std::size_t index = 0; index < unit.item_ids.size(); ++index) {
            writer.u64(unit.item_ids[index]);
            writer.u16(unit.item_counts[index]);
        }
        writer.u64(unit.drop_item_id);
        writer.u8(unit.drop_chance);
        // And what this unit adds to the reach of what it holds, appended for
        // the same reason everything before it was: every offset ahead of it
        // stays the offset it already had. A browser sheet needs it because a
        // weapon row's band is the weapon's band widened by this, and the
        // weapon record on the wire is the shared authored one.
        writer.u8(unit.reach_bonus);
        // The talk gesture, appended for the same reason: what talking to this
        // character records, and whether they have already been talked off the
        // board. Both are needed to draw the row: the first says a talk could
        // reach them at all, the second that it no longer can. Departure is
        // its own byte rather than a health of zero, because leaving and
        // dying are two different facts and a browser reading one field would
        // have had to guess which it was being told.
        writer.u64(unit.talk_record_id);
        writer.u8(unit.departed ? 1u : 0u);
        // And the wave, appended for the same reason: the round this character
        // comes in on and whether it has. A browser drawing the board needs
        // both: the first to say a wave is coming at all, the second to keep
        // somebody who is not here yet off the tiles. Not-arrived is its
        // own byte rather than a health of zero for the reason departure is:
        // not here yet and fallen are two different facts.
        writer.u32(unit.arrival_round);
        writer.u8(unit.arrived ? 1u : 0u);
        // And whether this character has already spent its turn's one walk,
        // appended for the same reason everything above it was. A browser needs
        // it to grey the move row rather than offer a walk the engine will
        // refuse by name, exactly as `has_acted` above it is what greys a whole
        // character out.
        writer.u8(unit.has_moved ? 1u : 0u);
        // And how much of its budget this character's own turn has spent,
        // appended for the same reason. Under `side_blocks` several characters
        // may be part-way through their turns at once, so the side-wide
        // `remaining_action_points` above says nothing about any of them: this
        // is what a browser sheet subtracts to show what the character it is
        // drawing has left. Zero under the other two orders, where the
        // side-wide count is the whole of the answer.
        writer.u8(unit.spent_action_points);
        // And the engine's own answer to "is this character standing on the
        // board", appended for the same reason everything above it was.
        //
        // The three fields it folds (health, `departed` and `arrived`) are
        // all already on the wire, so a browser could compose it. That is
        // exactly why this byte exists: a predicate restated on a client is a
        // rule that drifts where nothing can notice it, and this is the
        // predicate that decides who holds a tile, who may be aimed at and who
        // may be given an order. Every other client asks `sim::on_board`
        // directly; the browser is across an ABI, so it is asked here and the
        // answer is sent.
        writer.u8(sim::on_board(unit) ? 1u : 0u);
    }
    writer.u32(static_cast<std::uint32_t>(snapshot.objectives.size()));
    for (const sim::ObjectiveResult& objective : snapshot.objectives) {
        writer.u64(objective.id);
        writer.u8(static_cast<std::uint8_t>(objective.state));
    }
    // What has fallen, in the order it fell. Appended after the objectives so
    // every offset before it is the offset it already had.
    writer.u32(static_cast<std::uint32_t>(snapshot.drops.size()));
    for (const sim::DropRecord& drop : snapshot.drops) {
        writer.u64(drop.unit_id);
        writer.u64(drop.claimant_id);
        writer.u64(drop.item_id);
    }
    // The deployment phase, appended after the drops for the same reason: every
    // offset before it is the offset it already had. An encounter with no
    // region writes a marker of zero and a count of zero, which is exactly what
    // a board opening on its first activation has always meant.
    writer.u8(snapshot.deploying ? 1u : 0u);
    writer.u32(static_cast<std::uint32_t>(snapshot.deployment_tiles.size()));
    for (const sim::Position& tile : snapshot.deployment_tiles) {
        writer.i16(tile.x);
        writer.i16(tile.y);
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Prices one attack without committing it. The error byte is exactly the
// refusal gl_sim_apply would return for the same attack in the same state;
// when it is zero, the numbers are what apply would inflict. `weapon_id` names
// which carried weapon to price, zero meaning the weapon in hand.
//
// Payload out: u8 status, u8 command error, i16 damage,
// i16 target health after, u8 lethal, u8 counter, i16 counter damage,
// i16 attacker health after, u8 counter lethal, u8 hit chance,
// u8 counter chance.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_forecast_attack(
    std::uint32_t handle,
    std::uint64_t attacker_id,
    std::uint64_t target_id,
    std::uint64_t weapon_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const sim::AttackForecast forecast = sim::forecast_attack(
        encounter->snapshot(), attacker_id, target_id, encounter->weapons(),
        weapon_id
    );
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(forecast.error));
    writer.i16(forecast.damage);
    writer.i16(forecast.target_health_after);
    writer.u8(forecast.lethal ? 1u : 0u);
    // The counter half, appended: the browser draws the same promise the
    // native clients do, so a player deciding in a tab is told what the strike
    // will cost them as well as what it will take.
    writer.u8(forecast.counter ? 1u : 0u);
    writer.i16(forecast.counter_damage);
    writer.i16(forecast.attacker_health_after);
    writer.u8(forecast.counter_lethal ? 1u : 0u);
    // The two chances, appended last: what the engine will actually roll
    // against for the strike and for the answer. Everything above says what
    // happens when they land, so a browser that draws the numbers without
    // these is drawing a promise the engine stopped making.
    writer.u8(forecast.hit_chance);
    writer.u8(forecast.counter_chance);
    return writer.size();
}

// Prices spending one carried item without committing it, on the same terms:
// the error byte is exactly the refusal gl_sim_apply would return for the same
// use in the same state. `target_id` zero means the acting unit. There is no
// chance byte, because using an item rolls nothing.
//
// Payload out: u8 status, u8 command error, u8 kind, i16 restored,
// i16 target health after, u16 remaining after.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_forecast_item(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint64_t target_id,
    std::uint64_t item_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const sim::ItemForecast forecast = sim::forecast_item(
        encounter->snapshot(), unit_id, target_id, encounter->items(), item_id
    );
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(forecast.error));
    writer.u8(static_cast<std::uint8_t>(forecast.kind));
    writer.i16(forecast.restored);
    writer.i16(forecast.target_health_after);
    writer.u16(forecast.remaining_after);
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Prices one talk without committing it, on the same terms as the two forecasts
// above: the error byte is exactly the refusal gl_sim_apply would return for the
// same talk in the same state. There is no chance byte and no number at all,
// because a talk rolls nothing and clamps nothing. Everything it could have
// gone wrong at is decided by the refusal, and what is left is one character
// leaving and one authored record being reported.
//
// This is what a browser asks before it draws the row. Asking it per neighbour
// is how the row's legality stays the engine's answer rather than a rule the
// client keeps a second copy of.
//
// Payload out: u8 status, u8 command error, u64 departing id, u64 record id.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_forecast_talk(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint64_t target_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const sim::TalkForecast forecast =
        sim::forecast_talk(encounter->snapshot(), unit_id, target_id);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(forecast.error));
    writer.u64(forecast.departing_id);
    writer.u64(forecast.record_id);
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Every tile the unit could occupy after one accepted move command, from the
// engine's own traversal, the same one apply() judges a move against. Exposed
// so a browser client lights reachable tiles without re-implementing the
// movement rule. Read-only: no state and no hash changes.
//
// Payload out: u8 status, u32 tile count, then i16 x, i16 y per tile.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_reachable_tiles(
    std::uint32_t handle,
    std::uint64_t unit_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    return write_tiles(sim::reachable_tiles(encounter->snapshot(), unit_id));
}

// Every tile the named character could be deployed to, from the engine's own
// judgement of the deployment rule, the same one apply() makes. Exposed for
// the reason the reachability query is: a browser client lights the region
// without keeping a second copy of who may stand where. Empty once the phase
// closes, and empty for anybody the content did not make arrangeable.
//
// Payload out: u8 status, u32 tile count, then i16 x, i16 y per tile.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_deployable_tiles(
    std::uint32_t handle,
    std::uint64_t unit_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    return write_tiles(sim::deployable_tiles(encounter->snapshot(), unit_id));
}

// Every tile at least one living unit on `side` could reach and strike:
// movement plus the band of every weapon it carries and every damaging ability
// it knows, honouring minimum reach. `side` is 0 for the first side and any
// other value for the second, matching the encoding gl_sim_create reads.
// Read-only, like the reachability query beside it.
//
// Payload out: u8 status, u32 tile count, then i16 x, i16 y per tile.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_danger_tiles(
    std::uint32_t handle,
    std::uint32_t side
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const sim::Side which = side == 0u ? sim::Side::first : sim::Side::second;
    return write_tiles(
        sim::danger_tiles(
            encounter->snapshot(), which, encounter->weapons(),
            encounter->abilities()
        )
    );
}

// Every tile the unit could aim one gesture at, from the engine's own
// judgement of that gesture: a tile is in the result exactly when the command
// committing the gesture there would be accepted. The aiming counterpart of
// the reachability query above, and exposed for the same reason: a browser
// lights the squares a strike or a cast can reach without keeping a second
// copy of any reach rule beside the engine's.
//
// `kind` is the `Gesture` value: 0 walk, 1 strike, 2 cast, 3 talk. `weapon` is
// read only by a strike, with `0` meaning the weapon in hand exactly as an
// attack command means it, and `ability` only by a cast. Anything else in
// `kind` is a boundary failure rather than an empty answer.
//
// An empty list is not a refusal. A strike with nobody in reach lights no tile
// and is still a gesture this character may make, which is the neighbouring
// export's question rather than this one's, so nothing may be read backwards
// from a count of zero. Read-only: no state and no hash changes.
//
// Payload out: u8 status, u32 tile count, then i16 x, i16 y per tile.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_aimable_tiles(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint32_t kind,
    std::uint64_t weapon_id,
    std::uint64_t ability_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    sim::AimedGesture gesture;
    if (!read_gesture(kind, weapon_id, ability_id, gesture)) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }
    return write_tiles(
        sim::aimable_tiles(
            encounter->snapshot(), unit_id, gesture, encounter->weapons(),
            encounter->abilities()
        )
    );
}

// Whether the unit could make that gesture at all right now, whatever it were
// aimed at. This is what decides whether a menu offers the row, and it is a
// different question from the one above rather than a summary of it: false
// means every command carrying the gesture is refused before the engine looks
// at what it named, while true means the gesture is accepted and only the aim
// is left to judge.
//
// So the two are told two different ways. A menu that dropped its strike row
// because no tile lit would be teaching that rows come and go for reasons the
// board does not show; the aiming highlight is what says "nobody, from here".
// Arguments are read exactly as `gl_sim_aimable_tiles` reads them.
//
// Payload out: u8 status, u8 available.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_gesture_available(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint32_t kind,
    std::uint64_t weapon_id,
    std::uint64_t ability_id
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    sim::AimedGesture gesture;
    if (!read_gesture(kind, weapon_id, ability_id, gesture)) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(
        sim::gesture_available(
            encounter->snapshot(), unit_id, gesture, encounter->weapons(),
            encounter->abilities()
        )
            ? 1u
            : 0u
    );
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Every tile an area cast aimed at one centre would cover: the same membership
// test gl_sim_apply walks the units against, asked of the board instead, so a
// drawn splash and a caught character cannot disagree. Clipped to the board,
// and the centre itself is in it.
//
// Separate from the aiming query because it describes a candidate tile rather
// than a character: it changes every time a cursor moves, where the aiming
// query changes only when the character does, and folding them together would
// make the cheap answer pay for the moving one. Nothing here asks whether the
// cast may be aimed at the centre at all; that is the aiming query's question.
//
// Empty for an ability no registry resolves and empty for a single-tile one,
// whose splash is the tile the cursor is already on. The centre travels as two
// signed arguments rather than through the scratch buffer, for the reason the
// aim above does; one outside the range a board coordinate is carried in is a
// boundary failure, because truncating it would answer about a different tile.
//
// Payload out: u8 status, u32 tile count, then i16 x, i16 y per tile.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_area_tiles(
    std::uint32_t handle,
    std::uint64_t ability_id,
    std::int32_t x,
    std::int32_t y
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    constexpr std::int32_t coordinate_minimum = -32768;
    constexpr std::int32_t coordinate_maximum = 32767;
    if (x < coordinate_minimum || x > coordinate_maximum ||
        y < coordinate_minimum || y > coordinate_maximum) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }
    const sim::Position centre{
        static_cast<std::int16_t>(x), static_cast<std::int16_t>(y)
    };
    return write_tiles(
        sim::area_tiles(
            encounter->snapshot(), ability_id, centre, encounter->abilities()
        )
    );
}

// Returns the canonical 64-bit state hash. An unknown handle returns 0, which a
// live encounter never produces for the FNV-1a construction in use; callers
// still validate the handle before trusting the value.
EMSCRIPTEN_KEEPALIVE std::uint64_t gl_sim_canonical_hash(std::uint32_t handle) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) return 0u;
    return encounter->canonical_hash();
}

// Chooses a command for an unattended unit and writes it back in the same wire
// format gl_sim_apply reads, so the caller can hand the proposal straight to
// the simulation. Behaviour is policy: the engine still validates the result.
//
// Payload in: u64 unit id, u8 behaviour, u16 patrol count, then i16 x/y pairs.
// Payload out: u8 status, u8 actionable, then a command record.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_ai_decide(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    const sim::Encounter* encounter = resolve(handle);
    if (encounter == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const sim::UnitId unit_id = reader.u64();
    const std::uint8_t behavior_code = reader.u8();
    const std::uint32_t patrol_count = reader.u16();
    if (reader.overflowed() || behavior_code > 2u ||
        patrol_count > (io_capacity / 4u)) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }
    std::vector<sim::Position> patrol;
    patrol.reserve(patrol_count);
    for (std::uint32_t index = 0; index < patrol_count; ++index) {
        const std::int16_t x = reader.i16();
        const std::int16_t y = reader.i16();
        patrol.push_back({x, y});
    }
    if (reader.overflowed()) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }

    const auto plan = grandleon::tactics::decide(
        encounter->snapshot(),
        unit_id,
        static_cast<grandleon::tactics::Behavior>(behavior_code),
        patrol,
        encounter->abilities(),
        encounter->weapons()
    );

    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(plan.actionable ? 1u : 0u);
    writer.u8(static_cast<std::uint8_t>(plan.command.type));
    writer.u64(plan.command.unit_id);
    writer.i16(plan.command.destination.x);
    writer.i16(plan.command.destination.y);
    writer.u64(plan.command.target_id);
    writer.u64(plan.command.ability_id);
    writer.u64(plan.command.weapon_id);
    return writer.overflowed() ? 0u : writer.size();
}

// Maps a source key, written into the scratch buffer as UTF-8, to its stable
// content identity. Exposed so that the browser derives the same unit and unit
// type identifiers the content compiler does, rather than inventing its own.
// Those identifiers are part of canonical state, so a second implementation of
// this mapping would silently produce a different canonical hash.
EMSCRIPTEN_KEEPALIVE std::uint64_t gl_core_stable_content_id(
    std::uint32_t length
) {
    if (length > io_capacity) return 0u;
    const std::string_view key(
        reinterpret_cast<const char*>(io_buffer),
        static_cast<std::size_t>(length)
    );
    return grandleon::core::stable_content_id_v1(key);
}

// Writes the engine's own name for a CreateError into the scratch buffer and
// returns its length. Exposing the names keeps the JavaScript error vocabulary
// from drifting away from the C++ enumeration.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_create_error_name(std::uint32_t error) {
    // The last enumerator, so the browser can name every refusal the engine
    // can produce. It had been left at `invalid_item` while
    // `invalid_deployment` was appended past it, which made a board refused
    // for its deployment region nameless on the one client that shows the
    // author their own project.
    if (error >
        static_cast<std::uint32_t>(sim::CreateError::invalid_arrival)) {
        return 0u;
    }
    const std::string_view name =
        sim::error_name(static_cast<sim::CreateError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the engine's own name for a CommandError into the scratch buffer and
// returns its length.
//
// The bound is the last enumerator, and it moves with the enumeration: the
// browser reads names from zero until this answers nothing, so a refusal past
// the bound is one the browser cannot name and quietly reports as something
// else. Every refusal the engine can return has to be inside it.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_sim_command_error_name(std::uint32_t error) {
    if (error >
        static_cast<std::uint32_t>(sim::CommandError::departed_unit)) {
        return 0u;
    }
    const std::string_view name =
        sim::error_name(static_cast<sim::CommandError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Reads a compiled campaign record from the scratch buffer, loads it through
// package_runtime::load_campaign, and creates a campaign cursor. Returns a
// handle, or 0 on failure; the buffer is rewritten with a status byte and a
// CampaignError byte, so a rule-level refusal is the engine's own.
//
// Payload in: u64 campaign id, u32 record size, the campaign record payload
// exactly as tools/game_content encodes it, u16 encounter count, then one u64
// per encounter identity the flow's encounter nodes may reference.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_create(std::uint32_t payload_size) {
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        writer.u8(static_cast<std::uint8_t>(pr::CampaignError::none));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint64_t campaign_id = reader.u64();
    const std::uint32_t record_size = reader.u32();
    if (reader.overflowed() || record_size > io_capacity) return malformed();

    pf::LoadedPackage package;
    package.bytes.resize(record_size);
    if (!reader.copy(package.bytes.data(), record_size)) return malformed();

    const std::uint16_t encounter_count = reader.u16();
    if (reader.overflowed()) return malformed();
    pf::SectionView encounters;
    encounters.type = pf::SectionType::encounters;
    encounters.schema_major = 1u;
    encounters.flags = pf::section_flag_required;
    encounters.records.reserve(encounter_count);
    for (std::uint16_t index = 0; index < encounter_count; ++index) {
        // Existence is all load_campaign checks for an encounter reference,
        // so the synthetic records carry no payload.
        encounters.records.push_back({reader.u64(), 0u, 0u});
    }
    if (reader.overflowed()) return malformed();
    // LoadedPackage::find binary-searches records by stable identity, so a
    // synthetic section must hold the same ordering invariant the container
    // loader establishes.
    std::sort(
        encounters.records.begin(),
        encounters.records.end(),
        [](const pf::RecordView& lhs, const pf::RecordView& rhs) {
            return lhs.stable_id < rhs.stable_id;
        }
    );

    pf::SectionView campaigns;
    campaigns.type = pf::SectionType::campaigns;
    campaigns.schema_major = 1u;
    campaigns.flags = pf::section_flag_required;
    campaigns.records.push_back({campaign_id, 0u, record_size});
    pf::SectionView dialogue;
    dialogue.type = pf::SectionType::dialogue;
    dialogue.schema_major = 1u;
    dialogue.flags = pf::section_flag_required;
    package.sections.push_back(campaigns);
    package.sections.push_back(encounters);
    package.sections.push_back(dialogue);

    auto loaded = pr::load_campaign(package, campaign_id);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(loaded.error));
    if (!loaded) return 0u;
    return store_campaign(new CampaignInstance{
        std::move(package),
        pr::CampaignCursor(std::move(loaded.definition))
    });
}

// Releases a campaign handle. Releasing an unknown handle is a no-op.
EMSCRIPTEN_KEEPALIVE void gl_campaign_destroy(std::uint32_t handle) {
    CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) return;
    campaign_registry()[static_cast<std::size_t>(handle) - 1u] = nullptr;
    delete instance;
}

// Attaches one compiled dialogue record to a campaign instance, so that
// gl_campaign_dialogue can decode it later with package_runtime's loader.
// Records arrive one call at a time so no payload has to scale with a whole
// project's text. Returns bytes written (a single ok status byte), or 0 on a
// boundary failure.
//
// Payload in: u64 dialogue id, u32 record size, the dialogue record payload
// exactly as tools/game_content encodes it.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_add_dialogue(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint64_t dialogue_id = reader.u64();
    const std::uint32_t record_size = reader.u32();
    if (reader.overflowed() || record_size > io_capacity) return malformed();
    // A stable identity names one record. A duplicate would shadow the first
    // silently, which is exactly the drift this surface exists to prevent.
    if (instance->package.find(pf::SectionType::dialogue, dialogue_id) !=
        nullptr) {
        return malformed();
    }
    const std::size_t offset = instance->package.bytes.size();
    if (offset + record_size > campaign_bytes_capacity) return malformed();
    instance->package.bytes.resize(offset + record_size);
    if (!reader.copy(instance->package.bytes.data() + offset, record_size)) {
        instance->package.bytes.resize(offset);
        return malformed();
    }
    // Insert in identity order: LoadedPackage::find binary-searches records,
    // and callers attach dialogues in authored order, not sorted order.
    auto& records = instance->package.sections[campaign_dialogue_section].records;
    const auto position = std::lower_bound(
        records.begin(),
        records.end(),
        dialogue_id,
        [](const pf::RecordView& record, std::uint64_t id) {
            return record.stable_id < id;
        }
    );
    records.insert(
        position,
        {dialogue_id, static_cast<std::uint32_t>(offset), record_size}
    );
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    return writer.size();
}

// Writes the cursor's current node and returns the number of bytes written,
// or 0 on a boundary failure.
//
// Payload out: u8 status, u8 complete, u64 node id, u8 kind, u64 encounter
// id, u16 dialogue count, then one u64 per dialogue in authored order.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_state(std::uint32_t handle) {
    const CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const pr::CampaignNode& node = instance->cursor.current();
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(instance->cursor.complete() ? 1u : 0u);
    writer.u64(node.id);
    writer.u8(static_cast<std::uint8_t>(node.kind));
    writer.u64(node.encounter_id);
    writer.u16(static_cast<std::uint16_t>(node.dialogue_ids.size()));
    for (const std::uint64_t dialogue_id : node.dialogue_ids) {
        writer.u64(dialogue_id);
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Advances the cursor past the current encounter node using the encounter's
// outcome and reported objective results, the same inputs the native client
// session passes, so branch predicates are evaluated by the engine alone.
// Returns bytes written, or 0 on a boundary failure.
//
// Payload in: u8 outcome, u32 objective count, then u64 id and u8 state per
// objective. Payload out: u8 status, u8 CampaignError.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_advance(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint8_t outcome = reader.u8();
    const std::uint32_t objective_count = reader.u32();
    if (reader.overflowed() || outcome > 2u ||
        objective_count > (io_capacity / 9u)) {
        return malformed();
    }
    std::vector<sim::ObjectiveResult> objectives;
    objectives.reserve(objective_count);
    for (std::uint32_t index = 0; index < objective_count; ++index) {
        sim::ObjectiveResult result;
        result.id = reader.u64();
        const std::uint8_t state = reader.u8();
        if (state > 2u) return malformed();
        result.state = static_cast<sim::ObjectiveState>(state);
        objectives.push_back(result);
    }
    if (reader.overflowed()) return malformed();

    const pr::CampaignError error = instance->cursor.advance_after(
        static_cast<sim::Outcome>(outcome),
        objectives
    );
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    return writer.size();
}

// Advances the cursor past a story node, which has no outcome to evaluate.
// Payload out: u8 status, u8 CampaignError.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_advance_story(std::uint32_t handle) {
    CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(instance->cursor.advance_story()));
    return writer.size();
}

// Decodes one attached dialogue record with package_runtime's loader and
// writes it back. Returns bytes written, or 0 on a boundary failure.
//
// Payload out: u8 status, u8 DialogueError, then on success a u16-length
// name string, u16 line count, a u16-length speaker and text string per
// line in authored order, a u8 backdrop (the art library's menu index plus
// one, or zero for a scene that names none), then a u8 cast size, one u64
// unit type identity per cast entry, and a u8 per line naming the entry that
// speaks it, plus one.
//
// The two tails go last, in that order, for the reason they go last in the
// record they were decoded from: everything before each of them is what a
// caller written before it existed reads, and such a caller stops where it
// always stopped. Unlike the record, both are written unconditionally here.
// This is a length-prefixed buffer rather than a record read to its exact end,
// so there is no byte identity to preserve and a fixed shape is cheaper for
// the reader.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_dialogue(
    std::uint32_t handle,
    std::uint64_t dialogue_id
) {
    const CampaignInstance* instance = resolve_campaign(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto loaded = pr::load_dialogue(instance->package, dialogue_id);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(loaded.error));
    if (loaded) {
        // Every string was decoded from a u16-length prefix, so these casts
        // cannot truncate.
        writer.u16(static_cast<std::uint16_t>(loaded.dialogue.name.size()));
        writer.bytes(loaded.dialogue.name);
        writer.u16(static_cast<std::uint16_t>(loaded.dialogue.lines.size()));
        for (const pr::DialogueLine& line : loaded.dialogue.lines) {
            writer.u16(static_cast<std::uint16_t>(line.speaker.size()));
            writer.bytes(line.speaker);
            writer.u16(static_cast<std::uint16_t>(line.text.size()));
            writer.bytes(line.text);
        }
        writer.u8(loaded.dialogue.backdrop);
        // A cast is capped at 255 by the source reader, so this cannot
        // truncate either.
        writer.u8(static_cast<std::uint8_t>(loaded.dialogue.cast.size()));
        for (const std::uint64_t unit_type_id : loaded.dialogue.cast) {
            writer.u64(unit_type_id);
        }
        for (const pr::DialogueLine& line : loaded.dialogue.lines) {
            writer.u8(line.cast_entry);
        }
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Writes the engine's own name for a CampaignError into the scratch buffer
// and returns its length.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_error_name(std::uint32_t error) {
    if (error > static_cast<std::uint32_t>(pr::CampaignError::outcome_incomplete)) {
        return 0u;
    }
    const std::string_view name =
        pr::error_name(static_cast<pr::CampaignError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the engine's own name for a DialogueError into the scratch buffer
// and returns its length.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_dialogue_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(pr::DialogueError::malformed_payload)) {
        return 0u;
    }
    const std::string_view name =
        pr::error_name(static_cast<pr::DialogueError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// ---------------------------------------------------------------------------
// The campaign session
// ---------------------------------------------------------------------------
//
// Six entry points, and between them they are the session `platform/client`
// holds: create it, give it its content, begin it, ask where it stands, take a
// board through the roster, and commit what a battle did. Nothing here decides
// anything. Every one of them is a call into `client::CampaignSession` with the
// answer written back out; the exclusion, the experience, the growth rolls, the
// drops, the commit, the envelope and the edge are all the same C++ the terminal
// runs, and a browser that computed any of it would be a browser that could
// disagree with the game.
//
// Why the steps and not the whole loop: `run_persistent_campaign` blocks. A
// browser cannot block. A battle is clicks arriving over many event-loop turns,
// and an author must be able to leave one and come back to an editor. So the
// session is driven from the outside here and from the inside there, and it is
// the same session.

// Creates a campaign session over a synthetic package holding the authored
// flow. Returns a handle, or 0 on failure; the buffer holds a single status
// byte. Content refusals are reported by gl_campaign_session_begin, which is
// where the flow is actually decoded.
//
// Payload in: 16 bytes package identity, u32 content revision, u64 campaign id,
// u32 record size, the campaign record payload exactly as tools/game_content
// encodes it, u16 encounter count, then one u64 per encounter identity the
// flow's encounter nodes may reference.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_create(
    std::uint32_t payload_size
) {
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    grandleon::core::PackageId package_id{};
    if (!reader.copy(package_id.data(), package_id.size())) return malformed();
    const std::uint32_t content_revision = reader.u32();
    const std::uint64_t campaign_id = reader.u64();
    const std::uint32_t record_size = reader.u32();
    if (reader.overflowed() || record_size > io_capacity) return malformed();

    pf::LoadedPackage package;
    package.game_id = package_id;
    package.content_revision = content_revision;
    package.bytes.resize(record_size);
    if (!reader.copy(package.bytes.data(), record_size)) return malformed();

    const std::uint16_t encounter_count = reader.u16();
    if (reader.overflowed()) return malformed();
    pf::SectionView encounters;
    encounters.type = pf::SectionType::encounters;
    encounters.schema_major = 1u;
    encounters.flags = pf::section_flag_required;
    encounters.records.reserve(encounter_count);
    for (std::uint16_t index = 0; index < encounter_count; ++index) {
        // Existence is all load_campaign checks for an encounter reference. The
        // board itself arrives through gl_campaign_session_add_board, because
        // this caller has boards and not encounter records.
        encounters.records.push_back({reader.u64(), 0u, 0u});
    }
    if (reader.overflowed()) return malformed();
    // LoadedPackage::find binary-searches records by stable identity, so a
    // synthetic section must hold the same ordering invariant the container
    // loader establishes.
    std::sort(
        encounters.records.begin(),
        encounters.records.end(),
        [](const pf::RecordView& lhs, const pf::RecordView& rhs) {
            return lhs.stable_id < rhs.stable_id;
        }
    );

    pf::SectionView campaigns;
    campaigns.type = pf::SectionType::campaigns;
    campaigns.schema_major = 1u;
    campaigns.flags = pf::section_flag_required;
    campaigns.records.push_back({campaign_id, 0u, record_size});
    pf::SectionView unit_types;
    unit_types.type = pf::SectionType::unit_types;
    unit_types.schema_major = 1u;
    unit_types.flags = pf::section_flag_required;
    // The section order is fixed here and read back by the two indices above.
    package.sections.push_back(campaigns);
    package.sections.push_back(encounters);
    package.sections.push_back(unit_types);

    SessionInstance* const instance =
        new SessionInstance{campaign_id, client::CampaignSessionOptions{}};
    instance->package = std::move(package);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    return store_session(instance);
}

// Releases a campaign session handle, and the battle it was holding. Releasing
// an unknown handle is a no-op.
EMSCRIPTEN_KEEPALIVE void gl_campaign_session_destroy(std::uint32_t handle) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) return;
    gl_sim_destroy(instance->battle);
    session_registry()[static_cast<std::size_t>(handle) - 1u] = nullptr;
    delete instance;
}

// Attaches one compiled unit type record, so that the growth block a level-up
// rolls against is the author's own. Records arrive one at a time so no payload
// scales with a whole project. Returns bytes written (a single status byte), or
// 0 on a boundary failure.
//
// Payload in: u64 unit type id, u32 record size, the unit type record payload
// exactly as tools/game_content encodes it.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_add_unit_type(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint64_t unit_type_id = reader.u64();
    const std::uint32_t record_size = reader.u32();
    if (reader.overflowed() || record_size > io_capacity) return malformed();
    // A stable identity names one record. A duplicate would shadow the first
    // silently, which is exactly the drift this surface exists to prevent.
    if (instance->package.find(pf::SectionType::unit_types, unit_type_id) !=
        nullptr) {
        return malformed();
    }
    const std::size_t offset = instance->package.bytes.size();
    if (offset + record_size > campaign_bytes_capacity) return malformed();
    instance->package.bytes.resize(offset + record_size);
    if (!reader.copy(instance->package.bytes.data() + offset, record_size)) {
        instance->package.bytes.resize(offset);
        return malformed();
    }
    auto& records =
        instance->package.sections[session_unit_type_section].records;
    const auto position = std::lower_bound(
        records.begin(),
        records.end(),
        unit_type_id,
        [](const pf::RecordView& record, std::uint64_t id) {
            return record.stable_id < id;
        }
    );
    records.insert(
        position,
        {unit_type_id, static_cast<std::uint32_t>(offset), record_size}
    );
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    return writer.size();
}

// Attaches one encounter's board, as the caller built it, with the authored
// placement identity of every unit on it. Returns bytes written (a single
// status byte), or 0 on a boundary failure.
//
// The source key is what joins a roster member to a placement, and it is the
// same identity in every encounter that member appears in, which is precisely
// why the exclusion pass uses it and why it has to travel beside the board.
//
// Payload in: u64 encounter id, the encounter definition exactly as gl_sim_create
// reads it, then u32 placement count and one u64 source key per unit, in the
// definition's own unit order, then u16 deployment capacity, then u32
// specificity count and one record per entry: u64 member id, eleven i16 stat
// deltas in `package_runtime::SpecificStat` order, u8 reach bonus.
//
// The capacity is counted rather than optional, on this file's own convention
// for a tail: zero is a board that caps nothing, which is every board by
// default, and it is written rather than left off so that a caller with no cap
// to give and a caller whose payload was truncated are not the same bytes. It
// rides beside the board rather than inside the definition because it is a
// campaign judgement the simulation never learns. `EncounterLoadResult` is
// exactly where `package_runtime::load_encounter` puts it for the same reason.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_add_board(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint64_t encounter_id = reader.u64();
    if (reader.overflowed()) return malformed();
    pr::EncounterLoadResult board;
    if (!read_encounter_definition(reader, board.definition)) {
        return malformed();
    }
    const std::uint32_t placement_count = reader.u32();
    if (reader.overflowed() ||
        placement_count != board.definition.units.size()) {
        return malformed();
    }
    board.placements.reserve(placement_count);
    for (std::uint32_t index = 0; index < placement_count; ++index) {
        board.placements.push_back(
            {board.definition.units[index].id, reader.u64()}
        );
    }
    board.deployment_capacity = reader.u16();
    // What the author wrote about the characters this board fields, beyond
    // their unit types. Counted rather than optional, on the same convention
    // the capacity above is counted on: a caller with nobody specific to
    // declare writes a count of zero, so it is never the same bytes as a
    // payload that was cut short.
    //
    // Dense rather than sparse, unlike the package tail this mirrors. The
    // package is written once and read on two consoles, so its bytes are worth
    // packing; this crosses one boundary inside one process, and eleven deltas
    // written flat is a writer on the other side with no branch in it.
    const std::uint32_t specificity_count = reader.u32();
    if (reader.overflowed() ||
        specificity_count > (io_capacity / (8u + 2u * pr::specific_stat_count))) {
        return malformed();
    }
    board.member_specificities.reserve(specificity_count);
    for (std::uint32_t index = 0; index < specificity_count; ++index) {
        pr::MemberSpecificity specificity;
        specificity.member_id = reader.u64();
        for (std::size_t stat = 0; stat < pr::specific_stat_count; ++stat) {
            specificity.stat_deltas[stat] = reader.i16();
        }
        specificity.reach_bonus = reader.u8();
        if (reader.overflowed() || specificity.member_id == 0u) {
            return malformed();
        }
        board.member_specificities.push_back(specificity);
    }
    if (reader.overflowed()) return malformed();
    instance->boards.add(encounter_id, std::move(board));
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    return writer.size();
}

// Founds the roster from the content, enters the graph, and, when asked, reads
// the named slot back over the top. Returns bytes written, or 0 on a boundary
// failure.
//
// Payload in: u8 resume, u8 player side, u16 slot name length, the slot name.
// Payload out: u8 status, u8 CampaignSessionError, u8 refused, u8 resumed, then
// the refusal in each layer's own vocabulary: u8 StorageError, u8
// MigrationError, u8 SaveError, u8 StateError, u8 wrong campaign.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_begin(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    client::CampaignSessionOptions options;
    options.resume = reader.u8() != 0u;
    options.player_side =
        reader.u8() == 0u ? sim::Side::first : sim::Side::second;
    // A u16 length cannot exceed the scratch buffer, so there is no bound to
    // check beyond the read itself; the copy below is what refuses a length the
    // payload does not actually carry.
    const std::uint16_t slot_size = reader.u16();
    if (reader.overflowed()) return malformed();
    std::vector<std::uint8_t> slot(slot_size);
    if (slot_size != 0u && !reader.copy(slot.data(), slot_size)) {
        return malformed();
    }
    options.slot.assign(slot.begin(), slot.end());

    // The options are only knowable now, so the session is rebuilt over the
    // same package, boards and device rather than mutated. A begin is the start
    // of a campaign either way.
    instance->session = client::CampaignSession{
        instance->package,
        instance->campaign_id,
        instance->boards,
        browser_slots(),
        options
    };
    client::SlotFailure failure;
    bool refused = false;
    bool resumed = false;
    const client::CampaignSessionError error =
        instance->session.begin(failure, refused, resumed);

    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    writer.u8(refused ? 1u : 0u);
    writer.u8(resumed ? 1u : 0u);
    writer.u8(static_cast<std::uint8_t>(failure.storage));
    writer.u8(static_cast<std::uint8_t>(failure.migration));
    writer.u8(static_cast<std::uint8_t>(failure.save));
    writer.u8(static_cast<std::uint8_t>(failure.state));
    writer.u8(failure.wrong_campaign ? 1u : 0u);
    return writer.size();
}

// Where the campaign stands, and who is on the roster. Returns bytes written,
// or 0 on a boundary failure.
//
// Payload out: u8 status, u8 CampaignSessionError, u64 node id, u8 node kind,
// u64 encounter id, u16 dialogue count and one u64 each, then u32 roster count
// and per member: u64 member id, u64 placement source key, u64 unit type id,
// u8 Availability, u16 level, u32 experience, and ten u16 permanent gains in
// GrowableStat order.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_state(
    std::uint32_t handle
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const client::CampaignSession::Standing where = instance->session.standing();
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(where.error));
    writer.u64(where.node.stable_id);
    writer.u8(static_cast<std::uint8_t>(where.kind));
    writer.u64(where.encounter_id);
    writer.u16(static_cast<std::uint16_t>(where.dialogue_ids.size()));
    for (const std::uint64_t dialogue_id : where.dialogue_ids) {
        writer.u64(dialogue_id);
    }
    write_roster(writer, instance->session.roster());
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Completes a story node through the graph. Payload out: u8 status, u8
// CampaignSessionError, then the roster record of everybody the node
// recruited, in authored order.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_advance_story(
    std::uint32_t handle
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    std::vector<client::RosterEntry> joined;
    const client::CampaignSessionError error =
        instance->session.advance_story(joined);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    write_roster(writer, joined);
    // Checked here as it is after every other variable-length write on this
    // surface. A truncated roster would still declare its full count, so the
    // reader would take whatever followed the buffer as the next member.
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Takes the standing node's board through the roster and starts the battle on
// it. Returns an ordinary encounter handle, which every gl_sim_* entry point
// drives, or 0 when there is no battle to fight.
//
// Payload out: u8 status, u8 CampaignSessionError, u8 RosterError, u8
// EncounterLoadError, u8 CreateError, u64 encounter id, u32 excluded count and
// one u64 member each, u32 binding count and a u64 board unit with its u64
// member each, then u32 unit count and a u64 board unit with its u64 placement
// source key each, in the board's own order.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_board(
    std::uint32_t handle
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    // A battle left standing from a previous board is released rather than
    // leaked: a session fights one board at a time.
    gl_sim_destroy(instance->battle);
    instance->battle = 0u;
    instance->events.clear();

    const client::CampaignSession::PreparedBoard prepared =
        instance->session.prepare_board();
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(prepared.error));
    writer.u8(static_cast<std::uint8_t>(prepared.roster_error));
    writer.u8(static_cast<std::uint8_t>(prepared.encounter.load_error));
    if (prepared.error != client::CampaignSessionError::none) {
        writer.u8(static_cast<std::uint8_t>(sim::CreateError::none));
        return 0u;
    }

    auto created = sim::create_encounter(prepared.encounter.encounter.definition);
    writer.u8(static_cast<std::uint8_t>(created.error));
    if (!created) return 0u;

    writer.u64(prepared.board.encounter_id);
    writer.u32(static_cast<std::uint32_t>(prepared.board.excluded.size()));
    for (const campaign::PersistentEntityId member : prepared.board.excluded) {
        writer.u64(member.value);
    }
    const std::vector<sim::UnitDefinition>& units =
        prepared.encounter.encounter.definition.units;
    std::uint32_t bound = 0;
    for (const sim::UnitDefinition& unit : units) {
        if (prepared.board.binding
                .persistent_of(campaign::BattleEntityId{unit.id})
                .value != 0U) {
            ++bound;
        }
    }
    writer.u32(bound);
    for (const sim::UnitDefinition& unit : units) {
        const campaign::PersistentEntityId member =
            prepared.board.binding.persistent_of(
                campaign::BattleEntityId{unit.id}
            );
        if (member.value == 0U) continue;
        writer.u64(unit.id);
        writer.u64(member.value);
    }
    writer.u32(
        static_cast<std::uint32_t>(prepared.encounter.encounter.placements.size())
    );
    for (const pr::PlacementIdentity& placement :
         prepared.encounter.encounter.placements) {
        writer.u64(placement.unit_id);
        writer.u64(placement.source_key_id);
    }
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }

    instance->battle = store(new sim::Encounter(created.encounter));
    record_events(instance->battle, &instance->events);
    return instance->battle;
}

// Commits what the battle did and writes the campaign to its slot. Returns
// bytes written, or 0 on a boundary failure.
//
// Nothing is read in: the outcome, the hash, the final snapshot, the objective
// results and every event are the engine's own, held on this side of the
// boundary since the board started.
//
// Payload out: u8 status, u8 CampaignSessionError, u8 StorageError, u8
// simulation Outcome, u8 ProgressionSourceError, u64 canonical hash, u32 fallen
// count and one u64 member each, u32 level-up count and per level-up a u64
// member, u16 from, u16 to and ten u16 points in GrowableStat order, u32
// operation count and per operation a u8 kind, u8 selector, u64 subject, u8
// content category, u64 stable id and i64 amount, then u8 advanced, u8 already
// advanced, u64 target node id, the roster record of everybody this node
// recruited, the roster as it stands after the commit, and the company's
// shared store as the commit left it.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_commit(
    std::uint32_t handle
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    sim::Encounter* const battle = resolve(instance->battle);
    if (battle == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }

    client::BattleReport report;
    report.final_snapshot = battle->snapshot();
    report.outcome = report.final_snapshot.outcome;
    report.objectives = report.final_snapshot.objectives;
    report.canonical_hash = battle->canonical_hash();
    report.events = instance->events;

    client::BattleAftermath aftermath;
    const client::CampaignSessionError error =
        instance->session.commit_battle(report, aftermath);

    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    // Reserved. The save has not happened yet, and it deliberately has not:
    // this reply carries two full rosters, every level-up and every operation,
    // and a company large enough to overflow it would otherwise be advanced,
    // written to its slot, and then reported as a boundary failure, leaving
    // the slot a node ahead of whatever is drawing it, with nothing said. The
    // whole answer is composed first, and only a reply that fits earns a save.
    const std::size_t save_error_at = writer.size();
    writer.u8(0u);
    writer.u8(static_cast<std::uint8_t>(aftermath.outcome));
    writer.u8(static_cast<std::uint8_t>(aftermath.progression.error));
    writer.u64(aftermath.canonical_hash);
    writer.u32(static_cast<std::uint32_t>(aftermath.fallen.size()));
    for (const campaign::PersistentEntityId member : aftermath.fallen) {
        writer.u64(member.value);
    }
    writer.u32(
        static_cast<std::uint32_t>(aftermath.progression.level_ups.size())
    );
    for (const cr::LevelUp& level_up : aftermath.progression.level_ups) {
        writer.u64(level_up.member.value);
        writer.u16(level_up.from_level);
        writer.u16(level_up.to_level);
        for (const std::uint16_t points : level_up.points) {
            writer.u16(points);
        }
    }
    writer.u32(
        static_cast<std::uint32_t>(aftermath.progression.operations.size())
    );
    for (const campaign::CampaignOutcomeOperation& operation :
         aftermath.progression.operations) {
        writer.u8(static_cast<std::uint8_t>(operation.kind));
        writer.u8(operation.selector);
        writer.u64(operation.subject.value);
        writer.u8(static_cast<std::uint8_t>(operation.definition.category));
        writer.u64(operation.definition.stable_id);
        writer.u64(static_cast<std::uint64_t>(operation.amount));
    }
    writer.u8(aftermath.completion.advanced ? 1u : 0u);
    writer.u8(aftermath.completion.already_advanced ? 1u : 0u);
    writer.u8(static_cast<std::uint8_t>(aftermath.completion.error));
    writer.u64(aftermath.completion.target.stable_id);
    write_roster(writer, aftermath.recruited);
    write_roster(writer, aftermath.roster);
    // And what the company owns beyond what its members are carrying. The
    // operations above say what moved; this says what is there, which is the
    // question a screen between two battles is actually asked.
    write_stacks(writer, aftermath.store);
    // And the rule that decided what the fallen above became. Last, because
    // everything before it was already here and appending is what keeps a reader
    // written against the old payload reading the same fields.
    //
    // Published rather than left to the browser to infer from the roster,
    // because the browser must be able to say the right word before it looks:
    // "Mirea died" and "Mirea fell and is back" are two different sentences
    // about the same event, and which one is true is a fact about the campaign
    // rather than something a screen may work out from who is still available.
    writer.u8(static_cast<std::uint8_t>(aftermath.character_loss));
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    // The campaign is saved whether or not it moved, exactly as the terminal
    // saves it: a node with no route out of it still buried somebody.
    const storage::StorageError written =
        aftermath.encounter_id == 0U
            ? storage::StorageError::none
            : instance->session.save();
    writer.patch(save_error_at, static_cast<std::uint8_t>(written));
    return writer.size();
}

// The company between battles: what it is, and what the next board has room
// for.
//
// Payload out: u8 status, u8 CampaignSessionError, u64 node id, u64 encounter
// id, u32 placeable count and one u64 member each, u32 fielded count and one
// u64 member each, u16 deployment capacity, then the roster record and the
// company's store. Everything is read out of committed campaign state or off
// the authored board, so a screen drawn from it derives nothing.
//
// The two counts are published rather than left to the screen because they are
// counted against each other: `fielded` is who would actually take this board
// as the company stands, and `capacity` is how many its author lets out. A
// screen that summed either for itself would be a second implementation of a
// rule, and two clients that count differently offer gestures the engine
// refuses. Zero capacity is a board that caps nothing.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_company(
    std::uint32_t handle
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    const client::CompanyManagement company = instance->session.management();
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(company.error));
    writer.u64(company.node.stable_id);
    writer.u64(company.encounter_id);
    writer.u32(static_cast<std::uint32_t>(company.placeable.size()));
    for (const campaign::PersistentEntityId member : company.placeable) {
        writer.u64(member.value);
    }
    writer.u32(static_cast<std::uint32_t>(company.fielded.size()));
    for (const campaign::PersistentEntityId member : company.fielded) {
        writer.u64(member.value);
    }
    writer.u16(company.capacity);
    write_roster(writer, company.roster);
    write_stacks(writer, company.store);
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// One management gesture, committed or refused.
//
// Payload in: u8 verb (0 give, 1 take, 2 field, 3 bench), u64 member, u64 item
// identity. The item is read for the two moves and ignored for the two
// availability verbs.
//
// Payload out: u8 status, u8 ManagementError, u8 OutcomeError, u8 StateError,
// u8 already applied, u8 saved, u8 StorageError, u32 operation count and one
// {u8 kind, u8 selector, u64 subject, u64 item identity, u64 amount} each, then
// the roster and the store as the gesture left them. The refreshed company
// rides along because a screen redraws after every gesture and a second call to
// ask what changed would be a second chance to disagree about it.
//
// What that reply costs before the roster and the store are counted: the seven
// status and outcome bytes, and the operation count. And what one gesture may
// add on top of the company as it stands: the operations it records, and the
// one stack a give or a take moves between the store and a member's kit. The
// second is declared generously, because it bounds a refusal that must never
// fire on a gesture that would have fitted.
constexpr std::size_t manage_reply_fixed = 11u;
constexpr std::size_t manage_reply_allowance = 512u;

EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_manage(
    std::uint32_t handle,
    std::uint32_t payload_size
) {
    SessionInstance* instance = resolve_session(handle);
    if (instance == nullptr) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::unknown_handle));
        return 0u;
    }
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    const std::uint8_t verb = reader.u8();
    const std::uint64_t member = reader.u64();
    const std::uint64_t item = reader.u64();
    if (reader.overflowed() || verb > 3u) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    }

    // What the reply will cost, measured before the gesture rather than after.
    //
    // The reply carries the roster and the store as the gesture leaves them,
    // and this is the one entry point that both commits *and* saves before it
    // writes a word, so a company too large to report on would be moved, be
    // written to its slot, and only then produce an answer that does not fit,
    // leaving the module a gesture ahead of whatever is drawing it. Measuring
    // the company as it stands and refusing here means nothing was applied.
    //
    // The payload has already been read out into the three values above, so
    // measuring into the same buffer costs nothing.
    Writer probe;
    write_roster(probe, instance->session.roster());
    write_stacks(probe, instance->session.store());
    if (probe.overflowed() ||
        probe.size() + manage_reply_fixed + manage_reply_allowance >
            io_capacity) {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }

    const campaign::PersistentEntityId who{member};
    const campaign::DefinitionRef thing{
        instance->package.game_id, core::ContentCategory::item, item
    };
    client::ManagementCommit result;
    switch (verb) {
        case 0u: result = instance->session.give_item(who, thing); break;
        case 1u: result = instance->session.take_item(who, thing); break;
        case 2u: result = instance->session.set_fielded(who, true); break;
        default: result = instance->session.set_fielded(who, false); break;
    }

    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(result.error));
    writer.u8(static_cast<std::uint8_t>(result.application.error));
    writer.u8(static_cast<std::uint8_t>(result.application.state_error));
    writer.u8(result.application.already_applied ? 1u : 0u);
    writer.u8(result.saved ? 1u : 0u);
    writer.u8(static_cast<std::uint8_t>(result.save));
    writer.u32(static_cast<std::uint32_t>(result.batch.operations.size()));
    for (const campaign::CampaignOutcomeOperation& operation :
         result.batch.operations) {
        writer.u8(static_cast<std::uint8_t>(operation.kind));
        writer.u8(operation.selector);
        writer.u64(operation.subject.value);
        writer.u64(operation.definition.stable_id);
        writer.u64(static_cast<std::uint64_t>(operation.amount));
    }
    write_roster(writer, instance->session.roster());
    write_stacks(writer, instance->session.store());
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// The slot device
// ---------------------------------------------------------------------------
//
// Three entry points over the one device `browser_slots()` returns: read a
// slot, replace a slot, forget a slot. They are the whole of what this module
// knows about outliving a page.
//
// **The campaign session does not know they exist.** It writes a save to a
// `storage::SlotStorage` exactly as it does on a desktop and will on a console,
// and something on the other side of this ABI mirrors those bytes into the
// browser's own store and puts them back before the next
// `gl_campaign_session_begin`. Nothing about a browser reaches C++, which is
// what keeps one campaign loop rather than a browser-shaped variant of it.
//
// **The mirroring is out there rather than in here because IndexedDB answers
// later.** `campaign::save_campaign` returns on this event-loop turn and a
// browser's store answers on a subsequent one, so a device that called out to
// it would have to suspend the whole module mid-save and unwind through every
// caller between here and there. The caller already has a turn of its own after
// each commit; that is where a mirror belongs.
//
// The bytes crossing here are the `GLSV` envelope verbatim. This surface reads
// them as opaque exactly as `storage::SlotStorage` does. The save format
// carries its own integrity, its own versioning and its own package
// requirements, and a second wrapper around it would be a second thing to keep
// in step.

// Reads one slot's bytes out of the browser device. Returns bytes written, or 0
// on a boundary failure.
//
// Payload in: u16 slot name length, the slot name.
// Payload out: u8 status, u8 StorageError, u32 size, the bytes.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_storage_read(std::uint32_t payload_size) {
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    std::string slot;
    if (!read_slot_name(reader, slot)) return malformed();

    const storage::StorageRead read = browser_slots().read(slot);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    if (!read) {
        writer.u8(static_cast<std::uint8_t>(read.error));
        writer.u32(0u);
        return writer.size();
    }
    // A slot the device holds and this buffer cannot carry is refused by the
    // device's own word for it rather than truncated. The budget above makes
    // this unreachable for anything the session itself wrote.
    if (read.bytes.size() > browser_slot_capacity) {
        writer.u8(static_cast<std::uint8_t>(storage::StorageError::too_large));
        writer.u32(0u);
        return writer.size();
    }
    writer.u8(static_cast<std::uint8_t>(storage::StorageError::none));
    writer.u32(static_cast<std::uint32_t>(read.bytes.size()));
    for (const std::uint8_t byte : read.bytes) writer.u8(byte);
    if (writer.overflowed()) {
        Writer overflow;
        overflow.u8(static_cast<std::uint8_t>(AbiStatus::buffer_overflow));
        return 0u;
    }
    return writer.size();
}

// Replaces one slot's bytes on the browser device, creating it if it is absent.
// This is how a save kept somewhere durable is put back where a session begins
// looking for it. Returns bytes written, or 0 on a boundary failure.
//
// Payload in: u16 slot name length, the slot name, u32 size, the bytes.
// Payload out: u8 status, u8 StorageError.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_storage_write(std::uint32_t payload_size) {
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    std::string slot;
    if (!read_slot_name(reader, slot)) return malformed();
    const std::uint32_t size = reader.u32();
    if (reader.overflowed() || size > io_capacity) return malformed();
    std::vector<std::uint8_t> bytes(size);
    if (size != 0u && !reader.copy(bytes.data(), size)) return malformed();

    const storage::StorageError error = browser_slots().write(slot, bytes);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    return writer.size();
}

// Forgets one slot on the browser device. This is what founding a campaign anew
// does to the one it replaces, so that the next begin finds nothing rather than
// the campaign the player deliberately left behind. Returns bytes written, or 0
// on a boundary failure.
//
// Payload in: u16 slot name length, the slot name.
// Payload out: u8 status, u8 StorageError.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_storage_erase(std::uint32_t payload_size) {
    const auto malformed = [] {
        Writer writer;
        writer.u8(static_cast<std::uint8_t>(AbiStatus::malformed_payload));
        return 0u;
    };
    Reader reader{payload_size > io_capacity ? io_capacity : payload_size};
    std::string slot;
    if (!read_slot_name(reader, slot)) return malformed();

    const storage::StorageError error = browser_slots().erase(slot);
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(AbiStatus::ok));
    writer.u8(static_cast<std::uint8_t>(error));
    return writer.size();
}

// Writes the campaign's own name for an OutcomeError into the scratch buffer
// and returns its length. This is what a client shows a player when a
// management gesture is refused: `insufficient_items`, `unit_is_dead`,
// `unknown_unit`. The campaign's words, not a paraphrase.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_outcome_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(
                    campaign::OutcomeError::invalid_candidate
                )) {
        return 0u;
    }
    const std::string_view name =
        campaign::outcome_error_name(static_cast<campaign::OutcomeError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the engine's own name for a CampaignSessionError into the scratch
// buffer and returns its length.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_session_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(
                    client::CampaignSessionError::flow_stalled
                )) {
        return 0u;
    }
    const std::string_view name = client::campaign_session_error_name(
        static_cast<client::CampaignSessionError>(error)
    );
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the engine's own name for a RosterError into the scratch buffer and
// returns its length.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_roster_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(
                    cr::RosterError::over_deployment_capacity
                )) {
        return 0u;
    }
    const std::string_view name =
        cr::roster_error_name(static_cast<cr::RosterError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the save format's own name for a SaveError into the scratch buffer and
// returns its length.
//
// This is the vocabulary a refused resume is reported in. A save the player
// deserves a reason for is one whose bytes are intact and whose meaning this
// build cannot honour: `incompatible_rules`, `unsupported_schema`,
// `unknown_required_section`. The format's own word for it is the reason.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_save_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(campaign::SaveError::invalid_state)) {
        return 0u;
    }
    const std::string_view name =
        campaign::save_error_name(static_cast<campaign::SaveError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the migration registry's own name for a MigrationError into the
// scratch buffer and returns its length.
//
// The other half of a refused resume, and the half that answers "the content
// moved out from under this save": `unmounted_package` when the save belongs to
// a different game, `downgrade_refused` when it was written against content
// newer than what is loaded, `missing_definition` when a reference it holds no
// longer names anything.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_migration_error_name(
    std::uint32_t error
) {
    if (error > static_cast<std::uint32_t>(
                    campaign::MigrationError::invalid_result
                )) {
        return 0u;
    }
    const std::string_view name = campaign::migration_error_name(
        static_cast<campaign::MigrationError>(error)
    );
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the device's own name for a StorageError into the scratch buffer and
// returns its length.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_storage_error_name(std::uint32_t error) {
    if (error > static_cast<std::uint32_t>(storage::StorageError::io_failure)) {
        return 0u;
    }
    const std::string_view name =
        storage::storage_error_name(static_cast<storage::StorageError>(error));
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// Writes the campaign's own name for one outcome operation kind into the
// scratch buffer and returns its length. Operation kinds start at one, so a
// caller walks them from one until the answer is empty.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_campaign_operation_name(
    std::uint32_t kind
) {
    if (kind == 0u ||
        kind > static_cast<std::uint32_t>(
                   campaign::OutcomeOperationKind::grow_stat
               )) {
        return 0u;
    }
    const std::string_view name = campaign::outcome_operation_name(
        static_cast<campaign::OutcomeOperationKind>(kind)
    );
    Writer writer;
    writer.bytes(name);
    return writer.overflowed() ? 0u : writer.size();
}

// ---------------------------------------------------------------------------
// The content compiler
// ---------------------------------------------------------------------------
//
// `tools/game_content` parses an authored project and compiles it to a package.
// It is bound here for one reason: the browser had no package bytes at all, so
// nothing downstream of the compiler could be reached from the editor, not a
// cartridge, not a checksum, not a download.
//
// It is bound rather than ported. The same two translation units already run on
// a Nintendo 64 cartridge with no filesystem and no operating system
// (`platform/nintendo64/src/play_rom.cpp` embeds `project.json` and calls these
// two functions on the console), which is a stronger portability proof than a
// successful wasm link would have been.

// Address of the content buffer in linear memory.
EMSCRIPTEN_KEEPALIVE std::uintptr_t gl_content_buffer(void) {
    return reinterpret_cast<std::uintptr_t>(content_buffer);
}

// Capacity of the content buffer in bytes. It bounds the authored source going
// in and the whole reply coming back, package included.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_content_capacity(void) {
    return static_cast<std::uint32_t>(content_capacity);
}

// Compiles the authored project source sitting in the content buffer and
// rewrites the buffer with the result. Returns bytes written, or 0 when the
// reply did not fit.
//
// The reply is diagnostics first, then, only when the status is success, the
// project's identities and its package. Diagnostics carry the engine's own name
// for their code rather than a number the caller would have to map, because the
// two stages have two vocabularies and a caller that restated either could
// disagree with the compiler about what it just refused.
EMSCRIPTEN_KEEPALIVE std::uint32_t gl_content_compile(
    std::uint32_t source_size
) {
    Writer writer{content_buffer, content_capacity};
    if (source_size > content_capacity) {
        writer.u8(static_cast<std::uint8_t>(ContentStatus::source_too_large));
        writer.u16(0u);
        return writer.size();
    }

    // The source has to be copied out before anything is written back, because
    // the reply goes into the same buffer the source is sitting in.
    const std::string json(
        reinterpret_cast<const char*>(content_buffer),
        static_cast<std::size_t>(source_size)
    );

    const auto parsed = gc::parse_source_project_json(json);
    if (!parsed) {
        writer.u8(static_cast<std::uint8_t>(ContentStatus::source_rejected));
        writer.u16(static_cast<std::uint16_t>(
            parsed.diagnostics.size() > 0xffffu ? 0xffffu
                                                : parsed.diagnostics.size()
        ));
        for (const auto& diagnostic : parsed.diagnostics) {
            writer.string(gc::source_diagnostic_name(diagnostic.code));
            writer.string(diagnostic.path);
            writer.string(diagnostic.detail);
        }
        return writer.overflowed() ? 0u : writer.size();
    }

    const auto compiled = gc::compile(parsed.source);
    if (!compiled) {
        writer.u8(static_cast<std::uint8_t>(ContentStatus::content_rejected));
        writer.u16(static_cast<std::uint16_t>(
            compiled.diagnostics.size() > 0xffffu ? 0xffffu
                                                  : compiled.diagnostics.size()
        ));
        for (const auto& diagnostic : compiled.diagnostics) {
            writer.string(gc::diagnostic_name(diagnostic.code));
            writer.string(diagnostic.path);
            writer.string(std::string_view{});
        }
        return writer.overflowed() ? 0u : writer.size();
    }

    writer.u8(static_cast<std::uint8_t>(ContentStatus::compiled));
    writer.u16(0u);
    // The style, and the identities a cartridge names its opening board and
    // its campaign by. They are the compiler's answers, not the caller's: a
    // second derivation of a stable identity is a second chance to derive it
    // differently, which is the same reason `gl_core_stable_content_id` is
    // bound at all.
    writer.u8(parsed.source.character_style);
    writer.u16(static_cast<std::uint16_t>(parsed.source.encounters.size()));
    for (const auto& encounter : parsed.source.encounters) {
        writer.u64(encounter.id);
    }
    writer.u16(static_cast<std::uint16_t>(parsed.source.campaigns.size()));
    for (const auto& campaign_entry : parsed.source.campaigns) {
        writer.u64(campaign_entry.id);
    }
    writer.u32(static_cast<std::uint32_t>(compiled.package.size()));
    writer.raw(compiled.package.data(), compiled.package.size());
    return writer.overflowed() ? 0u : writer.size();
}

}  // extern "C"
