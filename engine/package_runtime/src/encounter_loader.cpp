// SPDX-License-Identifier: MIT
#include <grandleon/package_runtime/encounter_loader.hpp>

#include <grandleon/package_runtime/progression.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace grandleon::package_runtime {
namespace {

using package_format::LoadedPackage;
using package_format::RecordView;
using package_format::SectionType;

// Whether a decoded number may stand in the rules' damage arithmetic.
//
// **This gate and `simulation::create_encounter`'s have to agree**, and they
// agree by both asking `simulation::maximum_stat` rather than by both writing
// out the same literal. A package carrying a stat past the bound is refused
// here, where the refusal names the package, instead of reaching a board and
// wrapping an `int16` so that a blow heals what it hits.
bool bounded_stat(std::int16_t value) noexcept {
    return value >= 0 && value <= simulation::maximum_stat;
}

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

    bool i16(std::int16_t& value) {
        std::uint16_t bits = 0;
        if (!u16(bits)) return false;
        value = static_cast<std::int16_t>(bits);
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

    bool skip(std::size_t count) {
        if (remaining() < count) return false;
        cursor_ += count;
        return true;
    }

    bool skip_ids() {
        std::vector<std::uint64_t> ignored;
        return ids(ignored);
    }

    bool ids(std::vector<std::uint64_t>& values) {
        std::uint16_t count = 0;
        if (!u16(count) ||
            static_cast<std::size_t>(count) > remaining() / 8U) {
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

    [[nodiscard]] bool finished() const noexcept {
        return cursor_ == end_;
    }

private:
    const std::uint8_t* bytes_;
    std::size_t cursor_;
    std::size_t end_;
};

// The fewest bytes a placement and a deployment tile can occupy, so a declared
// count is measured against the bytes actually present before any container is
// asked to hold that many. A placement is its fixed fields with no patrol
// route; a tile is its two coordinates.
//
// Checked by division rather than multiplication, because the product of a
// hostile count and a stride is not representable everywhere the quotient is.
constexpr std::size_t placement_minimum_size = 32;
constexpr std::size_t deployment_tile_size = 4;

struct ClassStats final {
    std::int16_t health{};
    std::int16_t strength{};
    std::int16_t defense{};
    std::int16_t resistance{};
    std::uint8_t movement{1};
    std::uint8_t action_points{1};
    std::uint8_t speed{1};
    bool acts_after_attacking{false};
    std::uint8_t crossings{simulation::crossing_none};
    std::int16_t skill{};
    std::int16_t luck{};
    std::int16_t evasion{};
    std::int16_t magic{};
};

// Reach and power of the weapon in hand. A unit with no weapon keeps the
// bare-handed defaults rather than becoming unable to attack.
struct EquippedWeapon final {
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    std::int16_t power{};
    std::uint8_t accuracy{100};
};

// A class record ends in two appended tails, each read only when the record has
// not finished. The crossings byte came first: a record that ends before it is
// a class from a package written when every class walked, which is exactly what
// a walker encodes. The four richer stats came after it: a record that ends
// before them is a class from a package written before a blow could be dodged
// or a mage could cast harder, and all four are zero for it. In both cases the
// absent bytes and the zero bytes mean the same thing, which is why neither
// tail needs a version.
bool read_class(
    const LoadedPackage& package,
    const RecordView& record,
    ClassStats& stats
) {
    Reader reader(package, record);
    std::uint8_t after = 0;
    if (!(reader.skip_string() &&
          reader.i16(stats.health) &&
          reader.i16(stats.strength) &&
          reader.i16(stats.defense) &&
          reader.i16(stats.resistance) &&
          reader.u8(stats.movement) &&
          reader.u8(stats.action_points) &&
          reader.u8(stats.speed) &&
          reader.u8(after) &&
          reader.skip_ids())) {
        return false;
    }
    if (!reader.finished() && !reader.u8(stats.crossings)) return false;
    if (!reader.finished() &&
        !(reader.i16(stats.skill) && reader.i16(stats.luck) &&
          reader.i16(stats.evasion) && reader.i16(stats.magic))) {
        return false;
    }
    stats.acts_after_attacking = after == 1U;
    return reader.finished() && stats.health > 0 &&
           bounded_stat(stats.strength) && bounded_stat(stats.defense) &&
           bounded_stat(stats.resistance) && stats.skill >= 0 &&
           stats.luck >= 0 && stats.evasion >= 0 && bounded_stat(stats.magic) &&
           stats.movement > 0 && stats.action_points > 0 && stats.speed > 0 &&
           after <= 1U;
}

// Reads the unit type's class, the weapons it carries, the items it carries,
// and its abilities in one pass. Every carried weapon is kept: the package
// length-prefixes the list, and a loader that took only the first of it would
// throw the rest away. The item list is kept for the same reason: the package
// carries it, and decoding it into a local nobody reads would leave a character
// holding a potion that nothing could drink.
bool read_unit_type(
    const LoadedPackage& package,
    const RecordView& record,
    std::uint64_t& class_id,
    std::vector<std::uint64_t>& weapons,
    std::vector<std::uint64_t>& items,
    std::vector<std::uint64_t>& abilities,
    std::uint64_t& drop_item_id,
    std::uint8_t& drop_chance
) {
    Reader reader(package, record);
    std::uint64_t faction_id = 0;
    if (!(reader.skip_string() &&
          reader.u64(class_id) &&
          reader.u64(faction_id) &&
          reader.ids(weapons) &&
          reader.ids(items) &&
          reader.ids(abilities))) {
        return false;
    }
    // Everything after the ability list is the growth block, appended when
    // characters learned to grow, and then the drop, appended when a defeated
    // character started leaving things behind. A record that ends here is a
    // unit type from a package written before either, and it means exactly what
    // the defaults mean: worth nothing to defeat, growing nothing, and leaving
    // nothing.
    //
    // The growth numbers are skipped rather than kept (nothing on a board
    // reads them, and `progression.hpp` decodes the same bytes for the
    // campaign layer), but the drop is a battle rule and is read here.
    //
    // Two growth lengths are accepted, because the block's chance list grew a
    // tail of its own when the stat line did, so the record's total length has
    // to be discriminated on both tails at once. Anything else is a record
    // claiming to carry a block and not carrying one.
    if (!reader.finished()) {
        const std::size_t left = reader.remaining();
        const bool shorter_line =
            left == unit_type_progression_size_before_richer_line ||
            left == unit_type_progression_size_before_richer_line +
                        unit_type_drop_size;
        if (!reader.skip(
                shorter_line ? unit_type_progression_size_before_richer_line
                             : unit_type_progression_size
            )) {
            return false;
        }
        if (!reader.finished() &&
            !(reader.u64(drop_item_id) && reader.u8(drop_chance))) {
            return false;
        }
    }
    // The pair is authored together or not at all (the compiler refuses the
    // halves), so a package carrying one of them is a package this build does
    // not understand rather than a unit type that half-drops. The chance is a
    // whole percentage for the same reason a weapon's accuracy is.
    return reader.finished() && faction_id != 0 && drop_chance <= 100U &&
           (drop_item_id == 0) == (drop_chance == 0);
}

// One weapon record, decoded into the definition the rules read. This is data
// selection with no arithmetic, so it can be reproduced elsewhere without
// duplicating a rule.
bool read_weapon(
    const LoadedPackage& package,
    std::uint64_t weapon_id,
    simulation::WeaponDefinition& weapon
) {
    const RecordView* record = package.find(SectionType::weapons, weapon_id);
    if (record == nullptr) return false;
    Reader reader(package, *record);
    std::uint64_t type_id = 0;
    weapon.id = weapon_id;
    if (!reader.skip_string() || !reader.u64(type_id) ||
        !reader.i16(weapon.power) ||
        !reader.u8(weapon.minimum_reach) || !reader.u8(weapon.maximum_reach)) {
        return false;
    }
    // Accuracy is the last byte a weapon record carries, and a record that
    // ends before it is a weapon from a package written when every attack
    // landed. That is exactly what a hundred encodes, so the absent byte and
    // the hundred byte mean the same thing and neither needs a version.
    if (!reader.finished() && !reader.u8(weapon.accuracy)) return false;
    return reader.finished() && bounded_stat(weapon.power) &&
           weapon.minimum_reach > 0 &&
           weapon.minimum_reach <= weapon.maximum_reach &&
           weapon.accuracy <= 100U;
}

// One item record, decoded into the definition the rules read. Data selection
// with no arithmetic, exactly as a weapon record is.
bool read_item(
    const LoadedPackage& package,
    std::uint64_t item_id,
    simulation::ItemDefinition& item
) {
    const RecordView* record = package.find(SectionType::items, item_id);
    if (record == nullptr) return false;
    Reader reader(package, *record);
    std::uint64_t type_id = 0;
    std::uint16_t stack_limit = 0;
    item.id = item_id;
    if (!reader.skip_string() || !reader.u64(type_id) ||
        !reader.u16(stack_limit)) {
        return false;
    }
    // The effect is the tail, and a record that ends before it is an item from
    // a package written when nothing could be spent. That is exactly what
    // `ItemKind::none` encodes, so the absent bytes and the zero byte mean the
    // same thing and neither needs a version. The kind is read as a byte and
    // then checked, rather than cast and trusted: an unknown kind is a package
    // this build does not understand, not an item that does nothing.
    if (!reader.finished()) {
        std::uint8_t kind = 0;
        if (!reader.u8(kind) || !reader.i16(item.power)) return false;
        if (kind != static_cast<std::uint8_t>(simulation::ItemKind::none) &&
            kind != static_cast<std::uint8_t>(simulation::ItemKind::restore)) {
            return false;
        }
        item.kind = static_cast<simulation::ItemKind>(kind);
    }
    const bool restores = item.kind == simulation::ItemKind::restore;
    return reader.finished() && stack_limit > 0 && bounded_stat(item.power) &&
           (restores ? item.power > 0 : item.power == 0);
}

// Decodes every item a unit carries into the encounter's registry, adding each
// identity once however many units carry it. There is no equivalent of the
// weapon in hand: a pack has no first entry the rules read.
bool read_carried_items(
    const LoadedPackage& package,
    const std::vector<std::uint64_t>& carried,
    std::vector<simulation::ItemDefinition>& registry,
    std::set<std::uint64_t>& registered
) {
    for (const std::uint64_t item_id : carried) {
        if (registered.find(item_id) != registered.end()) continue;
        simulation::ItemDefinition item;
        if (!read_item(package, item_id, item)) return false;
        registry.push_back(item);
        registered.insert(item_id);
    }
    return true;
}

// Decodes every weapon a unit carries into the encounter's registry, adding
// each identity once however many units carry it, and reports the reach and
// power of the first, the one in hand.
bool read_carried_weapons(
    const LoadedPackage& package,
    const std::vector<std::uint64_t>& carried,
    std::vector<simulation::WeaponDefinition>& registry,
    std::set<std::uint64_t>& registered,
    EquippedWeapon& equipped
) {
    for (const std::uint64_t weapon_id : carried) {
        if (registered.find(weapon_id) == registered.end()) {
            simulation::WeaponDefinition weapon;
            if (!read_weapon(package, weapon_id, weapon)) return false;
            registry.push_back(weapon);
            registered.insert(weapon_id);
        }
    }
    if (carried.empty()) return true;
    const auto found = std::find_if(
        registry.begin(),
        registry.end(),
        [&carried](const simulation::WeaponDefinition& weapon) {
            return weapon.id == carried.front();
        }
    );
    if (found == registry.end()) return false;
    equipped.power = found->power;
    equipped.minimum_reach = found->minimum_reach;
    equipped.maximum_reach = found->maximum_reach;
    equipped.accuracy = found->accuracy;
    return true;
}

// The identities of a map's cells. Appended after them, in this order: what
// each cell asks of whoever stands in it and what it charges them.
//
// Both tails are optional and the reader tells them apart by what is left over:
// nothing, one byte per cell, or two. An absent passability list is an all-open
// board, which is what a package written before terrain had a meaning has
// always played as; an absent cost list is a board where every step costs one,
// which is what a package written before ground had a price has always played
// as. Neither is guessed at from the other.
bool read_map(
    const LoadedPackage& package,
    const RecordView& record,
    std::uint16_t& width,
    std::uint16_t& height,
    std::vector<std::uint64_t>& terrain,
    std::vector<simulation::Terrain>& passability,
    std::vector<std::uint8_t>& movement_cost
) {
    Reader reader(package, record);
    std::uint32_t terrain_count = 0;
    if (!reader.skip_string() || !reader.u16(width) || !reader.u16(height) ||
        !reader.u32(terrain_count) || width == 0 || height == 0 ||
        terrain_count !=
            static_cast<std::uint32_t>(width) *
                static_cast<std::uint32_t>(height) ||
        static_cast<std::size_t>(terrain_count) > reader.remaining() / 8U) {
        return false;
    }
    terrain.reserve(terrain_count);
    for (std::uint32_t index = 0; index < terrain_count; ++index) {
        std::uint64_t cell = 0;
        if (!reader.u64(cell)) return false;
        terrain.push_back(cell);
    }
    if (!reader.finished()) {
        const std::size_t tail = reader.remaining();
        if (tail != terrain_count && tail != 2U * terrain_count) return false;
        passability.reserve(terrain_count);
        for (std::uint32_t index = 0; index < terrain_count; ++index) {
            std::uint8_t asks = 0;
            if (!reader.u8(asks)) return false;
            switch (asks) {
                case 0U:
                    passability.push_back(simulation::Terrain::open);
                    break;
                case 1U:
                    passability.push_back(simulation::Terrain::water);
                    break;
                case 2U:
                    passability.push_back(simulation::Terrain::heights);
                    break;
                default:
                    // A cell asking something this engine does not know is a
                    // refusal, not a guess: guessing "open" would quietly
                    // publish a board whose rules are not the authored ones.
                    return false;
            }
        }
        if (tail == 2U * terrain_count) {
            movement_cost.reserve(terrain_count);
            for (std::uint32_t index = 0; index < terrain_count; ++index) {
                std::uint8_t charges = 0;
                if (!reader.u8(charges)) return false;
                // A cell charging nothing is refused on the same standard: it
                // is not free ground, it is a walk with no end, and guessing a
                // step for it would publish a board whose rules are not the
                // authored ones. The engine refuses one too; this refuses it
                // where the package is named.
                if (charges == 0U) return false;
                movement_cost.push_back(charges);
            }
        }
    }
    return reader.finished();
}

// Decodes an objective record. Targets are named by the placement's own source
// key identity, which the encounter section carries alongside each instance id.
bool read_objective(
    const LoadedPackage& package,
    std::uint64_t objective_id,
    simulation::ObjectiveDefinition& objective,
    std::uint64_t& target_source_key,
    EncounterLoadError& error
) {
    const RecordView* record =
        package.find(SectionType::objectives, objective_id);
    if (record == nullptr) {
        error = EncounterLoadError::missing_reference;
        return false;
    }
    Reader reader(package, *record);
    std::uint8_t kind = 0;
    std::uint8_t side = 0;
    if (!reader.skip_string() || !reader.u8(kind) || !reader.u8(side) ||
        !reader.u64(target_source_key)) {
        error = EncounterLoadError::malformed_payload;
        return false;
    }
    objective.id = objective_id;
    switch (kind) {
        case 1U:
            objective.kind = simulation::ObjectiveKind::defeat_all_opponents;
            break;
        case 2U:
            objective.kind = simulation::ObjectiveKind::defeat_target;
            break;
        case 3U:
            objective.kind = simulation::ObjectiveKind::protect_target;
            break;
        case 4U: {
            // The count, a tail written only for this kind. Every other kind's
            // record ends where it always ended, so `finished()` below is the
            // check it always was for them. This kind could not have been
            // written by any package that came before, so nothing old reaches
            // here.
            objective.kind = simulation::ObjectiveKind::survive_rounds;
            std::uint16_t rounds = 0;
            if (!reader.u16(rounds) || rounds == 0) {
                error = EncounterLoadError::malformed_payload;
                return false;
            }
            objective.round_count = rounds;
            break;
        }
        default:
            error = EncounterLoadError::unsupported_objective;
            return false;
    }
    if (!reader.finished()) {
        error = EncounterLoadError::malformed_payload;
        return false;
    }
    if (side != 1U && side != 2U) {
        error = EncounterLoadError::malformed_payload;
        return false;
    }
    objective.side = side == 1U ? simulation::Side::first
                                : simulation::Side::second;
    return true;
}

// Decodes the ability registry once per encounter load.
bool read_abilities(
    const LoadedPackage& package,
    std::vector<simulation::AbilityDefinition>& abilities
) {
    const auto* section = package.find(SectionType::abilities);
    if (section == nullptr) return true;
    for (const RecordView& record : section->records) {
        Reader reader(package, record);
        simulation::AbilityDefinition ability;
        std::uint8_t kind = 0;
        std::uint8_t damage_type = 0;
        std::uint8_t area = 0;
        if (!reader.skip_string() || !reader.u8(kind) ||
            !reader.u8(damage_type) || !reader.u8(area) ||
            !reader.i16(ability.power) || !reader.u8(ability.minimum_reach) ||
            !reader.u8(ability.maximum_reach) || !reader.u8(ability.radius)) {
            return false;
        }
        // Appended for the reason a weapon's is: a record that ends here is a
        // cast that always lands.
        if (!reader.finished() && !reader.u8(ability.accuracy)) return false;
        if (!reader.finished()) return false;
        if (kind != 1U && kind != 2U) return false;
        if (area < 1U || area > 3U) return false;
        if (ability.accuracy > 100U) return false;
        if (!bounded_stat(ability.power)) return false;
        ability.id = record.stable_id;
        ability.kind = kind == 2U ? simulation::AbilityKind::restore
                                  : simulation::AbilityKind::damage;
        ability.damage_type = damage_type == 2U
                                  ? simulation::DamageType::magical
                                  : simulation::DamageType::physical;
        ability.area = area == 3U   ? simulation::AreaShape::diamond
                       : area == 2U ? simulation::AreaShape::cross
                                    : simulation::AreaShape::single;
        abilities.push_back(ability);
    }
    return true;
}

}  // namespace

std::string_view error_name(EncounterLoadError error) noexcept {
    switch (error) {
        case EncounterLoadError::none: return "none";
        case EncounterLoadError::missing_section: return "missing_section";
        case EncounterLoadError::missing_record: return "missing_record";
        case EncounterLoadError::malformed_payload: return "malformed_payload";
        case EncounterLoadError::missing_reference: return "missing_reference";
        case EncounterLoadError::unsupported_objective:
            return "unsupported_objective";
    }
    return "unknown";
}

EncounterLoadResult load_encounter(
    const LoadedPackage& package,
    std::uint64_t encounter_id
) {
    auto fail = [](EncounterLoadError error) {
        EncounterLoadResult result;
        result.error = error;
        return result;
    };
    if (package.find(SectionType::encounters) == nullptr ||
        package.find(SectionType::maps) == nullptr ||
        package.find(SectionType::objectives) == nullptr ||
        package.find(SectionType::unit_types) == nullptr ||
        package.find(SectionType::classes) == nullptr) {
        return fail(EncounterLoadError::missing_section);
    }
    const RecordView* encounter =
        package.find(SectionType::encounters, encounter_id);
    if (encounter == nullptr) {
        return fail(EncounterLoadError::missing_record);
    }

    Reader reader(package, *encounter);
    std::uint64_t map_id = 0;
    std::uint8_t encoded_order = 0;
    std::uint16_t objective_count = 0;
    if (!reader.skip_string() || !reader.u64(map_id) ||
        !reader.u8(encoded_order) || !reader.u16(objective_count) ||
        objective_count == 0 || encoded_order < 1U || encoded_order > 3U) {
        return fail(EncounterLoadError::malformed_payload);
    }
    // Objective targets are resolved after placements are read, because a
    // target names a placement source key rather than an instance.
    std::vector<simulation::ObjectiveDefinition> objectives;
    std::vector<std::uint64_t> objective_targets;
    for (std::uint16_t index = 0; index < objective_count; ++index) {
        std::uint64_t objective_id = 0;
        if (!reader.u64(objective_id)) {
            return fail(EncounterLoadError::malformed_payload);
        }
        EncounterLoadError error = EncounterLoadError::none;
        simulation::ObjectiveDefinition objective;
        std::uint64_t target_source_key = 0;
        if (!read_objective(
                package, objective_id, objective, target_source_key, error
            )) {
            return fail(error);
        }
        objectives.push_back(objective);
        objective_targets.push_back(target_source_key);
    }
    const RecordView* map = package.find(SectionType::maps, map_id);
    if (map == nullptr) {
        return fail(EncounterLoadError::missing_reference);
    }
    EncounterLoadResult result;
    if (!read_map(package, *map, result.definition.width,
                  result.definition.height, result.terrain,
                  result.definition.terrain,
                  result.definition.movement_cost)) {
        return fail(EncounterLoadError::malformed_payload);
    }
    if (!read_abilities(package, result.definition.abilities)) {
        return fail(EncounterLoadError::malformed_payload);
    }
    std::map<std::uint64_t, std::uint64_t> placement_instances;

    std::uint16_t placement_count = 0;
    if (!reader.u16(placement_count) || placement_count == 0 ||
        static_cast<std::size_t>(placement_count) >
            reader.remaining() / placement_minimum_size) {
        return fail(EncounterLoadError::malformed_payload);
    }
    result.definition.units.reserve(placement_count);
    std::set<std::uint64_t> placement_ids;
    std::set<std::uint64_t> registered_weapons;
    std::set<std::uint64_t> registered_items;
    for (std::uint16_t index = 0; index < placement_count; ++index) {
        std::uint64_t instance_id = 0;
        std::uint64_t source_key_id = 0;
        std::uint64_t unit_type_id = 0;
        std::uint8_t encoded_side = 0;
        std::int16_t x = 0;
        std::int16_t y = 0;
        std::uint8_t encoded_behavior = 0;
        std::uint16_t patrol_count = 0;
        if (!reader.u64(instance_id) || !reader.u64(source_key_id) ||
            !reader.u64(unit_type_id) ||
            !reader.u8(encoded_side) || !reader.i16(x) || !reader.i16(y) ||
            !reader.u8(encoded_behavior) || !reader.u16(patrol_count) ||
            instance_id == 0 || !placement_ids.insert(instance_id).second ||
            (encoded_side != 1U && encoded_side != 2U) ||
            encoded_behavior < 1U || encoded_behavior > 3U) {
            return fail(EncounterLoadError::malformed_payload);
        }
        UnitBehaviorBinding binding;
        binding.unit_id = instance_id;
        binding.behavior = encoded_behavior == 3U ? tactics::Behavior::pursue
                           : encoded_behavior == 2U ? tactics::Behavior::patrol
                                                    : tactics::Behavior::hold;
        for (std::uint16_t point = 0; point < patrol_count; ++point) {
            std::int16_t px = 0;
            std::int16_t py = 0;
            if (!reader.i16(px) || !reader.i16(py)) {
                return fail(EncounterLoadError::malformed_payload);
            }
            binding.patrol.push_back({px, py});
        }
        result.behaviors.push_back(std::move(binding));
        const RecordView* unit_type =
            package.find(SectionType::unit_types, unit_type_id);
        if (unit_type == nullptr) {
            return fail(EncounterLoadError::missing_reference);
        }
        std::uint64_t class_id = 0;
        std::vector<std::uint64_t> weapons;
        std::vector<std::uint64_t> items;
        std::vector<std::uint64_t> abilities;
        std::uint64_t drop_item_id = 0;
        std::uint8_t drop_chance = 0;
        if (!read_unit_type(
                package, *unit_type, class_id, weapons, items, abilities,
                drop_item_id, drop_chance
            )) {
            return fail(EncounterLoadError::malformed_payload);
        }
        EquippedWeapon weapon;
        if (!read_carried_weapons(
                package, weapons, result.definition.weapons,
                registered_weapons, weapon
            )) {
            return fail(EncounterLoadError::malformed_payload);
        }
        if (!read_carried_items(
                package, items, result.definition.items, registered_items
            )) {
            return fail(EncounterLoadError::malformed_payload);
        }
        const RecordView* unit_class =
            package.find(SectionType::classes, class_id);
        if (unit_class == nullptr) {
            return fail(EncounterLoadError::missing_reference);
        }
        ClassStats stats;
        if (!read_class(package, *unit_class, stats)) {
            return fail(EncounterLoadError::malformed_payload);
        }
        placement_instances.emplace(source_key_id, instance_id);
        result.placements.push_back({instance_id, source_key_id});
        result.definition.units.push_back(
            {
                instance_id,
                unit_type_id,
                encoded_side == 1U
                    ? simulation::Side::first
                    : simulation::Side::second,
                {x, y},
                stats.health,
                stats.strength,
                weapon.power,
                stats.defense,
                stats.resistance,
                stats.skill,
                stats.luck,
                stats.evasion,
                stats.magic,
                stats.movement,
                stats.action_points,
                stats.speed,
                stats.acts_after_attacking,
                weapon.minimum_reach,
                weapon.maximum_reach,
                abilities,
                weapons,
                stats.crossings,
                weapon.accuracy,
                // One of each, which is what a unit type's authored list says.
                // The counts are left to `create_encounter` to fill in, so
                // there is one place that decides what an unstated count means.
                items,
                {},
                // What this character's type leaves behind, and how often. The
                // identity is carried through unresolved: the board never reads
                // what the thing does, so it is not registered among the items
                // the encounter defines.
                drop_item_id,
                drop_chance
            }
        );
    }
    for (std::size_t index = 0; index < objectives.size(); ++index) {
        const bool needs_target =
            objectives[index].kind ==
                simulation::ObjectiveKind::defeat_target ||
            objectives[index].kind ==
                simulation::ObjectiveKind::protect_target;
        if (!needs_target) continue;
        const auto found = placement_instances.find(objective_targets[index]);
        if (found == placement_instances.end()) {
            return fail(EncounterLoadError::missing_reference);
        }
        objectives[index].target_unit_id = found->second;
    }
    result.definition.objectives = std::move(objectives);
    result.definition.turn_order =
        encoded_order == 3U ? simulation::TurnOrder::initiative
        : encoded_order == 2U ? simulation::TurnOrder::side_blocks
                              : simulation::TurnOrder::alternating;

    // Who can be talked to on this board, out of the talks section rather than
    // out of this record. A package where nobody is talkable has no such
    // section, so the lookup simply finds nothing and every unit keeps the zero
    // it was decoded with: the same board, decoded by the same code, as before
    // the gesture existed.
    if (const RecordView* talks = package.find(SectionType::talks, encounter_id)) {
        Reader talk_reader(package, *talks);
        std::uint16_t talk_count = 0;
        if (!talk_reader.u16(talk_count) || talk_count == 0) {
            return fail(EncounterLoadError::malformed_payload);
        }
        for (std::uint16_t index = 0; index < talk_count; ++index) {
            std::uint64_t placement_id = 0;
            std::uint64_t flag_id = 0;
            if (!talk_reader.u64(placement_id) || !talk_reader.u64(flag_id) ||
                placement_id == 0 || flag_id == 0) {
                return fail(EncounterLoadError::malformed_payload);
            }
            // A mark naming a placement this board does not field is a package
            // that disagrees with itself, and saying so beats placing a
            // character nobody can talk to on a board that says they can.
            const auto found = std::find_if(
                result.definition.units.begin(),
                result.definition.units.end(),
                [placement_id](const simulation::UnitDefinition& unit) {
                    return unit.id == placement_id;
                }
            );
            if (found == result.definition.units.end()) {
                return fail(EncounterLoadError::missing_reference);
            }
            found->talk_record_id = flag_id;
        }
        if (!talk_reader.finished()) {
            return fail(EncounterLoadError::malformed_payload);
        }
    }

    // What is said while this battle is on, out of the moments section. Optional
    // where talks and arrivals are required: a runtime that cannot read this
    // plays the same battle in silence, so a package holding none simply has no
    // such section and every encounter keeps the empty list it decoded with.
    if (const RecordView* moments = package.find(SectionType::moments, encounter_id)) {
        Reader moment_reader(package, *moments);
        std::uint16_t moment_count = 0;
        if (!moment_reader.u16(moment_count) || moment_count == 0) {
            return fail(EncounterLoadError::malformed_payload);
        }
        for (std::uint16_t index = 0; index < moment_count; ++index) {
            std::uint64_t moment_id = 0;
            std::uint8_t trigger = 0;
            std::uint64_t placement_id = 0;
            std::uint64_t dialogue_id = 0;
            if (!moment_reader.u64(moment_id) || !moment_reader.u8(trigger) ||
                !moment_reader.u64(placement_id) ||
                !moment_reader.u64(dialogue_id) || moment_id == 0 ||
                dialogue_id == 0) {
                return fail(EncounterLoadError::malformed_payload);
            }
            if (trigger < static_cast<std::uint8_t>(MomentTrigger::stage_opens) ||
                trigger > static_cast<std::uint8_t>(MomentTrigger::character_falls)) {
                return fail(EncounterLoadError::malformed_payload);
            }
            const auto kind = static_cast<MomentTrigger>(trigger);
            // A moment about somebody has to be about somebody this board
            // fields, on the terms a talk mark is held to: naming a placement
            // the board does not have is a package disagreeing with itself, and
            // saying so beats staying silent at the moment the words were due.
            if (kind == MomentTrigger::stage_opens) {
                if (placement_id != 0) {
                    return fail(EncounterLoadError::malformed_payload);
                }
            } else {
                if (placement_id == 0) {
                    return fail(EncounterLoadError::malformed_payload);
                }
                const auto stands = std::find_if(
                    result.definition.units.begin(),
                    result.definition.units.end(),
                    [placement_id](const simulation::UnitDefinition& unit) {
                        return unit.id == placement_id;
                    }
                );
                if (stands == result.definition.units.end()) {
                    return fail(EncounterLoadError::missing_reference);
                }
            }
            result.moments.push_back(
                EncounterMoment{moment_id, kind, placement_id, dialogue_id}
            );
        }
        if (!moment_reader.finished()) {
            return fail(EncounterLoadError::malformed_payload);
        }
    }

    // When a placement comes in, out of the arrivals section rather than out of
    // this record, for the reason the talks section is read this way. A package
    // no encounter of which authors a wave has no such section, so the lookup
    // finds nothing and every character keeps the zeroes it was decoded with:
    // the same board, decoded by the same code, as before waves existed.
    if (const RecordView* arrivals =
            package.find(SectionType::arrivals, encounter_id)) {
        Reader arrival_reader(package, *arrivals);
        std::uint16_t arrival_count = 0;
        if (!arrival_reader.u16(arrival_count) || arrival_count == 0) {
            return fail(EncounterLoadError::malformed_payload);
        }
        for (std::uint16_t index = 0; index < arrival_count; ++index) {
            std::uint64_t placement_id = 0;
            std::uint16_t round = 0;
            std::uint16_t every = 0;
            std::uint16_t times = 0;
            if (!arrival_reader.u64(placement_id) ||
                !arrival_reader.u16(round) || !arrival_reader.u16(every) ||
                !arrival_reader.u16(times) || placement_id == 0 || round < 2U ||
                (every == 0U) != (times == 0U)) {
                return fail(EncounterLoadError::malformed_payload);
            }
            const auto found = std::find_if(
                result.definition.units.begin(),
                result.definition.units.end(),
                [placement_id](const simulation::UnitDefinition& unit) {
                    return unit.id == placement_id;
                }
            );
            if (found == result.definition.units.end()) {
                return fail(EncounterLoadError::missing_reference);
            }
            found->arrival_round = round;
            found->arrival_every = every;
            found->arrival_times = times;
        }
        if (!arrival_reader.finished()) {
            return fail(EncounterLoadError::malformed_payload);
        }
    }

    // The deployment region, a tail. A record that ends here is an encounter
    // from a package written before troops could be arranged, or one that
    // authors no region: the same bytes and the same meaning, which is what
    // makes such a board play exactly as it played.
    if (!reader.finished()) {
        std::uint64_t zone_id = 0;
        std::uint16_t tile_count = 0;
        if (!reader.u64(zone_id) || !reader.u16(tile_count) || zone_id == 0 ||
            static_cast<std::size_t>(tile_count) >
                reader.remaining() / deployment_tile_size) {
            return fail(EncounterLoadError::malformed_payload);
        }
        result.definition.deployment_tiles.reserve(tile_count);
        for (std::uint16_t index = 0; index < tile_count; ++index) {
            std::int16_t x = 0;
            std::int16_t y = 0;
            if (!reader.i16(x) || !reader.i16(y)) {
                return fail(EncounterLoadError::malformed_payload);
            }
            result.definition.deployment_tiles.push_back({x, y});
        }
        // How many of a company may take this field, a further tail inside the
        // deployment's own. A record that ends with the tiles is an encounter
        // with a region and no cap: every encounter written before caps
        // existed, and every one that authors none. The two are the same
        // bytes and the same meaning.
        if (!reader.finished()) {
            std::uint16_t capacity = 0;
            if (!reader.u16(capacity) || capacity == 0) {
                return fail(EncounterLoadError::malformed_payload);
            }
            result.deployment_capacity = capacity;
        }
        // A deployment states at least one of the two things it can state. No
        // tiles and no capacity is a tail that says nothing, and it was
        // malformed before this capacity existed for exactly the same reason.
        if (tile_count == 0 && result.deployment_capacity == 0) {
            return fail(EncounterLoadError::malformed_payload);
        }
        result.deployment_zone_id = zone_id;
    }

    if (!reader.finished()) {
        return fail(EncounterLoadError::malformed_payload);
    }
    return result;
}

}  // namespace grandleon::package_runtime
