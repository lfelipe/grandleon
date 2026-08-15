// SPDX-License-Identifier: MIT
// Every field of a battle is either folded into its canonical hash or refused
// entry with a reason, and this file is where each field is held to one of
// those two verdicts.
//
// **The failure it is written against is silence.** A fold is a list somebody
// has to remember to add to, and a field left off it looks exactly like a field
// deliberately kept off it: nothing fails, the hash goes on being computed, and
// it quietly stops telling two battles apart. The cost of that is not one
// wrong number. The encounter's seed is derived from the hash, so two battles
// that share an identity share their dice, and play the same rolls out of two
// different boards.
//
// So the list is not left as a list. Each structure the hash reads is taken
// apart here by name, into exactly as many names as it has fields, and a field
// added to any of them stops the build in this file until whoever added it says
// which verdict it takes. The verdicts are then checked rather than asserted in
// a comment: a folded field is proved folded by two battles differing only in
// it hashing differently, and a definition field with no place in the snapshot
// is proved to have one.

#include <grandleon/core/random.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace core = grandleon::core;
namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A character with every optional field of the hash lifted off its default.
//
// Several fields are folded only where they are not their default, and a
// character standing at every default folds none of them. A table that varies
// one field at a time therefore has to start from a character where all of them
// are already present, or half of it would be measuring the presence of a field
// instead of its value. Presence itself is measured separately, by
// `no_two_sparse_characters_share_a_hash` below, and it has to be: a table like
// this one cannot see two *absent* fields being confused for each other,
// because it never lets a field be absent.
sim::UnitSnapshot open_character() {
    sim::UnitSnapshot unit;
    unit.id = 10;
    unit.unit_type_id = 100;
    unit.side = sim::Side::first;
    unit.spent_action_points = 1;
    unit.position = {1, 2};
    unit.health = 9;
    unit.maximum_health = 12;
    unit.strength = 5;
    unit.power = 3;
    unit.defense = 2;
    unit.resistance = 1;
    unit.skill = 4;
    unit.luck = 3;
    unit.evasion = 2;
    unit.magic = 6;
    unit.movement = 3;
    unit.action_points = 2;
    unit.speed = 7;
    unit.acts_after_attacking = true;
    unit.minimum_reach = 2;
    unit.maximum_reach = 4;
    unit.ability_ids = {300, 301};
    unit.has_acted = true;
    unit.has_moved = true;
    unit.weapon_ids = {400, 401};
    unit.crossings = sim::crossing_water;
    unit.accuracy = 90;
    unit.item_ids = {500, 501};
    unit.item_counts = {2, 1};
    unit.drop_item_id = 600;
    unit.drop_chance = 40;
    unit.reach_bonus = 1;
    unit.talk_record_id = 700;
    unit.departed = false;
    unit.arrival_round = 3;
    unit.arrived = false;
    unit.endures = true;
    return unit;
}

// And a battle with every one of its own guards held open, the character above
// standing on it, and a second character to keep the list from being a list of
// one.
sim::EncounterSnapshot open_battle() {
    sim::EncounterSnapshot battle;
    battle.width = 6;
    battle.height = 4;
    battle.active_side = sim::Side::second;
    battle.active_unit_id = 20;
    battle.remaining_action_points = 1;
    battle.round = 2;
    battle.activation_count = 5;
    battle.outcome = sim::Outcome::ongoing;
    battle.turn_order = sim::TurnOrder::side_blocks;
    sim::UnitSnapshot other = open_character();
    other.id = 20;
    other.side = sim::Side::second;
    other.position = {4, 2};
    battle.units = {open_character(), other};
    battle.objectives = {{800, sim::ObjectiveState::pending}};
    battle.terrain.assign(
        static_cast<std::size_t>(battle.width) * battle.height,
        sim::Terrain::open
    );
    battle.terrain[3] = sim::Terrain::water;
    battle.movement_cost.assign(
        static_cast<std::size_t>(battle.width) * battle.height, 1U
    );
    battle.movement_cost[5] = 2U;
    battle.drops = {{10, 20, 600}};
    battle.random.seed = 0x0123456789abcdefULL;
    battle.random.positions[1] = 3;
    battle.deployment_tiles = {{0, 0}, {0, 1}};
    battle.deploying = true;
    return battle;
}

// The seventeen fields of `EncounterSnapshot`, every one of them folded.
void every_field_of_a_battle_is_folded() {
    const sim::EncounterSnapshot baseline = open_battle();
    const std::uint64_t named = sim::canonical_hash(baseline);

    sim::EncounterSnapshot battle = baseline;
    // **This binding is the canary.** It names exactly as many fields as
    // `EncounterSnapshot` has; an eighteenth stops the build right here, and
    // the line that fixes it is a line below saying what the new field does to
    // a battle's identity.
    auto& [
        width, height, active_side, active_unit_id, remaining_action_points,
        round, activation_count, outcome, turn_order, units, objectives,
        terrain, movement_cost, drops, random, deployment_tiles, deploying
    ] = battle;

    // Mutate one field, ask whether the hash noticed, put it back.
    const auto folded = [&](std::string_view field) {
        expect(
            sim::canonical_hash(battle) != named,
            std::string("two battles differing only in ") + std::string(field) +
                " do not share a canonical hash"
        );
        battle = baseline;
    };

    width = 7;
    folded("the board's width");
    height = 5;
    folded("the board's height");
    active_side = sim::Side::first;
    folded("whose side the turn belongs to");
    active_unit_id = 10;
    folded("which character is part-way through an activation");
    remaining_action_points = 2;
    folded("what is left of that activation");
    round = 3;
    folded("the round");
    activation_count = 6;
    folded("the activation count");
    outcome = sim::Outcome::first_side_won;
    folded("the outcome");
    turn_order = sim::TurnOrder::initiative;
    folded("who acts next");
    units.pop_back();
    folded("who is in the battle");
    // The three small records the snapshot's own lists are made of get the same
    // treatment, because a field added to one of them is a field added to a
    // battle exactly as much as a field on the battle itself is. These canaries
    // read the untouched baseline, so each name is both the field count being
    // pinned and the value being moved away from.
    const auto& [objective_id, objective_state] = baseline.objectives[0];
    const auto& [tile_x, tile_y] = baseline.deployment_tiles[0];
    const auto& [fallen, claimant, dropped] = baseline.drops[0];

    objectives[0].id = objective_id + 1;
    folded("which objective an entry is about");
    objectives[0].state = objective_state == sim::ObjectiveState::pending
                              ? sim::ObjectiveState::satisfied
                              : sim::ObjectiveState::pending;
    folded("what an objective has come to");
    deployment_tiles[0].x = static_cast<std::int16_t>(tile_x + 3);
    folded("how far across a region tile sits");
    deployment_tiles[0].y = static_cast<std::int16_t>(tile_y + 3);
    folded("and how far down");
    drops[0].unit_id = fallen + 1;
    folded("who a drop fell from");
    drops[0].claimant_id = claimant + 1;
    folded("who is owed it");
    drops[0].item_id = dropped + 1;
    folded("what fell");
    drops.clear();
    folded("whether anything has fallen at all");
    terrain[0] = sim::Terrain::heights;
    folded("the ground");
    movement_cost[0] = 3U;
    folded("what the ground charges");
    random.seed = 1;
    folded("the seed");
    random.positions[1] = 4;
    folded("how many numbers a stream has taken");
    deployment_tiles.pop_back();
    folded("the region a player arranges in");
    deploying = false;
    folded("whether the arranging phase is still open");
}

// The thirty-seven fields of `UnitSnapshot`, every one of them folded.
void every_field_of_a_character_is_folded() {
    const sim::EncounterSnapshot board = open_battle();
    const std::uint64_t named = sim::canonical_hash(board);

    const sim::UnitSnapshot baseline = open_character();
    sim::UnitSnapshot character = baseline;
    // The same canary over the character. `character` is a standalone object
    // that is only ever assigned through, never replaced, so these names keep
    // aliasing it for the whole of the table below.
    auto& [
        id, unit_type_id, side, spent_action_points, position, health,
        maximum_health, strength, power, defense, resistance, skill, luck,
        evasion, magic, movement, action_points, speed, acts_after_attacking,
        minimum_reach, maximum_reach, ability_ids, has_acted, has_moved,
        weapon_ids, crossings, accuracy, item_ids, item_counts, drop_item_id,
        drop_chance, reach_bonus, talk_record_id, departed, arrival_round,
        arrived, endures
    ] = character;

    const auto folded = [&](std::string_view field) {
        sim::EncounterSnapshot mutated = board;
        mutated.units[0] = character;
        expect(
            sim::canonical_hash(mutated) != named,
            std::string("two battles differing only in ") + std::string(field) +
                " do not share a canonical hash"
        );
        character = baseline;
    };

    id = 11;
    folded("which character this is");
    unit_type_id = 101;
    folded("what it is");
    side = sim::Side::second;
    folded("whose it is");
    spent_action_points = 2;
    folded("what its own turn has spent");
    position = {2, 2};
    folded("where it stands");
    health = 8;
    folded("its health");
    maximum_health = 13;
    folded("the health it started with");
    strength = 6;
    folded("its strength");
    power = 4;
    folded("the power of what it holds");
    defense = 3;
    folded("its defence");
    resistance = 2;
    folded("its resistance");
    skill = 5;
    folded("its skill");
    luck = 4;
    folded("its luck");
    evasion = 3;
    folded("its evasion");
    magic = 7;
    folded("its magic");
    movement = 4;
    folded("how far it walks");
    action_points = 3;
    folded("how much a turn of its is worth");
    speed = 8;
    folded("its speed");
    acts_after_attacking = false;
    folded("whether striking finishes it");
    minimum_reach = 1;
    folded("the floor of the band it holds");
    maximum_reach = 5;
    folded("the ceiling of that band");
    ability_ids.pop_back();
    folded("what it knows");
    has_acted = false;
    folded("whether it has finished this turn");
    has_moved = false;
    folded("whether it has spent this turn's walk");
    weapon_ids.pop_back();
    folded("what it carries to strike with");
    crossings = sim::crossing_every;
    folded("what ground it may cross");
    accuracy = 95;
    folded("how often what it holds lands");
    item_ids[0] = 502;
    folded("what is in its pack");
    item_counts[0] = 1;
    folded("how much of it is left");
    drop_item_id = 601;
    folded("what it would leave behind");
    drop_chance = 41;
    folded("how often it would");
    // The saturating case, spelled out because it is the whole reason this
    // field is folded at all rather than read out of `maximum_reach`. Both of
    // these characters snapshot a `maximum_reach` of 255 (`widened_reach`
    // saturates there), so a hash inferring the bonus from the band in hand
    // would call them the same battle, while a second carried weapon reaches
    // ten tiles further for one of them than for the other.
    maximum_reach = 255;
    reach_bonus = 10;
    const sim::UnitSnapshot saturated = character;
    reach_bonus = 20;
    {
        sim::EncounterSnapshot narrow = board;
        sim::EncounterSnapshot wide = board;
        narrow.units[0] = saturated;
        wide.units[0] = character;
        expect(
            narrow.units[0].maximum_reach == wide.units[0].maximum_reach,
            "a saturated band says the same number for both bonuses"
        );
        expect(
            sim::canonical_hash(narrow) != sim::canonical_hash(wide),
            "and the hash still tells the two characters apart"
        );
    }
    character = baseline;
    reach_bonus = 2;
    folded("what it adds to the reach of what it holds");
    talk_record_id = 701;
    folded("what talking to it records");
    departed = true;
    folded("whether it has been talked off the board");
    arrival_round = 4;
    folded("the round it comes in on");
    arrived = true;
    folded("whether it has");
    endures = false;
    folded("whether it can be felled at all");
}

// Every combination of the six optional parts of a character, on a board where
// nothing else about it varies.
//
// **This is the case the table above is structurally blind to**, and it is the
// case that matters most. That table starts from a character every optional
// part of which is already present, so what it measures is whether the hash
// notices a part's *value* changing. It never lets a part be absent, so it can
// say nothing about whether the hash notices *which* part is present. A fold
// that writes an optional part's bytes with nothing ahead of them to say which
// part they are answers that question wrongly. Four of the six carry a single
// small number; unannounced, `endures`, `has_moved`, one spent point and one
// tile of bonus reach are the same byte at the same offset, and four different
// boards are one battle.
//
// The consequence is worse than a wrong number. The hash is what
// `derive_random_seed` seeds the dice from, so two boards sharing it share
// every roll of hits and misses ever played out on them.
//
// So the six are enumerated rather than sampled: each part takes every state it
// has, including absent, and no two of the resulting boards may share a hash.
// Each state is one the engine reaches: somebody who may be talked to, someone
// talked away, a wave still marching, a wave that has landed, a character who
// cannot be felled, one who has spent this turn's walk, one part-way through a
// turn under `side_blocks`, and one written to reach past the weapon in its
// hand. The two pairs are enumerated as pairs because that is how they are
// reachable: `arrived` is false only for a character with a round to arrive on,
// and `departed` true only for one there was a talk to depart by.
void no_two_sparse_characters_share_a_hash() {
    sim::EncounterSnapshot board;
    board.width = 4;
    board.height = 3;
    board.terrain.assign(
        static_cast<std::size_t>(board.width) * board.height,
        sim::Terrain::open
    );
    board.movement_cost.assign(
        static_cast<std::size_t>(board.width) * board.height, 1U
    );
    sim::UnitSnapshot plain;
    plain.id = 10;
    plain.unit_type_id = 100;
    plain.side = sim::Side::first;
    plain.position = {0, 1};
    plain.health = 9;
    plain.maximum_health = 9;
    sim::UnitSnapshot foe = plain;
    foe.id = 20;
    foe.side = sim::Side::second;
    foe.position = {3, 1};
    board.units = {plain, foe};
    board.objectives = {{800, sim::ObjectiveState::pending}};

    // One optional part of a character, and everything it can say. The empty
    // description is the part saying nothing, which is the state every board
    // that never uses the gesture is in.
    using Part = std::vector<std::pair<std::string_view, void (*)(
        sim::UnitSnapshot&)>>;
    const std::vector<Part> parts = {
        {
            {"", nullptr},
            {"somebody may talk to it",
             [](sim::UnitSnapshot& unit) { unit.talk_record_id = 2; }},
            {"it has been talked away",
             [](sim::UnitSnapshot& unit) {
                 unit.talk_record_id = 2;
                 unit.departed = true;
             }},
        },
        {
            {"", nullptr},
            {"its wave is still marching",
             [](sim::UnitSnapshot& unit) {
                 unit.arrival_round = 2;
                 unit.arrived = false;
             }},
            {"its wave has landed",
             [](sim::UnitSnapshot& unit) { unit.arrival_round = 2; }},
        },
        {
            {"", nullptr},
            {"it cannot be felled",
             [](sim::UnitSnapshot& unit) { unit.endures = true; }},
        },
        {
            {"", nullptr},
            {"it has spent this turn's walk",
             [](sim::UnitSnapshot& unit) { unit.has_moved = true; }},
        },
        {
            {"", nullptr},
            {"it is one point into its turn",
             [](sim::UnitSnapshot& unit) { unit.spent_action_points = 1; }},
            {"it is two points into its turn",
             [](sim::UnitSnapshot& unit) { unit.spent_action_points = 2; }},
        },
        {
            {"", nullptr},
            {"it reaches one tile past what it holds",
             [](sim::UnitSnapshot& unit) { unit.reach_bonus = 1; }},
            {"it reaches two tiles past what it holds",
             [](sim::UnitSnapshot& unit) { unit.reach_bonus = 2; }},
        },
    };

    std::size_t combinations = 1;
    for (const Part& part : parts) combinations *= part.size();

    std::vector<std::string> descriptions;
    std::vector<std::uint64_t> hashes;
    descriptions.reserve(combinations);
    hashes.reserve(combinations);
    for (std::size_t index = 0; index < combinations; ++index) {
        sim::UnitSnapshot unit = plain;
        std::string description;
        std::size_t remaining = index;
        for (const Part& part : parts) {
            const auto& [phrase, change] = part[remaining % part.size()];
            remaining /= part.size();
            if (change == nullptr) continue;
            change(unit);
            if (!description.empty()) description += ", ";
            description += phrase;
        }
        sim::EncounterSnapshot mutated = board;
        mutated.units[0] = unit;
        descriptions.push_back(
            description.empty() ? "nothing optional about it" : description
        );
        hashes.push_back(sim::canonical_hash(mutated));
    }

    // Every collision is counted; only the first few are printed, because a
    // fold with no discriminator produces hundreds of them and the first
    // handful already name the shape.
    std::size_t collisions = 0;
    for (std::size_t left = 0; left < hashes.size(); ++left) {
        for (std::size_t right = left + 1; right < hashes.size(); ++right) {
            if (hashes[left] != hashes[right]) continue;
            ++collisions;
            if (collisions <= 6) {
                std::cerr << "FAIL: a character of which " << descriptions[left]
                          << ", and one of which " << descriptions[right]
                          << ", are two battles and one hash, " << std::hex
                          << hashes[left] << std::dec << '\n';
            }
        }
    }
    if (collisions > 6) {
        std::cerr << "FAIL: and " << (collisions - 6)
                  << " further pairs of battles sharing one hash\n";
    }
    failures += static_cast<int>(collisions);
}

// The eleven fields of `EncounterDefinition`, and the thirty-three of
// `UnitDefinition` below it.
//
// These two canaries catch the shape of omission the tables above cannot. The
// hash is a function of the snapshot, so a definition field that changes the
// battle and has nowhere in the snapshot to live never reaches the hash however
// carefully the fold is written. No amount of care over the fold would notice,
// because from inside the fold there is nothing missing.
void every_field_of_a_definition_reaches_the_snapshot() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 3;
    definition.units = {
        {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1},
        {20, 200, sim::Side::second, {4, 1}, 4, 3, 0, 1},
    };
    definition.objectives = {
        {800, sim::ObjectiveKind::defeat_all_opponents, sim::Side::first, 0, 0},
    };
    definition.turn_order = sim::TurnOrder::initiative;
    definition.terrain.assign(
        static_cast<std::size_t>(definition.width) * definition.height,
        sim::Terrain::open
    );
    definition.terrain[2] = sim::Terrain::water;
    definition.movement_cost.assign(
        static_cast<std::size_t>(definition.width) * definition.height, 1U
    );
    definition.movement_cost[3] = 2U;
    definition.random_seed = 0xfeedfacecafebeefULL;
    definition.deployment_tiles = {{0, 0}, {0, 2}};

    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the definition under test is valid");
    const sim::EncounterSnapshot battle = created.encounter.snapshot();

    // The same canary a third time. A thirteenth field stops the build here,
    // and the line that fixes it says where in the snapshot the new field
    // lands, or, for the three registries below, why it deliberately lands
    // nowhere.
    const auto& [
        width, height, units, abilities, objectives, turn_order, weapons, items,
        terrain, movement_cost, random_seed, deployment_tiles
    ] = definition;

    expect(battle.width == width, "the width reaches the snapshot");
    expect(battle.height == height, "the height reaches the snapshot");
    expect(
        battle.units.size() == units.size(),
        "the characters reach the snapshot, and each of their own fields is "
        "held to a verdict above"
    );
    expect(
        battle.objectives.size() == objectives.size() &&
            battle.objectives[0].id == objectives[0].id,
        "the objectives reach the snapshot as results, by identity"
    );
    expect(
        battle.turn_order == turn_order, "the turn order reaches the snapshot"
    );
    expect(battle.terrain == terrain, "the ground reaches the snapshot");
    expect(
        battle.movement_cost == movement_cost,
        "what the ground charges reaches the snapshot"
    );
    expect(
        battle.random.seed == random_seed,
        "the seed reaches the snapshot, and a definition that names none has "
        "one derived from this very hash"
    );
    expect(
        battle.deployment_tiles.size() == deployment_tiles.size(),
        "the region reaches the snapshot"
    );

    // And the three that deliberately do not, which is the boundary that makes
    // a canonical hash a fact about a battle rather than about the package the
    // battle travels in. A character names what it knows, what it carries and
    // what it has in its pack by identity, and those identities are folded;
    // these lists are where the identities resolve, and the records behind them
    // are content. A package can double in size while an arrangement of
    // characters stays exactly as it is, and hash exactly the same.
    //
    // The one number a record holds that the arrangement cannot restate (the
    // band, power and accuracy of the weapon in hand) is resolved onto the
    // character at creation, and is folded there.
    expect(
        abilities.empty() && weapons.empty() && items.empty(),
        "the registries are content the battle names rather than state it "
        "holds, and this definition names none"
    );
}

void every_field_of_a_character_definition_reaches_the_snapshot() {
    sim::EncounterDefinition definition;
    definition.width = 6;
    definition.height = 3;
    sim::AbilityDefinition arc;
    arc.id = 300;
    arc.power = 3;
    definition.abilities = {arc};
    definition.weapons = {{400, 3, 2, 4, 85}};
    definition.items = {{500, sim::ItemKind::restore, 4}};

    sim::UnitDefinition described;
    described.id = 10;
    described.unit_type_id = 100;
    described.side = sim::Side::first;
    described.position = {0, 1};
    described.health = 9;
    described.strength = 5;
    described.power = 1;
    described.defense = 2;
    described.resistance = 3;
    described.skill = 4;
    described.luck = 5;
    described.evasion = 6;
    described.magic = 7;
    described.movement = 2;
    described.action_points = 2;
    described.speed = 8;
    described.acts_after_attacking = true;
    described.minimum_reach = 1;
    described.maximum_reach = 1;
    described.ability_ids = {300};
    described.weapon_ids = {400};
    described.crossings = sim::crossing_water;
    described.accuracy = 100;
    described.item_ids = {500};
    described.item_counts = {2};
    described.drop_item_id = 600;
    described.drop_chance = 30;
    described.reach_bonus = 1;
    described.talk_record_id = 700;
    described.arrival_round = 2;
    described.arrival_every = 2;
    described.arrival_times = 3;
    described.endures = true;

    // The character under test is a wave, so somebody of its side has to be
    // standing when the battle opens: a board one of whose sides is entirely
    // still marching is refused as `missing_side`.
    sim::UnitDefinition standing = described;
    standing.id = 11;
    standing.position = {0, 0};
    standing.talk_record_id = 0;
    standing.arrival_round = 0;
    standing.arrival_every = 0;
    standing.arrival_times = 0;

    sim::UnitDefinition opponent = standing;
    opponent.id = 20;
    opponent.side = sim::Side::second;
    opponent.position = {5, 1};
    definition.units = {described, standing, opponent};

    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the character under test is valid");
    const sim::EncounterSnapshot battle = created.encounter.snapshot();
    const sim::UnitSnapshot* found = nullptr;
    for (const sim::UnitSnapshot& unit : battle.units) {
        if (unit.id == described.id) found = &unit;
    }
    expect(found != nullptr, "and stands on the board it was written onto");
    if (found == nullptr) return;
    const sim::UnitSnapshot& character = *found;

    // A twelfth... thirty-fourth field stops the build here.
    const auto& [
        id, unit_type_id, side, position, health, strength, power, defense,
        resistance, skill, luck, evasion, magic, movement, action_points, speed,
        acts_after_attacking, minimum_reach, maximum_reach, ability_ids,
        weapon_ids, crossings, accuracy, item_ids, item_counts, drop_item_id,
        drop_chance, reach_bonus, talk_record_id, arrival_round, arrival_every,
        arrival_times, endures
    ] = described;

    const auto carried = [&](std::string_view field, bool held) {
        expect(held, std::string("a character's ") + std::string(field) +
                         " reaches the snapshot");
    };

    carried("identity", character.id == id);
    carried("kind", character.unit_type_id == unit_type_id);
    carried("side", character.side == side);
    carried("tile", character.position == position);
    carried("health", character.health == health);
    carried("full health", character.maximum_health == health);
    carried("strength", character.strength == strength);
    carried("defence", character.defense == defense);
    carried("resistance", character.resistance == resistance);
    carried("skill", character.skill == skill);
    carried("luck", character.luck == luck);
    carried("evasion", character.evasion == evasion);
    carried("magic", character.magic == magic);
    carried("walk", character.movement == movement);
    carried("budget", character.action_points == action_points);
    carried("speed", character.speed == speed);
    carried(
        "licence to act after striking",
        character.acts_after_attacking == acts_after_attacking
    );
    carried("known abilities", character.ability_ids == ability_ids);
    carried("carried weapons", character.weapon_ids == weapon_ids);
    carried("crossings", character.crossings == crossings);
    carried("pack", character.item_ids == item_ids);
    carried("counts", character.item_counts == item_counts);
    carried("drop", character.drop_item_id == drop_item_id);
    carried("drop chance", character.drop_chance == drop_chance);
    carried("reach bonus", character.reach_bonus == reach_bonus);
    carried("talk record", character.talk_record_id == talk_record_id);
    carried("arrival round", character.arrival_round == arrival_round);
    carried("floor", character.endures == endures);

    // The four the engine resolves rather than copies. A character carrying a
    // weapon holds that weapon's power, band and accuracy rather than whatever
    // its own definition said, so a loader that flattened content cannot
    // disagree with the engine about what is in its hand. The ceiling of the
    // band is the weapon's widened by the character's own bonus. That
    // resolution is why these four are the definition's numbers only for a
    // character carrying nothing.
    const sim::WeaponDefinition& held = definition.weapons[0];
    carried("in-hand power", character.power == held.power);
    carried("in-hand floor", character.minimum_reach == held.minimum_reach);
    carried(
        "in-hand ceiling",
        character.maximum_reach == held.maximum_reach + reach_bonus
    );
    carried("in-hand accuracy", character.accuracy == held.accuracy);
    expect(
        power != held.power && minimum_reach != held.minimum_reach &&
            accuracy != held.accuracy,
        "and the definition said something else for each of them, so the "
        "resolution is what was measured"
    );

    // And the recurrence, which is the one pair with no field of its own in the
    // snapshot. That is deliberate: `create_encounter` spends it. Each
    // expanded character states the single round it comes on, so the pair
    // reaches the hash as several characters rather than as two numbers on one,
    // and nothing downstream has to expand anything a second time.
    std::size_t arrivals = 0;
    for (const sim::UnitSnapshot& unit : battle.units) {
        if (unit.unit_type_id == unit_type_id && !unit.arrived) ++arrivals;
    }
    expect(
        arrivals == arrival_times && arrival_every != 0,
        "a recurrence reaches the snapshot as the characters it means"
    );
}

// Two boards laid out identically under different orders are two battles, and
// the reference vector is not one of them.
void who_acts_next_names_the_battle() {
    const auto board = [](sim::TurnOrder order) {
        sim::EncounterDefinition definition;
        definition.width = 6;
        definition.height = 3;
        definition.turn_order = order;
        sim::UnitDefinition quick;
        quick.id = 10;
        quick.unit_type_id = 100;
        quick.side = sim::Side::first;
        quick.position = {0, 0};
        quick.health = 10;
        quick.strength = 3;
        quick.speed = 9;
        sim::UnitDefinition slow = quick;
        slow.id = 11;
        slow.position = {0, 2};
        slow.speed = 1;
        sim::UnitDefinition foe = quick;
        foe.id = 20;
        foe.side = sim::Side::second;
        foe.position = {5, 1};
        foe.speed = 5;
        definition.units = {quick, slow, foe};
        auto created = sim::create_encounter(definition);
        expect(static_cast<bool>(created), "each ordered board is valid");
        return std::move(created.encounter);
    };

    const sim::Encounter alternating = board(sim::TurnOrder::alternating);
    const sim::Encounter blocks = board(sim::TurnOrder::side_blocks);
    const sim::Encounter ordered = board(sim::TurnOrder::initiative);
    expect(
        alternating.canonical_hash() != blocks.canonical_hash(),
        "an alternating board and a side-blocks board are two battles"
    );
    expect(
        blocks.canonical_hash() != ordered.canonical_hash(),
        "a side-blocks board and an initiative board are two battles"
    );
    expect(
        alternating.canonical_hash() != ordered.canonical_hash(),
        "an alternating board and an initiative board are two battles"
    );
    // And the seed, which is the reason this mattered enough to move goldens
    // for: it is derived from the opening hash, so two battles sharing a hash
    // shared their dice as well.
    expect(
        alternating.snapshot().random.seed != blocks.snapshot().random.seed &&
            blocks.snapshot().random.seed != ordered.snapshot().random.seed,
        "and each of the three rolls its own numbers"
    );
}

}  // namespace

int main() {
    every_field_of_a_battle_is_folded();
    every_field_of_a_character_is_folded();
    no_two_sparse_characters_share_a_hash();
    every_field_of_a_definition_reaches_the_snapshot();
    every_field_of_a_character_definition_reaches_the_snapshot();
    who_acts_next_names_the_battle();
    return failures == 0 ? 0 : 1;
}
