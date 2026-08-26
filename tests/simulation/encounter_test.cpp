// SPDX-License-Identifier: MIT
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace sim = grandleon::simulation;

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A canonical hash written the way the goldens below are written, so a
// deliberate rule change reads the new value out of the failure rather than
// obliging somebody to add a print statement to find it.
std::string hex(std::uint64_t value) {
    std::string digits(16, '0');
    for (int index = 15; index >= 0; --index) {
        digits[static_cast<std::size_t>(index)] =
            "0123456789abcdef"[value & 0xFULL];
        value >>= 4U;
    }
    return "0x" + digits;
}

sim::EncounterDefinition definition() {
    return {
        4,
        3,
        {
            {20, 200, sim::Side::second, {2, 1}, 4, 3, 0, 1},
            {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1},
        }
    };
}

sim::Encounter make_encounter() {
    auto created = sim::create_encounter(definition());
    expect(static_cast<bool>(created), "representative encounter is valid");
    return std::move(created.encounter);
}

const sim::UnitSnapshot* unit(
    const sim::EncounterSnapshot& snapshot,
    sim::UnitId id
) {
    const auto found = std::find_if(
        snapshot.units.begin(),
        snapshot.units.end(),
        [id](const sim::UnitSnapshot& value) { return value.id == id; }
    );
    return found == snapshot.units.end() ? nullptr : &*found;
}

void validates_definition_atomically() {
    auto invalid = definition();
    invalid.units.back().position = invalid.units.front().position;
    expect(
        sim::create_encounter(invalid).error ==
            sim::CreateError::occupied_position,
        "occupied initial tile is rejected"
    );

    invalid = definition();
    invalid.units.pop_back();
    expect(
        sim::create_encounter(invalid).error ==
            sim::CreateError::missing_side,
        "both sides require a unit"
    );
}

// The damage formulae widen to `int32` and narrow back to the `int16` health is
// kept in, so a pair of unbounded stats wraps: strength 20 000 carrying power
// 20 000 narrows to −25 536, so being hit *heals* the target by twenty-five
// thousand. The forecast keeps its promise about that (both halves call the
// same wrapping function) and the rule does not, which is precisely why a
// number this shape is a refusal at creation rather than a promise kept about
// nonsense.
void a_stat_that_would_wrap_the_damage_is_refused() {
    const std::int16_t past = sim::maximum_stat + 1;
    {
        auto over = definition();
        over.units[1].strength = past;
        expect(
            sim::create_encounter(over).error == sim::CreateError::invalid_unit,
            "a strength past the bound is refused"
        );
    }
    {
        auto over = definition();
        over.units[1].power = past;
        expect(
            sim::create_encounter(over).error == sim::CreateError::invalid_unit,
            "and so is a power"
        );
    }
    {
        auto over = definition();
        over.units[0].defense = past;
        expect(
            sim::create_encounter(over).error == sim::CreateError::invalid_unit,
            "and a defence, which is the other side of the same subtraction"
        );
    }
    {
        auto over = definition();
        over.units[0].resistance = past;
        expect(
            sim::create_encounter(over).error == sim::CreateError::invalid_unit,
            "and a resistance"
        );
    }
    {
        auto over = definition();
        over.units[1].magic = past;
        expect(
            sim::create_encounter(over).error == sim::CreateError::invalid_unit,
            "and a magic, which is what a cast adds"
        );
    }
    {
        auto over = definition();
        over.abilities = {
            {700, sim::AbilityKind::damage, sim::DamageType::magical,
             sim::AreaShape::single, past, 1, 1}
        };
        over.units[1].ability_ids = {700};
        expect(
            sim::create_encounter(over).error ==
                sim::CreateError::invalid_ability,
            "an ability's power is bounded on the same terms"
        );
    }
    {
        auto over = definition();
        over.weapons = {{800, past, 1, 1}};
        over.units[1].weapon_ids = {800};
        expect(
            sim::create_encounter(over).error ==
                sim::CreateError::invalid_weapon,
            "and a weapon's, because it is the power the swing actually uses"
        );
    }

    // The bound itself is content, on both sides of one exchange, and what it
    // guarantees is that the arithmetic stays inside `int16` rather than that
    // the numbers are sensible.
    auto extreme = definition();
    extreme.units[0].position = {1, 1};
    extreme.units[0].health = 30000;
    extreme.units[0].defense = 0;
    extreme.units[1].strength = sim::maximum_stat;
    extreme.units[1].power = sim::maximum_stat;
    auto created = sim::create_encounter(extreme);
    expect(static_cast<bool>(created), "the bound itself is valid content");
    const auto forecast =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(forecast) && forecast.damage > 0 &&
            forecast.damage == sim::maximum_stat * 2,
        "and the hardest legal blow is still a positive number, got " +
            std::to_string(forecast.damage)
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(static_cast<bool>(struck), "the blow lands");
    expect(
        unit(created.encounter.snapshot(), 20)->health <
            unit(created.encounter.snapshot(), 20)->maximum_health,
        "and takes health off rather than putting it on"
    );
}

void moves_and_alternates_sides() {
    auto encounter = make_encounter();
    const auto moved = encounter.apply(
        {sim::CommandType::move, 10, {1, 1}, 0}
    );
    expect(static_cast<bool>(moved), "orthogonal move succeeds");
    // One action point, so the move both happens and ends the activation.
    expect(
        moved.events.size() == 2 &&
            moved.events.front().type == sim::EventType::unit_moved &&
            moved.events.back().type == sim::EventType::activation_ended,
        "move emits a semantic event and closes the activation"
    );
    const auto snapshot = encounter.snapshot();
    expect(
        unit(snapshot, 10)->position == sim::Position{1, 1},
        "move updates authoritative position"
    );
    expect(
        snapshot.active_side == sim::Side::second &&
            snapshot.activation_count == 1,
        "accepted activation advances to other side"
    );
}

void rejected_commands_are_atomic() {
    auto encounter = make_encounter();
    const auto before = encounter.canonical_hash();
    const auto rejected = encounter.apply(
        {sim::CommandType::move, 10, {1, 2}, 0}
    );
    expect(
        rejected.error == sim::CommandError::invalid_destination,
        "diagonal move is rejected"
    );
    expect(rejected.events.empty(), "rejection emits no gameplay event");
    expect(
        encounter.canonical_hash() == before,
        "rejection leaves canonical state unchanged"
    );

    const auto wrong_side = encounter.apply(
        {sim::CommandType::wait, 20, {}, 0}
    );
    expect(
        wrong_side.error == sim::CommandError::wrong_side,
        "inactive side cannot act"
    );
    expect(
        encounter.canonical_hash() == before,
        "wrong-side rejection is atomic"
    );
}

void resolves_combat_and_objective() {
    auto encounter = make_encounter();
    expect(
        static_cast<bool>(encounter.apply(
            {sim::CommandType::move, 10, {1, 1}, 0}
        )),
        "first side approaches"
    );
    expect(
        static_cast<bool>(encounter.apply(
            {sim::CommandType::wait, 20, {}, 0}
        )),
        "second side waits"
    );
    const auto first_attack = encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(static_cast<bool>(first_attack), "adjacent hostile attack succeeds");
    expect(
        first_attack.events.front().amount == 3,
        "damage is strength minus defense"
    );
    expect(
        unit(encounter.snapshot(), 20)->health == 1,
        "damage changes target health"
    );
    expect(
        static_cast<bool>(encounter.apply(
            {sim::CommandType::wait, 20, {}, 0}
        )),
        "surviving target can activate"
    );
    const auto final_attack = encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    const auto snapshot = encounter.snapshot();
    expect(static_cast<bool>(final_attack), "final attack succeeds");
    expect(
        unit(snapshot, 20)->health == 0,
        "health clamps at zero"
    );
    expect(
        snapshot.outcome == sim::Outcome::first_side_won,
        "defeating all opponents completes objective"
    );
    expect(
        final_attack.events.size() == 3 &&
            final_attack.events[1].type == sim::EventType::unit_defeated &&
            final_attack.events[2].type ==
                sim::EventType::encounter_completed,
        "completion emits damage, defeat, and objective events"
    );
    const auto completed_hash = encounter.canonical_hash();
    expect(
        encounter.apply({sim::CommandType::wait, 10, {}, 0}).error ==
            sim::CommandError::encounter_complete,
        "completed encounter rejects further commands"
    );
    expect(
        encounter.canonical_hash() == completed_hash,
        "post-completion rejection is atomic"
    );
}

void enforces_attack_rules_and_minimum_damage() {
    auto encounter = make_encounter();
    const auto before = encounter.canonical_hash();
    expect(
        encounter.apply({sim::CommandType::attack, 10, {}, 10}).error ==
            sim::CommandError::friendly_target,
        "a strike aimed at an ally is refused"
    );
    expect(
        encounter.canonical_hash() == before,
        "and the refusal is atomic"
    );
    expect(
        encounter.apply({sim::CommandType::attack, 10, {}, 20}).error ==
            sim::CommandError::target_out_of_range,
        "non-adjacent attack is rejected"
    );

    auto armored = definition();
    armored.units.front().position = {1, 1};
    armored.units.front().defense = 100;
    auto created = sim::create_encounter(armored);
    const auto attacked = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(
        attacked.events.front().amount == 1,
        "damage has an explicit minimum of one"
    );
}

void forecasts_a_chance_and_the_numbers_behind_it() {
    // What the forecast promises, now that an attack can miss: a stated chance
    // and the exact numbers behind it. The refusal must still be the refusal
    // apply() would return; the chance must be the very number apply() rolls
    // against; and when the roll lands, apply() must deliver exactly the
    // numbers forecast and nothing else. The tests after this one hold the
    // other half: that a miss takes exactly zero, and what the dice cost.
    auto encounter = make_encounter();
    auto snapshot = encounter.snapshot();

    expect(
        sim::forecast_attack(snapshot, 10, 10).error ==
            sim::CommandError::friendly_target,
        "the forecast refuses a strike aimed at an ally"
    );
    expect(
        sim::forecast_attack(snapshot, 10, 99).error ==
            sim::CommandError::unknown_target,
        "forecast rejects an unknown target"
    );
    expect(
        sim::forecast_attack(snapshot, 20, 10).error ==
            sim::CommandError::wrong_side,
        "forecast refuses the side not acting"
    );
    const auto out_of_range = sim::forecast_attack(snapshot, 10, 20);
    expect(
        out_of_range.error == sim::CommandError::target_out_of_range &&
            out_of_range.error ==
                encounter.apply({sim::CommandType::attack, 10, {}, 20}).error,
        "forecast and apply agree on an out-of-range attack"
    );

    // Step adjacent; the one-point move hands the turn over, so the forecast
    // now speaks for the second side.
    expect(
        static_cast<bool>(
            encounter.apply({sim::CommandType::move, 10, {1, 1}, 0})
        ),
        "closing move succeeds"
    );
    snapshot = encounter.snapshot();
    expect(
        sim::forecast_attack(snapshot, 10, 20).error ==
            sim::CommandError::wrong_side,
        "forecast tracks whose turn it is"
    );
    const auto promised = sim::forecast_attack(snapshot, 20, 10);
    expect(
        static_cast<bool>(promised) && promised.damage == 2 &&
            promised.target_health_after == 4 && !promised.lethal,
        "forecast prices the strike from the shared formula"
    );
    // The other half of the promise. Unit 10 survives at four, stands one tile
    // away, and its own band is one, so it answers. The forecast has to say so
    // before the player commits, because apply() resolves both halves under
    // the one command.
    expect(
        promised.counter && promised.counter_damage == 3 &&
            promised.attacker_health_after == 1 && !promised.counter_lethal,
        "forecast prices the counter the same command will provoke"
    );
    const auto struck = encounter.apply({sim::CommandType::attack, 20, {}, 10});
    expect(
        static_cast<bool>(struck) &&
            struck.events.front().amount == promised.damage &&
            unit(encounter.snapshot(), 10)->health ==
                promised.target_health_after,
        "apply delivers the forecast numbers exactly"
    );
    expect(
        struck.events.size() >= 2 &&
            struck.events[1].type == sim::EventType::unit_damaged &&
            struck.events[1].unit_id == 20 &&
            struck.events[1].related_unit_id == 10 &&
            struck.events[1].amount == promised.counter_damage &&
            unit(encounter.snapshot(), 20)->health ==
                promised.attacker_health_after,
        "apply delivers the forecast counter exactly"
    );

    // A finishing blow forecasts as lethal, and never below the health floor.
    auto fragile = definition();
    fragile.units.front().health = 2;
    fragile.units.front().position = {1, 1};
    auto created = sim::create_encounter(fragile);
    const auto lethal = sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(lethal) && lethal.damage == 3 &&
            lethal.target_health_after == 0 && lethal.lethal,
        "forecast marks a finishing blow and floors health at zero"
    );
    // And a *certain* finishing blow is answered by nobody, which the forecast
    // has to report rather than leave the client to infer from `lethal`.
    expect(
        !lethal.counter && lethal.counter_damage == 0 &&
            lethal.attacker_health_after ==
                unit(created.encounter.snapshot(), 10)->health &&
            !lethal.counter_lethal,
        "a certain lethal strike forecasts no counter and leaves the attacker "
        "whole"
    );

    // Every forecast above priced a weapon that always lands, so every one of
    // them said so, and none of them moved the dice.
    expect(
        promised.hit_chance == 100 && promised.counter_chance == 100 &&
            lethal.hit_chance == 100,
        "a certain strike forecasts a hundred per cent"
    );
    expect(
        encounter.snapshot().random.positions[
            static_cast<std::size_t>(grandleon::core::RandomStream::hit)
        ] == 0,
        "a battle of certain weapons never touches the hit stream"
    );
}

// The board every chance test below is fought on: two adjacent units, each
// inside the other's band, so an attack and its answer both happen under one
// command. Blue strikes for 3 and red answers for 2, which are the same
// numbers the certain forecasts above pin.
sim::EncounterDefinition chancy(
    std::uint64_t seed,
    std::uint8_t attacker_accuracy,
    std::uint8_t defender_accuracy,
    std::int16_t defender_health = 4
) {
    sim::EncounterDefinition definition;
    definition.width = 4;
    definition.height = 3;
    definition.units = {
        {20, 200, sim::Side::second, {1, 1}, defender_health, 3, 0, 1, 0, 0, 0,
         0, 0, 1, 1,
         1, false, 1, 1, {}, {}, sim::crossing_none, defender_accuracy},
        {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1,
         {}, {}, sim::crossing_none, attacker_accuracy},
    };
    definition.random_seed = seed;
    return definition;
}

std::uint64_t hit_draws(const sim::EncounterSnapshot& snapshot) {
    return snapshot.random.positions[
        static_cast<std::size_t>(grandleon::core::RandomStream::hit)
    ];
}

void attacks_land_or_miss_by_the_stated_chance() {
    // Two seeds, chosen because they make the first two numbers of the hit
    // stream fall on opposite sides of a half chance: seed 1 rolls 26 then 88,
    // seed 3 rolls 82 then 14. Against an accuracy of 50 that is hit-then-miss
    // and miss-then-hit, which is every outcome an exchange has, pinned
    // exactly rather than sampled.
    auto landing = sim::create_encounter(chancy(1, 50, 100));
    expect(static_cast<bool>(landing), "the landing encounter is valid");
    const auto promise =
        sim::forecast_attack(landing.encounter.snapshot(), 10, 20);
    expect(
        promise.hit_chance == 50 && promise.damage == 3 &&
            promise.target_health_after == 1 && !promise.lethal,
        "the forecast states the real chance and the exact on-hit numbers"
    );
    expect(
        promise.counter && promise.counter_chance == 100 &&
            promise.counter_damage == 2 && promise.attacker_health_after == 4,
        "and the same for the answer it will provoke"
    );
    const auto struck =
        landing.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    auto after = landing.encounter.snapshot();
    expect(
        static_cast<bool>(struck) &&
            struck.events.front().type == sim::EventType::unit_damaged &&
            struck.events.front().amount == promise.damage &&
            unit(after, 20)->health == promise.target_health_after &&
            unit(after, 10)->health == promise.attacker_health_after,
        "on a hit apply delivers exactly the numbers forecast"
    );
    expect(
        hit_draws(after) == 1,
        "a sub-certain strike takes exactly one number, and a certain answer "
        "none"
    );

    // The same board, the same chance, the seed that misses.
    auto missing = sim::create_encounter(chancy(3, 50, 100));
    const auto missed_promise =
        sim::forecast_attack(missing.encounter.snapshot(), 10, 20);
    expect(
        missed_promise.hit_chance == promise.hit_chance &&
            missed_promise.damage == promise.damage,
        "the forecast cannot know which way the roll will fall"
    );
    const auto swung =
        missing.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    after = missing.encounter.snapshot();
    expect(
        static_cast<bool>(swung) &&
            swung.events.front().type == sim::EventType::attack_missed &&
            swung.events.front().unit_id == 20 &&
            swung.events.front().related_unit_id == 10 &&
            swung.events.front().amount == 0,
        "a miss is an event of its own, taking nothing"
    );
    expect(
        unit(after, 20)->health == 4,
        "on a miss the target loses exactly zero"
    );
    // And the answer still comes: a blow that misses is a blow you were in
    // range of, and only a felled defender is silent.
    expect(
        swung.events.size() >= 2 &&
            swung.events[1].type == sim::EventType::unit_damaged &&
            swung.events[1].unit_id == 10 && swung.events[1].amount == 2 &&
            unit(after, 10)->health == 4,
        "a missed strike is still answered by a defender left standing"
    );
    expect(hit_draws(after) == 1, "and the miss cost exactly one number");
}

void the_hit_stream_is_consumed_in_one_fixed_order() {
    // Attack first, answer second, and nothing else. Seed 1 rolls 26 then 88,
    // so with both weapons at 50 the strike lands and the answer does not.
    // That is only true if the two rolls are taken in that order.
    auto exchange = sim::create_encounter(chancy(1, 50, 50));
    const auto both =
        exchange.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    auto after = exchange.encounter.snapshot();
    expect(
        static_cast<bool>(both) && both.events.size() >= 2 &&
            both.events[0].type == sim::EventType::unit_damaged &&
            both.events[0].unit_id == 20 &&
            both.events[1].type == sim::EventType::attack_missed &&
            both.events[1].unit_id == 10 &&
            unit(after, 20)->health == 1 && unit(after, 10)->health == 6,
        "the attack takes the first number and the counter the second"
    );
    expect(
        hit_draws(after) == 2,
        "an uncertain exchange takes exactly two numbers"
    );

    // Reversed by the seed rather than by the rule: seed 3 rolls 82 then 14.
    auto reversed = sim::create_encounter(chancy(3, 50, 50));
    const auto answered =
        reversed.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    after = reversed.encounter.snapshot();
    expect(
        static_cast<bool>(answered) && answered.events.size() >= 2 &&
            answered.events[0].type == sim::EventType::attack_missed &&
            answered.events[1].type == sim::EventType::unit_damaged &&
            answered.events[1].unit_id == 10 &&
            unit(after, 20)->health == 4 && unit(after, 10)->health == 4,
        "the same two numbers in the same order, falling the other way"
    );
    expect(hit_draws(after) == 2, "and still exactly two");

    // A counter that cannot happen draws nothing. The defender is on 3, the
    // strike takes 3, and seed 1 lands it, so the answer never reaches its
    // roll and the stream stops at one.
    auto finished = sim::create_encounter(chancy(1, 50, 50, 3));
    const auto killed =
        finished.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    after = finished.encounter.snapshot();
    expect(
        static_cast<bool>(killed) && unit(after, 20)->health == 0 &&
            unit(after, 10)->health == 6,
        "a landed finishing blow is not answered"
    );
    expect(
        hit_draws(after) == 1,
        "a counter that cannot occur consumes nothing"
    );

    // A certain weapon consumes nothing at all, which is what keeps every
    // package written before this playing exactly as it did.
    auto certain = sim::create_encounter(chancy(1, 100, 100));
    expect(
        static_cast<bool>(
            certain.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ) && hit_draws(certain.encounter.snapshot()) == 0,
        "certainty draws no number"
    );

    // And an impossible strike is not a coin either: zero consumes nothing.
    auto hopeless = sim::create_encounter(chancy(1, 0, 100));
    const auto flailed =
        hopeless.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    after = hopeless.encounter.snapshot();
    expect(
        static_cast<bool>(flailed) &&
            flailed.events.front().type == sim::EventType::attack_missed &&
            unit(after, 20)->health == 4 && hit_draws(after) == 0,
        "a strike that can never land misses without drawing"
    );

    // A refused attack never reaches the roll, so the Nth number belongs to
    // the Nth strike the engine actually resolved.
    auto refused = sim::create_encounter(chancy(1, 50, 50));
    const auto rejected =
        refused.encounter.apply({sim::CommandType::attack, 10, {}, 99});
    expect(
        rejected.error == sim::CommandError::unknown_target &&
            hit_draws(refused.encounter.snapshot()) == 0,
        "a refusal draws nothing"
    );
}

// The board the folded-chance tests are fought on: two adjacent units, each
// inside the other's band. The four richer stats are handed in per side so one
// fixture can pose every case the formula has.
struct RicherStats final {
    std::int16_t skill{};
    std::int16_t luck{};
    std::int16_t evasion{};
    std::int16_t magic{};
};

sim::EncounterDefinition folded(
    std::uint8_t attacker_accuracy,
    RicherStats attacker,
    RicherStats defender,
    std::uint8_t defender_accuracy = 100,
    std::uint64_t seed = 1
) {
    sim::EncounterDefinition definition;
    definition.width = 4;
    definition.height = 3;
    definition.units = {
        {20, 200, sim::Side::second, {1, 1}, 9, 3, 0, 1, 0, defender.skill,
         defender.luck, defender.evasion, defender.magic, 1, 1, 1, false, 1, 1,
         {}, {}, sim::crossing_none, defender_accuracy},
        {10, 100, sim::Side::first, {0, 1}, 9, 4, 0, 1, 0, attacker.skill,
         attacker.luck, attacker.evasion, attacker.magic, 1, 1, 1, false, 1, 1,
         {}, {}, sim::crossing_none, attacker_accuracy},
    };
    definition.random_seed = seed;
    return definition;
}

std::uint8_t stated_chance(const sim::EncounterDefinition& definition) {
    auto created = sim::create_encounter(definition);
    return sim::forecast_attack(created.encounter.snapshot(), 10, 20)
        .hit_chance;
}

void the_hit_chance_folds_both_units_into_one_number() {
    // Every term of the formula, one at a time, read off the forecast. It is
    // the same function `apply` rolls against, so pinning it here pins both.
    expect(
        stated_chance(folded(90, {}, {})) == 90,
        "with every new stat at zero the chance is the authored accuracy"
    );
    expect(
        stated_chance(folded(90, {5, 0, 0, 0}, {})) == 95,
        "the striker's skill raises the chance by its own points"
    );
    expect(
        stated_chance(folded(90, {}, {0, 0, 3, 0})) == 87,
        "the struck unit's evasion lowers it by its own points"
    );
    expect(
        stated_chance(folded(90, {0, 3, 0, 0}, {})) == 93,
        "luck on the striker raises it"
    );
    expect(
        stated_chance(folded(90, {}, {0, 3, 0, 0})) == 87,
        "and luck on the struck unit lowers it: the one term on both sides"
    );
    expect(
        stated_chance(folded(90, {0, 4, 0, 0}, {0, 4, 0, 0})) == 90,
        "equal luck cancels, because luck is a relative advantage"
    );
    expect(
        stated_chance(folded(90, {5, 2, 0, 0}, {0, 1, 3, 0})) == 93,
        "all four terms fold into the one number: 90 + 5 + 2 - 3 - 1"
    );

    // The clamp, on both ends, and what a clamped chance costs the stream.
    expect(
        stated_chance(folded(90, {40, 0, 0, 0}, {})) == 100,
        "a folded sum above a hundred is a hundred, not a hundred and thirty"
    );
    expect(
        stated_chance(folded(10, {}, {0, 0, 40, 0})) == 0,
        "and a sum below zero is zero, not a negative percentage"
    );
    auto certain = sim::create_encounter(folded(90, {40, 0, 0, 0}, {}));
    const auto swung =
        certain.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    expect(
        static_cast<bool>(swung) &&
            hit_draws(certain.encounter.snapshot()) == 0,
        "a chance folded up to a hundred draws no number, as an authored "
        "hundred always did"
    );
    auto hopeless = sim::create_encounter(folded(10, {}, {0, 0, 40, 0}));
    const auto flailed =
        hopeless.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    expect(
        static_cast<bool>(flailed) &&
            flailed.events.front().type == sim::EventType::attack_missed &&
            hit_draws(hopeless.encounter.snapshot()) == 0,
        "and a chance folded down to zero misses without drawing"
    );
}

void the_folded_chance_is_the_one_apply_rolls_against() {
    // The whole honesty rule in one place: what the forecast states is the
    // number the roll is taken against, not the weapon's authored accuracy.
    // Seed 1 rolls 26 first. Against a folded 25 that misses and against a
    // folded 27 it lands, so the two outcomes bracket the number and neither
    // is explicable by the authored 50 both boards share.
    auto missing =
        sim::create_encounter(folded(50, {}, {0, 0, 25, 0}, 100, 1));
    const auto low = sim::forecast_attack(missing.encounter.snapshot(), 10, 20);
    expect(low.hit_chance == 25, "the forecast states the folded 25");
    const auto missed =
        missing.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    expect(
        static_cast<bool>(missed) &&
            missed.events.front().type == sim::EventType::attack_missed &&
            hit_draws(missing.encounter.snapshot()) == 1,
        "and the roll is taken against it, missing, for exactly one number"
    );

    auto landing =
        sim::create_encounter(folded(50, {}, {0, 0, 23, 0}, 100, 1));
    const auto high =
        sim::forecast_attack(landing.encounter.snapshot(), 10, 20);
    expect(high.hit_chance == 27, "the forecast states the folded 27");
    const auto landed =
        landing.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after = landing.encounter.snapshot();
    expect(
        static_cast<bool>(landed) &&
            landed.events.front().type == sim::EventType::unit_damaged &&
            landed.events.front().amount == high.damage &&
            unit(after, 20)->health == high.target_health_after,
        "the same roll lands against the higher folded chance, for the "
        "numbers the forecast promised"
    );
}

void a_counter_folds_the_defender_the_other_way_round() {
    // The counter's chance swaps the roles: the defender's skill and luck add,
    // the original attacker's evasion and luck subtract. The attacker here has
    // evasion 4 and the defender skill 6, against a defending weapon authored
    // at 90. The fixture is chosen so that neither of those numbers may touch
    // the outgoing strike, which stays at its own authored 100.
    auto created = sim::create_encounter(
        folded(100, {0, 0, 4, 0}, {6, 0, 0, 0}, 90)
    );
    const auto promise =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        promise.hit_chance == 100,
        "a unit's own evasion does nothing to the strike it makes, and the "
        "target's skill does nothing to the strike it receives"
    );
    expect(
        promise.counter && promise.counter_chance == 92,
        "and the answer by the defender's own skill less the attacker's "
        "evasion: 90 + 6 - 4"
    );
}

void a_magical_cast_is_priced_by_the_caster_and_a_physical_one_is_not() {
    sim::EncounterDefinition definition;
    definition.width = 6;
    definition.height = 3;
    // A caster with magic 2 and strength 5, so a physical cast folding either
    // stat in would be visible. The target resists 1 and defends 3.
    definition.units = {
        {10, 100, sim::Side::first, {0, 1}, 9, 5, 0, 1, 0, 0, 0, 0, 2, 1, 2, 1,
         true, 1, 1, {1, 2}},
        {20, 200, sim::Side::second, {2, 1}, 20, 3, 0, 3, 1},
    };
    definition.abilities = {
        {1, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::single, 4, 1, 3},
        {2, sim::AbilityKind::damage, sim::DamageType::physical,
         sim::AreaShape::single, 6, 1, 3},
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the casting encounter is valid");
    const auto magical =
        created.encounter.apply({sim::CommandType::ability, 10, {2, 1}, 0, 1});
    auto after = created.encounter.snapshot();
    expect(
        static_cast<bool>(magical) && magical.events.front().amount == 5 &&
            unit(after, 20)->health == 15,
        "a magical cast deals magic + power - resistance: 2 + 4 - 1"
    );
    const auto physical =
        created.encounter.apply({sim::CommandType::ability, 10, {2, 1}, 0, 2});
    after = created.encounter.snapshot();
    expect(
        static_cast<bool>(physical) && physical.events.front().amount == 3 &&
            unit(after, 20)->health == 12,
        "a physical cast deals power - defense and nothing else: 6 - 3, with "
        "neither the caster's strength nor its magic added"
    );
}

void a_sub_certain_lethal_strike_is_still_answered() {
    // The one place the miss changes the counter rule rather than only its
    // odds: a blow that would fell the target leaves it standing whenever it
    // misses, so the forecast has to say an answer may come, and how often it
    // lands.
    auto created = sim::create_encounter(chancy(3, 50, 50, 3));
    const auto promise =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        promise.lethal && promise.hit_chance == 50 && promise.counter &&
            promise.counter_chance == 50 && promise.counter_damage == 2 &&
            promise.attacker_health_after == 4,
        "a sub-certain finishing blow forecasts the answer a miss would earn"
    );
    // Seed 3 misses, so the answer is exactly the one forecast.
    const auto swung =
        created.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after = created.encounter.snapshot();
    expect(
        static_cast<bool>(swung) && unit(after, 20)->health == 3 &&
            unit(after, 10)->health == promise.attacker_health_after,
        "and delivers it when the blow misses"
    );
}

void an_ability_rolls_once_per_unit_it_damages() {
    // Accuracy on a cast is per unit the area covers, in ascending identifier
    // order.
    sim::EncounterDefinition definition;
    definition.width = 6;
    definition.height = 3;
    definition.units = {
        {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1,
         {1}},
        {20, 200, sim::Side::second, {4, 1}, 6, 3, 0, 1},
        {30, 200, sim::Side::second, {5, 1}, 6, 3, 0, 1},
    };
    // A cross centred on the further opponent catches both.
    definition.abilities = {
        {1, sim::AbilityKind::damage, sim::DamageType::physical,
         sim::AreaShape::cross, 5, 1, 5, 0, 50}
    };
    definition.random_seed = 1;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the area encounter is valid");
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {5, 1}, 0, 1}
    );
    const auto after = created.encounter.snapshot();
    // Seed 1: 26 lands on unit 20, 88 misses unit 30.
    expect(
        static_cast<bool>(cast) && unit(after, 20)->health == 2 &&
            unit(after, 30)->health == 6,
        "each covered unit is rolled for separately, in identifier order"
    );
    expect(
        hit_draws(after) == 2,
        "a damaging area takes one number per unit it covers"
    );
}

void spends_action_points_within_one_activation() {
    // Two points, and explicitly allowed to keep acting after striking.
    sim::EncounterDefinition definition{
        4,
        3,
        {
            {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             2, 1, true, 1, 1, {}},
        },
        {},
        {}
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "two-point encounter is valid");

    const auto moved = created.encounter.apply(
        {sim::CommandType::move, 10, {1, 1}, 0, 0}
    );
    expect(static_cast<bool>(moved), "first point buys a move");
    expect(
        created.encounter.snapshot().active_side == sim::Side::first,
        "the activation is still the first side's"
    );
    expect(
        created.encounter.snapshot().remaining_action_points == 1,
        "one point remains"
    );
    // A different unit cannot barge into an activation already in progress.
    expect(
        created.encounter.apply({sim::CommandType::wait, 20, {}, 0, 0}).error ==
            sim::CommandError::wrong_side,
        "the other side still cannot act"
    );

    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0}
    );
    expect(static_cast<bool>(struck), "second point buys an attack");
    expect(
        created.encounter.snapshot().active_side == sim::Side::second,
        "spending the last point passes the turn"
    );
    expect(
        created.encounter.apply({sim::CommandType::wait, 10, {}, 0, 0}).error ==
            sim::CommandError::wrong_side,
        "the spent unit cannot act again"
    );
}

void striking_ends_an_activation_by_default() {
    sim::EncounterDefinition definition{
        4,
        3,
        {
            {20, 200, sim::Side::second, {1, 1}, 9, 2, 0, 1, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             2, 1, false, 1, 1, {}},
        },
        {},
        {}
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "default-policy encounter is valid");
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0}
    );
    expect(static_cast<bool>(struck), "the attack is accepted");
    // Two points, but striking forfeits the rest unless authored otherwise.
    expect(
        created.encounter.snapshot().active_side == sim::Side::second,
        "striking ends the activation even with a point left"
    );
}

void initiative_order_runs_fastest_first() {
    sim::EncounterDefinition definition{
        6,
        3,
        {
            // id, type, side, pos, hp, str, pow, def, res, mv, ap, speed,
            // after, min, max, abilities
            {10, 100, sim::Side::first, {0, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {11, 101, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 9, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 5, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::initiative
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "initiative encounter is valid");

    // Speed nine acts first even though a slower ally sorts earlier by id.
    expect(
        created.encounter.snapshot().active_unit_id == 11,
        "the fastest unit opens the round"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 11, {}, 0, 0}
        )),
        "the fastest unit acts"
    );
    // Speed five is next, and it belongs to the other side: initiative
    // interleaves rather than alternating.
    expect(
        created.encounter.snapshot().active_unit_id == 20,
        "the second fastest acts next regardless of side"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 20, {}, 0, 0}
        )),
        "the second fastest acts"
    );
    expect(
        created.encounter.snapshot().active_unit_id == 10,
        "the slowest closes the round"
    );
    expect(
        created.encounter.apply({sim::CommandType::wait, 11, {}, 0, 0}).error ==
            sim::CommandError::already_acted,
        "a unit that already acted cannot act again this round, and is told so"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0, 0}
        )),
        "the slowest acts"
    );
    expect(
        created.encounter.snapshot().round == 1,
        "a completed pass starts a new round"
    );
    expect(
        created.encounter.snapshot().active_unit_id == 11,
        "the new round opens with the fastest again"
    );
}

void side_blocks_run_one_side_at_a_time() {
    sim::EncounterDefinition definition{
        6,
        3,
        {
            {10, 100, sim::Side::first, {0, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {11, 101, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 9, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 5, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::side_blocks
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "side-block encounter is valid");

    // The block belongs to the first side, and the engine names nobody in it.
    expect(
        created.encounter.snapshot().active_side == sim::Side::first &&
            created.encounter.snapshot().active_unit_id == 0,
        "the first side's block opens with no actor named"
    );
    // So the slower of the two may go first, and the player is never made to
    // hunt for the one character the engine happened to name.
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0, 0}
        )),
        "any character of the active side may be the one that acts"
    );
    // Spent, and told so by name rather than by "another unit is acting".
    expect(
        created.encounter.apply({sim::CommandType::wait, 10, {}, 0, 0}).error ==
            sim::CommandError::already_acted,
        "and having acted it cannot act again"
    );
    // Still the first side's block: it does not end until everybody in it has
    // gone, whatever order they went in.
    expect(
        created.encounter.snapshot().active_side == sim::Side::first &&
            created.encounter.snapshot().active_unit_id == 0,
        "the block stays open while one of its characters has a turn in hand"
    );
    expect(
        created.encounter.apply({sim::CommandType::wait, 20, {}, 0, 0}).error ==
            sim::CommandError::wrong_side,
        "and the other side may not step into it"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 11, {}, 0, 0}
        )),
        "the block's last character takes its turn"
    );
    // And only now does the second side's block open.
    expect(
        created.encounter.snapshot().active_side == sim::Side::second &&
            created.encounter.snapshot().active_unit_id == 0,
        "the whole first side goes before the second, as the name says"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 20, {}, 0, 0}
        )),
        "the second side's only character takes its turn"
    );
    // Everybody has acted, so the round turns and the first side's block opens
    // again with everybody's turn back in hand.
    const auto turned = created.encounter.snapshot();
    expect(
        turned.round == 1 && turned.active_side == sim::Side::first &&
            turned.active_unit_id == 0,
        "and the round turns back to the first side's block"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 11, {}, 0, 0}
        )),
        "the new round gives everybody their turn back"
    );
}

// Why a side's block interleaves freely, and the sequence that proves it.
//
// If the first accepted command claimed the side's activation, then moving one
// character while it still had a point in hand would refuse a move to every
// other character on that side. Nothing on screen says that attacking or
// waiting is the way out of that, so the board reads as stopped. A player who
// walks one character sees the rest of the company go dead. Between the classic
// model, where moving commits you to that character until it strikes or waits,
// and free interleaving, this engine takes free interleaving, and the sequence
// below is the one it has to accept: move one, move a second, come back and
// strike with the first.
void turns_interleave_freely_inside_a_block() {
    sim::EncounterDefinition definition{
        9,
        3,
        {
            // Two points and three steps each, so each of the two can walk and
            // still have an action in hand. That in-hand point is exactly what
            // would lock the side under an activation-claiming model.
            {10, 100, sim::Side::first, {3, 0}, 8, 3, 0, 0, 0, 0, 0, 0, 0, 3,
             2, 1, false, 1, 1, {}},
            {11, 101, sim::Side::first, {3, 2}, 8, 3, 0, 0, 0, 0, 0, 0, 0, 3,
             2, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 1}, 8, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             2, 1, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::side_blocks
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the interleaving board is valid");

    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 10, {4, 1}, 0})
        ),
        "the first character walks"
    );
    // Nothing was claimed by that. This is the whole of it: the block names a
    // side and goes on naming only a side.
    const auto after_first = created.encounter.snapshot();
    expect(
        after_first.active_unit_id == 0 &&
            after_first.remaining_action_points == 0,
        "and claims no activation doing it"
    );
    expect(
        unit(after_first, 10)->has_moved && !unit(after_first, 10)->has_acted &&
            unit(after_first, 10)->spent_action_points == 1,
        "the walk is spent on the character that made it and finishes nobody"
    );
    // The command an activation-claiming model would refuse
    // `activation_in_progress`.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 11, {2, 2}, 0})
        ),
        "a second character walks while the first still holds a point"
    );
    expect(
        unit(created.encounter.snapshot(), 11)->has_moved &&
            unit(created.encounter.snapshot(), 10)->has_moved,
        "and both are recorded as part-way through their own turns"
    );
    // And the half of the sequence that made the board read as stopped: coming
    // back to the first character afterwards.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "the first character comes back and strikes after the second walked"
    );
    // A strike finishes the character, so the walk it never took is gone with
    // it. That is the other half of the rule: attack, and that one is done.
    const auto after_strike = created.encounter.snapshot();
    expect(
        unit(after_strike, 10)->has_acted &&
            !unit(after_strike, 10)->has_moved &&
            unit(after_strike, 10)->spent_action_points == 0,
        "a character that has struck is finished and carries no turn state"
    );
    expect(
        created.encounter.apply({sim::CommandType::move, 10, {4, 0}, 0})
                .error == sim::CommandError::already_acted,
        "and having struck it may not then walk"
    );
    expect(
        sim::reachable_tiles(created.encounter.snapshot(), 10).empty(),
        "so no client paints it a range"
    );
    // The second character is still owed its action, and the block is still
    // open for it.
    const auto standing = created.encounter.snapshot();
    expect(
        standing.active_side == sim::Side::first &&
            standing.active_unit_id == 0,
        "the block stays open for the character that has only walked"
    );
    expect(
        created.encounter.apply({sim::CommandType::move, 11, {1, 2}, 0})
                .error == sim::CommandError::already_moved,
        "which is one walk, not two"
    );
    // WAIT is how a player says "nothing more from this one", and it finishes
    // the character outright rather than only closing off its action. A walk
    // still in hand is not a reason to keep offering it.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 11, {}, 0, 0})
        ),
        "and waiting is the deliberate way to be done with it"
    );
    expect(
        unit(created.encounter.snapshot(), 11)->has_acted,
        "a character that waits reads as spent on every client that greys one"
    );
    // Both are finished, so the block closes and the other side's opens.
    expect(
        created.encounter.snapshot().active_side == sim::Side::second,
        "and the block ends when none of its characters can do anything more"
    );
}

// A character with one point may walk or act, not both, and free interleaving
// does not change that: the walk is its whole turn and it finishes on it. If it
// did not, the block would never close on a side of one-point characters.
void one_point_finishes_a_character_on_its_walk() {
    sim::EncounterDefinition definition{
        6,
        3,
        {
            {10, 100, sim::Side::first, {0, 1}, 8, 3, 0, 0, 0, 0, 0, 0, 0, 2,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 1}, 8, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::side_blocks
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the one-point board is valid");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 10, {1, 1}, 0})
        ),
        "the one point buys the walk"
    );
    const auto after = created.encounter.snapshot();
    expect(
        unit(after, 10)->has_acted && !unit(after, 10)->has_moved &&
            unit(after, 10)->spent_action_points == 0,
        "and the walk is the whole of its turn"
    );
    expect(
        after.active_side == sim::Side::second,
        "so the block closes behind it"
    );
}

// A character authored to keep acting after it strikes is the one case a pair
// of flags cannot describe, which is why the count is kept per character rather
// than derived: three points buy three strikes, and the third closes the turn.
void a_character_that_acts_after_attacking_spends_its_own_points() {
    sim::EncounterDefinition definition{
        6,
        3,
        {
            {10, 100, sim::Side::first, {4, 1}, 8, 1, 0, 0, 0, 0, 0, 0, 0, 1,
             3, 1, true, 1, 1, {}},
            // A second character on the same side, so the block is still open
            // when the striker runs out and the fourth strike earns the refusal
            // about the striker rather than about whose block it is.
            {11, 101, sim::Side::first, {0, 0}, 8, 1, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 1}, 20, 1, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::side_blocks
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the three-point board is valid");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "the first strike lands"
    );
    expect(
        !unit(created.encounter.snapshot(), 10)->has_acted &&
            unit(created.encounter.snapshot(), 10)->spent_action_points == 1,
        "and does not finish a character authored to keep acting"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "the second strike lands too"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "and the third"
    );
    expect(
        unit(created.encounter.snapshot(), 10)->has_acted,
        "the third point closes the turn, because the budget is the budget"
    );
    expect(
        created.encounter.apply({sim::CommandType::attack, 10, {}, 20}).error ==
            sim::CommandError::already_acted,
        "and a fourth is refused by name"
    );
}

void one_walk_per_activation() {
    // Two action points, three steps, and nobody within reach: the classic
    // two-point turn is move then act, and this pins that the second point can
    // never be a second walk.
    sim::EncounterDefinition definition;
    definition.width = 9;
    definition.height = 3;
    definition.units = {
        {10, 100, sim::Side::first, {0, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 3, 2},
        {20, 200, sim::Side::second, {8, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 3, 2},
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the two-point board is valid");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 10, {3, 1}, 0})
        ),
        "the first point buys a walk"
    );
    expect(
        created.encounter.snapshot().remaining_action_points == 1,
        "and leaves a point to act with"
    );
    expect(
        unit(created.encounter.snapshot(), 10)->has_moved,
        "the walk is recorded on the character that made it"
    );
    expect(
        created.encounter.apply({sim::CommandType::move, 10, {4, 1}, 0})
                .error == sim::CommandError::already_moved,
        "the second point is refused a second walk, by name"
    );
    expect(
        sim::reachable_tiles(created.encounter.snapshot(), 10).empty(),
        "so nothing is reachable and no client paints a range it cannot use"
    );
    // The point is still there for acting, which is the whole shape of the
    // two-point turn.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 10, {}, 0, 0})
        ),
        "the spare point still buys an action"
    );
    // The allowance comes back with the next activation.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 20, {5, 1}, 0})
        ),
        "the other side walks on its own turn"
    );
    expect(
        !unit(created.encounter.snapshot(), 10)->has_moved,
        "and the character whose activation ended has its walk back"
    );
}

// The triangle: a kind of weapon that beats another is worth more in the hand,
// and worth less against it. The forecast and the blow are checked together on
// every case, because the whole promise of pricing them in one function is
// that they cannot disagree.
void the_better_weapon_is_worth_the_advantage() {
    // Two kinds, one beating the other, worth two damage and fifteen points of
    // accuracy. Both characters carry a weapon of power three and accuracy
    // eighty, so that the only difference between the boards below is which
    // kind is in whose hand. The accuracy is on the weapons because that is
    // where a battle reads it: the weapon in hand replaces whatever the
    // character was defined with.
    const auto board = [](sim::ContentId first_kind, sim::ContentId second_kind) {
        sim::EncounterDefinition definition;
        definition.width = 4;
        definition.height = 3;
        definition.weapons = {
            {501, 3, 1, 1, 80, first_kind},
            {502, 3, 1, 1, 80, second_kind},
        };
        definition.weapon_types = {{901, {902}, 2, 15}};
        definition.units = {
            {10, 100, sim::Side::first, {0, 1}, 20, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {1, 1}, 20, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        };
        definition.units[0].weapon_ids = {501};
        definition.units[1].weapon_ids = {502};
        return definition;
    };

    // strength 4 + power 3 - defense 0 is seven, and that is the blow when
    // neither hand holds an edge.
    {
        auto created = sim::create_encounter(board(0, 0));
        expect(static_cast<bool>(created), "a board with no kinds is valid");
        const auto even =
            sim::forecast_attack(created.encounter.snapshot(), 10, 20);
        expect(
            even.damage == 7 && even.hit_chance == 80,
            "no kind in either hand is the fight there always was"
        );
    }

    // The same board with the winning kind in the attacker's hand: two more
    // damage and fifteen more points of the chance, on the strike and on the
    // forecast alike.
    {
        auto created = sim::create_encounter(board(901, 902));
        expect(static_cast<bool>(created), "a board with a triangle is valid");
        const auto snapshot = created.encounter.snapshot();
        const auto up = sim::forecast_attack(snapshot, 10, 20);
        expect(
            up.damage == 9 && up.hit_chance == 95,
            "the better weapon is worth the advantage in both numbers"
        );
        // And the counter it provokes is worth as much less, because the
        // defender is answering into the kind that beats theirs.
        expect(
            up.counter && up.counter_damage == 5 && up.counter_chance == 65,
            "and the answer to it is worth as much less"
        );
        const auto struck = created.encounter.apply(
            {sim::CommandType::attack, 10, {}, 20, 0}
        );
        expect(
            static_cast<bool>(struck) &&
                struck.events.front().amount == up.damage,
            "and the blow spends exactly what the forecast showed"
        );
    }

    // The edges are directed. With the kinds the other way round the attacker
    // is striking into the advantage and is worth that much less, which is the
    // same table read from the other end rather than a second rule.
    {
        auto created = sim::create_encounter(board(902, 901));
        expect(static_cast<bool>(created), "the mirrored board is valid");
        const auto down =
            sim::forecast_attack(created.encounter.snapshot(), 10, 20);
        expect(
            down.damage == 5 && down.hit_chance == 65,
            "striking into the advantage costs exactly what holding it pays"
        );
    }

    // A blow can never be driven below one by the advantage: a spear meeting
    // the weapon it beats still hurts.
    {
        sim::EncounterDefinition definition = board(902, 901);
        definition.weapon_types = {{901, {902}, 999, 100}};
        auto created = sim::create_encounter(definition);
        expect(static_cast<bool>(created), "a crushing advantage is valid");
        const auto crushed =
            sim::forecast_attack(created.encounter.snapshot(), 10, 20);
        expect(
            crushed.damage == 1 && crushed.hit_chance == 0,
            "the floor of one holds and the chance stops at never"
        );
    }

    // And the same crushing advantage struck *with* rather than *into*, which
    // is the direction the case above does not cover and the direction the
    // arithmetic could go wrong in. The floor cannot catch this one: a bounded
    // strength and a bounded weapon already sum to one short of what `int16`
    // holds, so an advantage on top of them is the third term nothing was left
    // for. Narrowed rather than wrapped, the blow is enormous; wrapped, it was
    // negative, and being hit healed the target by thirty-one thousand.
    {
        sim::EncounterDefinition definition = board(901, 902);
        definition.weapon_types = {{901, {902}, 999, 100}};
        definition.weapons = {{501, sim::maximum_stat, 1, 1, 100, 901},
                              {502, 3, 1, 1, 80, 902}};
        definition.units[0].strength = sim::maximum_stat;
        auto created = sim::create_encounter(definition);
        expect(
            static_cast<bool>(created),
            "a board at the stat bound with a triangle on it is valid content"
        );
        const auto snapshot = created.encounter.snapshot();
        const auto huge = sim::forecast_attack(snapshot, 10, 20);
        expect(
            huge.damage > 0,
            "a blow at the stat bound is damage rather than healing"
        );
        expect(
            huge.target_health_after == 0 && huge.lethal,
            "and it fells what it hits instead of restoring it"
        );
        // The promise is the point: the forecast and the blow come out of one
        // function, so a saturated number shown is the saturated number spent.
        const auto struck = created.encounter.apply(
            {sim::CommandType::attack, 10, {}, 20, 0}
        );
        expect(
            static_cast<bool>(struck) &&
                struck.events.front().amount == huge.damage,
            "and the blow spends exactly what the forecast showed"
        );
    }
}

// The triangle is content like any other content, and `create_encounter` is
// where content is judged.
//
// Every one of these is refused by the package loader as it decodes a kind and
// by the compiler where the author is. None of them was refused here, which
// left the two routes that do not go through a package -- the WebAssembly
// binding and a direct call -- running boards a cartridge would have turned
// down.
void a_malformed_weapon_triangle_is_refused() {
    const auto board = [](std::vector<sim::WeaponTypeDefinition> types) {
        sim::EncounterDefinition definition;
        definition.width = 4;
        definition.height = 3;
        definition.weapons = {{501, 3, 1, 1, 80, 901}, {502, 3, 1, 1, 80, 902}};
        definition.weapon_types = std::move(types);
        definition.units = {
            {10, 100, sim::Side::first, {0, 1}, 20, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {1, 1}, 20, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        };
        definition.units[0].weapon_ids = {501};
        definition.units[1].weapon_ids = {502};
        return definition;
    };
    const auto refused = [&](std::vector<sim::WeaponTypeDefinition> types,
                             std::string_view why) {
        expect(
            sim::create_encounter(board(std::move(types))).error ==
                sim::CreateError::invalid_weapon_type,
            why
        );
    };

    // The one that changes what a blow is worth. A kind that beats itself is
    // read once in each direction, so a mirror match collects the bonus twice:
    // a blade meeting a blade struck for twenty where the same pair with no
    // triangle at all struck for ten.
    refused({{901, {901}, 10, 0}}, "a kind that beats itself is refused");

    // An accuracy above a hundred is not a bigger advantage, it is a negative
    // one: it narrows to `int8` on its way into the hit chance, so two hundred
    // arrived as minus fifty-six and a weapon that always landed became one
    // that landed a third of the time.
    refused(
        {{901, {902}, 0, 200}},
        "an accuracy a percentage cannot hold is refused"
    );

    // The rest are the ordinary content checks every other list here already
    // had, and their absence is why the two above could reach a board.
    refused({{0, {902}, 1, 10}}, "a kind with no identity is refused");
    refused(
        {{901, {902}, 1, 10}, {901, {}, 2, 20}},
        "one identity carrying two kinds is refused"
    );
    refused({{901, {0}, 1, 10}}, "an edge naming nothing is refused");
    refused(
        {{901, {902}, -1, 10}},
        "an advantage that costs its holder damage is refused"
    );
    refused(
        {{901, {902}, static_cast<std::int16_t>(sim::maximum_stat + 1), 10}},
        "an advantage past the bound the damage arithmetic reads is refused"
    );

    // And the shape every shipped game is actually in: weapons that name kinds
    // where no kind has an advantage anywhere, so the table arrives empty. It
    // is not a malformed triangle, it is a game with no triangle in it, and
    // refusing it would refuse both games in this repository.
    expect(
        static_cast<bool>(sim::create_encounter(board({}))),
        "weapons naming kinds no table describes is a board with no triangle"
    );
}

void weapon_power_joins_the_attack_formula() {
    // strength 4 + power 3 - defense 5 = 2: power is added to strength, and a
    // powerless weapon leaves the v0 formula untouched.
    sim::EncounterDefinition powered{
        4,
        3,
        {
            {20, 200, sim::Side::second, {1, 1}, 9, 2, 0, 5, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {10, 100, sim::Side::first, {0, 1}, 8, 4, 3, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {}
    };
    auto created = sim::create_encounter(powered);
    expect(static_cast<bool>(created), "powered encounter is valid");

    const auto promised =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(promised) && promised.damage == 2 &&
            promised.target_health_after == 7 && !promised.lethal,
        "forecast prices strength plus power minus defense"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0}
    );
    expect(
        static_cast<bool>(struck) &&
            struck.events.front().amount == promised.damage &&
            unit(created.encounter.snapshot(), 20)->health ==
                promised.target_health_after,
        "apply delivers exactly the powered forecast"
    );

    // Even overwhelming defense leaves the one-point floor intact.
    auto armored = powered;
    armored.units.front().defense = 100;
    auto floored = sim::create_encounter(armored);
    const auto minimal = floored.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0}
    );
    expect(
        static_cast<bool>(minimal) && minimal.events.front().amount == 1,
        "powered damage still floors at one"
    );

    // A negative power cannot enter the encounter at all.
    auto invalid = powered;
    invalid.units.back().power = -1;
    expect(
        sim::create_encounter(invalid).error == sim::CreateError::invalid_unit,
        "negative weapon power is rejected"
    );
}

void caps_board_area() {
    auto oversized = definition();
    oversized.width = 1000;
    oversized.height = 1000;
    expect(
        sim::create_encounter(oversized).error == sim::CreateError::invalid_map,
        "a board over 65536 cells is an invalid map"
    );

    auto largest = definition();
    largest.width = 256;
    largest.height = 256;
    expect(
        static_cast<bool>(sim::create_encounter(largest)),
        "the largest permitted board still plays"
    );
}

void has_acted_is_false_under_alternating_order() {
    auto encounter = make_encounter();
    expect(
        static_cast<bool>(encounter.apply(
            {sim::CommandType::move, 10, {1, 1}, 0}
        )),
        "alternating activation completes"
    );
    const auto snapshot = encounter.snapshot();
    for (const sim::UnitSnapshot& value : snapshot.units) {
        expect(
            !value.has_acted,
            "has_acted stays false under alternating order"
        );
    }
}

void has_acted_tracks_rounds_under_ordered_turns() {
    sim::EncounterDefinition ordered{
        6,
        3,
        {
            {10, 100, sim::Side::first, {0, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 9, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 5, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::initiative
    };
    auto created = sim::create_encounter(ordered);
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0, 0}
        )),
        "the fast unit acts under initiative"
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10)->has_acted && !unit(snapshot, 20)->has_acted,
        "an ordered round records who has acted"
    );
}

void initiative_breaks_speed_ties_by_identifier_across_sides() {
    // Equal speed everywhere: the round runs in ascending identifier order,
    // interleaving sides, so the tie-break cannot depend on side or on
    // definition order.
    sim::EncounterDefinition tied{
        6,
        3,
        {
            {21, 201, sim::Side::second, {5, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 3, false, 1, 1, {}},
            {10, 100, sim::Side::first, {0, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 3, false, 1, 1, {}},
            {20, 200, sim::Side::second, {5, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 3, false, 1, 1, {}},
            {11, 101, sim::Side::first, {0, 1}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 3, false, 1, 1, {}},
        },
        {},
        {},
        sim::TurnOrder::initiative
    };
    auto created = sim::create_encounter(tied);
    expect(static_cast<bool>(created), "tied-speed encounter is valid");
    const sim::UnitId expected[] = {10, 11, 20, 21};
    for (const sim::UnitId id : expected) {
        expect(
            created.encounter.snapshot().active_unit_id == id,
            "speed ties resolve to the lowest identifier regardless of side"
        );
        expect(
            static_cast<bool>(created.encounter.apply(
                {sim::CommandType::wait, id, {}, 0, 0}
            )),
            "the tie-broken unit acts"
        );
    }
    expect(
        created.encounter.snapshot().round == 1,
        "the tied round completes"
    );
}

sim::EncounterDefinition restore_definition(
    std::int16_t caster_health,
    std::int16_t ally_health
) {
    return {
        4,
        3,
        {
            // Healer at (1,1) with a cross-shaped restore; ally adjacent at
            // (1,0); enemy adjacent at (2,1).
            {10, 100, sim::Side::first, {1, 1}, caster_health, 4, 0, 0, 0, 0,
             0, 0, 0, 1, 1, 1, false, 1, 1, {7}},
            {11, 101, sim::Side::first, {1, 0}, ally_health, 4, 0, 0, 0, 0, 0,
             0, 0, 1, 1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {2, 1}, 9, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {
            // Reach zero to one, so the healer can centre the cross on its
            // own tile.
            {7, sim::AbilityKind::restore, sim::DamageType::physical,
             sim::AreaShape::cross, 5, 0, 1, 0}
        },
        {}
    };
}

void restore_skips_full_health_targets() {
    // Authored health is also maximum health, so every unit in the area is
    // already full: the cast succeeds, restores nobody, and is still spent.
    auto created = sim::create_encounter(restore_definition(9, 7));
    expect(static_cast<bool>(created), "restore encounter is valid");
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {1, 1}, 0, 7}
    );
    expect(static_cast<bool>(cast), "the restore resolves");
    const auto snapshot = created.encounter.snapshot();
    expect(
        cast.events.size() == 1 &&
            cast.events.front().type == sim::EventType::activation_ended,
        "restoring only full-health units emits no restore event"
    );
    expect(
        snapshot.active_side == sim::Side::second,
        "an ineffective restore still consumes the activation"
    );
    expect(
        unit(snapshot, 11)->health == 7 && unit(snapshot, 10)->health == 9,
        "full-health units are untouched by a restore"
    );
}

void restore_heals_the_wounded_up_to_maximum() {
    // Wound the healer first so the self-tile restore has something to do:
    // the enemy strikes the healer for 4, then the healer restores 5 with
    // only 4 missing, proving the clamp.
    auto definition = restore_definition(9, 9);
    definition.units[2].strength = 4;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "wounded-restore encounter is valid");
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::move, 11, {0, 0}, 0, 0}
        )),
        "the ally opens"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10, 0}
    );
    expect(
        static_cast<bool>(struck) && struck.events.front().amount == 4,
        "the enemy wounds the healer"
    );
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {1, 1}, 0, 7}
    );
    expect(static_cast<bool>(cast), "the wounded healer restores");
    expect(
        cast.events.front().type == sim::EventType::unit_restored &&
            cast.events.front().amount == 4,
        "restore reports only the missing health"
    );
    // The enemy stands inside the cross, at full health: a restore is not a
    // weapon, so it neither heals nor harms it.
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10)->health == 9,
        "restored health clamps at maximum"
    );
    expect(
        unit(snapshot, 20)->health == 9,
        "a full-health enemy in the area is unaffected"
    );
}

void restore_heals_enemies_in_the_area() {
    // Area effects do not choose sides: a wounded enemy inside the cross is
    // healed like anyone else.
    auto definition = restore_definition(9, 9);
    definition.units[1].position = {2, 0};
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "enemy-restore encounter is valid");
    const auto wound = created.encounter.apply(
        {sim::CommandType::attack, 11, {}, 20, 0}
    );
    expect(
        static_cast<bool>(wound) && wound.events.front().amount == 4,
        "the ally wounds the enemy"
    );
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 20, {}, 0, 0}
        )),
        "the enemy holds"
    );
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {1, 1}, 0, 7}
    );
    expect(static_cast<bool>(cast), "the restore resolves over the enemy");
    expect(
        cast.events.front().type == sim::EventType::unit_restored &&
            cast.events.front().unit_id == 20 &&
            cast.events.front().amount == 4,
        "a wounded enemy inside the area is healed"
    );
    expect(
        unit(created.encounter.snapshot(), 20)->health == 9,
        "the healed enemy is back at maximum"
    );
}

// A battle is not won while an opponent is standing, and the last one is the
// one that decides it.
//
// Written because the opposite was reported: a campaign's slideshow showed a
// board with two opponents apparently alive, and the next photograph was the
// win banner. It was the camera: the trail photographs settled checkpoints
// only, so the activations that killed them happened between two frames and
// the aftermath screen's own experience arithmetic accounted for every kill.
// But "the engine can declare victory over living opponents" is too serious a
// claim to leave resting on an inference about a video.
//
// So it is pinned here, one defeat at a time, against the rule itself: three
// opponents, killed one after another, with the objective required to stay
// pending and the outcome required to stay ongoing until the last of them is
// down. A rule that counted the wrong set, or resolved on the first defeat, or
// treated a side as empty while some of it remained, fails on the first or the
// second blow rather than at the end of a campaign.
void a_battle_is_not_won_while_an_opponent_stands() {
    sim::EncounterDefinition definition{
        6,
        3,
        {
            // One striker and three opponents in a row beside it. Health 1 and
            // no defense on the opponents, so each blow is exactly one defeat
            // and nothing else is being measured; reach 3 and three action
            // points on the striker, so all three defeats happen inside one
            // activation and no turn passes between them. That matters: the
            // claim under test is about the moment an objective resolves, and
            // a turn change between blows would let a passing turn rather than
            // a defeat be what moved it.
            // `acts_after_attacking` is on, which is the field that makes the
            // three blows one activation: striking normally ends one, and a
            // turn passing between two defeats would let the turn rather than
            // the defeat be what moved the objective.
            {10, 100, sim::Side::first, {0, 1}, 9, 9, 0, 0, 0, 0, 0, 0, 0, 1,
             3, 1, true, 1, 3, {}},
            {20, 200, sim::Side::second, {2, 1}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {21, 200, sim::Side::second, {3, 1}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {22, 200, sim::Side::second, {1, 1}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {
            {90, sim::ObjectiveKind::defeat_all_opponents, sim::Side::first, 0}
        }
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the three-opponent board is valid");

    const sim::UnitId order[3] = {22, 20, 21};
    for (int index = 0; index < 3; ++index) {
        const auto strike = created.encounter.apply(
            {sim::CommandType::attack, 10, {0, 0}, order[index]}
        );
        expect(static_cast<bool>(strike), "the strike resolves");
        const auto snapshot = created.encounter.snapshot();
        expect(
            unit(snapshot, order[index])->health == 0,
            "the struck opponent is down"
        );
        const bool last = index == 2;
        expect(
            (snapshot.objectives.front().state ==
             (last ? sim::ObjectiveState::satisfied
                   : sim::ObjectiveState::pending)),
            last ? "the last defeat satisfies the objective"
                 : "an objective to defeat all opponents stays pending while "
                   "one of them is alive"
        );
        expect(
            (snapshot.outcome == (last ? sim::Outcome::first_side_won
                                       : sim::Outcome::ongoing)),
            last ? "and the battle is won on the last defeat"
                 : "and the battle is not won while one of them is alive"
        );
        // The claim in the form it was reported: count the standing opponents
        // and require the outcome to agree with that count rather than with
        // the order events happened to arrive in.
        int standing = 0;
        for (const sim::UnitSnapshot& value : snapshot.units) {
            if (value.side == sim::Side::second && value.health > 0) ++standing;
        }
        expect(
            standing == 2 - index,
            "the board holds the opponents it has not yet lost"
        );
        expect(
            (standing == 0) == (snapshot.outcome == sim::Outcome::first_side_won),
            "victory and an empty opposing side are the same fact"
        );
    }
}

void a_blast_spares_the_caster_and_its_own_side() {
    // A stormcaller standing in the middle of its own cross, with an ally on
    // one arm of it and an opponent on another. Everything about the cast is
    // the same for all three; the only thing that differs is whose side they
    // are on, and that is the whole of what the cast asks.
    //
    //     y=0  .  .  .  .
    //     y=1  .  C  E  .      C  the caster, side one
    //     y=2  .  A  .  .      A  its ally    E  an opponent, both at one tile
    sim::EncounterDefinition definition{
        4,
        3,
        {
            {10, 100, sim::Side::first, {1, 1}, 3, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {8}},
            {11, 101, sim::Side::first, {1, 2}, 3, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {2, 1}, 9, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {
            {8, sim::AbilityKind::damage, sim::DamageType::magical,
             sim::AreaShape::cross, 6, 0, 1, 0}
        },
        {
            {90, sim::ObjectiveKind::defeat_all_opponents, sim::Side::first, 0}
        }
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the self-centred blast board is valid");
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {1, 1}, 0, 8}
    );
    expect(static_cast<bool>(cast), "the cast on its own tile resolves");
    // Two of the three characters the cross covers are on the caster's side,
    // and neither is in the exchange at all: no damage, and no miss either. So
    // the whole command is one blow and the end of the turn that spent it.
    expect(
        cast.events.size() == 2 &&
            cast.events[0].type == sim::EventType::unit_damaged &&
            cast.events[0].unit_id == 20 && cast.events[0].amount == 6 &&
            cast.events[1].type == sim::EventType::activation_ended,
        "only the opponent in the cross is touched by it"
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10)->health == 3 && unit(snapshot, 11)->health == 3,
        "the caster and its ally keep every point of health"
    );
    expect(
        unit(snapshot, 20)->health == 3,
        "and the opponent standing beside them pays the whole cost"
    );
    expect(
        snapshot.outcome == sim::Outcome::ongoing &&
            snapshot.objectives.size() == 1 &&
            snapshot.objectives.front().state == sim::ObjectiveState::pending,
        "so nobody is felled by their own side and the battle runs on"
    );
}

// The other half of the elimination backstop: an objective owned by the side
// that gets wiped out is recorded as failed rather than left pending.
//
// It is a test of its own because nothing a side does to itself can reach it
// any more: a strike refuses an ally by name and a cast spares one. So the
// only road to it is the opposition finishing the job, and a branch with no
// road is a branch nobody checks.
void a_wiped_out_side_fails_its_own_objective() {
    sim::EncounterDefinition definition{
        3,
        1,
        {
            {10, 100, sim::Side::first, {0, 0}, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {1, 0}, 9, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {
            {90, sim::ObjectiveKind::defeat_all_opponents, sim::Side::first, 0}
        }
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the last-stand board is valid");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 10, {}, 0})
        ),
        "the lone defender passes its turn"
    );
    const auto strike = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10}
    );
    expect(static_cast<bool>(strike), "and the opposition fells it");
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10)->health == 0 &&
            snapshot.outcome == sim::Outcome::second_side_won,
        "losing your last unit hands the other side the win"
    );
    expect(
        snapshot.objectives.size() == 1 &&
            snapshot.objectives.front().state == sim::ObjectiveState::failed,
        "and the wiped-out side's objective is recorded as failed"
    );
}

// One cast, two objectives decided at once, in opposite directions: the area
// kills the enemy the first side was told to fell and the prisoner it was told
// to bring out alive. Both records must move, and the tie must be settled by
// objective identity (the order canonical state holds them in) rather than by
// the order the caller happened to list them, which no two clients need agree
// on.
//
// Both characters the blast catches stand on the other side, and they have to:
// a cast harms only the caster's opponents, so a command that decides two
// objectives at once can only do it among them. Somebody a side is told to
// keep alive who is not one of its own is exactly the shape an escort or a
// hostage takes, and it is what makes the pair of objectives authorable at all.
sim::EncounterDefinition simultaneous_decision(
    const std::vector<sim::ObjectiveDefinition>& objectives
) {
    return {
        5,
        3,
        {
            // The caster, the enemy an objective names, the fragile prisoner
            // beside them, and a second enemy far away so the elimination
            // backstop cannot be what decides this.
            {10, 100, sim::Side::first, {0, 1}, 9, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false,
             1, 1, {8}},
            {20, 200, sim::Side::second, {2, 1}, 4, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false,
             1, 1, {}},
            {21, 201, sim::Side::second, {2, 2}, 4, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false,
             1, 1, {}},
            {22, 202, sim::Side::second, {4, 0}, 9, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false,
             1, 1, {}},
        },
        {
            {8, sim::AbilityKind::damage, sim::DamageType::magical,
             sim::AreaShape::cross, 9, 1, 2, 0}
        },
        objectives
    };
}

sim::EncounterSnapshot decide_simultaneously(
    const std::vector<sim::ObjectiveDefinition>& objectives,
    std::string_view label
) {
    auto created = sim::create_encounter(simultaneous_decision(objectives));
    expect(static_cast<bool>(created), std::string(label) + " encounter is valid");
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {2, 1}, 0, 8}
    );
    expect(static_cast<bool>(cast), std::string(label) + " cast resolves");
    return created.encounter.snapshot();
}

void simultaneous_decisive_objectives_follow_identifier_order() {
    // Felling the enemy wins for the first side; losing the prisoner loses for
    // it. Both land on the same command, so exactly one of them decides.
    const sim::ObjectiveDefinition fell_low{
        90, sim::ObjectiveKind::defeat_target, sim::Side::first, 20
    };
    const sim::ObjectiveDefinition keep_high{
        91, sim::ObjectiveKind::protect_target, sim::Side::first, 21
    };
    const sim::ObjectiveDefinition keep_low{
        90, sim::ObjectiveKind::protect_target, sim::Side::first, 21
    };
    const sim::ObjectiveDefinition fell_high{
        91, sim::ObjectiveKind::defeat_target, sim::Side::first, 20
    };

    const auto won = decide_simultaneously({fell_low, keep_high}, "the win-first");
    expect(
        unit(won, 20)->health == 0 && unit(won, 21)->health == 0,
        "the cast fells the target and the prisoner in one command"
    );
    expect(
        won.outcome == sim::Outcome::first_side_won,
        "the lower-identified objective decides the battle"
    );
    expect(
        won.objectives.size() == 2 &&
            won.objectives[0].state == sim::ObjectiveState::satisfied &&
            won.objectives[1].state == sim::ObjectiveState::failed,
        "both objectives are still recorded, satisfied and failed"
    );

    // The same two objectives handed over in the opposite order. Canonical
    // state sorts them by identity, so nothing about the result may move.
    const auto reordered =
        decide_simultaneously({keep_high, fell_low}, "the reordered");
    expect(
        reordered.outcome == won.outcome &&
            reordered.objectives.size() == 2 &&
            reordered.objectives[0].state == won.objectives[0].state &&
            reordered.objectives[1].state == won.objectives[1].state,
        "listing the objectives the other way round changes nothing"
    );

    // Swap which objective carries the lower identity and the same board, the
    // same command, and the same two records now hand the battle the other way.
    const auto lost = decide_simultaneously({fell_high, keep_low}, "the loss-first");
    expect(
        lost.outcome == sim::Outcome::second_side_won,
        "the losing objective decides when it is the lower-identified one"
    );
    expect(
        lost.objectives.size() == 2 &&
            lost.objectives[0].state == sim::ObjectiveState::failed &&
            lost.objectives[1].state == sim::ObjectiveState::satisfied,
        "the record of each objective does not depend on which decided"
    );
}

void forecast_refuses_activation_states() {
    // Mid-activation: a two-point unit that has moved once still owns the
    // activation, so a forecast for its idle ally is refused.
    sim::EncounterDefinition definition{
        4,
        3,
        {
            {10, 100, sim::Side::first, {0, 0}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             2, 1, false, 1, 1, {}},
            {11, 101, sim::Side::first, {0, 2}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
            {20, 200, sim::Side::second, {3, 2}, 8, 4, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1, {}},
        },
        {},
        {}
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "activation-state encounter is valid");
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::move, 10, {1, 0}, 0, 0}
        )),
        "the two-point unit opens its activation"
    );
    auto snapshot = created.encounter.snapshot();
    expect(
        sim::forecast_attack(snapshot, 11, 20).error ==
            sim::CommandError::activation_in_progress,
        "forecast refuses a unit outside the running activation"
    );

    // Exhausted: the same snapshot with the points zeroed is exactly the state
    // apply would refuse with no_action_points, and the forecast agrees.
    snapshot.remaining_action_points = 0;
    expect(
        sim::forecast_attack(snapshot, 10, 20).error ==
            sim::CommandError::no_action_points,
        "forecast refuses an activation with no points left"
    );
}

// --- Carried items ---

// A duellist with a satchel and an opponent it can reach. The draught restores
// four; the keepsake authors no effect at all and exists so that the refusal
// for one is a tested refusal rather than a comment.
sim::EncounterDefinition satchel_definition() {
    sim::EncounterDefinition definition;
    definition.width = 6;
    definition.height = 3;
    definition.weapons = {{70, 4, 1, 1}};
    definition.items = {
        {90, sim::ItemKind::restore, 4},
        {91, sim::ItemKind::none, 0},
    };
    sim::UnitDefinition duellist;
    duellist.id = 10;
    duellist.unit_type_id = 100;
    duellist.side = sim::Side::first;
    duellist.position = {0, 1};
    duellist.health = 12;
    duellist.strength = 3;
    duellist.weapon_ids = {70};
    duellist.item_ids = {90, 91};
    sim::UnitDefinition opponent;
    opponent.id = 20;
    opponent.unit_type_id = 200;
    opponent.side = sim::Side::second;
    opponent.position = {3, 1};
    opponent.health = 20;
    opponent.strength = 2;
    definition.units = {duellist, opponent};
    return definition;
}

// Steps the duellist into the opponent's reach and trades a blow, so that the
// restore has room to work and the clamp is not the thing under test. Leaves
// the turn with the duellist.
void wound_the_duellist(sim::Encounter& encounter) {
    expect(
        static_cast<bool>(
            encounter.apply({sim::CommandType::move, 10, {1, 1}})
        ) &&
            static_cast<bool>(
                encounter.apply({sim::CommandType::move, 20, {2, 1}})
            ) &&
            static_cast<bool>(
                encounter.apply({sim::CommandType::attack, 10, {}, 20})
            ) &&
            static_cast<bool>(encounter.apply({sim::CommandType::wait, 20})),
        "the exchange leaves the duellist wounded and holding the turn"
    );
}

sim::Command drink(sim::ContentId item = 90, sim::UnitId target = 0) {
    sim::Command use;
    use.type = sim::CommandType::use_item;
    use.unit_id = 10;
    use.item_id = item;
    use.target_id = target;
    return use;
}

void an_unstated_count_is_one_of_each() {
    auto created = sim::create_encounter(satchel_definition());
    expect(static_cast<bool>(created), "the satchel board is valid");
    // The snapshot is held rather than taken inline: `snapshot()` returns by
    // value, so a pointer into a temporary would dangle the moment the full
    // expression ended.
    const auto opened = created.encounter.snapshot();
    const sim::UnitSnapshot* carrier = unit(opened, 10);
    expect(
        carrier != nullptr && carrier->item_ids.size() == 2U &&
            carrier->item_counts.size() == 2U &&
            carrier->item_counts[0] == 1U && carrier->item_counts[1] == 1U,
        "an authored list with no counts is one of each"
    );
}

void a_malformed_satchel_is_refused() {
    sim::EncounterDefinition undefined_item = satchel_definition();
    undefined_item.units.front().item_ids = {92};
    expect(
        sim::create_encounter(undefined_item).error ==
            sim::CreateError::invalid_item,
        "carrying an item the encounter does not define is refused"
    );

    sim::EncounterDefinition twice = satchel_definition();
    twice.units.front().item_ids = {90, 90};
    expect(
        sim::create_encounter(twice).error == sim::CreateError::invalid_item,
        "the same item in two slots is refused"
    );

    sim::EncounterDefinition mismatched = satchel_definition();
    mismatched.units.front().item_counts = {3};
    expect(
        sim::create_encounter(mismatched).error ==
            sim::CreateError::invalid_item,
        "counts that do not line up with what is carried are refused"
    );

    sim::EncounterDefinition none_carried = satchel_definition();
    none_carried.units.front().item_counts = {0, 1};
    expect(
        sim::create_encounter(none_carried).error ==
            sim::CreateError::invalid_item,
        "bringing none of something is refused rather than corrected"
    );

    sim::EncounterDefinition powerless = satchel_definition();
    powerless.items.front().power = 0;
    expect(
        sim::create_encounter(powerless).error ==
            sim::CreateError::invalid_item,
        "a restoring item that restores nothing is refused"
    );

    sim::EncounterDefinition duplicated = satchel_definition();
    duplicated.items.push_back({90, sim::ItemKind::restore, 2});
    expect(
        sim::create_encounter(duplicated).error ==
            sim::CreateError::invalid_item,
        "two definitions of one item identity are refused"
    );
}

void using_an_item_restores_and_spends_it() {
    auto created = sim::create_encounter(satchel_definition());
    expect(static_cast<bool>(created), "the satchel board is valid");
    wound_the_duellist(created.encounter);
    const auto wounded_state = created.encounter.snapshot();
    const sim::UnitSnapshot* before = unit(wounded_state, 10);
    expect(
        before != nullptr && before->health < before->maximum_health,
        "the duellist took the answer"
    );
    if (before == nullptr) return;
    const std::int16_t wounded = before->health;
    const std::int16_t missing =
        static_cast<std::int16_t>(before->maximum_health - wounded);
    const std::int16_t expected_gain = missing < 4 ? missing : 4;

    const auto result = created.encounter.apply(drink());
    expect(static_cast<bool>(result), "the draught is drunk");
    expect(
        result.events.size() >= 2U &&
            result.events[0].type == sim::EventType::item_used &&
            result.events[0].unit_id == 10U &&
            result.events[0].related_unit_id == 10U &&
            result.events[0].content_id == 90U &&
            result.events[0].amount == 0,
        "the event names who spent what, and how many are left"
    );
    expect(
        result.events.size() >= 2U &&
            result.events[1].type == sim::EventType::unit_restored &&
            result.events[1].unit_id == 10U &&
            result.events[1].amount == expected_gain,
        "and the restoring half arrives as the event a cast already emits"
    );
    const auto settled = created.encounter.snapshot();
    const sim::UnitSnapshot* after = unit(settled, 10);
    expect(
        after != nullptr &&
            after->health == static_cast<std::int16_t>(wounded + expected_gain),
        "the health went up by exactly what the item authored"
    );
    expect(
        after != nullptr && after->item_ids.size() == 2U &&
            after->item_counts.size() == 2U && after->item_counts[0] == 0U &&
            after->item_counts[1] == 1U,
        "the count went down and the slot stayed, so a client can draw it"
    );
}

void a_use_is_priced_like_a_cast() {
    auto created = sim::create_encounter(satchel_definition());
    expect(static_cast<bool>(created.encounter.apply(drink())), "it goes");
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.active_side == sim::Side::second &&
            snapshot.remaining_action_points == 0U,
        "spending an item closes the activation exactly as a cast does"
    );
}

void a_use_draws_nothing_from_any_stream() {
    auto created = sim::create_encounter(satchel_definition());
    const auto before = created.encounter.snapshot().random;
    expect(static_cast<bool>(created.encounter.apply(drink())), "it goes");
    const auto after = created.encounter.snapshot().random;
    bool untouched = before.seed == after.seed;
    for (std::size_t i = 0; i < before.positions.size(); ++i) {
        untouched = untouched && before.positions[i] == after.positions[i];
    }
    expect(untouched, "no stream moved: an item is not a die roll");
}

void every_refusal_a_use_can_earn() {
    auto created = sim::create_encounter(satchel_definition());
    expect(
        created.encounter.apply(drink(0)).error ==
            sim::CommandError::unknown_item,
        "a use naming nothing is unknown rather than the item in hand"
    );
    expect(
        created.encounter.apply(drink(92)).error ==
            sim::CommandError::unknown_item,
        "an item the encounter does not define is unknown"
    );
    expect(
        created.encounter.apply(drink(91)).error ==
            sim::CommandError::unusable_item,
        "an item that does nothing is refused rather than silently wasted"
    );
    expect(
        created.encounter.apply(drink(90, 20)).error ==
            sim::CommandError::target_out_of_range,
        "an item reaches the hand that holds it and nobody else"
    );
    expect(
        created.encounter.apply(drink(90, 999)).error ==
            sim::CommandError::unknown_target,
        "and a target nobody is standing on is unknown"
    );

    // The opponent carries nothing, and is asked to spend what it does not
    // have. Unavailable rather than unknown: the encounter defines the item.
    auto borrowed = sim::create_encounter(satchel_definition());
    expect(
        static_cast<bool>(
            borrowed.encounter.apply({sim::CommandType::wait, 10})
        ),
        "the first side passes"
    );
    sim::Command theft = drink();
    theft.unit_id = 20;
    expect(
        borrowed.encounter.apply(theft).error ==
            sim::CommandError::unavailable_item,
        "an item somebody else carries is unavailable, not unknown"
    );

    // And the same draught twice.
    auto twice = sim::create_encounter(satchel_definition());
    expect(
        static_cast<bool>(twice.encounter.apply(drink())), "the first goes"
    );
    expect(
        static_cast<bool>(twice.encounter.apply({sim::CommandType::wait, 20})),
        "the opponent waits it round"
    );
    expect(
        twice.encounter.apply(drink()).error ==
            sim::CommandError::depleted_item,
        "the second is depleted rather than unavailable"
    );
}

void the_item_forecast_is_the_number_apply_delivers() {
    auto created = sim::create_encounter(satchel_definition());
    wound_the_duellist(created.encounter);
    const auto snapshot = created.encounter.snapshot();
    const sim::ItemForecast promised =
        sim::forecast_item(snapshot, 10, 0, created.encounter.items(), 90);
    expect(static_cast<bool>(promised), "the draught is forecastable");
    expect(promised.kind == sim::ItemKind::restore, "and it restores");
    expect(promised.restored > 0, "by a number worth showing");
    expect(promised.remaining_after == 0U, "leaving none behind");

    const auto delivered = created.encounter.apply(drink());
    expect(static_cast<bool>(delivered), "and the use is accepted");
    const auto settled = created.encounter.snapshot();
    const sim::UnitSnapshot* after = unit(settled, 10);
    expect(
        after != nullptr && after->health == promised.target_health_after,
        "the shown number is the number apply delivered"
    );
    expect(
        after != nullptr && after->item_counts.size() == 2U &&
            after->item_counts[0] == promised.remaining_after,
        "down to what is left in the satchel"
    );
    expect(
        delivered.events.size() >= 2U &&
            delivered.events[1].amount == promised.restored,
        "and to the point restored"
    );
}

void a_full_health_use_forecasts_and_delivers_nothing() {
    auto created = sim::create_encounter(satchel_definition());
    const auto snapshot = created.encounter.snapshot();
    const sim::ItemForecast promised =
        sim::forecast_item(snapshot, 10, 0, created.encounter.items(), 90);
    expect(
        static_cast<bool>(promised) && promised.restored == 0,
        "a draught drunk at full health is forecast as the waste it is"
    );
    const auto delivered = created.encounter.apply(drink());
    expect(static_cast<bool>(delivered), "and it is still accepted");
    expect(
        delivered.events.size() == 2U &&
            delivered.events[0].type == sim::EventType::item_used &&
            delivered.events[1].type == sim::EventType::activation_ended,
        "spent for nothing, with no restoring event to claim otherwise"
    );
    const auto settled = created.encounter.snapshot();
    const sim::UnitSnapshot* after = unit(settled, 10);
    expect(
        after != nullptr && after->item_counts.size() == 2U &&
            after->item_counts[0] == 0U,
        "and the draught is gone, because the forecast said so first"
    );
}

void a_forecast_refuses_what_apply_would_refuse() {
    auto created = sim::create_encounter(satchel_definition());
    const auto snapshot = created.encounter.snapshot();
    const auto& carried = created.encounter.items();
    expect(
        sim::forecast_item(snapshot, 10, 0, carried, 0).error ==
            sim::CommandError::unknown_item,
        "the forecast refuses an unnamed item the way apply does"
    );
    expect(
        sim::forecast_item(snapshot, 10, 0, carried, 91).error ==
            sim::CommandError::unusable_item,
        "and an item that does nothing"
    );
    expect(
        sim::forecast_item(snapshot, 20, 0, carried, 90).error ==
            sim::CommandError::wrong_side,
        "and a character whose side is not acting"
    );
    expect(
        sim::forecast_item(snapshot, 10, 20, carried, 90).error ==
            sim::CommandError::target_out_of_range,
        "and a use aimed at somebody else"
    );
    expect(
        sim::forecast_item(snapshot, 999, 0, carried, 90).error ==
            sim::CommandError::unknown_unit,
        "and a character nobody is"
    );
}

void the_pack_is_canonical_state() {
    auto full = sim::create_encounter(satchel_definition());
    auto spent = sim::create_encounter(satchel_definition());
    expect(
        full.encounter.canonical_hash() == spent.encounter.canonical_hash(),
        "two identical satchels open identically"
    );
    expect(
        static_cast<bool>(spent.encounter.apply(drink())),
        "one of them is drunk from"
    );
    expect(
        static_cast<bool>(full.encounter.apply({sim::CommandType::wait, 10})),
        "and the other spends the same activation doing nothing"
    );
    expect(
        full.encounter.canonical_hash() != spent.encounter.canonical_hash(),
        "so what is left in the satchel moves the hash like a wound does"
    );

    // And the identity, not only the count: carried order is state too, for
    // the same reason it is for weapons.
    sim::EncounterDefinition other = satchel_definition();
    other.units.front().item_ids = {91, 90};
    auto reordered = sim::create_encounter(other);
    auto original = sim::create_encounter(satchel_definition());
    expect(
        static_cast<bool>(reordered) &&
            reordered.encounter.canonical_hash() !=
                original.encounter.canonical_hash(),
        "carried order is canonical state, exactly as it is for weapons"
    );
}

// The board a drop is measured on, and its exact twin with nothing to leave.
// `chancy` at a hundred on both sides strikes for three against three health,
// so the blow is certain and lethal and the felled defender never answers:
// nothing but the drop can move a stream, which is what makes the draw counts
// below readable.
sim::EncounterDefinition barren_board(std::uint64_t seed) {
    return chancy(seed, 100, 100, 3);
}

sim::EncounterDefinition dropper(std::uint64_t seed, std::uint8_t chance) {
    sim::EncounterDefinition definition = barren_board(seed);
    definition.units.front().drop_item_id = 90;
    definition.units.front().drop_chance = chance;
    return definition;
}

std::uint64_t drop_draws(const sim::EncounterSnapshot& snapshot) {
    return snapshot.random.positions[
        static_cast<std::size_t>(grandleon::core::RandomStream::drop)
    ];
}

void a_defeated_unit_leaves_what_its_type_authors() {
    // A certainty, so the drop is the rule and not the dice: it falls, and it
    // falls without taking a number.
    auto certain = sim::create_encounter(dropper(1, 100));
    expect(static_cast<bool>(certain), "the dropper board is valid");
    const auto felled =
        certain.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after = certain.encounter.snapshot();
    expect(
        static_cast<bool>(felled) && felled.events.size() >= 3 &&
            felled.events[0].type == sim::EventType::unit_damaged &&
            felled.events[1].type == sim::EventType::unit_defeated &&
            felled.events[2].type == sim::EventType::item_dropped,
        "what fell is reported after who fell, in that order"
    );
    expect(
        felled.events.size() >= 3 && felled.events[2].unit_id == 20U &&
            felled.events[2].related_unit_id == 10U &&
            felled.events[2].content_id == 90U &&
            felled.events[2].amount == 1,
        "the event names whose body it came off, who claims it, and what it is"
    );
    expect(
        after.drops.size() == 1U &&
            after.drops.front() == sim::DropRecord{20U, 10U, 90U},
        "and the encounter records it, so a snapshot carries it into a save"
    );
    expect(
        drop_draws(after) == 0U,
        "a certain drop consumes no number, exactly as a certain strike does"
    );

    // Nothing authored is nothing rolled and nothing left.
    auto barren = sim::create_encounter(barren_board(1));
    const auto quiet =
        barren.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto empty = barren.encounter.snapshot();
    expect(
        static_cast<bool>(quiet) && empty.drops.empty() &&
            drop_draws(empty) == 0U,
        "a unit type that authors no drop leaves nothing and draws nothing"
    );
    for (const sim::Event& event : quiet.events) {
        expect(
            event.type != sim::EventType::item_dropped,
            "and reports nothing either"
        );
    }
}

void the_drop_stream_is_consumed_in_one_fixed_order() {
    // Seed 1's drop stream reads 93, 43, 17, 38, chosen because at a chance of
    // sixty the first number refuses and the next three accept, so one board
    // shows both answers without changing the rule.
    auto refused = sim::create_encounter(dropper(1, 60));
    const auto missed =
        refused.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after_refusal = refused.encounter.snapshot();
    expect(
        static_cast<bool>(missed) && after_refusal.drops.empty() &&
            drop_draws(after_refusal) == 1U,
        "an uncertain drop takes exactly one number, and 93 against 60 keeps it"
    );

    // Seed 3's drop stream reads 53, 10, 88: the same chance, falling the
    // other way on the first number.
    auto granted = sim::create_encounter(dropper(3, 60));
    const auto given =
        granted.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after_grant = granted.encounter.snapshot();
    expect(
        static_cast<bool>(given) && after_grant.drops.size() == 1U &&
            drop_draws(after_grant) == 1U,
        "and 53 against 60 gives it up, on one number either way"
    );

    // A counterattack kill is a defeat like any other, and rolls like one. The
    // archer swings at a defender that survives and answers lethally.
    sim::EncounterDefinition answered;
    answered.width = 4;
    answered.height = 3;
    answered.units = {
        {20, 200, sim::Side::second, {1, 1}, 9, 6, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1},
        {10, 100, sim::Side::first, {0, 1}, 3, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1},
    };
    answered.units.back().drop_item_id = 91;
    answered.units.back().drop_chance = 60;
    answered.random_seed = 3;
    auto exchange = sim::create_encounter(answered);
    const auto traded =
        exchange.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto after_trade = exchange.encounter.snapshot();
    expect(
        static_cast<bool>(traded) && after_trade.drops.size() == 1U &&
            after_trade.drops.front() == sim::DropRecord{10U, 20U, 91U} &&
            drop_draws(after_trade) == 1U,
        "a counterattack kill rolls its drop and claims it for the defender"
    );

    // An area that fells two rolls twice, in ascending unit identifier order.
    // That is the same rule the hit stream states for the same loop. Seed 3
    // gives 53 then 10, so at sixty both fall and the order shows in the list.
    sim::EncounterDefinition swept;
    swept.width = 6;
    swept.height = 3;
    swept.units = {
        {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1,
         false, 1, 1, {1}},
        {20, 200, sim::Side::second, {4, 1}, 3, 3, 0, 1},
        {30, 200, sim::Side::second, {5, 1}, 3, 3, 0, 1},
    };
    swept.units[1].drop_item_id = 92;
    swept.units[1].drop_chance = 60;
    swept.units[2].drop_item_id = 93;
    swept.units[2].drop_chance = 60;
    swept.abilities = {
        {1, sim::AbilityKind::damage, sim::DamageType::physical,
         sim::AreaShape::cross, 5, 1, 5, 0, 100}
    };
    swept.random_seed = 3;
    auto area = sim::create_encounter(swept);
    const auto cast =
        area.encounter.apply({sim::CommandType::ability, 10, {5, 1}, 0, 1});
    const auto after_cast = area.encounter.snapshot();
    expect(
        static_cast<bool>(cast) && after_cast.drops.size() == 2U &&
            after_cast.drops[0] == sim::DropRecord{20U, 10U, 92U} &&
            after_cast.drops[1] == sim::DropRecord{30U, 10U, 93U} &&
            drop_draws(after_cast) == 2U,
        "a multi-kill cast rolls once per felled unit, in identifier order"
    );
}

void a_drop_cannot_move_the_hit_stream() {
    // The whole reason `RandomStream` is identified per purpose. Two battles
    // that differ only in whether the loser leaves something behind must roll
    // the same hits and take the same damage, and the drop's own draws must not
    // appear on the hit stream's counter.
    // Uncertain on both sides and lethal, so the exchange takes hit numbers and
    // the defeat takes a drop number, in the same battle.
    sim::EncounterDefinition mirrored = chancy(1, 50, 50, 3);
    mirrored.units.front().drop_item_id = 90;
    // Ninety-five: uncertain, so it draws, and above seed 1's 93, so it also
    // falls. Both halves matter: a comparison where nothing dropped would
    // prove nothing.
    mirrored.units.front().drop_chance = 95;
    auto plain = sim::create_encounter(chancy(1, 50, 50, 3));
    auto leaving = sim::create_encounter(mirrored);
    expect(
        static_cast<bool>(plain) && static_cast<bool>(leaving),
        "both boards are valid"
    );
    const auto without =
        plain.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto with =
        leaving.encounter.apply({sim::CommandType::attack, 10, {}, 20});
    const auto quiet = plain.encounter.snapshot();
    const auto loud = leaving.encounter.snapshot();
    expect(
        hit_draws(quiet) == hit_draws(loud) && hit_draws(loud) == 1U,
        "the same hit numbers are taken whether or not anything drops"
    );
    expect(
        drop_draws(quiet) == 0U && drop_draws(loud) == 1U &&
            loud.drops.size() == 1U,
        "and the dropping board really did roll a drop, so this is a "
        "comparison and not two boards doing nothing"
    );
    // The drop's own events are the only difference. Everything the exchange
    // itself produced has to match blow for blow.
    std::vector<sim::Event> quiet_exchange;
    std::vector<sim::Event> loud_exchange;
    for (const sim::Event& event : without.events) {
        if (event.type != sim::EventType::item_dropped) {
            quiet_exchange.push_back(event);
        }
    }
    for (const sim::Event& event : with.events) {
        if (event.type != sim::EventType::item_dropped) {
            loud_exchange.push_back(event);
        }
    }
    bool same_exchange = quiet_exchange.size() == loud_exchange.size();
    for (std::size_t index = 0;
         same_exchange && index < quiet_exchange.size();
         ++index) {
        same_exchange =
            quiet_exchange[index].type == loud_exchange[index].type &&
            quiet_exchange[index].unit_id == loud_exchange[index].unit_id &&
            quiet_exchange[index].amount == loud_exchange[index].amount;
    }
    expect(
        same_exchange,
        "and the exchange resolves identically, blow for blow"
    );
    expect(
        unit(quiet, 20)->health == unit(loud, 20)->health &&
            unit(quiet, 10)->health == unit(loud, 10)->health,
        "so a drop chance cannot change what a battle costs"
    );
}

void a_half_authored_drop_is_refused() {
    sim::EncounterDefinition nothing_to_leave = barren_board(1);
    nothing_to_leave.units.front().drop_chance = 60;
    expect(
        sim::create_encounter(nothing_to_leave).error ==
            sim::CreateError::invalid_item,
        "a chance with nothing to leave is refused rather than half-honoured"
    );

    sim::EncounterDefinition no_chance_of_it = barren_board(1);
    no_chance_of_it.units.front().drop_item_id = 90;
    expect(
        sim::create_encounter(no_chance_of_it).error ==
            sim::CreateError::invalid_item,
        "and so is something to leave with no chance of leaving it, so that "
        "leaving nothing has one spelling"
    );

    sim::EncounterDefinition impossible = dropper(1, 101);
    expect(
        sim::create_encounter(impossible).error ==
            sim::CreateError::invalid_item,
        "a chance above a hundred is refused rather than clamped"
    );

    // The identity is deliberately not resolved against the encounter's item
    // registry: a drop is recorded and handed to nobody, so the board never
    // reads what it does.
    expect(
        static_cast<bool>(sim::create_encounter(dropper(1, 60))),
        "a drop naming an item this board does not define is accepted"
    );
}

void what_fell_is_canonical_state() {
    auto nothing = sim::create_encounter(barren_board(1));
    auto something = sim::create_encounter(dropper(1, 100));
    expect(
        nothing.encounter.canonical_hash() !=
            something.encounter.canonical_hash(),
        "what a unit would leave is canonical before anybody falls: one of "
        "these boards can draw from the drop stream and the other cannot"
    );
    expect(
        static_cast<bool>(
            something.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "the dropper falls"
    );
    auto again = sim::create_encounter(dropper(1, 100));
    expect(
        again.encounter.canonical_hash() !=
            something.encounter.canonical_hash(),
        "and what has fallen moves the hash, so a mid-battle save resumes with "
        "it still fallen"
    );
}

// ---------------------------------------------------------------------------
// The talk gesture.
// ---------------------------------------------------------------------------

// A board with somebody worth talking to on it: the duellist, the captain who
// turns out not to be a real enemy standing right beside her, and a brute who
// has nothing to say and is nowhere near either of them. Three characters so
// that talking the captain down does not end the battle by elimination, which
// is a separate test below.
constexpr sim::ContentId captain_talk_record = 7000U;

sim::EncounterDefinition parley_definition(
    sim::ContentId record = captain_talk_record
) {
    sim::EncounterDefinition definition;
    definition.width = 4;
    definition.height = 3;

    sim::UnitDefinition duellist;
    duellist.id = 10;
    duellist.unit_type_id = 100;
    duellist.side = sim::Side::first;
    duellist.position = {0, 1};
    duellist.health = 6;
    duellist.strength = 4;
    duellist.action_points = 1;

    sim::UnitDefinition captain;
    captain.id = 20;
    captain.unit_type_id = 200;
    captain.side = sim::Side::second;
    captain.position = {1, 1};
    captain.health = 5;
    captain.strength = 3;
    captain.action_points = 1;
    captain.talk_record_id = record;

    sim::UnitDefinition brute;
    brute.id = 30;
    brute.unit_type_id = 300;
    brute.side = sim::Side::second;
    brute.position = {3, 0};
    brute.health = 5;
    brute.strength = 3;
    brute.action_points = 1;

    definition.units = {duellist, captain, brute};
    return definition;
}

sim::Command parley(sim::UnitId target = 20, sim::UnitId speaker = 10) {
    sim::Command command;
    command.type = sim::CommandType::talk;
    command.unit_id = speaker;
    command.target_id = target;
    return command;
}

bool has_event(const sim::CommandResult& result, sim::EventType type) {
    for (const sim::Event& event : result.events) {
        if (event.type == type) return true;
    }
    return false;
}

// The zero-cost claim, and the only one that has to be proved from inside this
// suite rather than argued: a board on which nobody is talkable folds no talk
// record and no departure, so it is byte for byte the board an author who never
// heard of the gesture writes. The identity checked against is the browser
// conformance vector, which authors no talk.
void a_parley_nobody_authors_costs_nothing() {
    auto created = sim::create_encounter(
        {
            4,
            3,
            {
                {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1},
                {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0},
            }
        }
    );
    expect(static_cast<bool>(created), "the talkless board is valid");
    expect(
        created.encounter.canonical_hash() == 0x0e41227fef2c075fULL,
        "a board nobody authors a talk on hashes the reference vector, got " +
            hex(created.encounter.canonical_hash())
    );
    const auto snapshot = created.encounter.snapshot();
    for (const sim::UnitSnapshot& value : snapshot.units) {
        expect(
            value.talk_record_id == 0U && !value.departed,
            "nobody on it is talkable and nobody has left"
        );
    }
    // And the gesture is refused rather than silently absent, so a client that
    // asked would be told why.
    expect(
        created.encounter.apply(parley(20, 10)).error ==
            sim::CommandError::not_talkable,
        "and a talk aimed at anybody on it is refused as not talkable"
    );
}

void talking_takes_the_character_off_the_board() {
    auto created = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(created), "the parley board is valid");
    const auto result = created.encounter.apply(parley());
    expect(static_cast<bool>(result), "the captain is talked to");
    expect(
        !result.events.empty() &&
            result.events[0].type == sim::EventType::unit_talked &&
            result.events[0].unit_id == 20U &&
            result.events[0].related_unit_id == 10U &&
            result.events[0].position.x == 1 &&
            result.events[0].position.y == 1 &&
            result.events[0].content_id == captain_talk_record,
        "the event names who was talked to, by whom, where, and what it records"
    );
    const auto settled = created.encounter.snapshot();
    const sim::UnitSnapshot* captain = unit(settled, 20);
    expect(
        captain != nullptr && captain->departed && captain->health == 5,
        "he left the board carrying the health he had"
    );
}

// The load-bearing distinction, asserted from the outside: nothing about a
// departure looks like a defeat to anybody reading the events.
void a_departure_is_not_a_defeat() {
    auto created = sim::create_encounter(parley_definition());
    const auto result = created.encounter.apply(parley());
    expect(static_cast<bool>(result), "the captain is talked to");
    expect(
        !has_event(result, sim::EventType::unit_defeated),
        "no defeat event is emitted, so nobody is paid and nobody is buried"
    );
    expect(
        !has_event(result, sim::EventType::unit_damaged) &&
            !has_event(result, sim::EventType::item_dropped),
        "and nothing was taken off him on the way out"
    );
}

void a_talk_draws_nothing_from_any_stream() {
    auto created = sim::create_encounter(parley_definition());
    const auto before = created.encounter.snapshot().random;
    expect(static_cast<bool>(created.encounter.apply(parley())), "it goes");
    const auto after = created.encounter.snapshot().random;
    bool untouched = before.seed == after.seed;
    for (std::size_t i = 0; i < before.positions.size(); ++i) {
        untouched = untouched && before.positions[i] == after.positions[i];
    }
    expect(untouched, "no stream moved: a conversation is not a die roll");
}

void a_talk_is_priced_like_a_cast() {
    auto created = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(created.encounter.apply(parley())), "it goes");
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.active_side == sim::Side::second &&
            snapshot.remaining_action_points == 0U,
        "talking closes the activation exactly as a cast does"
    );
}

void every_refusal_a_talk_can_earn() {
    auto created = sim::create_encounter(parley_definition());
    expect(
        created.encounter.apply(parley(0)).error ==
            sim::CommandError::unknown_target,
        "a talk naming nobody is unknown: there is no default listener"
    );
    expect(
        created.encounter.apply(parley(999)).error ==
            sim::CommandError::unknown_target,
        "and a talk naming somebody this battle does not contain is unknown"
    );
    expect(
        created.encounter.apply(parley(30)).error ==
            sim::CommandError::not_talkable,
        "somebody with nothing authored to say is refused, not silently missed"
    );

    // Out of range: the same captain, from two tiles away. She needs a second
    // action point to step away and still have a turn left to talk with;
    // without one the refusal would be `no_action_points` and would prove
    // nothing about reach.
    auto stepping = parley_definition();
    stepping.units[0].action_points = 2;
    auto far = sim::create_encounter(stepping);
    expect(
        static_cast<bool>(
            far.encounter.apply({sim::CommandType::move, 10, {0, 0}, 0})
        ),
        "she steps away first"
    );
    expect(
        far.encounter.apply(parley()).error ==
            sim::CommandError::target_out_of_range,
        "a talk reaches one tile and no further"
    );

    // Already gone: talking twice to the same character.
    auto gone = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(gone.encounter.apply(parley())), "she talks");
    expect(
        static_cast<bool>(
            gone.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits"
    );
    expect(
        gone.encounter.apply(parley(20, 10)).error ==
            sim::CommandError::target_departed,
        "somebody already gone earns his own refusal, not the dead one"
    );
}

// Self-talk needs no rule of its own: a conversation takes two, and the reach
// says so at distance zero.
void a_talk_cannot_be_aimed_at_the_talker() {
    auto marked = parley_definition();
    marked.units[0].talk_record_id = 7002U;
    auto created = sim::create_encounter(marked);
    expect(
        created.encounter.apply(parley(10, 10)).error ==
            sim::CommandError::target_out_of_range,
        "talking to yourself is out of range rather than a rule of its own"
    );
}

void a_departed_character_holds_no_ground() {
    auto created = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(created.encounter.apply(parley())), "he leaves");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits"
    );
    const auto result =
        created.encounter.apply({sim::CommandType::move, 10, {1, 1}, 0});
    expect(
        static_cast<bool>(result),
        "the tile he was standing on is free the moment he is gone"
    );
}

void a_departed_character_is_never_chosen_to_act() {
    auto ordered = parley_definition();
    ordered.turn_order = sim::TurnOrder::initiative;
    auto created = sim::create_encounter(ordered);
    expect(static_cast<bool>(created), "the ordered parley board is valid");
    bool talked = false;
    for (int step = 0; step < 12; ++step) {
        const auto snapshot = created.encounter.snapshot();
        if (snapshot.outcome != sim::Outcome::ongoing) break;
        const sim::UnitId active = snapshot.active_unit_id;
        expect(
            !(talked && active == 20U),
            "a departed character is never handed an activation"
        );
        if (active == 0U) break;
        if (active == 10U && !talked) {
            talked = static_cast<bool>(created.encounter.apply(parley()));
            continue;
        }
        expect(
            static_cast<bool>(
                created.encounter.apply(
                    {sim::CommandType::wait, active, {}, 0}
                )
            ),
            "every other activation is spent waiting"
        );
    }
    expect(talked, "the captain was talked off the board along the way");
}

void the_last_opponent_walking_away_ends_the_battle() {
    auto lone = parley_definition();
    lone.units.pop_back();  // no brute: the captain is the whole opposition
    auto created = sim::create_encounter(lone);
    expect(static_cast<bool>(created), "the two-character board is valid");
    const auto result = created.encounter.apply(parley());
    expect(static_cast<bool>(result), "she talks him down");
    expect(
        !has_event(result, sim::EventType::unit_defeated),
        "and killed nobody doing it"
    );
    expect(
        has_event(result, sim::EventType::encounter_completed) &&
            created.encounter.snapshot().outcome ==
                sim::Outcome::first_side_won,
        "the battle is won because nobody is left standing against her"
    );
}

void objectives_answer_a_departure_deliberately() {
    {
        auto board = parley_definition();
        board.objectives = {
            {900, sim::ObjectiveKind::defeat_target, sim::Side::first, 20}
        };
        auto created = sim::create_encounter(board);
        expect(static_cast<bool>(created.encounter.apply(parley())), "talked");
        const auto snapshot = created.encounter.snapshot();
        expect(
            snapshot.objectives.size() == 1U &&
                snapshot.objectives[0].state == sim::ObjectiveState::failed,
            "an objective demanding his defeat is lost, not left pending"
        );
    }
    {
        auto board = parley_definition();
        board.objectives = {
            {901, sim::ObjectiveKind::protect_target, sim::Side::first, 20}
        };
        auto created = sim::create_encounter(board);
        expect(static_cast<bool>(created.encounter.apply(parley())), "talked");
        const auto snapshot = created.encounter.snapshot();
        expect(
            snapshot.objectives.size() == 1U &&
                snapshot.objectives[0].state == sim::ObjectiveState::pending,
            "an objective demanding his protection stays pending and cannot "
            "fail, rather than winning the battle on the spot"
        );
    }
    {
        auto board = parley_definition();
        board.units.pop_back();
        board.objectives = {
            {902, sim::ObjectiveKind::defeat_all_opponents, sim::Side::first, 0}
        };
        auto created = sim::create_encounter(board);
        expect(static_cast<bool>(created.encounter.apply(parley())), "talked");
        const auto snapshot = created.encounter.snapshot();
        expect(
            snapshot.objectives.size() == 1U &&
                snapshot.objectives[0].state == sim::ObjectiveState::satisfied,
            "defeating every opponent is satisfied when the last one departs"
        );
    }
}

void a_departed_character_cannot_be_struck() {
    auto created = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(created.encounter.apply(parley())), "he leaves");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits"
    );
    expect(
        created.encounter.apply({sim::CommandType::attack, 10, {}, 20}).error ==
            sim::CommandError::target_departed,
        "a strike aimed at somebody who walked away says so"
    );
}

void the_talk_forecast_is_the_promise() {
    auto created = sim::create_encounter(parley_definition());
    const auto forecast =
        sim::forecast_talk(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(forecast) && forecast.departing_id == 20U &&
            forecast.record_id == captain_talk_record,
        "the forecast names who leaves and what it records"
    );
    const auto result = created.encounter.apply(parley());
    expect(static_cast<bool>(result), "and applying it is accepted");
    expect(
        !result.events.empty() &&
            result.events[0].type == sim::EventType::unit_talked &&
            result.events[0].unit_id == forecast.departing_id &&
            result.events[0].content_id == forecast.record_id,
        "and delivers exactly what was shown"
    );
}

void a_talk_forecast_refuses_what_apply_would_refuse() {
    auto created = sim::create_encounter(parley_definition());
    const auto snapshot = created.encounter.snapshot();
    const sim::UnitId targets[] = {0U, 999U, 30U};
    for (const sim::UnitId target : targets) {
        const auto forecast = sim::forecast_talk(snapshot, 10, target);
        const auto applied = created.encounter.apply(parley(target));
        expect(
            forecast.error == applied.error && forecast.departing_id == 0U &&
                forecast.record_id == 0U,
            "the forecast refuses exactly what apply refuses, and promises "
            "nothing while it does"
        );
    }
}

// The other half of `a_departed_character_cannot_be_struck`, and the more
// dangerous one. Being struck is something done *to* a departed character and
// the engine has always refused it; acting is something done *by* one, and
// under `alternating` the caller chooses the actor, so nothing between the
// player and the rule refuses it but this.
void a_departed_character_cannot_act() {
    auto created = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(created.encounter.apply(parley())), "he leaves");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 10, {}, 0})
        ),
        "the duellist waits, so the turn comes back round to the second side"
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.active_side == sim::Side::second &&
            unit(snapshot, 20)->departed && unit(snapshot, 20)->health > 0,
        "and the captain is off the board with the health he walked away with"
    );
    // Every command he could be handed, each refused by its own name rather
    // than by the defeat refusal: he did not die.
    const sim::CommandType kinds[] = {
        sim::CommandType::wait,
        sim::CommandType::move,
        sim::CommandType::attack,
        sim::CommandType::talk,
        sim::CommandType::ability,
        sim::CommandType::use_item
    };
    for (const sim::CommandType kind : kinds) {
        sim::Command command;
        command.type = kind;
        command.unit_id = 20;
        command.destination = {2, 1};
        command.target_id = 10;
        expect(
            created.encounter.apply(command).error ==
                sim::CommandError::departed_unit,
            "a character talked off the board is refused every command by name"
        );
    }
    // The consequence the refusal prevents: he must not be struck down while
    // taking a swing he was never entitled to.
    const auto after = created.encounter.snapshot();
    expect(
        unit(after, 20)->health == unit(snapshot, 20)->health &&
            unit(after, 20)->position == unit(snapshot, 20)->position,
        "and nothing about him moved while he was being refused"
    );
}

// The three forecasts and `apply` refuse the same things in the same order.
// Driven from the outside for every actor and target this board can name, so
// that a refusal added to one and not the other is caught by the pairing rather
// than by somebody remembering to write a case for it.
void a_departed_or_unarrived_character_forecasts_what_apply_refuses() {
    // A duellist, a captain she can talk down, a brute, and a wave still
    // marching that she could reach if it were standing.
    auto authored = parley_definition();
    // Far enough out that the round the brute's turn closes does not bring it
    // in: this case is about a character who has not arrived, so it must still
    // be marching when every question below is asked.
    sim::UnitDefinition marching{
        40, 400, sim::Side::second, {1, 0}, 6, 3, 0, 1
    };
    marching.arrival_round = 4;
    marching.talk_record_id = 7003U;
    authored.units.push_back(marching);
    auto created = sim::create_encounter(authored);
    expect(static_cast<bool>(created), "the parley board carries a wave");
    expect(static_cast<bool>(created.encounter.apply(parley())), "he leaves");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits, handing the turn back to the duellist"
    );

    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 20)->departed && !unit(snapshot, 40)->arrived,
        "one character has gone and one has not come"
    );

    // Aimed at: the departed captain and the wave, from the duellist who still
    // holds her activation.
    expect(
        sim::forecast_attack(snapshot, 10, 20).error ==
            sim::CommandError::target_departed,
        "the attack forecast refuses a departed target rather than pricing it"
    );
    expect(
        sim::forecast_attack(snapshot, 10, 40).error ==
            sim::CommandError::target_unarrived,
        "and a target still marching"
    );
    expect(
        sim::forecast_talk(snapshot, 10, 20).error ==
            sim::CommandError::target_departed &&
            sim::forecast_talk(snapshot, 10, 40).error ==
                sim::CommandError::target_unarrived,
        "and the talk forecast says the same two things"
    );
    expect(
        created.encounter.apply({sim::CommandType::attack, 10, {}, 20}).error ==
                sim::CommandError::target_departed &&
            created.encounter.apply({sim::CommandType::attack, 10, {}, 40})
                    .error == sim::CommandError::target_unarrived,
        "which is exactly what applying those strikes earns"
    );

    // Asked of: the same two characters as actors. Both are on the second side,
    // so the turn is handed over first. A forecast taken while it is not their
    // side would answer `wrong_side` and prove nothing.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 10, {}, 0})
        ),
        "the duellist closes her turn"
    );
    const auto theirs = created.encounter.snapshot();
    expect(
        theirs.active_side == sim::Side::second,
        "and the second side holds the turn"
    );
    const sim::UnitId absent[] = {20U, 40U};
    for (const sim::UnitId actor : absent) {
        const sim::CommandError expected =
            actor == 20U ? sim::CommandError::departed_unit
                         : sim::CommandError::unarrived_unit;
        expect(
            sim::forecast_attack(theirs, actor, 10).error == expected,
            "the attack forecast refuses an actor who is not on the board"
        );
        expect(
            sim::forecast_talk(theirs, actor, 10).error == expected,
            "and so does the talk forecast, whose whole promise is that an "
            "accepted talk cannot fail"
        );
        expect(
            sim::forecast_item(theirs, actor, 0, {}, 0).error == expected,
            "and so does the item forecast"
        );
        expect(
            created.encounter.apply({sim::CommandType::attack, actor, {}, 10})
                    .error == expected,
            "which is the refusal apply hands back for the same command"
        );
    }
    // And nothing the forecasts refused promised anything while refusing it.
    const auto talked = sim::forecast_talk(theirs, 20, 10);
    expect(
        talked.departing_id == 0U && talked.record_id == 0U,
        "a refused talk describes no consequence"
    );
}

void a_talk_is_canonical_state() {
    auto plain = sim::create_encounter(parley_definition(0));
    auto marked = sim::create_encounter(parley_definition());
    expect(
        plain.encounter.canonical_hash() != marked.encounter.canonical_hash(),
        "a board with somebody talkable on it is not the board without one"
    );
    auto talked = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(talked.encounter.apply(parley())), "he leaves");
    expect(
        talked.encounter.canonical_hash() != marked.encounter.canonical_hash(),
        "and a battle he has left is not the battle he is still standing in"
    );
}

// A board where one character cannot be reduced below one health, and one where
// nobody can. Two units standing next to each other, so the very first command
// is a strike: 20 hits far harder than 10 has health, which is the overkill this
// floor exists to catch.
sim::EncounterDefinition enduring_definition(bool endures) {
    sim::EncounterDefinition board{
        4,
        3,
        {
            {20, 200, sim::Side::second, {1, 1}, 40, 30, 0, 0},
            {10, 100, sim::Side::first, {0, 1}, 3, 4, 0, 0},
        }
    };
    for (sim::UnitDefinition& unit : board.units) {
        if (unit.id == 10U) unit.endures = endures;
    }
    return board;
}

void a_character_who_endures_is_left_standing() {
    auto created = sim::create_encounter(enduring_definition(true));
    expect(static_cast<bool>(created), "the enduring board is valid");
    // 20 acts first. It is the second side, so hand it the turn by having 10
    // wait, and its swing is worth far more than 10 has left.
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0}
        )),
        "the enduring character passes the turn over"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10}
    );
    expect(static_cast<bool>(struck), "the overkill strike is accepted");
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10)->health == 1,
        "a character who endures is left holding one health"
    );
    expect(
        snapshot.outcome == sim::Outcome::ongoing,
        "and the battle is not over, because nobody was eliminated"
    );
    // The damage event says what was struck for, raw, exactly as an overkill
    // against anybody else does; the health total is where the floor shows. And
    // an event says the floor is what stopped it, immediately afterwards.
    expect(
        struck.events.size() >= 2 &&
            struck.events[0].type == sim::EventType::unit_damaged &&
            struck.events[1].type == sim::EventType::unit_endured &&
            struck.events[1].unit_id == 10U &&
            struck.events[1].related_unit_id == 20U,
        "the damage event is followed by an event naming who held on and who "
        "struck"
    );
    expect(
        std::none_of(
            struck.events.begin(),
            struck.events.end(),
            [](const sim::Event& event) {
                return event.type == sim::EventType::unit_defeated;
            }
        ),
        "and nobody was defeated"
    );

    // The same board without the floor is the board this engine always had.
    auto ordinary = sim::create_encounter(enduring_definition(false));
    expect(
        static_cast<bool>(ordinary.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0}
        )),
        "the ordinary character passes the turn over"
    );
    const auto felled = ordinary.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10}
    );
    expect(
        unit(ordinary.encounter.snapshot(), 10)->health == 0,
        "a character who does not endure still reaches zero"
    );
    expect(
        std::any_of(
            felled.events.begin(),
            felled.events.end(),
            [](const sim::Event& event) {
                return event.type == sim::EventType::unit_defeated;
            }
        ),
        "and is still defeated"
    );
    expect(
        std::none_of(
            felled.events.begin(),
            felled.events.end(),
            [](const sim::Event& event) {
                return event.type == sim::EventType::unit_endured;
            }
        ),
        "and nothing claims they held on"
    );
}

void a_blow_the_floor_did_not_catch_says_nothing() {
    auto plenty = enduring_definition(true);
    for (sim::UnitDefinition& unit : plenty.units) {
        if (unit.id == 10U) unit.health = 100;
        if (unit.id == 20U) unit.strength = 4;
    }
    auto created = sim::create_encounter(plenty);
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0}
        )),
        "the well-fed character passes the turn over"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10}
    );
    expect(
        unit(created.encounter.snapshot(), 10)->health > 1,
        "a blow that leaves health to spare leaves health to spare"
    );
    expect(
        std::none_of(
            struck.events.begin(),
            struck.events.end(),
            [](const sim::Event& event) {
                return event.type == sim::EventType::unit_endured;
            }
        ),
        "and emits no enduring event, because the floor caught nothing"
    );
}

// The promise, on the one blow most likely to break it. A forecast that reported
// this strike as lethal and then left the character standing would be the engine
// lying to a player about the only number that decides whether they commit.
void the_forecast_computes_the_floor_rather_than_clamping_after_it() {
    auto created = sim::create_encounter(enduring_definition(true));
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0}
        )),
        "the enduring character passes the turn over"
    );
    const auto snapshot = created.encounter.snapshot();
    const auto promised = sim::forecast_attack(snapshot, 20, 10);
    expect(static_cast<bool>(promised), "the strike is forecastable");
    expect(
        promised.damage > unit(snapshot, 10)->health,
        "the strike really is worth more than the character has"
    );
    expect(
        promised.target_health_after == 1,
        "and the forecast says one health, not zero"
    );
    expect(!promised.lethal, "and does not call it lethal");
    // A character who cannot be felled is still standing when the strike
    // resolves, so they answer, and both halves agree about that because both
    // computed the same post-blow health.
    expect(
        promised.counter,
        "a defender who cannot be felled answers the blow"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10}
    );
    expect(static_cast<bool>(struck), "and the same strike is accepted");
    const auto after = created.encounter.snapshot();
    expect(
        unit(after, 10)->health == promised.target_health_after,
        "apply delivers exactly the health the forecast promised"
    );
    expect(
        unit(after, 20)->health == promised.attacker_health_after,
        "and exactly the counter it promised"
    );
}

void enduring_is_canonical_only_where_it_is_authored() {
    auto plain = sim::create_encounter(enduring_definition(false));
    auto floored = sim::create_encounter(enduring_definition(true));
    expect(
        plain.encounter.canonical_hash() != floored.encounter.canonical_hash(),
        "a board nobody can be lost on is not the board they can"
    );
    // And the load-bearing half: a board that authors none of this folds no
    // byte for it, so it hashes to the reference vector every console and the
    // browser are pinned to rather than to some value of its own.
    auto reference = sim::create_encounter(
        {
            4,
            3,
            {
                {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1},
                {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0},
            }
        }
    );
    expect(
        reference.encounter.canonical_hash() == 0x0e41227fef2c075fULL,
        "the reference encounter hashes to its golden: " +
            hex(reference.encounter.canonical_hash())
    );
}

void hashes_are_canonical() {
    auto first_definition = definition();
    auto second_definition = first_definition;
    std::reverse(second_definition.units.begin(), second_definition.units.end());
    auto first = sim::create_encounter(first_definition);
    auto second = sim::create_encounter(second_definition);
    expect(
        first.encounter.canonical_hash() == second.encounter.canonical_hash(),
        "source unit ordering does not affect canonical state"
    );

    const sim::Command commands[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(first.encounter.apply(command)),
            "first replay command succeeds"
        );
        expect(
            static_cast<bool>(second.encounter.apply(command)),
            "second replay command succeeds"
        );
    }
    expect(
        first.encounter.canonical_hash() == second.encounter.canonical_hash(),
        "identical command streams produce identical hashes"
    );
}

void matches_browser_conformance_vector() {
    auto created = sim::create_encounter(
        {
            4,
            3,
            {
                {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1},
                {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0},
            }
        }
    );
    expect(
        created.encounter.canonical_hash() == 0x0e41227fef2c075fULL,
        "initial state matches browser conformance hash, got " +
            hex(created.encounter.canonical_hash())
    );
    const sim::Command commands[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(created.encounter.apply(command)),
            "browser conformance command succeeds natively"
        );
    }
    expect(
        created.encounter.canonical_hash() == 0x9090072b2c0a69c5ULL,
        "completed state matches browser conformance hash, got " +
            hex(created.encounter.canonical_hash())
    );
}

// The board for the reachability queries: a 5x5 field, one mover with two
// steps, an ally standing directly north of it, and a distant opponent.
sim::EncounterDefinition reach_definition() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 5;
    definition.units = {
        {1, 100, sim::Side::first, {2, 2}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 2},
        {2, 100, sim::Side::first, {2, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1},
        {9, 200, sim::Side::second, {4, 4}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1},
    };
    return definition;
}

void reachable_tiles_respect_range_blockers_and_occupancy() {
    auto created = sim::create_encounter(reach_definition());
    expect(static_cast<bool>(created), "reachability board is valid");
    const auto snapshot = created.encounter.snapshot();

    // Two steps from (2,2): the diamond of radius two, minus only the tile the
    // ally is standing on, in row-major order.
    //
    // **(2,0) is the tile that says which rule this is.** Its only two-step
    // path runs straight through the ally at (2,1), so it is offered exactly
    // when a walk may pass through one. And (2,1) itself is not offered, so
    // passing through is not the same permission as stopping. The two halves
    // are pinned by the same list, which is the only way they can be pinned
    // without one of them being able to drift.
    const std::vector<sim::Position> expected = {
        {2, 0},
        {1, 1}, {3, 1},
        {0, 2}, {1, 2}, {3, 2}, {4, 2},
        {1, 3}, {2, 3}, {3, 3},
        {2, 4},
    };
    const auto tiles = sim::reachable_tiles(snapshot, 1);
    expect(tiles.size() == expected.size(), "reachable tile count matches");
    bool identical = tiles.size() == expected.size();
    for (std::size_t index = 0; identical && index < tiles.size(); ++index) {
        identical = tiles[index] == expected[index];
    }
    expect(identical, "reachable tiles list the expected set in row-major order");
}

// The same board with the one character north of the mover moved to the other
// side. Nothing else about it changes, so whatever the two answers differ by is
// the whole of what a side is worth to a walk.
void an_opponent_blocks_the_way_an_ally_only_stands_in_it() {
    auto definition = reach_definition();
    definition.units[1].side = sim::Side::second;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the opposed board is valid");
    const auto snapshot = created.encounter.snapshot();

    // The diamond again, less the tile the opponent holds and less (2,0)
    // behind it: an opponent is a wall, so the corridor it stands in is shut
    // rather than merely occupied.
    const std::vector<sim::Position> expected = {
        {1, 1}, {3, 1},
        {0, 2}, {1, 2}, {3, 2}, {4, 2},
        {1, 3}, {2, 3}, {3, 3},
        {2, 4},
    };
    const auto tiles = sim::reachable_tiles(snapshot, 1);
    bool identical = tiles.size() == expected.size();
    for (std::size_t index = 0; identical && index < tiles.size(); ++index) {
        identical = tiles[index] == expected[index];
    }
    expect(identical, "an opponent shuts the way behind it as well as on it");

    // And the move rule says the same thing, which is the half a query cannot
    // promise on its own.
    auto fresh = sim::create_encounter(definition);
    const auto blocked = fresh.encounter.apply(
        {sim::CommandType::move, 1, sim::Position{2, 0}, 0, 0}
    );
    expect(
        blocked.error == sim::CommandError::invalid_destination,
        "and apply refuses the tile behind it as unreachable"
    );

    // The ally board answers the other way at the very same tile, and refuses
    // the tile itself for the one reason that is not about sides.
    auto allied = sim::create_encounter(reach_definition());
    expect(
        static_cast<bool>(allied.encounter.apply(
            {sim::CommandType::move, 1, sim::Position{2, 0}, 0, 0}
        )),
        "while a walk goes straight through an ally to the same tile"
    );
    auto onto = sim::create_encounter(reach_definition());
    expect(
        onto.encounter
                .apply({sim::CommandType::move, 1, sim::Position{2, 1}, 0, 0})
                .error == sim::CommandError::occupied_destination,
        "and never stops on the ally it passed"
    );
}

void reachable_tiles_agree_with_apply() {
    const auto reference = sim::reachable_tiles(
        sim::create_encounter(reach_definition()).encounter.snapshot(), 1
    );
    for (std::int16_t y = 0; y < 5; ++y) {
        for (std::int16_t x = 0; x < 5; ++x) {
            const sim::Position tile{x, y};
            const bool listed = std::find(
                reference.begin(), reference.end(), tile
            ) != reference.end();
            auto fresh = sim::create_encounter(reach_definition());
            const auto result = fresh.encounter.apply(
                {sim::CommandType::move, 1, tile, 0, 0}
            );
            expect(
                static_cast<bool>(result) == listed,
                "apply accepts a move exactly when reachable_tiles lists it"
            );
        }
    }
}

void reachable_tiles_exclude_unknown_defeated_and_immobile() {
    auto definition = reach_definition();
    definition.units[1].movement = 0;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "immobile-unit board is valid");
    expect(
        sim::reachable_tiles(created.encounter.snapshot(), 2).empty(),
        "a unit with no movement reaches nothing"
    );
    expect(
        sim::reachable_tiles(created.encounter.snapshot(), 77).empty(),
        "an unknown unit reaches nothing"
    );

    // Strike the opponent down, then ask again: the defeated reach nothing.
    sim::EncounterDefinition lethal;
    lethal.width = 3;
    lethal.height = 2;
    lethal.units = {
        {1, 100, sim::Side::first, {0, 0}, 8, 9, 0, 0, 0, 0, 0, 0, 0, 1},
        {2, 200, sim::Side::second, {1, 0}, 1, 1, 0, 0, 0, 0, 0, 0, 0, 3},
    };
    auto fight = sim::create_encounter(lethal);
    expect(
        static_cast<bool>(
            fight.encounter.apply({sim::CommandType::attack, 1, {}, 2, 0})
        ),
        "the lethal blow lands"
    );
    expect(
        sim::reachable_tiles(fight.encounter.snapshot(), 2).empty(),
        "a defeated unit reaches nothing"
    );
}

// --- Terrain ---

// A five-wide board cut in half by a river in column two, with a ford at the
// bottom. A walker on the west bank, a flier beside it, and an opponent on the
// east bank.
sim::EncounterDefinition river_definition() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 3;
    const auto open = sim::Terrain::open;
    const auto water = sim::Terrain::water;
    definition.terrain = {
        open, open, water, open, open,
        open, open, water, open, open,
        open, open, open,  open, open,
    };
    definition.units = {
        {1, 100, sim::Side::first, {1, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 2},
        {2, 101, sim::Side::first, {1, 0}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 2},
        {9, 200, sim::Side::second, {4, 0}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1},
    };
    definition.units[1].crossings = sim::crossing_every;
    return definition;
}

void terrain_stops_a_walker_and_not_a_flier() {
    auto created = sim::create_encounter(river_definition());
    expect(static_cast<bool>(created), "the river board is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto lists = [](const std::vector<sim::Position>& tiles,
                          std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };

    const auto walker = sim::reachable_tiles(snapshot, 1);
    expect(!lists(walker, 2, 1), "a walker does not stand in the river");
    expect(!lists(walker, 3, 1), "nor does it cross to the far bank");
    expect(lists(walker, 2, 2), "it takes the ford, which is why the ford is there");

    const auto flier = sim::reachable_tiles(snapshot, 2);
    expect(lists(flier, 2, 0), "a flier stands over the water");
    expect(lists(flier, 3, 0), "and reaches the far bank in one activation");

    // Apply agrees with the query on every tile, for both of them: this is the
    // rule, not a second opinion about it.
    for (const sim::UnitId mover : {sim::UnitId{1}, sim::UnitId{2}}) {
        const auto reference = sim::reachable_tiles(snapshot, mover);
        for (std::int16_t y = 0; y < 3; ++y) {
            for (std::int16_t x = 0; x < 5; ++x) {
                const sim::Position tile{x, y};
                const bool listed =
                    std::find(reference.begin(), reference.end(), tile) !=
                    reference.end();
                auto fresh = sim::create_encounter(river_definition());
                const auto result = fresh.encounter.apply(
                    {sim::CommandType::move, mover, tile, 0, 0}
                );
                expect(
                    static_cast<bool>(result) == listed,
                    "apply accepts a move over terrain exactly when the query "
                    "lists it"
                );
            }
        }
    }
}

void terrain_is_validated_and_canonical() {
    auto short_board = river_definition();
    short_board.terrain.pop_back();
    expect(
        sim::create_encounter(short_board).error ==
            sim::CreateError::invalid_map,
        "a board with a cell missing its terrain is refused"
    );

    auto unknown = river_definition();
    unknown.terrain[0] = static_cast<sim::Terrain>(9);
    expect(
        sim::create_encounter(unknown).error == sim::CreateError::invalid_map,
        "a cell asking something the rules do not define is refused"
    );

    auto drowned = river_definition();
    drowned.units[0].position = {2, 1};
    expect(
        sim::create_encounter(drowned).error == sim::CreateError::invalid_unit,
        "a walker may not begin standing in the river"
    );
    drowned.units[0].crossings = sim::crossing_water;
    expect(
        static_cast<bool>(sim::create_encounter(drowned)),
        "a swimmer may"
    );

    // Two boards identical but for the ground, and two units identical but for
    // their wings, are different encounters.
    auto level = river_definition();
    level.terrain.clear();
    expect(
        sim::create_encounter(level).encounter.canonical_hash() !=
            sim::create_encounter(river_definition())
                .encounter.canonical_hash(),
        "terrain is part of canonical state"
    );
    auto grounded = river_definition();
    grounded.units[1].crossings = sim::crossing_none;
    expect(
        sim::create_encounter(grounded).encounter.canonical_hash() !=
            sim::create_encounter(river_definition())
                .encounter.canonical_hash(),
        "what a unit can cross is part of canonical state"
    );
}

void terrain_narrows_the_danger_zone() {
    // The opponent on the east bank has two points, so it may walk and strike.
    // The river is the only thing keeping it off the west bank.
    auto definition = river_definition();
    definition.units[2].movement = 3;
    definition.units[2].action_points = 2;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the danger board is valid");
    const auto banked =
        sim::danger_tiles(created.encounter.snapshot(), sim::Side::second);
    const auto threatens = [](const std::vector<sim::Position>& tiles,
                              std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };
    expect(threatens(banked, 3, 1), "its own bank is threatened");
    expect(
        !threatens(banked, 1, 0),
        "the far bank is not, because the water is in the way"
    );

    auto level = definition;
    level.terrain.clear();
    const auto unbanked = sim::danger_tiles(
        sim::create_encounter(level).encounter.snapshot(), sim::Side::second
    );
    expect(
        threatens(unbanked, 1, 0) && unbanked.size() > banked.size(),
        "the same board without a river threatens strictly more"
    );
}

// A five-by-five field where the whole middle column is heavy going: every
// cell of x == 2 costs two to walk into, and everything else costs one. The
// mover stands on the west edge with four steps to spend.
sim::EncounterDefinition priced_definition() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 5;
    definition.units = {
        {1, 100, sim::Side::first, {0, 2}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 4},
        {9, 200, sim::Side::second, {4, 4}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 0},
    };
    definition.movement_cost.assign(25U, 1U);
    for (std::size_t y = 0; y < 5U; ++y) {
        definition.movement_cost[y * 5U + 2U] = 2U;
    }
    return definition;
}

void ground_can_be_slow_without_being_shut() {
    auto created = sim::create_encounter(priced_definition());
    expect(static_cast<bool>(created), "a priced board is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto tiles = sim::reachable_tiles(snapshot, 1);
    const auto offered = [&](std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };

    // Four steps down the row: two to enter the middle column, one for each
    // cell after it. So (3,2) costs four and is offered, and (4,2) costs five
    // and is not. On a board where every step cost one the mover would reach
    // the far edge with nothing to spare.
    expect(offered(3, 2), "four buys the cell past the heavy column");
    expect(!offered(4, 2), "and stops one short of the cell after it");
    // Nothing about the heavy column is *shut*: the mover may stand in it, and
    // may stand two cells beyond it by paying the way round.
    expect(offered(2, 2), "the heavy cell itself is open ground");
    expect(
        offered(0, 0), "and the near edge is reachable where nothing charges"
    );

    // Every offered tile is a tile the move rule accepts, and every unoffered
    // one is refused: the reach query and `apply` read one field.
    for (std::int16_t y = 0; y < 5; ++y) {
        for (std::int16_t x = 0; x < 5; ++x) {
            const sim::Position tile{x, y};
            if (tile == sim::Position{0, 2}) continue;
            auto fresh = sim::create_encounter(priced_definition());
            const auto result = fresh.encounter.apply(
                {sim::CommandType::move, 1, tile, 0, 0}
            );
            expect(
                static_cast<bool>(result) == offered(tile.x, tile.y),
                "the priced reach query and the move rule agree on every tile"
            );
        }
    }
}

void a_board_with_no_price_plays_as_it_always_did() {
    // The precedent this follows is passability's: absent means the default,
    // and a package that says nothing about price is a package where every
    // step costs one. An all-ones list says the same thing, so the two are the
    // same battle: same reach, same danger, same hash.
    auto bare = sim::create_encounter(reach_definition());
    auto definition = reach_definition();
    definition.movement_cost.assign(25U, 1U);
    auto priced = sim::create_encounter(definition);
    expect(
        static_cast<bool>(bare) && static_cast<bool>(priced),
        "both boards are valid"
    );
    expect(
        sim::reachable_tiles(bare.encounter.snapshot(), 1) ==
            sim::reachable_tiles(priced.encounter.snapshot(), 1),
        "a board priced at one everywhere offers exactly what an unpriced one "
        "offers"
    );
    expect(
        sim::danger_tiles(bare.encounter.snapshot(), sim::Side::second) ==
            sim::danger_tiles(priced.encounter.snapshot(), sim::Side::second),
        "and threatens exactly what it threatens"
    );
    expect(
        bare.encounter.canonical_hash() == priced.encounter.canonical_hash(),
        "and is the same battle, down to its canonical hash"
    );

    // And a cell charging nothing is refused rather than read as free ground.
    definition.movement_cost[7] = 0U;
    auto free_cell = sim::create_encounter(definition);
    expect(
        free_cell.error == sim::CreateError::invalid_map,
        "a cell charging nothing is an invalid map"
    );
    // As is a list that prices some of the board and not the rest.
    auto partial = reach_definition();
    partial.movement_cost.assign(3U, 1U);
    expect(
        sim::create_encounter(partial).error == sim::CreateError::invalid_map,
        "so is a price list that does not cover the board"
    );
}

void price_narrows_the_danger_zone() {
    // The danger zone is every tile the other side could reach *and* strike
    // before you act again, so it has to count price exactly as a walk does.
    // A warning that counted steps while a walk counted price would shade
    // tiles the board does not threaten.
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 1;
    definition.units = {
        {1, 100, sim::Side::first, {0, 0}, 8, 3, 0, 1},
        {9, 200, sim::Side::second, {6, 0}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 3, 2},
    };
    const auto threatens = [](const std::vector<sim::Position>& tiles,
                              std::int16_t x) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, 0}) !=
               tiles.end();
    };

    // Three steps and a point to spare from the strike: the opponent walks to
    // (3,0) and its one-tile band covers (2,0).
    auto level = sim::create_encounter(definition);
    expect(static_cast<bool>(level), "the level board is valid");
    const auto wide =
        sim::danger_tiles(level.encounter.snapshot(), sim::Side::second);
    expect(threatens(wide, 2), "the far tile is threatened over level ground");

    // Make the cell at (4,0) heavy going and the same three steps stop there:
    // one to enter (5,0) and two more to enter (4,0) spends the lot, so the
    // furthest stance is (4,0), its band still covers (3,0), and (2,0) falls
    // out of the zone.
    definition.movement_cost.assign(7U, 1U);
    definition.movement_cost[4] = 2U;
    auto marsh = sim::create_encounter(definition);
    expect(static_cast<bool>(marsh), "the priced board is valid");
    const auto narrow =
        sim::danger_tiles(marsh.encounter.snapshot(), sim::Side::second);
    expect(
        !threatens(narrow, 2),
        "and is not once the ground between charges for itself"
    );
    expect(threatens(narrow, 3), "while the tile it can still afford stays in");

    // And the promise underneath: every tile the warning shades is a tile the
    // walk can actually reach with a point left over.
    const auto stances = sim::movement_field(
        marsh.encounter.snapshot(), {6, 0}, 3, sim::crossing_none,
        sim::Side::second
    );
    expect(
        stances[6] == 0U && stances[5] == 1U && stances[4] == 3U &&
            stances[3] == sim::unreachable_cost,
        "which is exactly what the one movement field says: three spent to "
        "stand on the heavy cell, and no walk past it"
    );
}

void a_flier_pays_one_everywhere() {
    auto definition = priced_definition();
    definition.units[0].crossings = sim::crossing_every;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the flier's board is valid");
    const auto tiles = sim::reachable_tiles(created.encounter.snapshot(), 1);
    const auto offered = [&](std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };
    // Four steps carry the flier to the far edge of its own row, because
    // nothing underfoot slows somebody who is not walking on it.
    expect(offered(4, 2), "a flier crosses the heavy column for a step");
    expect(
        sim::entry_cost(9U, sim::crossing_every) == 1U,
        "and pays one for the dearest ground a board can state"
    );
}

void the_danger_zone_is_budgeted_by_action_points() {
    // One opponent with three steps and a one-tile reach, alone on a wide
    // board, so its zone is exactly what its budget buys.
    sim::EncounterDefinition definition;
    definition.width = 11;
    definition.height = 5;
    definition.units = {
        {1, 100, sim::Side::first, {0, 0}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1},
        {9, 200, sim::Side::second, {5, 2}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 3},
    };
    const auto zone = [&definition](std::uint8_t points) {
        auto budgeted = definition;
        budgeted.units[1].action_points = points;
        return sim::danger_tiles(
            sim::create_encounter(budgeted).encounter.snapshot(),
            sim::Side::second
        );
    };
    const auto covers = [](const std::vector<sim::Position>& tiles,
                           std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };

    // One point: it may move or strike. Only the band around where it stands.
    const auto single = zone(1);
    expect(single.size() == 4, "one point threatens the four tiles it reaches");
    expect(covers(single, 5, 1) && !covers(single, 5, 0), "and no further");

    // Two: three steps and then a strike, which is the classic zone.
    const auto pair = zone(2);
    expect(covers(pair, 5, 0) && !covers(pair, 0, 2), "two points walk once");

    // Three: still one walk and a strike, because one walk is all an
    // activation buys however many points are left over. The zone says what
    // the board will actually take rather than what the arithmetic alone would
    // suggest, so the third point widens nothing.
    const auto triple = zone(3);
    expect(
        !covers(triple, 0, 2) && triple.size() == pair.size(),
        "a third point buys no second walk"
    );

    // And a character who has already walked threatens only its own band,
    // whatever it has left, because it cannot be standing anywhere else when
    // it strikes.
    auto walked = definition;
    walked.units[1].action_points = 3;
    auto moving = sim::create_encounter(walked);
    expect(
        static_cast<bool>(
            moving.encounter.apply({sim::CommandType::wait, 1, {}, 0, 0})
        ),
        "the first side takes its turn"
    );
    expect(
        static_cast<bool>(
            moving.encounter.apply({sim::CommandType::move, 9, {5, 1}, 0})
        ),
        "and the opponent spends its one walk"
    );
    const auto after_walking =
        sim::danger_tiles(moving.encounter.snapshot(), sim::Side::second);
    expect(
        after_walking.size() == 4,
        "a character that has walked threatens only the band it stands in"
    );
}

void a_spent_unit_leaves_the_danger_zone() {
    // Ordered turns are where "has acted" means anything: the first side's
    // unit acts, and once it has, it is not a danger until the round turns.
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 3;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.units = {
        {1, 100, sim::Side::first, {0, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1, 2},
        {2, 100, sim::Side::first, {6, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1},
        {9, 200, sim::Side::second, {3, 1}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1},
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the ordered board is valid");
    const auto before =
        sim::danger_tiles(created.encounter.snapshot(), sim::Side::first);
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.active_side == sim::Side::first && snapshot.active_unit_id == 0,
        "a side block opens on the side and names nobody"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 1, {}, 0, 0})
        ),
        "and whichever of its characters is picked spends its activation"
    );
    const auto after =
        sim::danger_tiles(created.encounter.snapshot(), sim::Side::first);
    expect(
        after.size() < before.size(),
        "a unit that has acted this round stops painting the board"
    );
    const auto covers = [](const std::vector<sim::Position>& tiles,
                           std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };
    expect(
        !covers(after, 1, 1) && covers(after, 5, 1),
        "its own band is gone and its ally's remains"
    );
}

// --- Carried weapons ---

// A duellist carrying a dagger and a bow, and a lone opponent three tiles
// away. The dagger is the weapon in hand; only the bow reaches that far.
sim::EncounterDefinition carrying_definition() {
    sim::EncounterDefinition definition;
    definition.width = 6;
    definition.height = 3;
    definition.weapons = {
        {70, 4, 1, 1},   // dagger: strong, adjacent only
        {71, 1, 2, 3},   // bow: weak, reaches two to three
    };
    definition.units = {
        {10, 100, sim::Side::first, {0, 1}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 2,
         1, false,
         1, 1, {}, {70, 71}},
        {20, 200, sim::Side::second, {3, 1}, 20, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false,
         1, 1, {}, {70}},
    };
    return definition;
}

void carried_weapons_resolve_the_weapon_in_hand() {
    auto created = sim::create_encounter(carrying_definition());
    expect(static_cast<bool>(created), "the carrying encounter is valid");
    const auto snapshot = created.encounter.snapshot();
    const sim::UnitSnapshot* duellist = unit(snapshot, 10);
    expect(
        duellist != nullptr &&
            duellist->weapon_ids == std::vector<sim::ContentId>{70, 71},
        "the snapshot reports every carried weapon, in carried order"
    );
    // The definition left power and band at their defaults; the engine
    // resolved them from the first carried weapon rather than trusting them.
    expect(
        duellist != nullptr && duellist->power == 4 &&
            duellist->minimum_reach == 1 && duellist->maximum_reach == 1,
        "the engine resolves power and band from the weapon in hand"
    );
    expect(
        created.encounter.weapons().size() == 2,
        "the encounter exposes the weapons it was created with"
    );
}

void an_attack_naming_a_weapon_uses_that_weapon() {
    auto created = sim::create_encounter(carrying_definition());
    expect(static_cast<bool>(created), "the carrying encounter is valid");

    // The dagger cannot reach three tiles, whether it is named or merely held.
    const auto held = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0, 0}
    );
    expect(
        held.error == sim::CommandError::target_out_of_range,
        "the weapon in hand cannot reach the distant opponent"
    );
    const auto named_dagger = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0, 70}
    );
    expect(
        named_dagger.error == sim::CommandError::target_out_of_range,
        "naming the weapon in hand is the same command as naming nothing"
    );

    // The bow reaches, and lands for the bow's power rather than the dagger's.
    const auto forecast = sim::forecast_attack(
        created.encounter.snapshot(), 10, 20, created.encounter.weapons(), 71
    );
    expect(
        static_cast<bool>(forecast) && forecast.damage == 4,
        "the forecast prices the named weapon: strength 3 plus power 1"
    );
    const auto shot = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0, 71}
    );
    expect(static_cast<bool>(shot), "the bow reaches the distant opponent");
    expect(
        !shot.events.empty() && shot.events.front().amount == 4,
        "the strike lands for the named weapon's power"
    );
}

void naming_a_weapon_the_unit_cannot_use_is_refused() {
    auto created = sim::create_encounter(carrying_definition());
    expect(static_cast<bool>(created), "the carrying encounter is valid");
    // The opponent carries the dagger and nothing else, so the bow is a
    // weapon this encounter defines but that unit does not carry. Reach it by
    // handing the turn over first.
    expect(
        static_cast<bool>(created.encounter.apply(
            {sim::CommandType::wait, 10, {}, 0, 0, 0}
        )),
        "the duellist yields the turn"
    );
    const auto borrowed = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10, 0, 71}
    );
    expect(
        borrowed.error == sim::CommandError::unavailable_weapon,
        "a weapon the unit does not carry is refused"
    );
    const auto invented = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 10, 0, 999}
    );
    expect(
        invented.error == sim::CommandError::unknown_weapon,
        "a weapon the encounter does not define is refused"
    );
    // The weapon is judged before the target, so the refusal names what the
    // player got wrong rather than a consequence of it.
    const auto both_wrong = created.encounter.apply(
        {sim::CommandType::attack, 20, {}, 4242, 0, 999}
    );
    expect(
        both_wrong.error == sim::CommandError::unknown_weapon,
        "an unknown weapon is decided before an unknown target"
    );
    expect(
        created.encounter.snapshot().activation_count == 1,
        "every refusal leaves the state exactly as it was"
    );
    expect(
        sim::error_name(sim::CommandError::unknown_weapon) ==
                "unknown_weapon" &&
            sim::error_name(sim::CommandError::unavailable_weapon) ==
                "unavailable_weapon",
        "the new refusals have names"
    );
}

void a_malformed_weapon_registry_is_refused() {
    const auto refuse = [](std::vector<sim::WeaponDefinition> weapons,
                           std::vector<sim::ContentId> carried) {
        sim::EncounterDefinition definition = carrying_definition();
        definition.weapons = std::move(weapons);
        definition.units.front().weapon_ids = std::move(carried);
        definition.units.back().weapon_ids = {};
        return sim::create_encounter(definition).error;
    };
    expect(
        refuse({{0, 1, 1, 1}}, {}) == sim::CreateError::invalid_weapon,
        "a weapon with no identity is refused"
    );
    expect(
        refuse({{70, -1, 1, 1}}, {70}) == sim::CreateError::invalid_weapon,
        "negative weapon power is refused"
    );
    expect(
        refuse({{70, 1, 0, 1}}, {70}) == sim::CreateError::invalid_weapon,
        "a zero minimum reach is refused"
    );
    expect(
        refuse({{70, 1, 3, 2}}, {70}) == sim::CreateError::invalid_weapon,
        "a minimum reach past its maximum is refused"
    );
    expect(
        refuse({{70, 1, 1, 1}, {70, 2, 1, 1}}, {70}) ==
            sim::CreateError::invalid_weapon,
        "a duplicate weapon identity is refused"
    );
    expect(
        refuse({{70, 1, 1, 1}}, {99}) == sim::CreateError::invalid_weapon,
        "carrying an identity the registry does not define is refused"
    );
    expect(
        sim::error_name(sim::CreateError::invalid_weapon) == "invalid_weapon",
        "the new creation refusal has a name"
    );
}

void the_carried_list_is_canonical_state() {
    auto carrying = sim::create_encounter(carrying_definition());
    sim::EncounterDefinition lighter = carrying_definition();
    // Drop the bow the duellist never draws. Everything it holds is the same.
    lighter.units.front().weapon_ids = {70};
    auto travelling_light = sim::create_encounter(lighter);
    expect(
        static_cast<bool>(carrying) && static_cast<bool>(travelling_light),
        "both loadouts are valid"
    );
    // Both snapshots are held: `snapshot()` returns by value, so a pointer
    // into one taken inline would outlive the thing it points into.
    const auto heavy_state = carrying.encounter.snapshot();
    const auto light_state = travelling_light.encounter.snapshot();
    const sim::UnitSnapshot* heavy = unit(heavy_state, 10);
    const sim::UnitSnapshot* light = unit(light_state, 10);
    expect(
        heavy != nullptr && light != nullptr && heavy->power == light->power &&
            heavy->minimum_reach == light->minimum_reach &&
            heavy->maximum_reach == light->maximum_reach,
        "the weapon in hand is identical in both loadouts"
    );
    expect(
        carrying.encounter.canonical_hash() !=
            travelling_light.encounter.canonical_hash(),
        "a second carried weapon is part of canonical state"
    );
}

void danger_tiles_count_every_band_a_unit_can_use() {
    // One opponent, standing still, carrying a dagger and a bow and knowing
    // both a long-range bolt and a long-range mend.
    sim::EncounterDefinition definition;
    definition.width = 9;
    definition.height = 3;
    definition.weapons = {{70, 4, 1, 1}, {71, 1, 3, 3}};
    definition.abilities = {
        {80, sim::AbilityKind::damage, sim::DamageType::physical,
         sim::AreaShape::single, 3, 5, 5, 0},
        {81, sim::AbilityKind::restore, sim::DamageType::physical,
         sim::AreaShape::single, 3, 7, 7, 0},
    };
    definition.units = {
        {10, 100, sim::Side::first, {8, 2}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 1, 1,
         1, false,
         1, 1, {}, {}},
        {20, 200, sim::Side::second, {0, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false,
         1, 1, {80, 81}, {70, 71}},
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the many-banded board is valid");
    const auto snapshot = created.encounter.snapshot();

    const auto narrow = sim::danger_tiles(snapshot, sim::Side::second);
    const auto wide = sim::danger_tiles(
        snapshot, sim::Side::second, created.encounter.weapons(),
        created.encounter.abilities()
    );
    const auto in = [](const std::vector<sim::Position>& tiles,
                       std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };

    expect(in(narrow, 1, 0), "the weapon in hand threatens what it always did");
    expect(
        !in(narrow, 3, 0) && in(wide, 3, 0),
        "the second weapon's band widens the warning"
    );
    expect(
        !in(narrow, 5, 0) && in(wide, 5, 0),
        "a damaging ability's band widens the warning"
    );
    expect(
        !in(wide, 7, 0),
        "a restoring ability reaches further and is still not a danger"
    );
    expect(
        !in(narrow, 8, 0) && !in(wide, 8, 0),
        "a tile beyond every band a unit can use stays safe"
    );

    // A unit carrying nothing still threatens the band it snapshots with, and
    // an identity the caller cannot resolve is skipped rather than guessed at.
    const auto unarmed = sim::danger_tiles(
        snapshot, sim::Side::first, created.encounter.weapons(),
        created.encounter.abilities()
    );
    expect(
        in(unarmed, 7, 2),
        "a unit carrying no weapon still contributes its own band"
    );
    const auto unresolvable =
        sim::danger_tiles(snapshot, sim::Side::second, {}, {});
    expect(
        in(unresolvable, 1, 0) && !in(unresolvable, 3, 0),
        "an unresolvable carried identity falls back to the unit's own band"
    );
}

// A character written to shoot further shoots further with everything, and the
// three places the engine resolves a band all agree about by how much.
//
// The bonus rides on the unit rather than on the weapon, so the shared authored
// weapon record is untouched: the archer below and the swordsman opposite draw
// from the same registry, and only one of them was written to reach.
void a_reach_bonus_widens_every_band_a_character_strikes_with() {
    sim::EncounterDefinition definition;
    definition.width = 12;
    definition.height = 3;
    // A dagger that reaches one, and a bow that reaches exactly three.
    definition.weapons = {{70, 4, 1, 1}, {71, 1, 3, 3}};
    definition.abilities = {
        {80, sim::AbilityKind::damage, sim::DamageType::physical,
         sim::AreaShape::single, 3, 5, 5, 0},
    };
    definition.units = {
        {10, 100, sim::Side::first, {11, 2}, 30, 3, 0, 0, 0, 0, 0, 0, 0, 1, 1,
         1, false, 1, 1, {}, {}},
        {20, 200, sim::Side::second, {0, 0}, 30, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {80}, {70, 71}},
    };
    // Written to reach two tiles further than whatever she is holding.
    definition.units[1].reach_bonus = 2;

    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the written archer's board is valid");
    const auto snapshot = created.encounter.snapshot();
    const sim::UnitSnapshot& archer = snapshot.units[1];

    // 1. The weapon in hand, resolved by `create_encounter`.
    expect(
        archer.minimum_reach == 1U && archer.maximum_reach == 3U,
        "the band of the weapon in hand is the weapon's floor and the "
        "character's ceiling"
    );
    expect(
        archer.reach_bonus == 2U,
        "and the unit carries the bonus itself, for the resolutions its "
        "in-hand band does not describe"
    );
    expect(
        snapshot.units[0].maximum_reach == 1U &&
            snapshot.units[0].reach_bonus == 0U,
        "the character nobody wrote anything about reaches exactly what they "
        "were defined to reach"
    );
    expect(
        created.encounter.weapons()[1].maximum_reach == 3U,
        "and the authored weapon record is untouched, which is what makes the "
        "bonus a fact about the archer rather than about the bow"
    );

    // 2. The danger overlay, in both of its shapes.
    const auto in = [](const std::vector<sim::Position>& tiles,
                       std::int16_t x, std::int16_t y) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, y}) !=
               tiles.end();
    };
    const auto narrow = sim::danger_tiles(snapshot, sim::Side::second);
    const auto wide = sim::danger_tiles(
        snapshot, sim::Side::second, created.encounter.weapons(),
        created.encounter.abilities()
    );
    expect(
        in(narrow, 3, 0),
        "the in-hand overlay warns as far as the widened band, because it "
        "reads the band the unit snapshots with"
    );
    expect(
        in(wide, 5, 0) && !in(wide, 6, 0),
        "and the weapon-aware overlay widens the bow's own band by the same "
        "number, and by no more"
    );
    // The ability reaches five on its own and is not widened; if it were, the
    // tile at seven would be threatened.
    expect(
        !in(wide, 7, 0),
        "an ability's reach is the ability's, so a character's bonus does not "
        "move it"
    );

    // 3. A weapon an attack names, resolved by `resolve_strike`.
    const auto strike_at = [&](std::int16_t x, sim::ContentId weapon) {
        sim::EncounterDefinition board = definition;
        board.units[0].position = {x, 0};
        auto pair = sim::create_encounter(board);
        expect(static_cast<bool>(pair), "the striking board is valid");
        // The second side acts after the first waits.
        (void)pair.encounter.apply({sim::CommandType::wait, 10, {}, 0, 0, 0, 0});
        return pair.encounter
            .apply({sim::CommandType::attack, 20, {}, 10, 0, weapon, 0})
            .error;
    };
    expect(
        strike_at(5, 71) == sim::CommandError::none,
        "the bow reaches five, which is its own three widened by the "
        "character's two"
    );
    expect(
        strike_at(6, 71) == sim::CommandError::target_out_of_range,
        "and stops there, so a bonus widens a band rather than removing one"
    );
    // 4. The floor is the weapon's and is never lowered.
    expect(
        strike_at(2, 71) == sim::CommandError::target_out_of_range,
        "the bow still cannot answer from inside its own minimum, which is the "
        "same target_out_of_range every client prints as OUT OF RANGE"
    );
}

// The counter gate is the defender's own band, so a character written to reach
// further also answers from further, and still cannot answer from inside the
// floor the bonus never touched.
void a_reach_bonus_lets_a_character_answer_from_further() {
    sim::EncounterDefinition definition;
    definition.width = 8;
    definition.height = 2;
    // The defender's bow: floor two, ceiling two. The attacker carries a
    // longer one so that it can strike from a tile the defender's authored
    // band does not reach back from.
    definition.weapons = {{70, 2, 2, 2}, {71, 2, 1, 3}};
    definition.units = {
        {10, 100, sim::Side::first, {0, 0}, 40, 3, 0, 0, 0, 0, 0, 0, 0, 4, 2,
         2, false, 1, 1, {}, {71}},
        {20, 200, sim::Side::second, {3, 0}, 40, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}, {70}},
    };
    definition.units[1].reach_bonus = 1;

    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the answering board is valid");
    const auto snapshot = created.encounter.snapshot();
    expect(
        snapshot.units[1].minimum_reach == 2U &&
            snapshot.units[1].maximum_reach == 3U,
        "the defender's band is her weapon's floor and her own ceiling"
    );
    expect(
        created.encounter.weapons()[0].minimum_reach == 2U &&
            created.encounter.weapons()[0].maximum_reach == 2U,
        "while the authored weapon she is holding still reaches exactly two, "
        "so nothing else carrying it reaches further"
    );

    // Struck from three tiles away: further than the authored bow reaches,
    // and inside the band the character was written to have.
    const std::int16_t before = snapshot.units[0].health;
    auto answered = sim::create_encounter(definition);
    (void)answered.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0, 0, 0}
    );
    expect(
        answered.encounter.snapshot().units[0].health < before,
        "a character written to reach further strikes back from the distance "
        "she was written to reach"
    );

    // And from one tile away: inside the floor the bonus did not lower.
    sim::EncounterDefinition adjacent = definition;
    adjacent.units[0].position = {2, 0};
    auto unanswered = sim::create_encounter(adjacent);
    expect(static_cast<bool>(unanswered), "the adjacent board is valid");
    const std::int16_t standing =
        unanswered.encounter.snapshot().units[0].health;
    (void)unanswered.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20, 0, 0, 0}
    );
    expect(
        unanswered.encounter.snapshot().units[0].health == standing,
        "and still cannot answer somebody standing on top of her, which is the "
        "promise the untouched floor keeps"
    );
}

// A bonus wider than a byte saturates rather than wrapping. An archer who
// wrapped would reach nothing at all for having been written to reach
// everything, and 255 is already wider than any board this engine admits.
void a_reach_bonus_saturates_rather_than_wrapping() {
    sim::EncounterDefinition definition;
    definition.width = 4;
    definition.height = 2;
    definition.weapons = {{70, 2, 1, 250}};
    definition.units = {
        {10, 100, sim::Side::first, {0, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}, {70}},
        {20, 200, sim::Side::second, {3, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1,
         1, false, 1, 1, {}, {70}},
    };
    definition.units[1].reach_bonus = 255;
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "the saturating board is valid");
    expect(
        created.encounter.snapshot().units[1].maximum_reach == 255U,
        "250 widened by 255 is 255 and not 249"
    );
}

// The warning grows where the walk does, and this is the case that says so.
//
// A seven-tile corridor with a swordsman at the east end and one of its own
// standing in front of it. The swordsman may file past its fellow, so it can
// stand two tiles further west than the corridor looks like it allows, and
// everything its blade covers from there is threatened. Put an opponent on that
// same square instead and the corridor is shut: the same character, the same
// two points, the same blade, and a shorter warning.
//
// **This is the invariant the whole overlay rests on.** A board that let a walk
// through a gap while the drawing still called the gap plugged would be a board
// lying about danger, so the two are pinned against each other here rather than
// only against themselves.
void danger_tiles_reach_through_the_threatening_side_own_line() {
    const auto corridor = [](sim::Side blocker_side) {
        sim::EncounterDefinition definition;
        definition.width = 7;
        definition.height = 1;
        definition.units = {
            {1, 100, sim::Side::first, {0, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 1,
             1, 1, false, 1, 1},
            // Two points and two steps: one walk and one blow, which is the
            // classic zone this query is named for.
            {5, 200, sim::Side::second, {5, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 2,
             2, 1, false, 1, 1},
            // Standing in the corridor and going nowhere.
            {6, 201, blocker_side, {4, 0}, 12, 3, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
             false, 1, 1},
        };
        auto created = sim::create_encounter(definition);
        expect(static_cast<bool>(created), "the corridor board is valid");
        return sim::danger_tiles(
            created.encounter.snapshot(), sim::Side::second
        );
    };
    const auto shades = [](const std::vector<sim::Position>& tiles,
                           std::int16_t x) {
        return std::find(tiles.begin(), tiles.end(), sim::Position{x, 0}) !=
               tiles.end();
    };

    const auto through_an_ally = corridor(sim::Side::second);
    expect(
        shades(through_an_ally, 2),
        "a swordsman that can file past its own reaches two tiles further"
    );
    expect(
        shades(through_an_ally, 3),
        "and the tile it would swing from is shaded with it"
    );

    const auto stopped_by_an_opponent = corridor(sim::Side::first);
    expect(
        !shades(stopped_by_an_opponent, 2),
        "an opponent in the corridor shuts the warning as it shuts the walk"
    );
    expect(
        shades(stopped_by_an_opponent, 4),
        "while the tile that opponent stands on is still inside the blade"
    );
}

void danger_tiles_union_movement_and_weapon_band() {
    // A bow with a minimum range: one step of movement, strikes at two or
    // three. Beside it, a knight with a fragile grip on life.
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 5;
    definition.units = {
        {1, 100, sim::Side::first, {5, 2}, 12, 9, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1,
         false,
         1, 3},
        {5, 200, sim::Side::second, {1, 2}, 8, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1,
         false,
         2, 3},
        {6, 201, sim::Side::second, {6, 0}, 1, 3, 0, 1, 0, 0, 0, 0, 0, 1, 1, 1,
         false,
         1, 1},
    };
    auto created = sim::create_encounter(definition);
    expect(static_cast<bool>(created), "danger board is valid");
    const auto snapshot = created.encounter.snapshot();
    const auto danger = sim::danger_tiles(snapshot, sim::Side::second);
    const auto threatened = [&danger](std::int16_t x, std::int16_t y) {
        return std::find(
            danger.begin(), danger.end(), sim::Position{x, y}
        ) != danger.end();
    };

    expect(threatened(3, 2), "a tile inside the bow band is threatened");
    expect(
        !threatened(1, 2),
        "the archer's own tile sits inside its minimum range"
    );
    // One action point buys one command. These two tiles are exactly the ones
    // a sidestep would open, and neither is reachable in a turn that also
    // contains a shot.
    expect(
        !threatened(2, 2),
        "a one-point archer cannot step aside and shoot in the same turn"
    );
    expect(
        !threatened(5, 2),
        "nor walk into range of an occupied tile and shoot it"
    );
    expect(!threatened(4, 4), "a tile beyond every band is safe");
    expect(
        threatened(6, 1),
        "the knight's melee band joins the union"
    );

    // The same board, with the archer given a point to move and a point to
    // shoot. Both tiles the budget denied above come back, which is the whole
    // difference the budget makes.
    {
        auto walking = definition;
        walking.units[1].action_points = 2;
        auto pair = sim::create_encounter(walking);
        expect(static_cast<bool>(pair), "the two-point board is valid");
        const auto wider =
            sim::danger_tiles(pair.encounter.snapshot(), sim::Side::second);
        const auto reaches = [&wider](std::int16_t x, std::int16_t y) {
            return std::find(
                wider.begin(), wider.end(), sim::Position{x, y}
            ) != wider.end();
        };
        expect(
            reaches(2, 2) && reaches(5, 2),
            "a second point buys the sidestep, and the shot after it"
        );
        expect(
            wider.size() > danger.size(),
            "a unit that can move and strike threatens strictly more"
        );
    }

    // Fell the knight: its band leaves the union, the archer's remains.
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 1, {}, 6, 0})
        ),
        "the knight falls"
    );
    const auto after = sim::danger_tiles(
        created.encounter.snapshot(), sim::Side::second
    );
    const auto still = [&after](std::int16_t x, std::int16_t y) {
        return std::find(
            after.begin(), after.end(), sim::Position{x, y}
        ) != after.end();
    };
    expect(!still(6, 1), "a defeated unit threatens nothing");
    expect(still(3, 2), "the living archer still threatens its band");
}

// --- Counterattacks ---

// Two duellists a tile apart on a 5x3 field. Both bands default to one, so
// each answers the other; every case below moves exactly one number off this.
sim::EncounterDefinition duel_definition() {
    sim::EncounterDefinition definition;
    definition.width = 5;
    definition.height = 3;
    definition.units = {
        {10, 100, sim::Side::first, {1, 1}, 10, 4, 0, 1},
        {20, 200, sim::Side::second, {2, 1}, 10, 3, 0, 1},
    };
    return definition;
}

// How many units the events say were damaged, which is how the "a counter
// provokes no counter" rule is proved: one exchange means exactly two.
std::size_t damage_events(const sim::CommandResult& result) {
    return static_cast<std::size_t>(std::count_if(
        result.events.begin(),
        result.events.end(),
        [](const sim::Event& event) {
            return event.type == sim::EventType::unit_damaged;
        }
    ));
}

void a_surviving_defender_in_band_strikes_back() {
    auto created = sim::create_encounter(duel_definition());
    expect(static_cast<bool>(created), "the duel encounter is valid");
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(static_cast<bool>(struck), "the duel strike is accepted");
    // Out: 4 + 0 - 1 = 3. Back: 3 + 0 - 1 = 2, struck the other way by the
    // same formula, which is why a counter needs no second arithmetic.
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 20) != nullptr && unit(snapshot, 20)->health == 7 &&
            unit(snapshot, 10) != nullptr && unit(snapshot, 10)->health == 8,
        "the defender loses three and takes two back"
    );
    expect(
        damage_events(struck) == 2 &&
            struck.events[0].unit_id == 20 &&
            struck.events[0].related_unit_id == 10 &&
            struck.events[1].unit_id == 10 &&
            struck.events[1].related_unit_id == 20,
        "the exchange reports both halves, attributed to each other"
    );
    // The counter is not itself an attack command, so nothing recurses: two
    // damaged units and no third. Two units that annihilated each other over
    // one press would be the failure this pins against.
    expect(
        damage_events(struck) == 2,
        "a counter provokes no counter of its own"
    );
    // And it cost the defender nothing it will need on its own turn.
    expect(
        snapshot.active_side == sim::Side::second &&
            unit(snapshot, 20) != nullptr && !unit(snapshot, 20)->has_acted,
        "countering spends no turn and no action point"
    );
}

void a_felled_defender_does_not_answer() {
    auto fixture = duel_definition();
    fixture.units[1].health = 3;  // exactly what the strike removes
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the finishing encounter is valid");
    const auto snapshot_before = created.encounter.snapshot();
    const auto forecast = sim::forecast_attack(snapshot_before, 10, 20);
    expect(
        forecast.lethal && !forecast.counter &&
            forecast.attacker_health_after == 10,
        "a lethal strike is forecast as unanswered"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    // A killing blow being safe is what makes "can I finish it this turn" the
    // question worth asking, so it is a rule and not an accident.
    expect(
        damage_events(struck) == 1 &&
            unit(created.encounter.snapshot(), 10)->health == 10,
        "a defeated defender takes nothing back"
    );
}

void an_archer_struck_from_inside_its_minimum_cannot_answer() {
    auto fixture = duel_definition();
    // A bow that reaches two to three: it cannot shoot a neighbour, which is
    // the same target_out_of_range too far earns and every client prints as
    // OUT OF RANGE, rather than a second refusal of its own.
    fixture.units[1].minimum_reach = 2;
    fixture.units[1].maximum_reach = 3;
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the archer encounter is valid");
    const auto forecast =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(forecast) && !forecast.counter &&
            forecast.counter_damage == 0 &&
            forecast.attacker_health_after == 10,
        "the forecast says the archer cannot shoot back"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(
        damage_events(struck) == 1 &&
            unit(created.encounter.snapshot(), 10)->health == 10 &&
            unit(created.encounter.snapshot(), 20)->health == 7,
        "an archer struck from an adjacent tile takes the blow and answers "
        "nothing"
    );
}

void a_defender_outranged_cannot_answer() {
    // The other end of the same band: a swordsman shot from two tiles away is
    // hurt and cannot reply, which is what turns a bow from a longer weapon
    // into a different one.
    auto fixture = duel_definition();
    fixture.units[0].minimum_reach = 1;
    fixture.units[0].maximum_reach = 2;
    fixture.units[1].position = {3, 1};
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the outranging encounter is valid");
    const auto forecast =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(forecast) && forecast.damage == 3 &&
            !forecast.counter,
        "a defender outside its own maximum answers nothing"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(
        damage_events(struck) == 1 &&
            unit(created.encounter.snapshot(), 10)->health == 10,
        "the outranging attacker walks away whole"
    );
}

void a_counter_can_kill_and_can_decide_the_encounter() {
    auto fixture = duel_definition();
    // The attacker is one blow from falling and the defender will survive to
    // deal it. A counter that could not kill would need a clamp, and a clamp
    // would be a second damage formula nobody could predict from the first.
    fixture.units[0].health = 2;
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the lethal-counter encounter is valid");
    const auto forecast =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    expect(
        static_cast<bool>(forecast) && forecast.counter &&
            forecast.counter_damage == 2 &&
            forecast.attacker_health_after == 0 && forecast.counter_lethal,
        "the forecast warns that the counter is lethal to the attacker"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    const auto snapshot = created.encounter.snapshot();
    expect(
        unit(snapshot, 10) != nullptr && unit(snapshot, 10)->health == 0,
        "a counter can fell the unit that provoked it"
    );
    expect(
        std::any_of(
            struck.events.begin(),
            struck.events.end(),
            [](const sim::Event& event) {
                return event.type == sim::EventType::unit_defeated &&
                       event.unit_id == 10 && event.related_unit_id == 20;
            }
        ),
        "the defeat is credited to the counterattacker"
    );
    // The acting side has just emptied itself, which no activation could do
    // before counters existed. Without the second backstop the survivor would
    // be left standing on a board that never ends.
    expect(
        snapshot.outcome == sim::Outcome::second_side_won,
        "an activation that empties the acting side ends the encounter"
    );
}

void an_ability_provokes_no_counter() {
    auto fixture = duel_definition();
    fixture.abilities = {{70, sim::AbilityKind::damage,
                          sim::DamageType::physical, sim::AreaShape::single,
                          5, 1, 2, 0}};
    fixture.units[0].ability_ids = {70};
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the casting encounter is valid");
    const auto cast = created.encounter.apply(
        {sim::CommandType::ability, 10, {2, 1}, 0, 70}
    );
    expect(static_cast<bool>(cast), "the cast is accepted");
    expect(
        damage_events(cast) == 1 &&
            unit(created.encounter.snapshot(), 10)->health == 10,
        "an ability is answered by nobody, whatever it lands on"
    );
}

void a_counter_uses_the_weapon_in_hand() {
    // The defender carries a strong dagger in hand and a weak long bow behind
    // it. It answers with what it is holding, and never rummages: "your
    // equipped weapon" is the rule, and searching would make the gate depend
    // on a choice no player made.
    sim::EncounterDefinition fixture;
    fixture.width = 6;
    fixture.height = 3;
    fixture.weapons = {
        {70, 5, 1, 1},   // dagger in hand: strong, adjacent only
        {71, 1, 2, 3},   // bow behind it: weak, reaches two to three
    };
    fixture.units = {
        {10, 100, sim::Side::first, {1, 1}, 12, 4, 0, 1},
        {20, 200, sim::Side::second, {2, 1}, 12, 2, 0, 1, 0, 0, 0, 0, 0, 1, 1,
         1, false,
         1, 1, {}, {70, 71}},
    };
    auto created = sim::create_encounter(fixture);
    expect(static_cast<bool>(created), "the two-weapon defender is valid");
    const auto forecast =
        sim::forecast_attack(created.encounter.snapshot(), 10, 20);
    // 2 (strength) + 5 (dagger) - 1 (defense) = 6, not the bow's 2.
    expect(
        static_cast<bool>(forecast) && forecast.counter &&
            forecast.counter_damage == 6,
        "the counter is struck with the weapon in hand"
    );
    const auto struck = created.encounter.apply(
        {sim::CommandType::attack, 10, {}, 20}
    );
    expect(
        static_cast<bool>(struck) &&
            unit(created.encounter.snapshot(), 10)->health == 6,
        "apply strikes the counter with the same weapon the forecast priced"
    );
}

// A four-by-three board whose western column and the tile above it are the
// region a player arranges in. Three characters stand on the first side: two
// inside the region (the ones the player may move) and one the author pinned
// on the far side of the board.
sim::EncounterDefinition deployment_definition() {
    sim::EncounterDefinition value{
        4,
        3,
        {
            {20, 200, sim::Side::second, {3, 1}, 6, 3, 0, 1},
            {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 1},
            {11, 100, sim::Side::first, {0, 0}, 8, 4, 0, 1},
            {12, 100, sim::Side::first, {3, 2}, 8, 4, 0, 1},
        }
    };
    // Deliberately not in row-major order: the engine sorts it, so what an
    // author typed cannot reach the canonical hash.
    value.deployment_tiles = {{0, 2}, {1, 0}, {0, 0}, {0, 1}};
    return value;
}

void a_board_with_no_region_never_enters_the_phase() {
    auto created = sim::create_encounter(definition());
    const auto snapshot = created.encounter.snapshot();
    expect(
        !snapshot.deploying && snapshot.deployment_tiles.empty(),
        "an encounter that authors no region opens on the first activation"
    );
    for (const sim::UnitSnapshot& value : snapshot.units) {
        expect(
            !sim::is_deployable(snapshot, value),
            "and nobody on it is arrangeable"
        );
    }
    const auto deployed =
        created.encounter.apply({sim::CommandType::deploy, 10, {0, 0}, 0});
    expect(
        deployed.error == sim::CommandError::wrong_phase &&
            deployed.events.empty(),
        "a deploy on such a board is the wrong phase"
    );
    const auto begun =
        created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0});
    expect(
        begun.error == sim::CommandError::wrong_phase,
        "and so is beginning a battle that already began"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 10, {1, 1}, 0})
        ),
        "while an ordinary move is accepted exactly as it always was"
    );
}

void a_region_opens_the_phase_and_names_who_may_move() {
    auto created = sim::create_encounter(deployment_definition());
    expect(static_cast<bool>(created), "a region is valid content");
    const auto snapshot = created.encounter.snapshot();
    expect(snapshot.deploying, "an encounter with a region opens in the phase");
    expect(
        snapshot.activation_count == 0 && snapshot.active_unit_id == 0,
        "and no activation has begun"
    );
    const std::vector<sim::Position> expected{{0, 0}, {1, 0}, {0, 1}, {0, 2}};
    expect(
        snapshot.deployment_tiles.size() == expected.size(),
        "the region is carried in the snapshot"
    );
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(
            snapshot.deployment_tiles[index] == expected[index],
            "and is sorted row-major whatever order it was authored in"
        );
    }
    expect(
        sim::is_deployable(snapshot, *unit(snapshot, 10)) &&
            sim::is_deployable(snapshot, *unit(snapshot, 11)),
        "a first-side character inside the region is arrangeable"
    );
    expect(
        !sim::is_deployable(snapshot, *unit(snapshot, 12)),
        "one the author pinned outside it is not"
    );
    expect(
        !sim::is_deployable(snapshot, *unit(snapshot, 20)),
        "and neither is anybody on the other side"
    );
}

void deploying_is_free_and_repeatable() {
    auto created = sim::create_encounter(deployment_definition());
    const std::uint64_t opening = created.encounter.canonical_hash();
    const auto first =
        created.encounter.apply({sim::CommandType::deploy, 10, {0, 2}, 0});
    expect(
        static_cast<bool>(first) && first.events.size() == 1 &&
            first.events[0].type == sim::EventType::unit_deployed &&
            first.events[0].unit_id == 10 &&
            first.events[0].position == sim::Position{0, 2},
        "a deploy is reported by an event naming who and where"
    );
    expect(
        created.encounter.canonical_hash() != opening,
        "and moves the canonical hash, because the arrangement is state"
    );
    const auto again =
        created.encounter.apply({sim::CommandType::deploy, 10, {1, 0}, 0});
    const auto after = created.encounter.snapshot();
    expect(
        static_cast<bool>(again) && unit(after, 10)->position ==
                                        sim::Position{1, 0},
        "the same character may be stood somewhere else, any number of times"
    );
    expect(
        after.activation_count == 0 && after.active_unit_id == 0 &&
            after.deploying,
        "arranging spends no activation and does not end the phase"
    );
    expect(
        after.random.positions == sim::EncounterSnapshot{}.random.positions,
        "and draws nothing from any random stream"
    );
    const std::uint64_t standing = created.encounter.canonical_hash();
    const auto idle =
        created.encounter.apply({sim::CommandType::deploy, 10, {1, 0}, 0});
    expect(
        static_cast<bool>(idle) &&
            created.encounter.canonical_hash() == standing,
        "putting a character down where it stands is accepted and changes "
        "nothing"
    );
}

void every_refusal_a_deploy_can_earn() {
    auto created = sim::create_encounter(deployment_definition());
    const std::uint64_t before = created.encounter.canonical_hash();
    struct Case final {
        sim::Command command;
        sim::CommandError expected;
        const char* description;
    };
    const Case cases[] = {
        {{sim::CommandType::deploy, 999, {0, 0}, 0},
         sim::CommandError::unknown_unit,
         "a character nobody is"},
        {{sim::CommandType::deploy, 20, {0, 0}, 0},
         sim::CommandError::wrong_side,
         "a character on the other side"},
        {{sim::CommandType::deploy, 12, {0, 0}, 0},
         sim::CommandError::undeployable_unit,
         "a character the author pinned outside the region"},
        {{sim::CommandType::deploy, 10, {9, 9}, 0},
         sim::CommandError::invalid_destination,
         "a tile that is not on the board"},
        {{sim::CommandType::deploy, 10, {2, 2}, 0},
         sim::CommandError::outside_zone,
         "a tile that is on the board and not in the region"},
        {{sim::CommandType::deploy, 10, {0, 0}, 0},
         sim::CommandError::occupied_destination,
         "a region tile somebody else is standing on"},
    };
    for (const Case& value : cases) {
        const auto refused = created.encounter.apply(value.command);
        expect(
            refused.error == value.expected && refused.events.empty(),
            std::string("a deploy is refused for ") + value.description
        );
    }
    expect(
        created.encounter.canonical_hash() == before,
        "and no refusal touched the canonical hash"
    );
}

void the_phase_ends_only_when_it_is_ended() {
    auto created = sim::create_encounter(deployment_definition());
    const sim::Command ordinary[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 10, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
        {sim::CommandType::use_item, 10, {}, 0},
        {sim::CommandType::ability, 10, {1, 1}, 0},
    };
    for (const sim::Command& command : ordinary) {
        const auto refused = created.encounter.apply(command);
        expect(
            refused.error == sim::CommandError::wrong_phase &&
                refused.events.empty(),
            "every ordinary command is the wrong phase while arranging"
        );
    }
    expect(
        created.encounter.snapshot().random.positions ==
            sim::EncounterSnapshot{}.random.positions,
        "a refused strike moved no stream"
    );
    const auto begun =
        created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0});
    expect(
        static_cast<bool>(begun) && begun.events.size() == 1 &&
            begun.events[0].type == sim::EventType::deployment_ended,
        "beginning the battle is reported by an event of its own"
    );
    const auto opened = created.encounter.snapshot();
    expect(
        !opened.deploying && opened.active_side == sim::Side::first &&
            opened.active_unit_id == 0,
        "and leaves the board exactly where an unarranged one opens"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::move, 10, {1, 1}, 0})
        ),
        "after which an ordinary move is accepted"
    );
    expect(
        created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0})
                .error == sim::CommandError::wrong_phase,
        "and beginning it twice is the wrong phase"
    );
}

void deployable_tiles_agree_with_apply() {
    auto created = sim::create_encounter(deployment_definition());
    const auto snapshot = created.encounter.snapshot();
    const auto offered = sim::deployable_tiles(snapshot, 10);
    // (0,0) holds the second arrangeable character; every other region tile,
    // including the one this character already stands on, is offered.
    const std::vector<sim::Position> expected{{1, 0}, {0, 1}, {0, 2}};
    expect(
        offered.size() == expected.size(),
        "an occupied region tile is not offered"
    );
    for (std::size_t index = 0; index < expected.size(); ++index) {
        expect(
            index < offered.size() && offered[index] == expected[index],
            "and what is offered is row-major"
        );
    }
    for (std::int16_t y = 0; y < 3; ++y) {
        for (std::int16_t x = 0; x < 4; ++x) {
            auto probe = sim::create_encounter(deployment_definition());
            const bool accepted = static_cast<bool>(
                probe.encounter.apply({sim::CommandType::deploy, 10, {x, y}, 0})
            );
            const bool listed =
                std::find(offered.begin(), offered.end(), sim::Position{x, y}) !=
                offered.end();
            expect(
                accepted == listed,
                "a tile is offered exactly when a deploy to it is accepted"
            );
        }
    }
    expect(
        sim::deployable_tiles(snapshot, 12).empty(),
        "a pinned character is offered nothing"
    );
    expect(
        sim::deployable_tiles(snapshot, 20).empty(),
        "and so is the other side"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0})
        ) && sim::deployable_tiles(created.encounter.snapshot(), 10).empty(),
        "and nothing is arrangeable once the battle has begun"
    );
}

void the_other_queries_state_the_phase() {
    auto created = sim::create_encounter(deployment_definition());
    const auto snapshot = created.encounter.snapshot();
    expect(
        sim::reachable_tiles(snapshot, 10).empty(),
        "nothing is reachable while the phase is open"
    );
    expect(
        sim::forecast_attack(snapshot, 10, 20).error ==
            sim::CommandError::wrong_phase,
        "an attack forecast reports the phase rather than a number"
    );
    expect(
        sim::forecast_item(snapshot, 10, 0, created.encounter.items(), 90)
                .error == sim::CommandError::wrong_phase,
        "and so does an item forecast"
    );
    const auto opening = sim::danger_tiles(snapshot, sim::Side::second);
    expect(
        !opening.empty(),
        "the danger zone answers during the phase, which is why it is chosen in"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::deploy, 10, {0, 2}, 0})
        ),
        "a character is stood elsewhere"
    );
    const auto after = created.encounter.snapshot();
    expect(
        sim::danger_tiles(after, sim::Side::second) == opening,
        "and the opposing side's zone is read off the board as it now stands"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0})
        ),
        "the battle begins"
    );
    const auto opened = created.encounter.snapshot();
    expect(
        !sim::reachable_tiles(opened, 10).empty(),
        "after which the move rule answers again"
    );
    expect(
        sim::forecast_attack(opened, 10, 20).error !=
            sim::CommandError::wrong_phase,
        "and so does the forecast"
    );
}

void an_arrangement_replays_to_the_same_board() {
    auto first = sim::create_encounter(deployment_definition());
    auto second = sim::create_encounter(deployment_definition());
    const sim::Command script[] = {
        {sim::CommandType::deploy, 11, {1, 0}, 0},
        {sim::CommandType::deploy, 10, {0, 0}, 0},
        {sim::CommandType::begin_battle, 0, {}, 0},
        {sim::CommandType::move, 10, {0, 1}, 0},
    };
    for (const sim::Command& command : script) {
        expect(
            static_cast<bool>(first.encounter.apply(command)) &&
                static_cast<bool>(second.encounter.apply(command)),
            "the same arrangement is accepted twice"
        );
    }
    expect(
        first.encounter.canonical_hash() == second.encounter.canonical_hash(),
        "and two runs of it reach the same canonical state"
    );
    auto other = sim::create_encounter(deployment_definition());
    expect(
        static_cast<bool>(
            other.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0})
        ) &&
            other.encounter.canonical_hash() !=
                first.encounter.canonical_hash(),
        "a different arrangement is a different battle"
    );
}

// The compatibility claim that is worth more than the distinction it gave up:
// a board that gains a region, and whose player leaves everybody where the
// content put them, is the same battle in every observable way: the same
// canonical state, the same derived seed, and therefore the same rolls.
void a_region_nobody_used_costs_nothing() {
    auto plain_definition = deployment_definition();
    plain_definition.deployment_tiles.clear();
    auto plain = sim::create_encounter(plain_definition);
    auto zoned = sim::create_encounter(deployment_definition());
    expect(
        plain.encounter.snapshot().random.seed ==
            zoned.encounter.snapshot().random.seed,
        "a region does not move the seed the encounter derives for itself"
    );
    expect(
        static_cast<bool>(
            zoned.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0})
        ),
        "the battle begins with nobody rearranged"
    );
    expect(
        plain.encounter.canonical_hash() == zoned.encounter.canonical_hash(),
        "and from there the two are the same battle, got " +
            hex(zoned.encounter.canonical_hash())
    );
    const sim::Command script[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
    };
    for (const sim::Command& command : script) {
        expect(
            static_cast<bool>(plain.encounter.apply(command)) ==
                static_cast<bool>(zoned.encounter.apply(command)),
            "the same command is accepted on both"
        );
    }
    expect(
        plain.encounter.canonical_hash() == zoned.encounter.canonical_hash() &&
            plain.encounter.snapshot().random.positions ==
                zoned.encounter.snapshot().random.positions,
        "and they stay the same battle, dice included"
    );
}

void a_malformed_region_is_refused() {
    auto off_board = deployment_definition();
    off_board.deployment_tiles.push_back({4, 0});
    expect(
        sim::create_encounter(off_board).error ==
            sim::CreateError::invalid_deployment,
        "a region tile off the board is refused"
    );
    auto repeated = deployment_definition();
    repeated.deployment_tiles.push_back({0, 1});
    expect(
        sim::create_encounter(repeated).error ==
            sim::CreateError::invalid_deployment,
        "and so is the same tile named twice"
    );
}


// A board with two characters far enough apart that waiting is a whole turn
// and nobody dies by accident.
sim::EncounterDefinition rounds_definition() {
    return {
        4,
        3,
        {
            {20, 200, sim::Side::second, {3, 1}, 6, 3, 0, 1},
            {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1},
        }
    };
}

sim::ObjectiveDefinition survive(sim::Side side, std::uint32_t rounds) {
    sim::ObjectiveDefinition value;
    value.id = 1;
    value.kind = sim::ObjectiveKind::survive_rounds;
    value.side = side;
    value.round_count = rounds;
    return value;
}

// One turn for each side, which is one round under alternating order.
void pass(sim::Encounter& encounter, sim::UnitId first, sim::UnitId second) {
    expect(
        static_cast<bool>(encounter.apply({sim::CommandType::wait, first})),
        "the first side takes its turn"
    );
    expect(
        static_cast<bool>(encounter.apply({sim::CommandType::wait, second})),
        "and then the second side takes its turn"
    );
}

std::uint32_t steps_between(sim::Position from, sim::Position to) {
    const int dx = from.x > to.x ? from.x - to.x : to.x - from.x;
    const int dy = from.y > to.y ? from.y - to.y : to.y - from.y;
    return static_cast<std::uint32_t>(dx + dy);
}

void a_board_that_counts_no_rounds_costs_nothing() {
    // The browser conformance board, played through its own vector. It authors
    // no survive objective and nobody arrives on it, so the round it reports
    // and the bytes it folds are the ones it reported and folded before rounds
    // were counted under alternating order. That is what the golden says.
    auto created = sim::create_encounter(
        {
            4,
            3,
            {
                {20, 200, sim::Side::second, {2, 1}, 5, 2, 0, 1},
                {10, 100, sim::Side::first, {0, 1}, 8, 4, 0, 0},
            }
        }
    );
    const sim::Command commands[] = {
        {sim::CommandType::move, 10, {1, 1}, 0},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
        {sim::CommandType::wait, 20, {}, 0},
        {sim::CommandType::attack, 10, {}, 20},
    };
    for (const sim::Command& command : commands) {
        expect(
            static_cast<bool>(created.encounter.apply(command)),
            "the conformance vector still plays"
        );
        expect(
            created.encounter.snapshot().round == 0,
            "and a board nothing counts rounds for stands at zero throughout"
        );
    }
    expect(
        created.encounter.canonical_hash() == 0x9090072b2c0a69c5ULL,
        "and reaches the reference vector's completed hash, got " +
            hex(created.encounter.canonical_hash())
    );
}

void a_round_is_one_pass_through_the_turn_order() {
    auto authored = rounds_definition();
    authored.objectives = {survive(sim::Side::first, 9)};
    auto created = sim::create_encounter(authored);
    expect(static_cast<bool>(created), "a survive objective is valid content");
    expect(
        created.encounter.snapshot().round == 0,
        "a battle opens in its first round with none completed"
    );
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 10})),
        "the first side takes a turn"
    );
    expect(
        created.encounter.snapshot().round == 0,
        "and half a pass is not a round"
    );
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 20})),
        "the second side takes a turn"
    );
    expect(
        created.encounter.snapshot().round == 1,
        "and the turn coming back round to the opening side closes the round"
    );

    // The same rule under an ordered turn order, which counts exactly the same
    // thing: a pass over everybody still standing.
    auto ordered = rounds_definition();
    ordered.turn_order = sim::TurnOrder::side_blocks;
    ordered.objectives = {survive(sim::Side::first, 9)};
    auto blocks = sim::create_encounter(ordered);
    expect(
        static_cast<bool>(blocks.encounter.apply({sim::CommandType::wait, 10})),
        "the first side's block runs"
    );
    expect(
        blocks.encounter.snapshot().round == 0,
        "and is not a pass on its own"
    );
    expect(
        static_cast<bool>(blocks.encounter.apply({sim::CommandType::wait, 20})),
        "the second side's block runs"
    );
    expect(
        blocks.encounter.snapshot().round == 1,
        "and everybody having acted is the pass"
    );
}

void surviving_is_won_as_the_round_closes() {
    auto authored = rounds_definition();
    authored.objectives = {survive(sim::Side::first, 2)};
    auto created = sim::create_encounter(authored);
    pass(created.encounter, 10, 20);
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::ongoing &&
            created.encounter.snapshot().round == 1,
        "one round of two is not a win"
    );
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 10})),
        "the first side takes its second turn"
    );
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::ongoing,
        "and the middle of the second round is not a win either"
    );
    const auto closing = created.encounter.apply({sim::CommandType::wait, 20});
    expect(static_cast<bool>(closing), "the second side closes the round");
    expect(
        created.encounter.snapshot().round == 2 &&
            created.encounter.snapshot().outcome ==
                sim::Outcome::first_side_won,
        "and the battle is won by the very command that closed it"
    );
    const bool completed = std::any_of(
        closing.events.begin(),
        closing.events.end(),
        [](const sim::Event& event) {
            return event.type == sim::EventType::encounter_completed;
        }
    );
    expect(completed, "which is reported on that command rather than the next");
    expect(
        created.encounter.snapshot().objectives.front().state ==
            sim::ObjectiveState::satisfied,
        "and the objective is satisfied rather than left pending"
    );
    expect(
        created.encounter.objectives().front().round_count == 2,
        "the count an author wrote is readable beside the count reached"
    );
}

void surviving_with_nobody_left_is_failing() {
    sim::EncounterDefinition authored{
        3,
        1,
        {
            {20, 200, sim::Side::second, {1, 0}, 6, 9, 0, 0},
            {10, 100, sim::Side::first, {0, 0}, 1, 1, 0, 0},
        }
    };
    authored.objectives = {survive(sim::Side::first, 9)};
    auto created = sim::create_encounter(authored);
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 10})),
        "the side with the objective waits"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 20, {}, 10})
        ),
        "and is cut down"
    );
    expect(
        created.encounter.snapshot().objectives.front().state ==
            sim::ObjectiveState::failed,
        "surviving with nobody left is a failure rather than a pending hope"
    );
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::second_side_won,
        "and the other side has won"
    );
}

void a_survive_count_is_authored_with_its_kind() {
    auto missing = rounds_definition();
    missing.objectives = {survive(sim::Side::first, 0)};
    expect(
        sim::create_encounter(missing).error ==
            sim::CreateError::invalid_objective,
        "a survive objective with no count is refused"
    );
    auto spurious = rounds_definition();
    sim::ObjectiveDefinition counted;
    counted.id = 1;
    counted.kind = sim::ObjectiveKind::defeat_all_opponents;
    counted.round_count = 3;
    spurious.objectives = {counted};
    expect(
        sim::create_encounter(spurious).error ==
            sim::CreateError::invalid_objective,
        "and so is a count on a kind that could never read one"
    );
}

// The whole scenario in miniature: a garrison, and a wave behind it.
sim::EncounterDefinition wave_definition() {
    sim::EncounterDefinition value{
        5,
        3,
        {
            {20, 200, sim::Side::second, {3, 1}, 6, 3, 0, 1},
            {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1},
        }
    };
    sim::UnitDefinition arriving{30, 200, sim::Side::second, {4, 0}, 6, 3, 0, 1};
    arriving.arrival_round = 2;
    value.units.push_back(arriving);
    return value;
}

void a_wave_arrives_on_the_round_it_was_authored_for() {
    auto created = sim::create_encounter(wave_definition());
    expect(static_cast<bool>(created), "a wave is valid content");
    const auto opening = created.encounter.snapshot();
    const sim::UnitSnapshot* waiting = unit(opening, 30);
    expect(
        waiting != nullptr && !waiting->arrived && waiting->arrival_round == 2,
        "a wave is in the battle before it is on the board"
    );
    const auto before = opening.random;
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 10})),
        "the first side takes a turn"
    );
    const auto closing = created.encounter.apply({sim::CommandType::wait, 20});
    expect(static_cast<bool>(closing), "and the second side closes the round");
    const auto after = created.encounter.snapshot();
    expect(after.round == 1, "which opens the second round");
    const sim::UnitSnapshot* arrived = unit(after, 30);
    expect(
        arrived != nullptr && arrived->arrived &&
            arrived->position == sim::Position{4, 0},
        "and the wave stands on the tile the content asked for"
    );
    const auto entered = std::find_if(
        closing.events.begin(),
        closing.events.end(),
        [](const sim::Event& event) {
            return event.type == sim::EventType::unit_arrived;
        }
    );
    expect(
        entered != closing.events.end() && entered->unit_id == 30 &&
            entered->position == sim::Position{4, 0} && entered->amount == 2,
        "an arrival event names who, where, and which round in progress"
    );
    expect(
        after.random.seed == before.seed &&
            after.random.positions == before.positions,
        "and nothing about a wave draws from any stream"
    );
}

void a_wave_arrives_again_and_again() {
    auto authored = wave_definition();
    authored.units.back().arrival_every = 2;
    authored.units.back().arrival_times = 3;
    auto created = sim::create_encounter(authored);
    expect(static_cast<bool>(created), "a recurring wave is valid content");
    const auto opening = created.encounter.snapshot();
    expect(
        opening.units.size() == 5,
        "three arrivals of one authored character are three characters"
    );
    expect(
        unit(opening, 30) != nullptr && unit(opening, 30)->arrival_round == 2,
        "the first arrival keeps the identity the content wrote"
    );
    expect(
        unit(opening, 1) != nullptr && unit(opening, 1)->arrival_round == 4 &&
            unit(opening, 2) != nullptr && unit(opening, 2)->arrival_round == 6,
        "and the later ones take the lowest identifiers the board does not use"
    );
    for (int round = 0; round < 3; ++round) pass(created.encounter, 10, 20);
    const auto after = created.encounter.snapshot();
    expect(after.round == 3, "three rounds have closed");
    expect(
        unit(after, 30)->arrived && unit(after, 1)->arrived &&
            !unit(after, 2)->arrived,
        "so the waves for rounds two and four are in and the one for six is not"
    );
}

// Two waves listed in either order are one battle.
//
// The identifiers the later arrivals take come off a lowest-unused scan, which
// is a running state: handing them out in the order somebody happened to list
// the waves would have given `[A, B]` and `[B, A]` different characters for the
// same content, and therefore different canonical hashes, different derived
// seeds and different dice. They are handed out in identifier order instead, so
// the listing cannot reach the battle.
//
// This is what a board's conformance across the browser and the consoles rests
// on. The editor builds an `EncounterDefinition` in TypeScript and the package
// loader builds one out of a package; neither promises the other an order, and
// with this nothing has to.
void two_waves_listed_either_way_are_one_battle() {
    auto authored = wave_definition();
    authored.units.back().arrival_every = 2;
    authored.units.back().arrival_times = 3;
    sim::UnitDefinition second{40, 200, sim::Side::second, {4, 2}, 6, 3, 0, 1};
    second.arrival_round = 3;
    second.arrival_every = 2;
    second.arrival_times = 2;
    authored.units.push_back(second);

    auto reordered = authored;
    std::reverse(reordered.units.begin(), reordered.units.end());

    auto first = sim::create_encounter(authored);
    auto flipped = sim::create_encounter(reordered);
    expect(static_cast<bool>(first), "two recurring waves are valid content");
    expect(static_cast<bool>(flipped), "listed either way round");
    expect(
        first.encounter.canonical_hash() == flipped.encounter.canonical_hash(),
        "the order two waves are listed in does not name a different battle, "
        "got " + hex(first.encounter.canonical_hash()) + " and " +
            hex(flipped.encounter.canonical_hash())
    );
    expect(
        first.encounter.snapshot().random.seed ==
            flipped.encounter.snapshot().random.seed,
        "so both roll the same numbers"
    );
    // And the identifiers themselves, because equal hashes over two boards that
    // each expanded five characters would also be satisfied by two boards that
    // each got the assignment wrong in the same way.
    const auto opening = first.encounter.snapshot();
    expect(
        opening.units.size() == 7,
        "three arrivals and two arrivals stand beside the two placements"
    );
    expect(
        unit(opening, 1) != nullptr && unit(opening, 1)->arrival_round == 4 &&
            unit(opening, 2) != nullptr &&
            unit(opening, 2)->arrival_round == 6 &&
            unit(opening, 3) != nullptr && unit(opening, 3)->arrival_round == 5,
        "the wave with the lower identifier takes the lower identifiers, "
        "whichever way round the two are written"
    );
}

void a_half_stated_recurrence_is_refused() {
    auto every = wave_definition();
    every.units.back().arrival_every = 2;
    expect(
        sim::create_encounter(every).error == sim::CreateError::invalid_arrival,
        "a gap between arrivals with no number of them is refused"
    );
    auto times = wave_definition();
    times.units.back().arrival_times = 3;
    expect(
        sim::create_encounter(times).error == sim::CreateError::invalid_arrival,
        "and so is a number of arrivals with no gap"
    );
    auto opening = wave_definition();
    opening.units.back().arrival_round = 1;
    expect(
        sim::create_encounter(opening).error ==
            sim::CreateError::invalid_arrival,
        "and so is arriving in the round the battle opens in"
    );
    auto stray = wave_definition();
    stray.units.back().arrival_round = 0;
    stray.units.back().arrival_every = 2;
    stray.units.back().arrival_times = 2;
    expect(
        sim::create_encounter(stray).error == sim::CreateError::invalid_arrival,
        "and so is a recurrence with nowhere to start"
    );
    auto many = wave_definition();
    many.units.back().arrival_every = 1;
    many.units.back().arrival_times =
        static_cast<std::uint16_t>(sim::maximum_arrivals + 1);
    expect(
        sim::create_encounter(many).error == sim::CreateError::invalid_arrival,
        "and so is a wave with no ceiling"
    );
}

void a_wave_still_to_come_keeps_its_side_in_the_battle() {
    sim::EncounterDefinition authored{
        4,
        1,
        {
            {20, 200, sim::Side::second, {1, 0}, 1, 1, 0, 0},
            {10, 100, sim::Side::first, {0, 0}, 9, 9, 0, 0},
        }
    };
    sim::UnitDefinition arriving{30, 200, sim::Side::second, {3, 0}, 6, 3, 0, 0};
    arriving.arrival_round = 2;
    authored.units.push_back(arriving);
    auto created = sim::create_encounter(authored);
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::attack, 10, {}, 20})
        ),
        "the garrison falls to the opening blow"
    );
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::ongoing,
        "and the battle is not over, because a wave is still marching"
    );
    expect(
        created.encounter.apply({sim::CommandType::wait, 20}).error ==
            sim::CommandError::defeated_unit,
        "and the side that lost it cannot spend a turn it has nobody for"
    );
    // Which is why that side's turn was handed straight on: the round closed on
    // the blow that emptied the board, and the wave that round opened came in.
    expect(
        created.encounter.snapshot().active_side == sim::Side::first &&
            created.encounter.snapshot().round == 1,
        "the turn came back round rather than stalling on an empty side"
    );
    expect(
        unit(created.encounter.snapshot(), 30)->arrived,
        "and the second round brought the wave in"
    );
    expect(
        static_cast<bool>(created.encounter.apply({sim::CommandType::wait, 10})),
        "so the battle goes on"
    );
}

void a_wave_lands_beside_a_tile_somebody_took() {
    auto authored = wave_definition();
    authored.units.back().position = {0, 1};
    auto created = sim::create_encounter(authored);
    expect(
        static_cast<bool>(created),
        "a wave may be authored onto a tile somebody already stands on"
    );
    pass(created.encounter, 10, 20);
    const auto after = created.encounter.snapshot();
    const sim::UnitSnapshot* arrived = unit(after, 30);
    expect(arrived != nullptr && arrived->arrived, "and it still comes in");
    expect(
        !(arrived->position == sim::Position{0, 1}),
        "not on the tile somebody is holding"
    );
    expect(
        steps_between({0, 1}, arrived->position) == 1,
        "but on the nearest one it could stand on"
    );
}

void a_wave_still_to_come_threatens_nothing() {
    auto authored = wave_definition();
    // Somewhere it would shade tiles nothing else shades, if it were standing.
    authored.units.back().position = {0, 2};
    auto created = sim::create_encounter(authored);
    const auto pending =
        sim::danger_tiles(created.encounter.snapshot(), sim::Side::second);
    auto standing = authored;
    standing.units.back().arrival_round = 0;
    auto other = sim::create_encounter(standing);
    const auto present =
        sim::danger_tiles(other.encounter.snapshot(), sim::Side::second);
    expect(
        pending.size() < present.size(),
        "a character who stands nowhere shades nothing"
    );
    for (const sim::Position tile : pending) {
        expect(
            std::find(present.begin(), present.end(), tile) != present.end(),
            "and shades nothing the same board without it does not"
        );
    }
}

void every_refusal_an_unarrived_character_can_earn() {
    auto authored = wave_definition();
    authored.units.back().position = {1, 1};
    authored.units.back().talk_record_id = 77;
    auto created = sim::create_encounter(authored);
    const auto snapshot = created.encounter.snapshot();
    expect(
        created.encounter.apply({sim::CommandType::wait, 30}).error ==
            sim::CommandError::unarrived_unit,
        "somebody who has not come in cannot act"
    );
    expect(
        created.encounter.apply({sim::CommandType::attack, 10, {}, 30}).error ==
            sim::CommandError::target_unarrived,
        "nor be struck"
    );
    expect(
        sim::forecast_attack(snapshot, 10, 30).error ==
            sim::CommandError::target_unarrived,
        "and the forecast says what the strike would say"
    );
    expect(
        sim::forecast_talk(snapshot, 10, 30).error ==
            sim::CommandError::target_unarrived,
        "as does the talk forecast"
    );
    const auto reachable = sim::reachable_tiles(snapshot, 10);
    expect(
        std::find(reachable.begin(), reachable.end(), sim::Position{1, 1}) !=
            reachable.end(),
        "and the tile it is authored onto is nobody's until it arrives"
    );
}

// A board where a whole side is still marching at the opening is a board that
// can never be played, and the two turn orders break it in two different ways.
// That is why it is refused once, at creation, rather than half-honoured
// twice.
void a_board_nobody_stands_on_is_refused() {
    auto empty = wave_definition();
    for (sim::UnitDefinition& value : empty.units) value.arrival_round = 2;
    for (const sim::TurnOrder order :
         {sim::TurnOrder::alternating, sim::TurnOrder::initiative,
          sim::TurnOrder::side_blocks}) {
        empty.turn_order = order;
        expect(
            sim::create_encounter(empty).error ==
                sim::CreateError::missing_side,
            "a board on which nobody is standing at the opening is refused"
        );
    }

    // One side is enough to break it: the other holds the turn and can spend it
    // on nobody.
    auto marching = wave_definition();
    for (sim::UnitDefinition& value : marching.units) {
        if (value.side == sim::Side::first) value.arrival_round = 2;
    }
    expect(
        sim::create_encounter(marching).error ==
            sim::CreateError::missing_side,
        "and so is a board where one side is entirely still to come"
    );

    // And membership through a wave is still membership everywhere it always
    // was: a side standing on the board with a wave behind it is valid content,
    // which is the whole scenario waves exist for.
    expect(
        static_cast<bool>(sim::create_encounter(wave_definition())),
        "a side with somebody standing and a wave behind them is untouched"
    );
}

// Nothing off the board is offered a walk. `reachable_tiles`' contract is that
// a tile is in it exactly when the move would not be refused, and every move
// either of these two could make is refused by name.
// The whole query surface, asked about somebody who is not on the board, in one
// place. `on_board` is honoured at a dozen call sites and the set is only as
// good as the weakest of them, so this walks the lot rather than trusting each
// one's own case: a query added later that spells the predicate itself is what
// this is here to catch.
void nothing_answers_for_somebody_who_is_not_on_the_board() {
    auto authored = parley_definition();
    // The captain will be talked off the board; the wave never lands inside
    // this test. Both are on the second side, so one `danger_tiles` call
    // answers for both.
    sim::UnitDefinition marching{
        40, 400, sim::Side::second, {3, 2}, 6, 3, 0, 1
    };
    marching.arrival_round = 6;
    authored.units.push_back(marching);
    auto created = sim::create_encounter(authored);
    expect(static_cast<bool>(created), "the board is valid content");

    // The same board with both of them standing, for the danger overlay to be
    // compared against: what they shade there they must shade nowhere here.
    auto standing = authored;
    standing.units.back().arrival_round = 0;
    auto other = sim::create_encounter(standing);
    expect(static_cast<bool>(other), "and so is the board with both standing");
    const auto present = sim::danger_tiles(
        other.encounter.snapshot(), sim::Side::second,
        other.encounter.weapons(), other.encounter.abilities()
    );

    expect(static_cast<bool>(created.encounter.apply(parley())), "he leaves");
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::wait, 30, {}, 0})
        ),
        "the brute waits"
    );
    const auto snapshot = created.encounter.snapshot();
    const sim::UnitId absent[] = {20U, 40U};
    for (const sim::UnitId who : absent) {
        const sim::UnitSnapshot* value = unit(snapshot, who);
        expect(value != nullptr, "the character is still part of the battle");
        if (value == nullptr) continue;
        expect(
            value->health > 0 && (value->departed || !value->arrived),
            "alive, and off the board"
        );
        expect(
            !sim::is_deployable(snapshot, *value),
            "nobody off the board is arrangeable"
        );
        expect(
            sim::reachable_tiles(snapshot, who).empty(),
            "nobody off the board reaches a tile"
        );
        expect(
            sim::deployable_tiles(snapshot, who).empty(),
            "nobody off the board is offered a deployment tile"
        );
    }
    // The tile the captain walked off is nobody's, so the duellist beside him
    // may end a walk on it: occupancy is `on_board` too.
    const auto reachable = sim::reachable_tiles(snapshot, 10);
    expect(
        std::find(reachable.begin(), reachable.end(),
                  unit(snapshot, 20)->position) != reachable.end(),
        "and the tile a departed character is recorded on is free"
    );

    const auto shaded = sim::danger_tiles(
        snapshot, sim::Side::second, created.encounter.weapons(),
        created.encounter.abilities()
    );
    expect(
        shaded.size() < present.size(),
        "and neither of them shades a tile in the danger overlay"
    );
    for (const sim::Position tile : shaded) {
        expect(
            std::find(present.begin(), present.end(), tile) != present.end(),
            "nor any tile the same board with both standing does not shade"
        );
    }
}

void nobody_off_the_board_is_offered_a_walk() {
    auto created = sim::create_encounter(wave_definition());
    expect(
        sim::reachable_tiles(created.encounter.snapshot(), 30).empty(),
        "a character still marching reaches nothing"
    );
    expect(
        created.encounter.apply({sim::CommandType::move, 30, {4, 1}, 0})
                .error == sim::CommandError::unarrived_unit,
        "which is what a move naming a tile beside it earns"
    );

    auto talked = sim::create_encounter(parley_definition());
    expect(static_cast<bool>(talked.encounter.apply(parley())), "he leaves");
    expect(
        sim::reachable_tiles(talked.encounter.snapshot(), 20).empty(),
        "and a character talked off the board reaches nothing either"
    );
    expect(
        talked.encounter.apply({sim::CommandType::move, 20, {1, 0}, 0}).error ==
            sim::CommandError::departed_unit,
        "which is what a move naming a tile beside him earns"
    );
}

void a_wave_is_never_deployable_and_never_lands_in_the_phase() {
    auto authored = deployment_definition();
    sim::UnitDefinition arriving{30, 200, sim::Side::second, {0, 2}, 6, 3, 0, 1};
    arriving.arrival_round = 2;
    authored.units.push_back(arriving);
    // A *first-side* wave, authored inside the region, is the case this test is
    // named for. The second-side one above is refused by the side check long
    // before arrival is consulted, so on its own it proves nothing about a
    // wave: this one is the only unit here that reaches the arrival question.
    sim::UnitDefinition marching{40, 100, sim::Side::first, {0, 0}, 8, 4, 0, 1};
    marching.arrival_round = 2;
    authored.units.push_back(marching);
    auto created = sim::create_encounter(authored);
    expect(static_cast<bool>(created), "a wave and a region coexist");
    const auto opening = created.encounter.snapshot();
    expect(opening.deploying, "the phase opens as it always did");
    expect(
        !sim::is_deployable(opening, *unit(opening, 30)),
        "and somebody who has not come in is not arrangeable"
    );
    expect(
        !sim::is_deployable(opening, *unit(opening, 40)),
        "not even on the side that arranges, and inside the region"
    );
    expect(
        sim::deployable_tiles(opening, 40).empty(),
        "so no tile is lit for it"
    );
    expect(
        created.encounter.apply({sim::CommandType::deploy, 40, {1, 0}, 0})
                .error == sim::CommandError::unarrived_unit,
        "and a deploy naming it is refused by name rather than accepted"
    );
    // The consequence that refusal prevents, asserted rather than argued. The
    // wave holds no tile (occupancy is `on_board`), so accepting the deploy
    // above would have moved it to (1,0) and left (1,0) still offered to
    // everybody else, putting two characters on one square.
    expect(
        unit(created.encounter.snapshot(), 40)->position == sim::Position{0, 0},
        "its authored landing tile is untouched"
    );
    const auto tiles = sim::deployable_tiles(opening, 10);
    expect(
        std::find(tiles.begin(), tiles.end(), sim::Position{0, 2}) !=
            tiles.end(),
        "and holds no tile against the arrangement"
    );
    expect(
        static_cast<bool>(
            created.encounter.apply({sim::CommandType::deploy, 10, {0, 2}, 0})
        ),
        "so the player may stand somebody on the tile the wave was authored on"
    );
    const auto begun =
        created.encounter.apply({sim::CommandType::begin_battle, 0, {}, 0});
    expect(static_cast<bool>(begun), "the phase closes");
    const bool arrived_early = std::any_of(
        begun.events.begin(),
        begun.events.end(),
        [](const sim::Event& event) {
            return event.type == sim::EventType::unit_arrived;
        }
    );
    expect(
        !arrived_early,
        "and no wave lands on the way out of it, because the earliest is the "
        "second round"
    );
    pass(created.encounter, 11, 20);
    const auto after = created.encounter.snapshot();
    const sim::UnitSnapshot* wave = unit(after, 30);
    expect(wave != nullptr && wave->arrived, "the second round brings it in");
    expect(
        !(wave->position == sim::Position{0, 2}),
        "beside the tile the player took rather than on top of them"
    );
}


// A second conformance vector, for the two things the first cannot reach: a
// battle that counts rounds and a battle a wave arrives in. Replayed through
// the WebAssembly build by `editor/src/domain/encounter-simulation.test.ts`,
// which must arrive at the same two hashes: one set of rules rather than two
// agreeing sets.
sim::EncounterDefinition surviving_vector() {
    sim::EncounterDefinition value{
        5,
        3,
        {
            {20, 200, sim::Side::second, {3, 1}, 6, 3, 0, 1},
            {10, 100, sim::Side::first, {0, 1}, 6, 4, 0, 1},
        }
    };
    sim::UnitDefinition arriving{30, 200, sim::Side::second, {4, 0}, 6, 3, 0, 1};
    arriving.arrival_round = 2;
    arriving.arrival_every = 2;
    arriving.arrival_times = 2;
    value.units.push_back(arriving);
    value.objectives = {survive(sim::Side::first, 3)};
    return value;
}

void the_surviving_vector_hashes_the_same_on_every_build() {
    auto created = sim::create_encounter(surviving_vector());
    expect(static_cast<bool>(created), "the surviving vector is valid content");
    expect(
        created.encounter.canonical_hash() == 0x31f90d9772d39bedULL,
        "the opening state matches the browser conformance hash, got " +
            hex(created.encounter.canonical_hash())
    );
    for (int round = 0; round < 3; ++round) pass(created.encounter, 10, 20);
    expect(
        created.encounter.snapshot().outcome == sim::Outcome::first_side_won &&
            created.encounter.snapshot().round == 3,
        "three rounds are outlasted and the battle is won"
    );
    expect(
        created.encounter.canonical_hash() == 0x03377a446b1b5ac3ULL,
        "the completed state matches the browser conformance hash, got " +
            hex(created.encounter.canonical_hash())
    );
}

// A board built to make every aim answerable and every aim different: an archer
// carrying two bands, knowing two shapes, standing among opponents at one, two
// and three tiles, with one talkative ally beside her.
//
//     y=0  .  .  .  .  .  .  .
//     y=1  .  .  .  A  .  .  .      A  ally, talkative, side one
//     y=2  .  .  .  H  E  F  G      H  the archer, side one
//     y=3  .  .  .  .  .  .  .      E F G  opponents at 1, 2 and 3
//     y=4  .  .  .  .  .  .  .
const sim::ContentId sword_id = 0x5701;
const sim::ContentId bow_id = 0x5702;
const sim::ContentId spark_id = 0xAB01;
const sim::ContentId blast_id = 0xAB02;
const sim::ContentId unknown_id = 0xDEAD;

sim::EncounterDefinition aim_definition() {
    sim::EncounterDefinition definition;
    definition.width = 7;
    definition.height = 5;
    definition.turn_order = sim::TurnOrder::side_blocks;
    definition.weapons = {
        {sword_id, 3, 1, 1, 100},
        {bow_id, 2, 2, 3, 100},
    };
    definition.abilities = {
        // A single-tile bolt with a band that reaches past the archer's bow,
        // and a cross-shaped blast that has to be aimed nearer.
        {spark_id, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::single, 4, 1, 4, 0, 100},
        {blast_id, sim::AbilityKind::damage, sim::DamageType::magical,
         sim::AreaShape::cross, 3, 1, 2, 0, 100},
    };

    sim::UnitDefinition archer;
    archer.id = 1;
    archer.unit_type_id = 100;
    archer.side = sim::Side::first;
    archer.position = {3, 2};
    archer.health = 20;
    archer.strength = 4;
    archer.movement = 2;
    archer.action_points = 2;
    archer.weapon_ids = {sword_id, bow_id};
    archer.ability_ids = {spark_id, blast_id};

    sim::UnitDefinition ally;
    ally.id = 2;
    ally.unit_type_id = 101;
    ally.side = sim::Side::first;
    ally.position = {3, 1};
    ally.health = 20;
    ally.strength = 3;
    ally.movement = 1;
    ally.action_points = 1;
    ally.talk_record_id = 0x7001;

    definition.units = {archer, ally};
    for (int index = 0; index < 3; ++index) {
        sim::UnitDefinition opponent;
        opponent.id = static_cast<sim::UnitId>(10 + index);
        opponent.unit_type_id = 200;
        opponent.side = sim::Side::second;
        opponent.position = {static_cast<std::int16_t>(4 + index), 2};
        opponent.health = 30;
        opponent.strength = 3;
        opponent.movement = 1;
        opponent.action_points = 1;
        definition.units.push_back(opponent);
    }
    return definition;
}

// Whoever is standing on a tile in a snapshot, or zero. The clients ask the
// board exactly this before committing a strike or a talk, so the agreement
// walk below asks it the same way.
sim::UnitId occupant_at(
    const sim::EncounterSnapshot& snapshot, sim::Position tile
) {
    for (const sim::UnitSnapshot& unit : snapshot.units) {
        if (unit.health > 0 && !unit.departed && unit.arrived &&
            unit.position == tile) {
            return unit.id;
        }
    }
    return 0;
}

bool lists(const std::vector<sim::Position>& tiles, sim::Position tile) {
    return std::find(tiles.begin(), tiles.end(), tile) != tiles.end();
}

// The whole contract, walked tile by tile for all four gestures: a tile is
// lit exactly when committing that gesture there is accepted. Every expectation
// comes from `apply` on a fresh board rather than from anybody's belief about
// the band.
void aimable_tiles_agree_with_apply() {
    auto created = sim::create_encounter(aim_definition());
    const auto snapshot = created.encounter.snapshot();
    const auto& weapons = created.encounter.weapons();
    const auto& abilities = created.encounter.abilities();

    struct Case final {
        const char* name;
        sim::AimedGesture gesture;
        sim::CommandType type;
    };
    const Case cases[] = {
        {"walk", {sim::Gesture::walk, 0, 0}, sim::CommandType::move},
        {"a strike with the weapon in hand",
         {sim::Gesture::strike, 0, 0},
         sim::CommandType::attack},
        {"a strike with the drawn bow",
         {sim::Gesture::strike, bow_id, 0},
         sim::CommandType::attack},
        {"a spark", {sim::Gesture::cast, 0, spark_id},
         sim::CommandType::ability},
        {"a blast", {sim::Gesture::cast, 0, blast_id},
         sim::CommandType::ability},
        {"a talk", {sim::Gesture::talk, 0, 0}, sim::CommandType::talk},
    };

    for (const Case& probe : cases) {
        const auto tiles = sim::aimable_tiles(
            snapshot, 1, probe.gesture, weapons, abilities
        );
        for (std::int16_t y = 0; y < 5; ++y) {
            for (std::int16_t x = 0; x < 7; ++x) {
                const sim::Position tile{x, y};
                auto fresh = sim::create_encounter(aim_definition());
                sim::Command command;
                command.type = probe.type;
                command.unit_id = 1;
                command.destination = tile;
                command.target_id = occupant_at(snapshot, tile);
                command.weapon_id = probe.gesture.weapon_id;
                command.ability_id = probe.gesture.ability_id;
                const auto result = fresh.encounter.apply(command);
                expect(
                    static_cast<bool>(result) == lists(tiles, tile),
                    std::string("apply accepts ") + probe.name +
                        " exactly where aimable_tiles lists it"
                );
            }
        }
        // And that the walk really did prove something: an aim nobody can land
        // would agree with an empty list for the wrong reason.
        expect(
            !tiles.empty(),
            std::string("the archer can aim ") + probe.name + " somewhere"
        );
    }
}

// The case a player meets on a console before touching anything, stated as a
// test: the board says "nobody" before the cursor is moved at all.
void a_character_with_nobody_in_reach_lights_nothing() {
    auto definition = aim_definition();
    // Push all three opponents out past the longest band the archer has.
    for (std::size_t index = 2; index < definition.units.size(); ++index) {
        definition.units[index].position.x = 6;
        definition.units[index].position.y =
            static_cast<std::int16_t>(index - 2);
    }
    definition.units[0].position = {0, 4};
    const auto snapshot = sim::create_encounter(definition).encounter.snapshot();
    auto created = sim::create_encounter(definition);
    const auto& weapons = created.encounter.weapons();
    const auto& abilities = created.encounter.abilities();

    expect(
        sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::strike, 0, 0}, weapons, abilities
        ).empty(),
        "a strike with nobody in the band lights no tile at all"
    );
    expect(
        sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::strike, bow_id, 0}, weapons, abilities
        ).empty(),
        "and neither does the longer bow"
    );
    expect(
        sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::talk, 0, 0}, weapons, abilities
        ).empty(),
        "nor a talk with nobody standing next to her"
    );
    // A cast still lights its band, and that is the point rather than an
    // inconsistency: a cast lands on ground, so the ground is aimable whether
    // or not anybody is standing on it.
    expect(
        !sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::cast, 0, spark_id}, weapons, abilities
        ).empty(),
        "a cast still lights the ground it may be aimed at"
    );
}

// Everything that refuses the gesture before it has been aimed anywhere.
void aimable_tiles_refuse_what_the_character_cannot_do() {
    const sim::AimedGesture strike{sim::Gesture::strike, 0, 0};
    const sim::AimedGesture cast{sim::Gesture::cast, 0, spark_id};
    auto created = sim::create_encounter(aim_definition());
    const auto& weapons = created.encounter.weapons();
    const auto& abilities = created.encounter.abilities();
    const auto snapshot = created.encounter.snapshot();

    expect(
        sim::aimable_tiles(snapshot, 77, strike, weapons, abilities).empty(),
        "an unknown character aims nothing"
    );
    expect(
        sim::aimable_tiles(snapshot, 10, strike, weapons, abilities).empty(),
        "and neither does one on the side that is not acting"
    );
    expect(
        sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::strike, unknown_id, 0}, weapons,
            abilities
        ).empty(),
        "a weapon the registry cannot resolve lights nothing"
    );
    expect(
        sim::aimable_tiles(
            snapshot, 1, {sim::Gesture::cast, 0, unknown_id}, weapons, abilities
        ).empty(),
        "and neither does an ability it cannot resolve"
    );
    expect(
        sim::aimable_tiles(snapshot, 2, cast, weapons, abilities).empty(),
        "an ability this character does not know lights nothing"
    );

    // A character who has spent its turn, asked after it has spent it.
    auto spent = sim::create_encounter(aim_definition());
    expect(
        static_cast<bool>(spent.encounter.apply({sim::CommandType::wait, 1})),
        "the archer may finish her turn"
    );
    expect(
        sim::aimable_tiles(
            spent.encounter.snapshot(), 1, strike, spent.encounter.weapons(),
            spent.encounter.abilities()
        ).empty(),
        "a character whose turn is over aims nothing"
    );

    // And a character on a board that is being arranged, where every one of
    // these commands is refused for its phase.
    auto arranging = aim_definition();
    arranging.deployment_tiles = {{3, 2}, {2, 2}};
    auto opening = sim::create_encounter(arranging);
    expect(
        opening.encounter.snapshot().deploying,
        "the arranged board opens in its phase"
    );
    expect(
        sim::aimable_tiles(
            opening.encounter.snapshot(), 1, strike, opening.encounter.weapons(),
            opening.encounter.abilities()
        ).empty(),
        "nothing is aimable while the board is being arranged"
    );
}

// The line between a gesture and its aim, which is what decides whether a menu
// offers a row.
void a_gesture_is_available_apart_from_what_it_can_reach() {
    const sim::AimedGesture walk{sim::Gesture::walk, 0, 0};
    const sim::AimedGesture strike{sim::Gesture::strike, 0, 0};
    auto created = sim::create_encounter(aim_definition());
    const auto& weapons = created.encounter.weapons();
    const auto& abilities = created.encounter.abilities();

    expect(
        sim::gesture_available(
            created.encounter.snapshot(), 1, walk, weapons, abilities
        ),
        "a character who has not walked may walk"
    );
    sim::Command step;
    step.type = sim::CommandType::move;
    step.unit_id = 1;
    step.destination = {2, 2};
    expect(static_cast<bool>(created.encounter.apply(step)), "the archer walks");
    expect(
        !sim::gesture_available(
            created.encounter.snapshot(), 1, walk, weapons, abilities
        ),
        "and may not walk twice"
    );
    expect(
        sim::gesture_available(
            created.encounter.snapshot(), 1, strike, weapons, abilities
        ),
        "but may still strike, because a walk is not a strike"
    );

    // The boundary the whole query exists to draw: a character with nobody in
    // reach may still make the gesture. The row stays; the board says nobody.
    auto lonely = aim_definition();
    for (std::size_t index = 2; index < lonely.units.size(); ++index) {
        lonely.units[index].position.x = 6;
        lonely.units[index].position.y =
            static_cast<std::int16_t>(index - 2);
    }
    lonely.units[0].position = {0, 4};
    auto alone = sim::create_encounter(lonely);
    expect(
        sim::gesture_available(
            alone.encounter.snapshot(), 1, strike, alone.encounter.weapons(),
            alone.encounter.abilities()
        ),
        "a strike with nobody in reach is still a gesture this character has"
    );
    expect(
        sim::aimable_tiles(
            alone.encounter.snapshot(), 1, strike, alone.encounter.weapons(),
            alone.encounter.abilities()
        ).empty(),
        "and the tiles are what say there is nobody to aim it at"
    );

    // A gesture nobody can make lights nothing, in every direction.
    expect(
        !sim::gesture_available(
            created.encounter.snapshot(), 1,
            {sim::Gesture::strike, unknown_id, 0}, weapons, abilities
        ),
        "a weapon this character is not carrying is not a gesture it has"
    );
    expect(
        !sim::gesture_available(
            created.encounter.snapshot(), 2, {sim::Gesture::cast, 0, spark_id},
            weapons, abilities
        ),
        "and neither is an ability it does not know"
    );
}

// The splash, checked against the units a cast actually catches rather than
// against the shape anybody believed it had.
void area_tiles_cover_what_a_cast_catches() {
    auto created = sim::create_encounter(aim_definition());
    const auto& abilities = created.encounter.abilities();
    const auto snapshot = created.encounter.snapshot();

    expect(
        sim::area_tiles(snapshot, spark_id, {3, 2}, abilities).empty(),
        "a single-tile ability has no splash to draw"
    );
    expect(
        sim::area_tiles(snapshot, unknown_id, {3, 2}, abilities).empty(),
        "and neither has one the registry cannot resolve"
    );

    const auto splash = sim::area_tiles(snapshot, blast_id, {4, 2}, abilities);
    const std::vector<sim::Position> expected = {
        {4, 1}, {3, 2}, {4, 2}, {5, 2}, {4, 3},
    };
    expect(splash == expected, "a cross splashes five tiles, row-major");
    expect(
        sim::area_tiles(snapshot, blast_id, {0, 0}, abilities).size() == 3,
        "and is clipped to the board at a corner"
    );

    // What the splash promises and what the cast delivers, held apart on
    // purpose. The splash is the *cover*: it names a tile, and it names the
    // archer's own tile at (3,2) as readily as the opponent's. A damaging cast
    // takes health off the caster's opponents and nobody else.
    // A unit is struck exactly when it is an opponent standing under the cover,
    // which is one sentence with both halves in it: a covered ally proves the
    // sparing, and an uncovered opponent proves the shape still bounds it.
    auto cast = sim::create_encounter(aim_definition());
    sim::Command command;
    command.type = sim::CommandType::ability;
    command.unit_id = 1;
    command.destination = {4, 2};
    command.ability_id = blast_id;
    const auto result = cast.encounter.apply(command);
    expect(static_cast<bool>(result), "the blast is accepted");
    const sim::UnitSnapshot* caster = unit(snapshot, 1);
    expect(
        caster != nullptr && lists(splash, caster->position),
        "the caster's own tile is under its own cross"
    );
    for (const sim::UnitSnapshot& other : snapshot.units) {
        bool struck = false;
        for (const sim::Event& event : result.events) {
            if (event.unit_id == other.id &&
                (event.type == sim::EventType::unit_damaged ||
                 event.type == sim::EventType::unit_defeated ||
                 event.type == sim::EventType::attack_missed)) {
                struck = true;
            }
        }
        const bool opposed = caster != nullptr && other.side != caster->side;
        expect(
            struck == (opposed && lists(splash, other.position)),
            "the cast reaches exactly the opponents the splash covers"
        );
    }
}

}  // namespace

int main() {
    validates_definition_atomically();
    a_stat_that_would_wrap_the_damage_is_refused();
    moves_and_alternates_sides();
    rejected_commands_are_atomic();
    resolves_combat_and_objective();
    enforces_attack_rules_and_minimum_damage();
    forecasts_a_chance_and_the_numbers_behind_it();
    attacks_land_or_miss_by_the_stated_chance();
    the_hit_chance_folds_both_units_into_one_number();
    the_folded_chance_is_the_one_apply_rolls_against();
    a_counter_folds_the_defender_the_other_way_round();
    a_magical_cast_is_priced_by_the_caster_and_a_physical_one_is_not();
    the_hit_stream_is_consumed_in_one_fixed_order();
    a_sub_certain_lethal_strike_is_still_answered();
    an_ability_rolls_once_per_unit_it_damages();
    weapon_power_joins_the_attack_formula();
    the_better_weapon_is_worth_the_advantage();
    a_malformed_weapon_triangle_is_refused();
    caps_board_area();
    has_acted_is_false_under_alternating_order();
    has_acted_tracks_rounds_under_ordered_turns();
    initiative_order_runs_fastest_first();
    initiative_breaks_speed_ties_by_identifier_across_sides();
    side_blocks_run_one_side_at_a_time();
    turns_interleave_freely_inside_a_block();
    one_point_finishes_a_character_on_its_walk();
    a_character_that_acts_after_attacking_spends_its_own_points();
    one_walk_per_activation();
    spends_action_points_within_one_activation();
    striking_ends_an_activation_by_default();
    restore_skips_full_health_targets();
    restore_heals_the_wounded_up_to_maximum();
    restore_heals_enemies_in_the_area();
    a_battle_is_not_won_while_an_opponent_stands();
    a_blast_spares_the_caster_and_its_own_side();
    a_wiped_out_side_fails_its_own_objective();
    simultaneous_decisive_objectives_follow_identifier_order();
    forecast_refuses_activation_states();
    reachable_tiles_respect_range_blockers_and_occupancy();
    an_opponent_blocks_the_way_an_ally_only_stands_in_it();
    reachable_tiles_agree_with_apply();
    reachable_tiles_exclude_unknown_defeated_and_immobile();
    danger_tiles_reach_through_the_threatening_side_own_line();
    danger_tiles_union_movement_and_weapon_band();
    a_reach_bonus_widens_every_band_a_character_strikes_with();
    a_reach_bonus_lets_a_character_answer_from_further();
    a_reach_bonus_saturates_rather_than_wrapping();
    terrain_stops_a_walker_and_not_a_flier();
    terrain_is_validated_and_canonical();
    terrain_narrows_the_danger_zone();
    ground_can_be_slow_without_being_shut();
    a_board_with_no_price_plays_as_it_always_did();
    price_narrows_the_danger_zone();
    a_flier_pays_one_everywhere();
    the_danger_zone_is_budgeted_by_action_points();
    a_spent_unit_leaves_the_danger_zone();
    carried_weapons_resolve_the_weapon_in_hand();
    an_attack_naming_a_weapon_uses_that_weapon();
    naming_a_weapon_the_unit_cannot_use_is_refused();
    a_malformed_weapon_registry_is_refused();
    the_carried_list_is_canonical_state();
    danger_tiles_count_every_band_a_unit_can_use();
    a_surviving_defender_in_band_strikes_back();
    a_felled_defender_does_not_answer();
    an_archer_struck_from_inside_its_minimum_cannot_answer();
    a_defender_outranged_cannot_answer();
    a_counter_can_kill_and_can_decide_the_encounter();
    an_ability_provokes_no_counter();
    a_counter_uses_the_weapon_in_hand();
    an_unstated_count_is_one_of_each();
    a_malformed_satchel_is_refused();
    using_an_item_restores_and_spends_it();
    a_use_is_priced_like_a_cast();
    a_use_draws_nothing_from_any_stream();
    every_refusal_a_use_can_earn();
    the_item_forecast_is_the_number_apply_delivers();
    a_full_health_use_forecasts_and_delivers_nothing();
    a_forecast_refuses_what_apply_would_refuse();
    the_pack_is_canonical_state();
    a_defeated_unit_leaves_what_its_type_authors();
    the_drop_stream_is_consumed_in_one_fixed_order();
    a_drop_cannot_move_the_hit_stream();
    a_half_authored_drop_is_refused();
    what_fell_is_canonical_state();
    a_parley_nobody_authors_costs_nothing();
    talking_takes_the_character_off_the_board();
    a_departure_is_not_a_defeat();
    a_talk_draws_nothing_from_any_stream();
    a_talk_is_priced_like_a_cast();
    every_refusal_a_talk_can_earn();
    a_talk_cannot_be_aimed_at_the_talker();
    a_departed_character_holds_no_ground();
    a_departed_character_is_never_chosen_to_act();
    the_last_opponent_walking_away_ends_the_battle();
    objectives_answer_a_departure_deliberately();
    a_departed_character_cannot_be_struck();
    a_departed_character_cannot_act();
    a_departed_or_unarrived_character_forecasts_what_apply_refuses();
    the_talk_forecast_is_the_promise();
    a_talk_forecast_refuses_what_apply_would_refuse();
    a_talk_is_canonical_state();
    hashes_are_canonical();
    matches_browser_conformance_vector();
    a_board_with_no_region_never_enters_the_phase();
    a_region_opens_the_phase_and_names_who_may_move();
    deploying_is_free_and_repeatable();
    every_refusal_a_deploy_can_earn();
    the_phase_ends_only_when_it_is_ended();
    deployable_tiles_agree_with_apply();
    the_other_queries_state_the_phase();
    an_arrangement_replays_to_the_same_board();
    a_region_nobody_used_costs_nothing();
    a_malformed_region_is_refused();
    a_board_that_counts_no_rounds_costs_nothing();
    a_round_is_one_pass_through_the_turn_order();
    surviving_is_won_as_the_round_closes();
    surviving_with_nobody_left_is_failing();
    a_survive_count_is_authored_with_its_kind();
    a_wave_arrives_on_the_round_it_was_authored_for();
    a_wave_arrives_again_and_again();
    two_waves_listed_either_way_are_one_battle();
    a_half_stated_recurrence_is_refused();
    a_wave_still_to_come_keeps_its_side_in_the_battle();
    a_wave_lands_beside_a_tile_somebody_took();
    a_wave_still_to_come_threatens_nothing();
    a_board_nobody_stands_on_is_refused();
    nobody_off_the_board_is_offered_a_walk();
    nothing_answers_for_somebody_who_is_not_on_the_board();
    every_refusal_an_unarrived_character_can_earn();
    a_wave_is_never_deployable_and_never_lands_in_the_phase();
    the_surviving_vector_hashes_the_same_on_every_build();
    a_character_who_endures_is_left_standing();
    a_blow_the_floor_did_not_catch_says_nothing();
    the_forecast_computes_the_floor_rather_than_clamping_after_it();
    enduring_is_canonical_only_where_it_is_authored();
    aimable_tiles_agree_with_apply();
    a_character_with_nobody_in_reach_lights_nothing();
    aimable_tiles_refuse_what_the_character_cannot_do();
    a_gesture_is_available_apart_from_what_it_can_reach();
    area_tiles_cover_what_a_cast_catches();
    return failures == 0 ? 0 : 1;
}
