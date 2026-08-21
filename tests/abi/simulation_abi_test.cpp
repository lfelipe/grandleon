// SPDX-License-Identifier: MIT
// Native round-trip test for the WebAssembly binding surface.
//
// platform/web/src/simulation_abi.cpp is compiled for the host and driven
// through the same exported C functions the browser calls, so the wire format,
// the size constants, and the boundary statuses are proved without a browser
// in the loop. The reference results come from the simulation library itself,
// never from restated constants, so this suite cannot drift from the engine.

#include <grandleon/core/content_identity.hpp>
#include <grandleon/package_format/package.hpp>
#include <grandleon/package_runtime/campaign.hpp>
#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace pf = grandleon::package_format;
namespace pr = grandleon::package_runtime;
namespace sim = grandleon::simulation;

extern "C" {
std::uintptr_t gl_sim_io_buffer(void);
std::uint32_t gl_sim_io_capacity(void);
std::uint32_t gl_sim_create(std::uint32_t payload_size);
void gl_sim_destroy(std::uint32_t handle);
std::uint32_t gl_sim_apply(std::uint32_t handle, std::uint32_t payload_size);
std::uint32_t gl_sim_snapshot(std::uint32_t handle);
std::uint64_t gl_sim_canonical_hash(std::uint32_t handle);
std::uint32_t gl_sim_forecast_attack(
    std::uint32_t handle,
    std::uint64_t attacker_id,
    std::uint64_t target_id,
    std::uint64_t weapon_id
);
std::uint32_t gl_sim_reachable_tiles(
    std::uint32_t handle,
    std::uint64_t unit_id
);
std::uint32_t gl_sim_danger_tiles(std::uint32_t handle, std::uint32_t side);
std::uint32_t gl_sim_aimable_tiles(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint32_t kind,
    std::uint64_t weapon_id,
    std::uint64_t ability_id
);
std::uint32_t gl_sim_gesture_available(
    std::uint32_t handle,
    std::uint64_t unit_id,
    std::uint32_t kind,
    std::uint64_t weapon_id,
    std::uint64_t ability_id
);
std::uint32_t gl_sim_area_tiles(
    std::uint32_t handle,
    std::uint64_t ability_id,
    std::int32_t x,
    std::int32_t y
);
std::uint32_t gl_ai_decide(std::uint32_t handle, std::uint32_t payload_size);
std::uint64_t gl_core_stable_content_id(std::uint32_t length);
std::uintptr_t gl_content_buffer(void);
std::uint32_t gl_content_capacity(void);
std::uint32_t gl_content_compile(std::uint32_t source_size);
std::uint32_t gl_sim_create_error_name(std::uint32_t error);
std::uint32_t gl_sim_command_error_name(std::uint32_t error);
std::uint32_t gl_campaign_create(std::uint32_t payload_size);
void gl_campaign_destroy(std::uint32_t handle);
std::uint32_t gl_campaign_add_dialogue(
    std::uint32_t handle,
    std::uint32_t payload_size
);
std::uint32_t gl_campaign_state(std::uint32_t handle);
std::uint32_t gl_campaign_advance(
    std::uint32_t handle,
    std::uint32_t payload_size
);
std::uint32_t gl_campaign_advance_story(std::uint32_t handle);
std::uint32_t gl_campaign_dialogue(
    std::uint32_t handle,
    std::uint64_t dialogue_id
);
std::uint32_t gl_campaign_error_name(std::uint32_t error);
std::uint32_t gl_campaign_dialogue_error_name(std::uint32_t error);
}

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Mirrors AbiStatus in simulation_abi.cpp.
constexpr std::uint8_t abi_ok = 0;
constexpr std::uint8_t abi_malformed_payload = 1;
constexpr std::uint8_t abi_unknown_handle = 2;
constexpr std::uint8_t abi_buffer_overflow = 3;

std::uint8_t* buffer() {
    return reinterpret_cast<std::uint8_t*>(gl_sim_io_buffer());
}

// Little-endian encoder into the shared scratch buffer, mirroring the
// TypeScript Cursor byte for byte.
class Writer final {
public:
    [[nodiscard]] std::uint32_t size() const {
        return static_cast<std::uint32_t>(cursor_);
    }

    void u8(std::uint8_t value) { buffer()[cursor_++] = value; }

    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xffU));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xffU));
    }

    void u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value & 0xffffU));
        u16(static_cast<std::uint16_t>((value >> 16) & 0xffffU));
    }

    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value & 0xffffffffU));
        u32(static_cast<std::uint32_t>((value >> 32) & 0xffffffffU));
    }

    void i16(std::int16_t value) {
        u16(static_cast<std::uint16_t>(value));
    }

private:
    std::size_t cursor_{};
};

class Reader final {
public:
    [[nodiscard]] std::uint8_t u8() { return buffer()[cursor_++]; }

    [[nodiscard]] std::uint16_t u16() {
        const std::uint16_t low = u8();
        const std::uint16_t high = u8();
        return static_cast<std::uint16_t>(low | (high << 8));
    }

    [[nodiscard]] std::uint32_t u32() {
        const std::uint32_t low = u16();
        const std::uint32_t high = u16();
        return low | (high << 16);
    }

    [[nodiscard]] std::uint64_t u64() {
        const std::uint64_t low = u32();
        const std::uint64_t high = u32();
        return low | (high << 32);
    }

    [[nodiscard]] std::int16_t i16() {
        return static_cast<std::int16_t>(u16());
    }

    // Steps past bytes this assertion is not about, such as a name or a line
    // of text, to reach the field that is. Separate from `u8()` so that
    // discarding a decoded value stays an error everywhere else: a reader that
    // silently dropped a byte it meant to check would read the rest of the
    // record off by one and compare the wrong thing.
    void skip(std::size_t count) { cursor_ += count; }

private:
    std::size_t cursor_{};
};

void write_unit(Writer& writer, const sim::UnitDefinition& unit) {
    writer.u64(unit.id);
    writer.u64(unit.unit_type_id);
    writer.u8(unit.side == sim::Side::first ? 0U : 1U);
    writer.i16(unit.position.x);
    writer.i16(unit.position.y);
    writer.i16(unit.health);
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
    writer.u8(unit.acts_after_attacking ? 1U : 0U);
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
    writer.u8(unit.crossings);
    writer.u8(unit.accuracy);
    writer.u32(static_cast<std::uint32_t>(unit.item_ids.size()));
    for (std::size_t index = 0; index < unit.item_ids.size(); ++index) {
        writer.u64(unit.item_ids[index]);
        writer.u16(
            index < unit.item_counts.size() ? unit.item_counts[index] : 1U
        );
    }
    writer.u64(unit.drop_item_id);
    writer.u8(unit.drop_chance);
    writer.u8(unit.reach_bonus);
    writer.u64(unit.talk_record_id);
    writer.u16(static_cast<std::uint16_t>(unit.arrival_round));
    writer.u16(static_cast<std::uint16_t>(unit.arrival_every));
    writer.u8(static_cast<std::uint8_t>(unit.arrival_times));
}

// Encodes a full definition exactly as the browser binding does and returns
// the payload size. 82 fixed bytes per unit record plus 8 per ability id, 8 per
// carried weapon id and 10 per carried item, then the board's passability after
// the turn order.
std::uint32_t write_definition(const sim::EncounterDefinition& definition) {
    Writer writer;
    writer.u16(definition.width);
    writer.u16(definition.height);
    writer.u32(static_cast<std::uint32_t>(definition.units.size()));
    for (const sim::UnitDefinition& unit : definition.units) {
        write_unit(writer, unit);
    }
    writer.u32(static_cast<std::uint32_t>(definition.abilities.size()));
    for (const sim::AbilityDefinition& ability : definition.abilities) {
        writer.u64(ability.id);
        writer.u8(static_cast<std::uint8_t>(ability.kind));
        writer.u8(static_cast<std::uint8_t>(ability.damage_type));
        writer.u8(static_cast<std::uint8_t>(ability.area));
        writer.i16(ability.power);
        writer.u8(ability.minimum_reach);
        writer.u8(ability.maximum_reach);
        writer.u8(ability.radius);
        writer.u8(ability.accuracy);
    }
    writer.u32(static_cast<std::uint32_t>(definition.weapons.size()));
    for (const sim::WeaponDefinition& weapon : definition.weapons) {
        writer.u64(weapon.id);
        writer.i16(weapon.power);
        writer.u8(weapon.minimum_reach);
        writer.u8(weapon.maximum_reach);
        writer.u8(weapon.accuracy);
        writer.u64(weapon.weapon_type);
    }
    // Which kinds of weapon beat which, and what beating them is worth. A
    // board with no triangle in it writes a count of zero.
    writer.u32(static_cast<std::uint32_t>(definition.weapon_types.size()));
    for (const sim::WeaponTypeDefinition& kind : definition.weapon_types) {
        writer.u64(kind.id);
        writer.u32(static_cast<std::uint32_t>(kind.strong_against.size()));
        for (const sim::ContentId beaten : kind.strong_against) {
            writer.u64(beaten);
        }
        writer.i16(kind.damage);
        writer.u8(kind.accuracy);
    }
    writer.u32(static_cast<std::uint32_t>(definition.items.size()));
    for (const sim::ItemDefinition& item : definition.items) {
        writer.u64(item.id);
        writer.u8(static_cast<std::uint8_t>(item.kind));
        writer.i16(item.power);
    }
    writer.u32(static_cast<std::uint32_t>(definition.objectives.size()));
    for (const sim::ObjectiveDefinition& objective : definition.objectives) {
        writer.u64(objective.id);
        writer.u8(static_cast<std::uint8_t>(objective.kind));
        writer.u8(objective.side == sim::Side::first ? 0U : 1U);
        writer.u64(objective.target_unit_id);
        writer.u32(objective.round_count);
    }
    writer.u8(static_cast<std::uint8_t>(definition.turn_order));
    writer.u32(static_cast<std::uint32_t>(definition.terrain.size()));
    for (const sim::Terrain cell : definition.terrain) {
        writer.u8(static_cast<std::uint8_t>(cell));
    }
    writer.u32(static_cast<std::uint32_t>(definition.movement_cost.size()));
    for (const std::uint8_t cell : definition.movement_cost) {
        writer.u8(cell);
    }
    // The deployment region, counted rather than optional. Zero is a board
    // with no phase, which is every board this fixture builds but one.
    writer.u32(
        static_cast<std::uint32_t>(definition.deployment_tiles.size())
    );
    for (const sim::Position& tile : definition.deployment_tiles) {
        writer.i16(tile.x);
        writer.i16(tile.y);
    }
    return writer.size();
}

std::uint32_t write_command(const sim::Command& command) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(command.type));
    writer.u64(command.unit_id);
    writer.i16(command.destination.x);
    writer.i16(command.destination.y);
    writer.u64(command.target_id);
    writer.u64(command.ability_id);
    writer.u64(command.weapon_id);
    writer.u64(command.item_id);
    return writer.size();
}

// The shared reference vector from tests/simulation/encounter_test.cpp, with
// weapon power and resistance exercised on top so every widened field crosses
// the boundary with a distinguishable value.
sim::EncounterDefinition reference_definition() {
    sim::EncounterDefinition definition;
    definition.width = 4;
    definition.height = 3;
    definition.units = {
        {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1, 0, 0, 0, 0, 0, 1, 1,
         1, false, 1, 1, {}},
        {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1, {}},
    };
    return definition;
}

const sim::Command reference_commands[] = {
    {sim::CommandType::move, 10, {1, 1}, 0, 0},
    {sim::CommandType::wait, 20, {}, 0, 0},
    {sim::CommandType::attack, 10, {}, 20, 0},
    {sim::CommandType::wait, 20, {}, 0, 0},
    {sim::CommandType::attack, 10, {}, 20, 0},
};

void unit_record_size_is_exact() {
    // A zero-slack payload: every declared byte is consumed and nothing more.
    // If unit_record_size ever disagrees with the parse loop again, one of the
    // two checks below fails.
    const auto definition = reference_definition();
    const std::uint32_t exact = write_definition(definition);
    // The trailing counts are one per list the definition carries, and each is
    // four bytes when the list is empty. A list added to the wire adds one of
    // them here, which is what makes this arithmetic worth writing out.
    expect(
        exact == 8U + 2U * 82U + 4U + 4U + 4U + 4U + 4U + 1U + 4U + 4U + 4U,
        "the encoded unit record is 82 bytes"
    );
    const std::uint32_t handle = gl_sim_create(exact);
    expect(handle != 0U, "an exact-size payload creates an encounter");
    gl_sim_destroy(handle);

    // One byte short: the last byte of the region count falls off the end,
    // the reader latches its overflow, and no encounter is created.
    write_definition(definition);
    expect(
        gl_sim_create(exact - 1U) == 0U && buffer()[0] == abi_malformed_payload,
        "a one-byte-short payload is a boundary failure"
    );

    // Shorter than the declared unit records: rejected before anything is
    // reserved.
    write_definition(definition);
    expect(
        gl_sim_create(8U + 2U * 47U - 1U) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "a payload shorter than its declared units is rejected"
    );
}

void replays_the_reference_vector() {
    const auto definition = reference_definition();
    auto native = sim::create_encounter(definition);
    expect(static_cast<bool>(native), "native reference encounter is valid");

    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(handle != 0U, "boundary reference encounter is valid");
    expect(
        gl_sim_canonical_hash(handle) == native.encounter.canonical_hash(),
        "initial hashes agree across the boundary"
    );

    for (const sim::Command& command : reference_commands) {
        const auto native_result = native.encounter.apply(command);
        const std::uint32_t written =
            gl_sim_apply(handle, write_command(command));
        expect(written != 0U, "boundary command is accepted");
        Reader reader;
        expect(reader.u8() == abi_ok, "boundary command status is ok");
        expect(
            reader.u8() == static_cast<std::uint8_t>(native_result.error),
            "boundary and native command errors agree"
        );
        const std::uint32_t events = reader.u32();
        expect(
            events == native_result.events.size(),
            "boundary and native event counts agree"
        );
        for (std::uint32_t index = 0; index < events; ++index) {
            const sim::Event& event = native_result.events[index];
            expect(
                reader.u8() == static_cast<std::uint8_t>(event.type) &&
                    reader.u64() == event.unit_id &&
                    reader.u64() == event.related_unit_id &&
                    reader.i16() == event.position.x &&
                    reader.i16() == event.position.y &&
                    reader.i16() == event.amount &&
                    reader.u8() == static_cast<std::uint8_t>(event.outcome) &&
                    reader.u64() == event.content_id,
                "boundary event round-trips field by field"
            );
        }
    }
    expect(
        gl_sim_canonical_hash(handle) == native.encounter.canonical_hash(),
        "completed hashes agree across the boundary"
    );
    gl_sim_destroy(handle);
}

void snapshots_round_trip_every_field() {
    // Distinguishable values everywhere, so a transposed field cannot pass.
    sim::EncounterDefinition definition;
    definition.width = 9;
    definition.height = 7;
    definition.units = {
        {11, 110, sim::Side::first, {1, 2}, 31, 4, 3, 2, 5, 0, 0, 0, 0, 6, 2,
         7, true, 2, 4, {77}},
        {22, 220, sim::Side::second, {8, 6}, 19, 3, 0, 1, 2, 0, 0, 0, 0, 1, 1,
         1, false, 1, 1, {}},
    };
    definition.abilities = {
        {77, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::diamond, 6, 1, 3, 2}
    };
    definition.objectives = {
        {90, sim::ObjectiveKind::defeat_target, sim::Side::first, 22}
    };
    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(handle != 0U, "field round-trip encounter is valid");

    const std::uint32_t written = gl_sim_snapshot(handle);
    expect(written != 0U, "snapshot is written");
    const sim::EncounterSnapshot native = [&definition] {
        auto created = sim::create_encounter(definition);
        return created.encounter.snapshot();
    }();

    Reader reader;
    expect(reader.u8() == abi_ok, "snapshot status is ok");
    expect(
        reader.u16() == native.width && reader.u16() == native.height,
        "snapshot dimensions round-trip"
    );
    expect(
        reader.u8() == static_cast<std::uint8_t>(native.active_side) &&
            reader.u8() == static_cast<std::uint8_t>(native.outcome) &&
            reader.u64() == native.active_unit_id &&
            reader.u8() == native.remaining_action_points &&
            reader.u32() == native.round &&
            reader.u64() == native.activation_count,
        "snapshot header round-trips"
    );
    const std::uint32_t units = reader.u32();
    expect(units == native.units.size(), "snapshot unit count round-trips");
    for (std::uint32_t index = 0; index < units; ++index) {
        const sim::UnitSnapshot& unit = native.units[index];
        expect(
            reader.u64() == unit.id && reader.u64() == unit.unit_type_id &&
                reader.u8() == static_cast<std::uint8_t>(unit.side) &&
                reader.i16() == unit.position.x &&
                reader.i16() == unit.position.y &&
                reader.i16() == unit.health &&
                reader.i16() == unit.maximum_health &&
                reader.i16() == unit.strength &&
                reader.i16() == unit.power &&
                reader.i16() == unit.defense &&
                reader.i16() == unit.resistance &&
                reader.i16() == unit.skill && reader.i16() == unit.luck &&
                reader.i16() == unit.evasion && reader.i16() == unit.magic &&
                reader.u8() == unit.movement &&
                reader.u8() == unit.action_points &&
                reader.u8() == unit.speed &&
                (reader.u8() != 0U) == unit.acts_after_attacking &&
                (reader.u8() != 0U) == unit.has_acted &&
                reader.u8() == unit.minimum_reach &&
                reader.u8() == unit.maximum_reach,
            "snapshot unit fields round-trip in wire order"
        );
        const std::uint32_t abilities = reader.u32();
        expect(
            abilities == unit.ability_ids.size(),
            "snapshot ability count round-trips"
        );
        for (std::uint32_t slot = 0; slot < abilities; ++slot) {
            expect(
                reader.u64() == unit.ability_ids[slot],
                "snapshot ability ids round-trip"
            );
        }
        const std::uint32_t carried = reader.u32();
        expect(
            carried == unit.weapon_ids.size(),
            "snapshot carried weapon count round-trips"
        );
        for (std::uint32_t slot = 0; slot < carried; ++slot) {
            expect(
                reader.u64() == unit.weapon_ids[slot],
                "snapshot carried weapon ids round-trip"
            );
        }
        const std::uint32_t packed = reader.u32();
        expect(
            packed == unit.item_ids.size(),
            "snapshot carried item count round-trips"
        );
        for (std::uint32_t slot = 0; slot < packed; ++slot) {
            expect(
                reader.u64() == unit.item_ids[slot] &&
                    reader.u16() == unit.item_counts[slot],
                "snapshot carried items round-trip with what is left of them"
            );
        }
        expect(
            reader.u64() == unit.drop_item_id &&
                reader.u8() == unit.drop_chance,
            "and what it would leave behind, at the tail of the record"
        );
        expect(
            reader.u8() == unit.reach_bonus,
            "and what it adds to the reach of what it holds, after that"
        );
        expect(
            reader.u64() == unit.talk_record_id &&
                (reader.u8() != 0U) == unit.departed,
            "and what talking to it records, and whether it has already gone"
        );
        expect(
            reader.u32() == unit.arrival_round &&
                (reader.u8() != 0U) == unit.arrived,
            "and when it comes in, and whether it has, after that"
        );
        expect(
            (reader.u8() != 0U) == unit.has_moved,
            "and whether it has already spent this turn's walk, after that"
        );
        expect(
            reader.u8() == unit.spent_action_points,
            "and how much of its budget its own turn has spent, after that"
        );
        expect(
            (reader.u8() != 0U) == sim::on_board(unit),
            "and the engine's own answer to whether it stands on the board, last"
        );
    }
    const std::uint32_t objectives = reader.u32();
    expect(
        objectives == native.objectives.size(),
        "snapshot objective count round-trips"
    );
    for (std::uint32_t index = 0; index < objectives; ++index) {
        expect(
            reader.u64() == native.objectives[index].id &&
                reader.u8() ==
                    static_cast<std::uint8_t>(native.objectives[index].state),
            "snapshot objectives round-trip"
        );
    }
    const std::uint32_t drops = reader.u32();
    expect(
        drops == native.drops.size(),
        "snapshot drop count round-trips at the tail of the record"
    );
    for (std::uint32_t index = 0; index < drops; ++index) {
        expect(
            reader.u64() == native.drops[index].unit_id &&
                reader.u64() == native.drops[index].claimant_id &&
                reader.u64() == native.drops[index].item_id,
            "and what fell, whose body it came off, and who claims it"
        );
    }
    expect(
        (reader.u8() != 0U) == native.deploying,
        "the deployment marker round-trips after the drops"
    );
    const std::uint32_t zone = reader.u32();
    expect(
        zone == native.deployment_tiles.size(),
        "and so does the size of the region"
    );
    for (std::uint32_t index = 0; index < zone; ++index) {
        expect(
            reader.i16() == native.deployment_tiles[index].x &&
                reader.i16() == native.deployment_tiles[index].y,
            "and every tile of it"
        );
    }
    gl_sim_destroy(handle);
}

void forecasts_across_the_boundary() {
    const auto definition = reference_definition();
    auto native = sim::create_encounter(definition);
    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(handle != 0U, "forecast encounter is valid");

    // Out of range first, then step adjacent and compare the priced numbers.
    const auto check = [&](sim::UnitId attacker, sim::UnitId target) {
        const auto promise =
            sim::forecast_attack(native.encounter.snapshot(), attacker, target);
        const std::uint32_t written =
            gl_sim_forecast_attack(handle, attacker, target, 0);
        expect(written == 15U, "forecast writes its fixed-size record");
        Reader reader;
        expect(reader.u8() == abi_ok, "forecast status is ok");
        expect(
            reader.u8() == static_cast<std::uint8_t>(promise.error) &&
                reader.i16() == promise.damage &&
                reader.i16() == promise.target_health_after &&
                (reader.u8() != 0U) == promise.lethal,
            "forecast agrees with the native forecast field by field"
        );
        // The counter half crosses the same boundary, so the browser draws the
        // promise rather than half of it.
        expect(
            (reader.u8() != 0U) == promise.counter &&
                reader.i16() == promise.counter_damage &&
                reader.i16() == promise.attacker_health_after &&
                (reader.u8() != 0U) == promise.counter_lethal,
            "forecast carries the counter across the boundary"
        );
        // And the two chances, which are the whole of what the forecast now
        // promises: a browser that cannot read them cannot draw the rule.
        expect(
            reader.u8() == promise.hit_chance &&
                reader.u8() == promise.counter_chance,
            "forecast carries both chances across the boundary"
        );
    };
    check(10, 20);
    check(10, 10);
    check(99, 20);

    const sim::Command approach{sim::CommandType::move, 10, {1, 1}, 0, 0};
    expect(
        static_cast<bool>(native.encounter.apply(approach)) &&
            gl_sim_apply(handle, write_command(approach)) != 0U,
        "both sides of the boundary step adjacent"
    );
    check(20, 10);
    gl_sim_destroy(handle);
}

// The tile-list record every spatial query writes, back into positions. Shared
// rather than written once per suite because the record's exact size is part
// of what is being proved: a query that wrote a tile too few would still decode
// into a plausible list, and only the length check catches it.
std::vector<sim::Position> read_tiles(std::uint32_t written) {
    std::vector<sim::Position> read;
    expect(written != 0U, "a tile query writes a record");
    Reader reader;
    expect(reader.u8() == abi_ok, "tile query status is ok");
    const std::uint32_t count = reader.u32();
    expect(written == 5U + count * 4U, "the tile record is exactly sized");
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::int16_t x = reader.i16();
        read.push_back({x, reader.i16()});
    }
    return read;
}

// The two read-only range queries. A browser client draws movement and danger
// from these, so the boundary must hand back exactly the engine's own tiles,
// in the engine's own order. Otherwise the editor's board and the console's
// board light different squares from the same rule.
void queries_reachability_across_the_boundary() {
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 5;
    definition.units = {
        // A mover with two steps, walled into the left column by three
        // opponents (a walk files past its own side and is stopped by the
        // other, and all three of these are the other); an executioner; an
        // archer whose weapon has a two-tile minimum; and a sentry about to be
        // killed.
        {10, 100, sim::Side::first, {0, 2}, 9, 4, 0, 1, 0, 0, 0, 0, 0, 2, 2, 1,
         false, 1, 1, {}},
        {11, 100, sim::Side::first, {6, 1}, 9, 9, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1,
         false, 1, 1, {}},
        {20, 200, sim::Side::second, {1, 1}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}},
        {21, 200, sim::Side::second, {1, 3}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}},
        {24, 200, sim::Side::second, {1, 2}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}},
        {22, 201, sim::Side::second, {5, 2}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1,
         1, false, 2, 3, {}},
        // Two points: one to walk with and one to strike with, so this sentry
        // threatens tiles its own band cannot reach and losing it is visible
        // in the danger zone rather than covered by the units beside it.
        {23, 200, sim::Side::second, {6, 0}, 1, 3, 0, 1, 0, 0, 0, 0, 0, 4, 2,
         1, false, 1, 1, {}},
    };
    auto native = sim::create_encounter(definition);
    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(
        native.error == sim::CreateError::none && handle != 0U,
        "reachability encounter is valid"
    );

    const auto snapshot = native.encounter.snapshot();
    expect(
        read_tiles(gl_sim_reachable_tiles(handle, 10)) ==
            sim::reachable_tiles(snapshot, 10),
        "reachable tiles cross the boundary tile for tile, in order"
    );
    // The pinched mover has somewhere to stand but cannot slip past any of the
    // three opponents, so the query is shaped by who is in the way rather than
    // by allowance.
    const auto pinched = sim::reachable_tiles(snapshot, 10);
    expect(!pinched.empty(), "the pinched mover still has somewhere to stand");
    expect(
        std::none_of(
            pinched.begin(),
            pinched.end(),
            [](sim::Position tile) { return tile.x > 0; }
        ),
        "no tile behind an opponent is stepped through"
    );
    const auto threatened = read_tiles(gl_sim_danger_tiles(handle, 1U));
    expect(
        threatened == sim::danger_tiles(snapshot, sim::Side::second),
        "danger tiles cross the boundary tile for tile, in order"
    );
    expect(
        read_tiles(gl_sim_danger_tiles(handle, 0U)) ==
            sim::danger_tiles(snapshot, sim::Side::first),
        "the side argument selects the side it names"
    );
    expect(
        read_tiles(gl_sim_reachable_tiles(handle, 999)).empty(),
        "an unknown unit reaches nothing across the boundary"
    );
    expect(
        read_tiles(gl_sim_reachable_tiles(handle, 20)).empty(),
        "a unit with no movement reaches nothing across the boundary"
    );

    const std::uint64_t before = gl_sim_canonical_hash(handle);
    (void)gl_sim_reachable_tiles(handle, 10);
    (void)gl_sim_danger_tiles(handle, 1U);
    expect(
        gl_sim_canonical_hash(handle) == before,
        "querying range changes no state"
    );

    // Kill the sentry on both sides of the boundary: a defeated unit reaches
    // nothing and takes the tiles only it threatened out of the danger zone.
    const sim::Command execute{sim::CommandType::attack, 11, {}, 23, 0};
    expect(
        static_cast<bool>(native.encounter.apply(execute)) &&
            gl_sim_apply(handle, write_command(execute)) != 0U,
        "both sides of the boundary defeat the sentry"
    );
    expect(
        read_tiles(gl_sim_reachable_tiles(handle, 23)).empty(),
        "a defeated unit reaches nothing across the boundary"
    );
    const auto after = read_tiles(gl_sim_danger_tiles(handle, 1U));
    expect(
        after == sim::danger_tiles(native.encounter.snapshot(), sim::Side::second),
        "the danger zone still agrees once a unit is defeated"
    );
    expect(
        after.size() < threatened.size(),
        "a defeated unit stops threatening the tiles only it could strike"
    );
    gl_sim_destroy(handle);
}

// The three aiming queries. A client holds one aim between choosing a menu row
// and pressing confirm, and these are what it asks about that aim: which tiles
// the aim may be committed at, whether the row is offered at all, and what an
// area cast would cover. The browser draws its board from them, so the boundary
// has to hand back the engine's own answers rather than an approximation.
// Lighting a square the engine will refuse is the one disagreement a selection
// helper must not have.
void queries_aiming_across_the_boundary() {
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 5;
    definition.abilities = {
        // A diamond wide enough to splash, and a single-tile cast nobody on
        // this board knows: one to read a splash from, one to prove that an
        // ability the caster does not know is refused before its aim is even
        // looked at.
        {800, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::diamond, 3, 1, 3, 1, 100},
        {801, sim::AbilityKind::restore, sim::DamageType::physical,
         sim::AreaShape::single, 4, 1, 1, 0, 100},
    };
    definition.weapons = {
        {901, 3, 1, 1, 100},
        // A bow whose own band stops one tile short of the far body, so the
        // striker's reach bonus is the only thing that brings it into range.
        {900, 2, 2, 3, 100},
    };
    definition.units = {
        // The aimer: a dagger in hand, a bow beside it, one spell known, and
        // two points so nothing asked of it is refused for its budget.
        {10, 100, sim::Side::first, {2, 2}, 9, 4, 0, 1, 0, 0, 0, 0, 0, 2, 2, 1,
         false, 1, 1, {800}, {901, 900}},
        // Somebody of the same side alone in a corner. Every gesture is
        // available to it and none of them lights a tile, which is the pair of
        // facts the two queries are told apart by.
        {11, 100, sim::Side::first, {0, 4}, 9, 4, 0, 1, 0, 0, 0, 0, 0, 1, 2, 1,
         false, 1, 1, {}, {901}},
        // Adjacent, so the dagger reaches, and carrying something to say, so a
        // talk lights somebody too.
        {20, 200, sim::Side::second, {3, 2}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1,
         1, false, 1, 1, {}, {901}},
        // Four tiles off: outside the dagger's band, and inside the bow's only
        // once the aimer's own reach has widened it.
        {21, 200, sim::Side::second, {6, 2}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1,
         1, false, 1, 1, {}, {901}},
    };
    definition.units[0].reach_bonus = 1;
    definition.units[2].talk_record_id = 7000;

    auto native = sim::create_encounter(definition);
    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(
        native.error == sim::CreateError::none && handle != 0U,
        "aiming encounter is valid"
    );
    const auto snapshot = native.encounter.snapshot();

    // Every comparison below is against the engine's own answer for the same
    // aim, never against a tile list restated here, so this suite cannot drift
    // from the rule it is guarding.
    const auto aimable = [&](sim::UnitId unit_id,
                             const sim::AimedGesture& gesture) {
        const auto crossed = read_tiles(gl_sim_aimable_tiles(
            handle, unit_id, static_cast<std::uint32_t>(gesture.kind),
            gesture.weapon_id, gesture.ability_id
        ));
        expect(
            crossed == sim::aimable_tiles(
                snapshot, unit_id, gesture, definition.weapons,
                definition.abilities
            ),
            "aimable tiles cross the boundary tile for tile, in order"
        );
        return crossed;
    };
    const auto available = [&](sim::UnitId unit_id,
                               const sim::AimedGesture& gesture) {
        const std::uint32_t written = gl_sim_gesture_available(
            handle, unit_id, static_cast<std::uint32_t>(gesture.kind),
            gesture.weapon_id, gesture.ability_id
        );
        expect(written == 2U, "the availability record is exactly sized");
        Reader reader;
        expect(reader.u8() == abi_ok, "availability status is ok");
        const bool answer = reader.u8() != 0U;
        expect(
            answer == sim::gesture_available(
                snapshot, unit_id, gesture, definition.weapons,
                definition.abilities
            ),
            "availability crosses the boundary unchanged"
        );
        return answer;
    };

    const sim::AimedGesture walk{sim::Gesture::walk, 0, 0};
    const sim::AimedGesture in_hand{sim::Gesture::strike, 0, 0};
    const sim::AimedGesture drawn_bow{sim::Gesture::strike, 900, 0};
    const sim::AimedGesture uncarried{sim::Gesture::strike, 999, 0};
    const sim::AimedGesture spark{sim::Gesture::cast, 0, 800};
    const sim::AimedGesture unlearned{sim::Gesture::cast, 0, 801};
    const sim::AimedGesture talk{sim::Gesture::talk, 0, 0};

    expect(
        aimable(10, walk) == sim::reachable_tiles(snapshot, 10),
        "a walk aims at exactly the tiles the reachability query names"
    );
    // The dagger reaches the adjacent body and nothing else; the bow reaches
    // the far one only because the aimer's reach bonus widened its band. Both
    // are asserted as tiles rather than as counts, so a band that drifted by
    // one would fail here rather than pass on a coincidence.
    expect(
        aimable(10, in_hand) == std::vector<sim::Position>{{3, 2}},
        "the weapon in hand lights the body it can actually reach"
    );
    expect(
        aimable(10, drawn_bow) == std::vector<sim::Position>{{6, 2}},
        "the drawn bow lights the body only the widened band reaches"
    );
    expect(aimable(10, uncarried).empty(), "an uncarried weapon lights nothing");
    expect(
        aimable(10, talk) == std::vector<sim::Position>{{3, 2}},
        "a talk lights the neighbour with something to say"
    );
    const auto cast_band = aimable(10, spark);
    expect(
        cast_band.size() > 1U &&
            std::find(cast_band.begin(), cast_band.end(), sim::Position{2, 0})
                != cast_band.end(),
        "a cast lights empty ground inside its band"
    );
    expect(aimable(10, unlearned).empty(), "an unknown spell lights nothing");

    // Available and empty, which is the distinction the two queries exist to
    // keep apart: the corner character may strike, and there is nobody to
    // strike, and those are two different things to tell a player.
    expect(available(11, in_hand), "a character alone may still strike");
    expect(
        aimable(11, in_hand).empty(),
        "a strike with nobody in reach lights nothing"
    );
    expect(!available(10, unlearned), "an unknown spell is not offered");
    expect(!available(10, uncarried), "an uncarried weapon is not offered");
    expect(available(10, spark), "a known spell is offered");
    expect(!available(20, walk), "the side that is not acting is offered none");
    expect(!available(999, talk), "an unknown character is offered none");

    const auto splash = read_tiles(gl_sim_area_tiles(handle, 800, 3, 2));
    expect(
        splash == sim::area_tiles(snapshot, 800, {3, 2}, definition.abilities),
        "an area crosses the boundary tile for tile, in order"
    );
    expect(splash.size() == 5U, "the diamond covers its centre and its four");
    expect(
        read_tiles(gl_sim_area_tiles(handle, 800, 0, 0)).size() == 3U,
        "an area against the corner is clipped to the board"
    );
    expect(
        read_tiles(gl_sim_area_tiles(handle, 801, 3, 2)).empty(),
        "a single-tile cast splashes nothing"
    );
    expect(
        read_tiles(gl_sim_area_tiles(handle, 555, 3, 2)).empty(),
        "an unknown ability splashes nothing"
    );

    // A gesture value outside the enum is a boundary failure rather than an
    // unavailable gesture: a caller that sent it never had an aim to ask about,
    // and answering "no" would look like a rule.
    expect(
        gl_sim_aimable_tiles(handle, 10, 4U, 0, 0) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "an out-of-range gesture is rejected at the boundary"
    );
    expect(
        gl_sim_gesture_available(handle, 10, 4U, 0, 0) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "the availability query rejects an out-of-range gesture too"
    );
    // Truncating a centre would answer about a different tile, so it is
    // refused rather than wrapped.
    expect(
        gl_sim_area_tiles(handle, 800, 40000, 2) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "a centre no board coordinate could hold is rejected"
    );

    const std::uint64_t before = gl_sim_canonical_hash(handle);
    (void)gl_sim_aimable_tiles(handle, 10, 1U, 900, 0);
    (void)gl_sim_gesture_available(handle, 10, 2U, 0, 800);
    (void)gl_sim_area_tiles(handle, 800, 3, 2);
    expect(
        gl_sim_canonical_hash(handle) == before,
        "aiming changes no state"
    );
    gl_sim_destroy(handle);
}

void proposes_plans_across_the_boundary() {
    sim::EncounterDefinition definition;
    definition.width = 8;
    definition.height = 6;
    definition.units = {
        {10, 100, sim::Side::first, {1, 1}, 9, 4, 0, 1, 0, 0, 0, 0, 0, 3, 1, 1,
         false, 1, 1, {}},
        {20, 200, sim::Side::second, {6, 4}, 9, 3, 0, 1, 0, 0, 0, 0, 0, 3, 1,
         1, false, 1, 1, {}},
    };
    const std::uint32_t handle = gl_sim_create(write_definition(definition));
    expect(handle != 0U, "decide encounter is valid");

    Writer writer;
    writer.u64(10);
    writer.u8(2U);  // pursue
    writer.u16(0U);
    const std::uint32_t written = gl_ai_decide(handle, writer.size());
    expect(written != 0U, "decide writes a proposal");
    Reader reader;
    expect(reader.u8() == abi_ok, "decide status is ok");
    expect(reader.u8() == 1U, "the pursue proposal is actionable");
    sim::Command command;
    command.type = static_cast<sim::CommandType>(reader.u8());
    command.unit_id = reader.u64();
    command.destination.x = reader.i16();
    command.destination.y = reader.i16();
    command.target_id = reader.u64();
    command.ability_id = reader.u64();
    command.weapon_id = reader.u64();
    const std::uint32_t applied = gl_sim_apply(handle, write_command(command));
    expect(applied != 0U, "the proposal crosses back to apply");
    Reader result;
    expect(
        result.u8() == abi_ok && result.u8() == 0U,
        "the simulation accepts the boundary proposal"
    );

    // A malformed behaviour byte is a boundary failure, not a game error.
    Writer bad;
    bad.u64(10);
    bad.u8(9U);
    bad.u16(0U);
    expect(
        gl_ai_decide(handle, bad.size()) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "an out-of-range behaviour is rejected at the boundary"
    );
    gl_sim_destroy(handle);
}

void rejects_unknown_handles() {
    expect(
        gl_sim_apply(9999U, write_command({sim::CommandType::wait, 1, {}, 0, 0}))
                == 0U &&
            buffer()[0] == abi_unknown_handle,
        "apply rejects an unknown handle"
    );
    expect(
        gl_sim_snapshot(9999U) == 0U && buffer()[0] == abi_unknown_handle,
        "snapshot rejects an unknown handle"
    );
    expect(
        gl_sim_forecast_attack(9999U, 1, 2, 0) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "forecast rejects an unknown handle"
    );
    expect(
        gl_sim_reachable_tiles(9999U, 1) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "the reachability query rejects an unknown handle"
    );
    expect(
        gl_sim_danger_tiles(9999U, 0U) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "the danger query rejects an unknown handle"
    );
    expect(
        gl_sim_aimable_tiles(9999U, 1, 0U, 0, 0) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "the aiming query rejects an unknown handle"
    );
    expect(
        gl_sim_gesture_available(9999U, 1, 0U, 0, 0) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "the availability query rejects an unknown handle"
    );
    expect(
        gl_sim_area_tiles(9999U, 1, 0, 0) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "the area query rejects an unknown handle"
    );
    Writer writer;
    writer.u64(1);
    writer.u8(0U);
    writer.u16(0U);
    expect(
        gl_ai_decide(9999U, writer.size()) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "decide rejects an unknown handle"
    );
    expect(
        gl_sim_canonical_hash(9999U) == 0U,
        "an unknown handle hashes to zero"
    );
    expect(gl_sim_canonical_hash(0U) == 0U, "handle zero is never valid");
    gl_sim_destroy(9999U);  // A no-op rather than a crash.

    // A destroyed handle behaves exactly like one never issued.
    const std::uint32_t handle =
        gl_sim_create(write_definition(reference_definition()));
    expect(handle != 0U, "destroy-test encounter is valid");
    gl_sim_destroy(handle);
    expect(
        gl_sim_snapshot(handle) == 0U && buffer()[0] == abi_unknown_handle,
        "a destroyed handle is unknown"
    );
    gl_sim_destroy(handle);  // Double destroy is a no-op.
}

void latches_reader_and_writer_overflows() {
    // Truncated command: 29 bytes declared as 28.
    const std::uint32_t handle =
        gl_sim_create(write_definition(reference_definition()));
    expect(handle != 0U, "overflow-test encounter is valid");
    const std::uint32_t command_size =
        write_command({sim::CommandType::wait, 10, {}, 0, 0});
    expect(
        gl_sim_apply(handle, command_size - 1U) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "a truncated command latches the reader overflow"
    );
    // The rejected command must not have touched the encounter.
    expect(
        gl_sim_apply(handle, write_command({sim::CommandType::wait, 10, {}, 0, 0}))
            != 0U,
        "the encounter survives a malformed command"
    );
    gl_sim_destroy(handle);

    // A declared unit count the buffer cannot hold is rejected before any
    // allocation.
    Writer writer;
    writer.u16(4);
    writer.u16(3);
    writer.u32(0xffffffffU);
    expect(
        gl_sim_create(writer.size()) == 0U &&
            buffer()[0] == abi_malformed_payload,
        "an absurd declared unit count is rejected up front"
    );

    // Enough units that the snapshot cannot fit in the scratch buffer while
    // the definition still does. A snapshot unit record is wider than the
    // create record, so the count has a window rather than a floor. At 82
    // bytes for a create record and 84 for a snapshot one that window is
    // 780 to 798 units, and 790 sits in the middle of it. The response is
    // dropped whole and the status byte says buffer_overflow.
    sim::EncounterDefinition wide;
    wide.width = 256;
    wide.height = 256;
    for (std::uint32_t index = 0; index < 790U; ++index) {
        sim::UnitDefinition unit;
        unit.id = index + 1U;
        unit.unit_type_id = 100U;
        unit.side = index % 2U == 0U ? sim::Side::first : sim::Side::second;
        unit.position = {
            static_cast<std::int16_t>(index % 256U),
            static_cast<std::int16_t>(index / 256U)
        };
        unit.health = 5;
        unit.strength = 1;
        wide.units.push_back(unit);
    }
    const std::uint32_t crowded = gl_sim_create(write_definition(wide));
    expect(crowded != 0U, "the crowded encounter itself is valid");
    expect(
        gl_sim_snapshot(crowded) == 0U && buffer()[0] == abi_buffer_overflow,
        "an oversized snapshot latches the writer overflow"
    );
    expect(
        gl_sim_canonical_hash(crowded) != 0U,
        "the crowded encounter still hashes"
    );
    gl_sim_destroy(crowded);
}

void surfaces_create_errors_through_the_status_bytes() {
    auto occupied = reference_definition();
    occupied.units[1].position = occupied.units[0].position;
    expect(
        gl_sim_create(write_definition(occupied)) == 0U &&
            buffer()[0] == abi_ok &&
            buffer()[1] ==
                static_cast<std::uint8_t>(sim::CreateError::occupied_position),
        "a rule-level refusal reports ok status and the engine's error"
    );

    auto huge = reference_definition();
    huge.width = 1000;
    huge.height = 1000;
    expect(
        gl_sim_create(write_definition(huge)) == 0U &&
            buffer()[1] ==
                static_cast<std::uint8_t>(sim::CreateError::invalid_map),
        "the board-area cap crosses the boundary as invalid_map"
    );
}

void exposes_every_error_name() {
    const auto read_name = [](std::uint32_t length) {
        return std::string(
            reinterpret_cast<const char*>(buffer()),
            static_cast<std::size_t>(length)
        );
    };
    // Every enumerator, including the newest, is exposed by name; one past the
    // end returns zero, which is how the JavaScript side sizes its vocabulary.
    // The last enumerator, for the reason spelled out below for the command
    // vocabulary: a bound left in the middle of the list makes the whole test
    // stop short of the names it exists to check.
    const auto last_create =
        static_cast<std::uint32_t>(sim::CreateError::invalid_arrival);
    for (std::uint32_t code = 0; code <= last_create; ++code) {
        const std::uint32_t length = gl_sim_create_error_name(code);
        expect(length != 0U, "every create error has a name");
        expect(
            read_name(length) ==
                sim::error_name(static_cast<sim::CreateError>(code)),
            "the exposed create error name is the engine's own"
        );
    }
    expect(
        gl_sim_create_error_name(last_create + 1U) == 0U,
        "the create error vocabulary ends after its last enumerator"
    );

    // The last enumerator, not a landmark somewhere in the middle of the list.
    // This bound had been left at `no_action_points` while eight refusals were
    // appended past it, so the loop stopped before every one of them and the
    // "one past the end returns zero" check asserted nothing about the real
    // end of the vocabulary. It is `departed_unit` now and must be moved
    // whenever a refusal is appended, which is the whole job of this test:
    // every name a client shows is the engine's own.
    const auto last_command =
        static_cast<std::uint32_t>(sim::CommandError::departed_unit);
    for (std::uint32_t code = 0; code <= last_command; ++code) {
        const std::uint32_t length = gl_sim_command_error_name(code);
        expect(length != 0U, "every command error has a name");
        expect(
            read_name(length) ==
                sim::error_name(static_cast<sim::CommandError>(code)),
            "the exposed command error name is the engine's own"
        );
    }
    expect(
        gl_sim_command_error_name(last_command + 1U) == 0U,
        "the command error vocabulary ends after its last enumerator"
    );
}

// --- Campaign surface -------------------------------------------------------
//
// The reference bytes are built once by encode_campaign_record /
// encode_dialogue_record, the same encodings tools/game_content emits. They
// are fed both to package_runtime directly and across the boundary, so the
// two cursors below cannot agree by accident.

struct TestPredicate final {
    std::uint64_t objective_id{};
    std::uint8_t result{1};  // 1 satisfied, 2 failed
};

struct TestBranch final {
    std::uint64_t target_id{};
    std::uint16_t priority{};
    std::uint8_t combinator{1};  // 1 all, 2 any, 3 none
    std::vector<TestPredicate> predicates;
};

struct TestNode final {
    std::uint64_t id{};
    std::uint8_t kind{2};  // 1 encounter, 2 terminal, 3 story
    std::uint64_t encounter_id{};
    std::vector<std::uint64_t> dialogue_ids;
    std::vector<std::uint64_t> unconditional_targets;
    std::vector<TestBranch> branches;
};

void put_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffU));
}

void put_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void put_string(std::vector<std::uint8_t>& bytes, std::string_view value) {
    put_u16(bytes, static_cast<std::uint16_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::uint8_t>(character));
    }
}

std::vector<std::uint8_t> encode_campaign_record(
    std::uint64_t entry_node_id,
    const std::vector<TestNode>& nodes
) {
    std::vector<std::uint8_t> bytes;
    put_string(bytes, "Reference Campaign");
    put_u64(bytes, entry_node_id);
    put_u16(bytes, static_cast<std::uint16_t>(nodes.size()));
    for (const TestNode& node : nodes) {
        put_u64(bytes, node.id);
        bytes.push_back(node.kind);
        put_u64(bytes, node.encounter_id);
        put_u16(bytes, static_cast<std::uint16_t>(node.dialogue_ids.size()));
        for (const std::uint64_t dialogue : node.dialogue_ids) {
            put_u64(bytes, dialogue);
        }
        put_u16(
            bytes, static_cast<std::uint16_t>(node.unconditional_targets.size())
        );
        for (const std::uint64_t target : node.unconditional_targets) {
            put_u64(bytes, target);
        }
        put_u16(bytes, static_cast<std::uint16_t>(node.branches.size()));
        for (const TestBranch& branch : node.branches) {
            put_u64(bytes, branch.target_id);
            put_u16(bytes, branch.priority);
            bytes.push_back(branch.combinator);
            put_u16(bytes, static_cast<std::uint16_t>(branch.predicates.size()));
            for (const TestPredicate& predicate : branch.predicates) {
                put_u64(bytes, predicate.objective_id);
                bytes.push_back(predicate.result);
            }
        }
    }
    return bytes;
}

std::vector<std::uint8_t> encode_dialogue_record(
    std::string_view name,
    const std::vector<std::pair<std::string_view, std::string_view>>& lines,
    // Who the scene casts, and which entry speaks each line. Empty is a scene
    // that casts nobody, which writes the record it wrote before a cast
    // existed: no tail at all.
    const std::vector<std::uint64_t>& cast = {},
    const std::vector<std::uint8_t>& cast_entries = {}
) {
    std::vector<std::uint8_t> bytes;
    put_string(bytes, name);
    put_u16(bytes, static_cast<std::uint16_t>(lines.size()));
    for (const auto& line : lines) {
        put_string(bytes, line.first);
        put_string(bytes, line.second);
    }
    if (cast.empty()) return bytes;
    bytes.push_back(0);  // backdrop: this scene names none
    bytes.push_back(static_cast<std::uint8_t>(cast.size()));
    for (const std::uint64_t unit_type_id : cast) {
        put_u64(bytes, unit_type_id);
    }
    for (const std::uint8_t entry : cast_entries) bytes.push_back(entry);
    return bytes;
}

// A miniature of the Tarnholt shape: story intro, an encounter that branches
// on an objective result with an unconditional fallback, a victory scene, and
// two endings.
std::vector<TestNode> reference_flow() {
    return {
        {1, 3, 0, {100}, {2}, {}},
        {2, 1, 500, {}, {3},
         {{3, 0, 1, {{900, 1}}}, {4, 1, 1, {{900, 2}}}}},
        {3, 3, 0, {101}, {5}, {}},
        {4, 2, 0, {}, {}, {}},
        {5, 2, 0, {102}, {}, {}},
    };
}

// Writes the gl_campaign_create payload for a record and returns its size.
std::uint32_t write_campaign_payload(
    std::uint64_t campaign_id,
    const std::vector<std::uint8_t>& record,
    const std::vector<std::uint64_t>& encounter_ids
) {
    Writer writer;
    writer.u64(campaign_id);
    writer.u32(static_cast<std::uint32_t>(record.size()));
    for (const std::uint8_t byte : record) {
        writer.u8(byte);
    }
    writer.u16(static_cast<std::uint16_t>(encounter_ids.size()));
    for (const std::uint64_t id : encounter_ids) {
        writer.u64(id);
    }
    return writer.size();
}

std::uint32_t write_dialogue_payload(
    std::uint64_t dialogue_id,
    const std::vector<std::uint8_t>& record
) {
    Writer writer;
    writer.u64(dialogue_id);
    writer.u32(static_cast<std::uint32_t>(record.size()));
    for (const std::uint8_t byte : record) {
        writer.u8(byte);
    }
    return writer.size();
}

// Builds the same synthetic package the boundary builds, for the library-side
// reference cursor.
pf::LoadedPackage reference_package(
    std::uint64_t campaign_id,
    const std::vector<std::uint8_t>& record,
    const std::vector<std::uint64_t>& encounter_ids
) {
    pf::LoadedPackage package;
    package.bytes = record;
    pf::SectionView campaigns;
    campaigns.type = pf::SectionType::campaigns;
    campaigns.records.push_back(
        {campaign_id, 0U, static_cast<std::uint32_t>(record.size())}
    );
    pf::SectionView encounters;
    encounters.type = pf::SectionType::encounters;
    for (const std::uint64_t id : encounter_ids) {
        encounters.records.push_back({id, 0U, 0U});
    }
    package.sections.push_back(campaigns);
    package.sections.push_back(encounters);
    return package;
}

struct BoundaryCampaignState final {
    std::uint8_t complete{};
    std::uint64_t node_id{};
    std::uint8_t kind{};
    std::uint64_t encounter_id{};
    std::vector<std::uint64_t> dialogue_ids;
};

BoundaryCampaignState read_campaign_state(std::uint32_t handle) {
    BoundaryCampaignState state;
    const std::uint32_t written = gl_campaign_state(handle);
    expect(written != 0U, "the campaign state is written");
    Reader reader;
    expect(reader.u8() == abi_ok, "campaign state status is ok");
    state.complete = reader.u8();
    state.node_id = reader.u64();
    state.kind = reader.u8();
    state.encounter_id = reader.u64();
    const std::uint16_t dialogues = reader.u16();
    for (std::uint16_t index = 0; index < dialogues; ++index) {
        state.dialogue_ids.push_back(reader.u64());
    }
    return state;
}

std::uint8_t boundary_advance(
    std::uint32_t handle,
    sim::Outcome outcome,
    const std::vector<sim::ObjectiveResult>& objectives
) {
    Writer writer;
    writer.u8(static_cast<std::uint8_t>(outcome));
    writer.u32(static_cast<std::uint32_t>(objectives.size()));
    for (const sim::ObjectiveResult& objective : objectives) {
        writer.u64(objective.id);
        writer.u8(static_cast<std::uint8_t>(objective.state));
    }
    const std::uint32_t written = gl_campaign_advance(handle, writer.size());
    expect(written != 0U, "campaign advance is written");
    Reader reader;
    expect(reader.u8() == abi_ok, "campaign advance status is ok");
    return reader.u8();
}

// Checks that the boundary cursor and a package_runtime cursor over the same
// bytes agree at every step, including the branch chosen from objective
// results.
void campaign_cursor_matches_the_library() {
    const auto record = encode_campaign_record(1, reference_flow());
    const std::uint32_t handle =
        gl_campaign_create(write_campaign_payload(7000, record, {500}));
    expect(handle != 0U, "the reference campaign creates a cursor");

    auto loaded = pr::load_campaign(reference_package(7000, record, {500}), 7000);
    expect(static_cast<bool>(loaded), "the library loads the same bytes");
    pr::CampaignCursor cursor(std::move(loaded.definition));

    const auto agree = [&](std::string_view step) {
        const BoundaryCampaignState state = read_campaign_state(handle);
        const pr::CampaignNode& node = cursor.current();
        expect(
            state.node_id == node.id &&
                state.kind == static_cast<std::uint8_t>(node.kind) &&
                state.encounter_id == node.encounter_id &&
                (state.complete != 0U) == cursor.complete() &&
                state.dialogue_ids == node.dialogue_ids,
            step
        );
    };
    agree("both cursors open on the story entry node");

    expect(
        gl_campaign_advance_story(handle) != 0U && buffer()[1] == 0U &&
            cursor.advance_story() == pr::CampaignError::none,
        "both cursors advance past the story node"
    );
    agree("both cursors reach the encounter node");

    // The satisfied objective takes the priority-zero victory branch on both
    // sides, not the unconditional fallback.
    const std::vector<sim::ObjectiveResult> victory = {
        {900, sim::ObjectiveState::satisfied}
    };
    expect(
        boundary_advance(handle, sim::Outcome::first_side_won, victory) == 0U &&
            cursor.advance_after(sim::Outcome::first_side_won, victory) ==
                pr::CampaignError::none,
        "both cursors take the victory branch"
    );
    agree("both cursors reach the victory scene");

    expect(
        gl_campaign_advance_story(handle) != 0U && buffer()[1] == 0U &&
            cursor.advance_story() == pr::CampaignError::none,
        "both cursors advance past the victory scene"
    );
    agree("both cursors reach the victory terminal");
    expect(
        read_campaign_state(handle).complete == 1U,
        "the terminal node reports the campaign complete"
    );

    // Advancing a complete campaign is the engine's own refusal.
    expect(
        boundary_advance(handle, sim::Outcome::first_side_won, victory) ==
            static_cast<std::uint8_t>(pr::CampaignError::already_complete),
        "a complete campaign refuses to advance"
    );
    gl_campaign_destroy(handle);

    // A failed objective reaches the defeat terminal instead.
    const std::uint32_t defeat =
        gl_campaign_create(write_campaign_payload(7000, record, {500}));
    expect(defeat != 0U, "the defeat-path campaign creates a cursor");
    expect(
        gl_campaign_advance_story(defeat) != 0U,
        "the defeat path passes the story node"
    );
    expect(
        boundary_advance(
            defeat,
            sim::Outcome::second_side_won,
            {{900, sim::ObjectiveState::failed}}
        ) == 0U,
        "the defeat branch is taken"
    );
    expect(
        read_campaign_state(defeat).node_id == 4U,
        "a failed objective reaches the defeat terminal"
    );

    // An outcome the engine calls incomplete is passed through, not decided
    // at the boundary.
    const std::uint32_t stalled =
        gl_campaign_create(write_campaign_payload(7000, record, {500}));
    expect(
        boundary_advance(stalled, sim::Outcome::ongoing, {}) ==
            static_cast<std::uint8_t>(pr::CampaignError::outcome_incomplete),
        "an ongoing outcome is the engine's own refusal"
    );
    gl_campaign_destroy(defeat);
    gl_campaign_destroy(stalled);
}

void campaign_dialogues_round_trip() {
    const auto record = encode_campaign_record(1, reference_flow());
    const std::uint32_t handle =
        gl_campaign_create(write_campaign_payload(7000, record, {500}));
    expect(handle != 0U, "the dialogue campaign creates a cursor");

    const std::vector<std::pair<std::string_view, std::string_view>> lines = {
        {"Narrator", "The valley waits."},
        {"Mirea", "Hold the line."},
    };
    const auto dialogue = encode_dialogue_record("Opening", lines);
    // Dialogues arrive in authored order, not identity order. Attaching a
    // higher identity first proves the section keeps the lookup invariant.
    expect(
        gl_campaign_add_dialogue(
            handle, write_dialogue_payload(300, encode_dialogue_record("Later", {}))
        ) != 0U,
        "a later dialogue record attaches"
    );
    expect(
        gl_campaign_add_dialogue(handle, write_dialogue_payload(100, dialogue))
            != 0U,
        "a dialogue record attaches"
    );
    expect(
        gl_campaign_dialogue(handle, 300) != 0U && buffer()[1] == 0U,
        "the out-of-order dialogue is still found"
    );

    // The decoded dialogue is what load_dialogue reads from the same bytes.
    const std::uint32_t written = gl_campaign_dialogue(handle, 100);
    expect(written != 0U, "the dialogue is decoded");
    Reader reader;
    expect(
        reader.u8() == abi_ok && reader.u8() == 0U,
        "the dialogue decodes without error"
    );
    const std::uint16_t name_size = reader.u16();
    std::string name;
    for (std::uint16_t index = 0; index < name_size; ++index) {
        name.push_back(static_cast<char>(reader.u8()));
    }
    expect(name == "Opening", "the dialogue name round-trips");
    const std::uint16_t line_count = reader.u16();
    expect(line_count == lines.size(), "the line count round-trips");
    for (std::uint16_t index = 0; index < line_count; ++index) {
        std::string speaker;
        std::string text;
        const std::uint16_t speaker_size = reader.u16();
        for (std::uint16_t at = 0; at < speaker_size; ++at) {
            speaker.push_back(static_cast<char>(reader.u8()));
        }
        const std::uint16_t text_size = reader.u16();
        for (std::uint16_t at = 0; at < text_size; ++at) {
            text.push_back(static_cast<char>(reader.u8()));
        }
        expect(
            speaker == lines[index].first && text == lines[index].second,
            "each dialogue line round-trips"
        );
    }
    // A scene that casts nobody: the backdrop byte, then an empty cast, then
    // one zero per line. The ABI writes both tails unconditionally. It is a
    // length-prefixed buffer rather than a record read to its exact end, so
    // there is no byte identity to preserve here and a fixed shape is cheaper
    // for the reader.
    expect(reader.u8() == 0U, "the scene names no backdrop");
    expect(reader.u8() == 0U, "and casts nobody");
    for (std::uint16_t index = 0; index < line_count; ++index) {
        expect(
            reader.u8() == 0U,
            "so every line of it names nobody, which is what every line "
            "carried before a scene could name anybody"
        );
    }

    // And a scene that does cast somebody. The identities and the per-line
    // references cross the boundary as the compiler resolved them, so the
    // browser reads an index where the console reads an index and neither
    // compares a speaker string.
    const std::vector<std::uint64_t> cast = {0xA1A2A3A4A5A6A7A8ULL, 77ULL};
    expect(
        gl_campaign_add_dialogue(
            handle,
            write_dialogue_payload(
                200,
                encode_dialogue_record("Cast", lines, cast, {2, 1})
            )
        ) != 0U,
        "a cast dialogue record attaches"
    );
    expect(gl_campaign_dialogue(handle, 200) != 0U, "and decodes");
    Reader cast_reader;
    expect(
        cast_reader.u8() == abi_ok && cast_reader.u8() == 0U,
        "the cast dialogue decodes without error"
    );
    const std::uint16_t cast_name_size = cast_reader.u16();
    cast_reader.skip(cast_name_size);
    const std::uint16_t cast_lines = cast_reader.u16();
    for (std::uint16_t index = 0; index < cast_lines; ++index) {
        const std::uint16_t speaker_size = cast_reader.u16();
        cast_reader.skip(speaker_size);
        const std::uint16_t text_size = cast_reader.u16();
        cast_reader.skip(text_size);
    }
    expect(cast_reader.u8() == 0U, "the cast scene names no backdrop");
    expect(cast_reader.u8() == cast.size(), "the cast size crosses");
    for (const std::uint64_t unit_type_id : cast) {
        expect(
            cast_reader.u64() == unit_type_id,
            "and every identity in it crosses whole"
        );
    }
    expect(
        cast_reader.u8() == 2U && cast_reader.u8() == 1U,
        "and each line names the entry that speaks it, in authored order"
    );

    // A dialogue nobody attached is the loader's own missing_record.
    expect(
        gl_campaign_dialogue(handle, 999) != 0U && buffer()[0] == abi_ok &&
            buffer()[1] ==
                static_cast<std::uint8_t>(pr::DialogueError::missing_record),
        "an unknown dialogue is missing_record"
    );

    // A duplicate identity is refused at the boundary, never shadowed.
    expect(
        gl_campaign_add_dialogue(handle, write_dialogue_payload(100, dialogue))
                == 0U &&
            buffer()[0] == abi_malformed_payload,
        "a duplicate dialogue identity is refused"
    );
    gl_campaign_destroy(handle);
}

void campaign_boundary_failures() {
    const auto record = encode_campaign_record(1, reference_flow());

    // Encounter identities arrive in authoring order; the reference check
    // must still find them.
    const std::uint32_t unsorted =
        gl_campaign_create(write_campaign_payload(7000, record, {900, 500}));
    expect(unsorted != 0U, "unsorted encounter identities still resolve");
    gl_campaign_destroy(unsorted);

    // A record whose referenced encounter is not declared is the campaign
    // loader's own missing_reference, reported through the status bytes.
    expect(
        gl_campaign_create(write_campaign_payload(7000, record, {})) == 0U &&
            buffer()[0] == abi_ok &&
            buffer()[1] ==
                static_cast<std::uint8_t>(pr::CampaignError::missing_reference),
        "an undeclared encounter reference is the loader's refusal"
    );

    // Truncated payloads are boundary failures, not loader errors.
    write_campaign_payload(7000, record, {500});
    expect(
        gl_campaign_create(11U) == 0U && buffer()[0] == abi_malformed_payload,
        "a truncated campaign payload is a boundary failure"
    );

    // Unknown handles are rejected on every campaign entry point.
    expect(
        gl_campaign_state(9999U) == 0U && buffer()[0] == abi_unknown_handle,
        "campaign state rejects an unknown handle"
    );
    expect(
        gl_campaign_advance_story(9999U) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "campaign story advance rejects an unknown handle"
    );
    Writer advance;
    advance.u8(1U);
    advance.u32(0U);
    expect(
        gl_campaign_advance(9999U, advance.size()) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "campaign advance rejects an unknown handle"
    );
    expect(
        gl_campaign_dialogue(9999U, 100) == 0U &&
            buffer()[0] == abi_unknown_handle,
        "campaign dialogue rejects an unknown handle"
    );
    gl_campaign_destroy(9999U);  // A no-op rather than a crash.

    // A destroyed handle behaves exactly like one never issued.
    const std::uint32_t handle =
        gl_campaign_create(write_campaign_payload(7000, record, {500}));
    expect(handle != 0U, "the destroy-test campaign is valid");
    gl_campaign_destroy(handle);
    expect(
        gl_campaign_state(handle) == 0U && buffer()[0] == abi_unknown_handle,
        "a destroyed campaign handle is unknown"
    );
    gl_campaign_destroy(handle);  // Double destroy is a no-op.
}

void exposes_every_campaign_error_name() {
    const auto read_name = [](std::uint32_t length) {
        return std::string(
            reinterpret_cast<const char*>(buffer()),
            static_cast<std::size_t>(length)
        );
    };
    const auto last_campaign =
        static_cast<std::uint32_t>(pr::CampaignError::outcome_incomplete);
    for (std::uint32_t code = 0; code <= last_campaign; ++code) {
        const std::uint32_t length = gl_campaign_error_name(code);
        expect(length != 0U, "every campaign error has a name");
        expect(
            read_name(length) ==
                pr::error_name(static_cast<pr::CampaignError>(code)),
            "the exposed campaign error name is the engine's own"
        );
    }
    expect(
        gl_campaign_error_name(last_campaign + 1U) == 0U,
        "the campaign error vocabulary ends after its last enumerator"
    );

    const auto last_dialogue =
        static_cast<std::uint32_t>(pr::DialogueError::malformed_payload);
    for (std::uint32_t code = 0; code <= last_dialogue; ++code) {
        const std::uint32_t length = gl_campaign_dialogue_error_name(code);
        expect(length != 0U, "every dialogue error has a name");
        expect(
            read_name(length) ==
                pr::error_name(static_cast<pr::DialogueError>(code)),
            "the exposed dialogue error name is the engine's own"
        );
    }
    expect(
        gl_campaign_dialogue_error_name(last_dialogue + 1U) == 0U,
        "the dialogue error vocabulary ends after its last enumerator"
    );
}

void maps_stable_content_identity() {
    const std::string_view key = "demo_campaign/bridge_encounter";
    std::memcpy(buffer(), key.data(), key.size());
    expect(
        gl_core_stable_content_id(static_cast<std::uint32_t>(key.size())) ==
            core::stable_content_id_v1(key),
        "content identity crosses the boundary unchanged"
    );
    expect(
        gl_core_stable_content_id(gl_sim_io_capacity() + 1U) == 0U,
        "an impossible key length is refused"
    );
}

// The content compiler across the boundary.
//
// The package itself is checked against the host compiler in
// `editor/src/domain/content-compiler.test.ts`, where the real module runs.
// What is checked here is the wire format and the two refusals: a caller must
// be able to tell "I gave you something that is not a project" from "the
// project you gave me does not hold together", because they send an author to
// different places.
void compiles_content_across_the_boundary() {
    auto* const content = reinterpret_cast<std::uint8_t*>(gl_content_buffer());
    const std::uint32_t capacity = gl_content_capacity();
    expect(capacity > gl_sim_io_capacity(), "the content buffer is the larger");

    const std::string_view not_a_project = "{}";
    std::memcpy(content, not_a_project.data(), not_a_project.size());
    std::uint32_t written = gl_content_compile(
        static_cast<std::uint32_t>(not_a_project.size())
    );
    expect(written > 3U, "a refusal still writes a reply");
    expect(content[0] == 1U, "text that is not a project is refused by stage one");
    const std::uint16_t refusals = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(content[1]) |
        static_cast<std::uint16_t>(content[2] << 8)
    );
    expect(refusals > 0U, "and the refusal is named rather than implied");

    written = gl_content_compile(capacity + 1U);
    expect(written == 3U, "a source larger than the buffer is refused");
    expect(content[0] == 3U, "with its own status rather than a parse failure");
}

}  // namespace

int main() {
    unit_record_size_is_exact();
    replays_the_reference_vector();
    snapshots_round_trip_every_field();
    forecasts_across_the_boundary();
    queries_reachability_across_the_boundary();
    queries_aiming_across_the_boundary();
    proposes_plans_across_the_boundary();
    rejects_unknown_handles();
    latches_reader_and_writer_overflows();
    surfaces_create_errors_through_the_status_bytes();
    campaign_cursor_matches_the_library();
    campaign_dialogues_round_trip();
    campaign_boundary_failures();
    exposes_every_error_name();
    exposes_every_campaign_error_name();
    maps_stable_content_identity();
    compiles_content_across_the_boundary();
    return failures == 0 ? 0 : 1;
}
