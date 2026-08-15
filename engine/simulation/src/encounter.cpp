// SPDX-License-Identifier: MIT
#include <grandleon/simulation/encounter.hpp>

#include <algorithm>
#include <deque>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace grandleon::simulation {
namespace {

Side other_side(Side side) noexcept {
    return side == Side::first ? Side::second : Side::first;
}

bool valid_side(Side side) noexcept {
    return side == Side::first || side == Side::second;
}

bool in_bounds(
    Position position,
    std::uint16_t width,
    std::uint16_t height
) noexcept {
    return position.x >= 0 && position.y >= 0 &&
           static_cast<std::uint16_t>(position.x) < width &&
           static_cast<std::uint16_t>(position.y) < height;
}

std::uint32_t distance(Position lhs, Position rhs) noexcept {
    const auto dx = static_cast<std::int32_t>(lhs.x) - rhs.x;
    const auto dy = static_cast<std::int32_t>(lhs.y) - rhs.y;
    return static_cast<std::uint32_t>(
        (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy)
    );
}

template <typename Units>
auto* find_unit(Units& units, UnitId id) {
    const auto found = std::lower_bound(
        units.begin(),
        units.end(),
        id,
        [](const auto& unit, UnitId expected) { return unit.id < expected; }
    );
    return found == units.end() || found->id != id ? nullptr : &*found;
}

// `in_the_battle` and `on_board` are declared in the header, beside the struct
// they read and beside `floor_of`, so that `engine/tactics` asks this engine
// who is on the board rather than keeping a copy of the answer.

// Whether an authored number may stand in the damage arithmetic. One function
// for every stat that can, so the two ends of a bound written out eight times
// cannot come to be written eight different ways. See `maximum_stat`.
bool bounded_stat(std::int16_t value) noexcept {
    return value >= 0 && value <= maximum_stat;
}

// What one turn of this character costs it, in points.
//
// Its authored budget, except that a character authored none still gets one
// command before its turn closes. That is not a nicety: the side-wide counter
// `alternating` and `initiative` keep reaches zero after one command whatever
// the budget says, so a character with no points has exactly one command and
// then finishes. Reading the zero literally would give it a turn it can never
// close, and a block that never closes is a board that never ends.
std::uint8_t turn_budget(const UnitSnapshot& unit) noexcept {
    return unit.action_points == 0U ? 1U : unit.action_points;
}

// What this character has left of its own turn, under any turn order.
//
// Three clauses, in the order they can apply: the side-wide count while this
// character is the one holding an activation, none once it has finished, and
// its own budget less what its own turn has spent. Under `alternating` and
// `initiative` the first clause is the answer for the acting character and the
// third gives everybody else their full budget, which is right because any one
// of them may be the next the side picks. Under `side_blocks` nobody ever holds
// an activation, so the third clause is the whole of it, and it has to be:
// several characters can each be part-way through a turn at once and one
// side-wide number cannot describe them.
std::uint8_t points_left(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& unit
) noexcept {
    if (snapshot.active_unit_id == unit.id) {
        return snapshot.remaining_action_points;
    }
    if (unit.has_acted) return 0U;
    const std::uint8_t budget = turn_budget(unit);
    if (unit.spent_action_points >= budget) return 0U;
    return static_cast<std::uint8_t>(budget - unit.spent_action_points);
}

bool occupied(
    const std::vector<UnitSnapshot>& units,
    Position position
) noexcept {
    return std::any_of(
        units.begin(),
        units.end(),
        [position](const UnitSnapshot& unit) {
            return on_board(unit) && unit.position == position;
        }
    );
}

// Whether somebody standing on `position` stops a character of `mover`'s side
// from walking across it. An opponent does; an ally does not.
//
// The other half of the rule is not here, because it is not about crossing: a
// walk may not *finish* on anybody either way. `movement_field` states both
// together, which is what keeps them one rule.
bool blocks_passage(
    const std::vector<UnitSnapshot>& units,
    Position position,
    Side mover
) noexcept {
    return std::any_of(
        units.begin(),
        units.end(),
        [position, mover](const UnitSnapshot& unit) {
            return on_board(unit) && unit.position == position &&
                   unit.side != mover;
        }
    );
}

// The same question asked by somebody who is allowed to be standing there:
// whether anybody *else* holds the tile. Deployment asks it this way so that
// putting a character down where it already stands is accepted and idle rather
// than refused. Refusing it would be a second rule about what a good
// arrangement is, in the shape §"Its forecast is exact" already rejected for a
// full-health character drinking a tonic.
bool occupied_by_other(
    const std::vector<UnitSnapshot>& units,
    Position position,
    UnitId self
) noexcept {
    return std::any_of(
        units.begin(),
        units.end(),
        [position, self](const UnitSnapshot& unit) {
            return unit.id != self && on_board(unit) &&
                   unit.position == position;
        }
    );
}

// Whether `position` is one of the encounter's deployment tiles. The list is
// kept sorted row-major, so this is a binary search rather than a scan and the
// answer cannot depend on authored order.
bool in_deployment_zone(
    const std::vector<Position>& zone,
    Position position
) noexcept {
    return std::binary_search(
        zone.begin(),
        zone.end(),
        position,
        [](Position lhs, Position rhs) {
            return lhs.y != rhs.y ? lhs.y < rhs.y : lhs.x < rhs.x;
        }
    );
}

// Whether a side still has anybody in the battle: the predicate behind
// `defeat_all_opponents`, behind the elimination backstop, and behind the rule
// an encounter with no authored objectives plays by.
//
// It asks `in_the_battle` rather than `on_board`, and that is the deliberate
// answer to what a wave does to "kill them all": **a side with a wave still to
// come is not beaten.** The opposite reading would make the scenario waves
// exist for unauthorable. A survive-seven map with arrivals on rounds three and
// six would be over on round one, and the six rounds the author wrote would
// never be played. It is also the honest reading of the sentence: "this side has
// nobody left" is false while more are marching. On a board with no arrivals the
// two predicates coincide, so nothing shipped changed answer.
bool has_living_unit(
    const std::vector<UnitSnapshot>& units,
    Side side
) noexcept {
    return std::any_of(
        units.begin(),
        units.end(),
        [side](const UnitSnapshot& unit) {
            return unit.side == side && in_the_battle(unit);
        }
    );
}

// Whether a side has anybody actually standing on the board. Once a wave can
// still be marching, that is a different question from whether it still has
// anybody at all. Only the turn order asks it: a side whose turn it is and who
// has nobody to spend it on hands the turn straight on, rather than leaving a
// client holding a turn it cannot take.
bool has_standing_unit(
    const std::vector<UnitSnapshot>& units,
    Side side
) noexcept {
    return std::any_of(
        units.begin(),
        units.end(),
        [side](const UnitSnapshot& unit) {
            return unit.side == side && on_board(unit);
        }
    );
}

bool within_reach(
    std::uint32_t separation,
    std::uint8_t minimum,
    std::uint8_t maximum
) noexcept {
    return separation >= minimum && separation <= maximum;
}

// A band's ceiling once the striker's own reach bonus is on it, saturating
// rather than wrapping. A bonus that wrapped would hand a player an archer who
// cannot reach the tile in front of her for having been written to shoot
// further, and 255 is already further than any board is wide.
//
// The one place the bonus is applied, called from each of the three sites that
// resolve a band, so that the weapon in hand, a weapon an attack names and the
// danger overlay cannot come to different answers about the same archer.
[[nodiscard]] std::uint8_t widened_reach(
    std::uint8_t maximum,
    std::uint8_t bonus
) noexcept {
    const auto total =
        static_cast<std::uint32_t>(maximum) + static_cast<std::uint32_t>(bonus);
    return static_cast<std::uint8_t>(total > 255U ? 255U : total);
}

// What one weapon strikes with: the power added to the attacker's strength and
// the band the separation has to fall inside. Resolving a command's weapon into
// this keeps every rule below written against one shape, whether the attack
// named a weapon or used the one in hand.
struct StrikeProfile final {
    std::int16_t power{};
    std::uint8_t minimum_reach{1};
    std::uint8_t maximum_reach{1};
    // How often this strike lands, as a percentage. One hundred is certain and
    // consumes no number; see `roll_hit`.
    std::uint8_t accuracy{100};
};

// The bound every hit chance is rolled against. Chances are authored as whole
// percentages and rolled as whole percentages: one number, one meaning, and
// the number a player is shown is the number this divides by.
constexpr std::uint32_t hit_chance_bound = 100U;

// Whether a strike lands, and the only place in the engine that asks.
//
// `core::RandomState::roll_chance` is what makes the consumption order below
// checkable: a chance of one hundred or more returns true without drawing, and
// a chance of zero returns false without drawing. So a certain weapon moves the
// hit stream not at all, and the stream's Nth number belongs to the Nth
// uncertain strike in the whole battle, in the order the rules resolve them.
//
// The order, stated once so a reader can check it against the rules:
//
//   1. An attack command rolls for the attacker's strike.
//   2. Then, and only if a counterattack actually occurs (the target is still
//      standing and the separation is inside its own band), it rolls for the
//      counter. A counter that cannot happen draws nothing.
//   3. An ability command rolls once per unit its area covers and damages, in
//      ascending unit identifier order. A damaging cast harms only the caster's
//      opponents, so an ally in the blast and the caster in its own draw
//      nothing: the roll comes after the sparing, so a number is drawn only
//      where damage was really on the table. A restoring cast draws nothing at
//      all.
bool roll_hit(core::RandomState& random, std::uint8_t chance) noexcept {
    return random.roll_chance(
        core::RandomStream::hit,
        static_cast<std::uint32_t>(chance),
        hit_chance_bound
    );
}

// The bound every drop chance is rolled against, and the same honesty rule the
// hit chance keeps: an author writes a whole percentage and this is the number
// it is divided by.
constexpr std::uint32_t drop_chance_bound = 100U;

// Whether a defeated unit leaves what its type says it leaves, and the only
// place in the engine that asks.
//
// It draws from `core::RandomStream::drop`, which exists precisely so that this
// roll cannot move the hit stream: streams are identified per purpose and a
// stream's Nth number does not depend on how many numbers any other stream
// drew, so a battle fought against droppers rolls exactly the hit numbers the
// same battle fought against non-droppers rolls. `tests/simulation` pins that.
//
// The order, stated once so a reader can check it against the rules:
//
//   1. Exactly one draw per defeated unit whose type authors a drop below a
//      certainty. An absent drop draws nothing and leaves nothing, and an
//      authored hundred draws nothing and always drops, on plain
//      `core::RandomState::roll_chance` semantics, the same ones accuracy and
//      growth keep. A chance of zero is the third case `roll_chance` names, and
//      it is unreachable here on purpose: a drop is authored as a pair or not
//      at all, so "leaves nothing" has exactly one spelling and
//      `create_encounter` refuses the other. So content that authors no drop
//      moves this stream not at all, and the stream's Nth number belongs to the
//      Nth uncertain drop in the whole battle.
//   2. The draw happens at the moment of defeat, immediately after the
//      `unit_defeated` event and before anything else the command does, so the
//      draws fall in the order the defeats resolve.
//   3. Within one attack command that fells twice, the strike's kill is rolled
//      before the counterattack's, because the strike resolves first. A
//      counterattack kill is a defeat like any other and rolls exactly like
//      one.
//   4. Within one ability command that fells several, one roll per felled unit
//      in **ascending unit identifier order**: the order the area loop already
//      walks, and deliberately the same rule the hit stream states for the same
//      loop, so that one order covers both streams and neither can be read off
//      the other's.
//   5. A defeat with no cause would draw nothing, and there is none: every
//      defeat this engine produces names the unit that caused it. Nobody
//      claims a drop nobody made.
bool roll_drop(core::RandomState& random, std::uint8_t chance) noexcept {
    return random.roll_chance(
        core::RandomStream::drop,
        static_cast<std::uint32_t>(chance),
        drop_chance_bound
    );
}

// The chance a strike lands, and the only place in the engine that computes
// one. The attack branch, the counterattack branch, the ability branch and both
// halves of `forecast_attack` all call this, so the number a player is shown
// and the number `roll_hit` divides by cannot be two different numbers.
//
//   chance = clamp(accuracy + skill + luck - evasion - luck, 0, 100)
//              striker  ---^      ^          ^          ^--- struck
//
// Three properties, each deliberate:
//
//   - **It degrades to the authored accuracy.** With the four stats at zero on
//     both units the expression is the accuracy the weapon or cast was authored
//     with, which is already bounded to [0, 100] at every layer. Content
//     written before these stats existed therefore rolls against precisely the
//     number it rolled against before.
//   - **It is integer addition with one clamp**, evaluated wide enough that two
//     int16 stats cannot overflow it. No multiplication, no division, no
//     rounding: three toolchains have to produce the same byte, and a player
//     reading a panel has to be able to do the arithmetic in their head.
//   - **Luck appears twice with opposite signs.** It is the only term that
//     helps whichever side of the exchange its owner is on; skill helps only
//     the striker and evasion only the struck. A luck that only defended would
//     be evasion under a second name.
//
// The fold costs no draw of its own. `roll_hit` takes exactly one number per
// sub-certain strike, and a folded hundred or a folded zero draws nothing, just
// as an authored hundred or an authored zero draws nothing.
std::uint8_t hit_chance_for(
    const UnitSnapshot& striker,
    const UnitSnapshot& struck,
    std::uint8_t accuracy
) noexcept {
    const std::int32_t folded = static_cast<std::int32_t>(accuracy) +
                                striker.skill + striker.luck -
                                struck.evasion - struck.luck;
    const std::int32_t bounded = std::min<std::int32_t>(
        static_cast<std::int32_t>(hit_chance_bound),
        std::max<std::int32_t>(0, folded)
    );
    return static_cast<std::uint8_t>(bounded);
}

// The basic-attack formula, shared by apply() and forecast_attack() so a
// forecast can never promise a number the attack would not deliver.
std::int16_t attack_damage(
    const UnitSnapshot& attacker,
    const UnitSnapshot& target,
    const StrikeProfile& strike
) noexcept {
    const auto raw = static_cast<std::int32_t>(attacker.strength) +
                     strike.power - target.defense;
    return static_cast<std::int16_t>(std::max<std::int32_t>(1, raw));
}

// One landed blow, spent against a unit's health and reported.
//
// Every place damage is taken in this file goes through here: the swing, the
// counter it provokes, and each unit a cast covers. The reason is that the
// lower bound of the subtraction is not always zero, and a bound written out
// three times is a bound that can come to be written three different ways.
//
// The bound is `floor_of`, which is the function `forecast_attack` calls when it
// predicts the same blow. That shared call is the whole promise: a forecast that
// said a character would be left standing is a forecast this delivers, because
// there is one floor and both halves ask for it rather than each clamping its
// own answer.
//
// `damage` is reported raw: a blow that overkills says what it struck for and
// the health total is where the clamp shows. Where the floor is what stopped
// the total, an event says so, immediately after the damage event, so that a
// client is told rather than left to subtract two numbers and notice they do
// not add up.
void take_damage(
    UnitSnapshot& struck,
    UnitId by,
    std::int16_t damage,
    CommandResult& result
) {
    const std::int16_t floor = floor_of(struck);
    const std::int32_t unclamped =
        static_cast<std::int32_t>(struck.health) - damage;
    struck.health = static_cast<std::int16_t>(
        std::max<std::int32_t>(floor, unclamped)
    );
    result.events.push_back(
        {
            EventType::unit_damaged,
            struck.id,
            by,
            struck.position,
            damage,
            Outcome::ongoing
        }
    );
    if (floor > 0 && unclamped <= 0) {
        result.events.push_back(
            {
                EventType::unit_endured,
                struck.id,
                by,
                struck.position,
                0,
                Outcome::ongoing
            }
        );
    }
}

// What a damaging cast takes off whoever it covers:
//
//   magical:  max(1, caster magic + power - resistance)
//   physical: max(1, power - defense)
//
// The magical case is shaped exactly like `attack_damage`: the striker's own
// stat, plus the implement's power, minus the defence it is resolved against.
// That is what makes a better mage cast harder. `magic` is folded in here
// and nowhere else, because here is the only place it could ever reach the
// board: nothing wields a magical weapon, so a magic stat this function
// ignored would be a stat that does nothing at all.
//
// The physical case is deliberately **not** symmetric, and the asymmetry is the
// decision rather than an oversight. `strength` already reaches the board, on
// every basic attack a unit makes. A physical ability priced by strength would
// be `strength + power - defense`, which is the swing formula character for
// character: the two would differ only in the number attached to the implement,
// and an ability's authored power would stop meaning "what this does" and start
// meaning "how much better than your sword this is". Every physical ability in
// shipped content would change what it deals without an author touching it.
//
// The honest cost, stated rather than hidden: a knight who grows strength
// swings harder and Power-Strikes exactly as hard as before. If content ever
// shows physical abilities needing to grow, the term to add is `strength`, and
// this is the one function it would be added to.
//
// At a `magic` of zero the magical case is `max(1, power - resistance)`.
std::int16_t ability_damage(
    const UnitSnapshot& caster,
    const AbilityDefinition& ability,
    const UnitSnapshot& affected
) noexcept {
    const bool magical = ability.damage_type == DamageType::magical;
    const std::int32_t offence = magical ? caster.magic : 0;
    const std::int32_t mitigation =
        magical ? affected.resistance : affected.defense;
    const std::int32_t raw = offence + ability.power - mitigation;
    return static_cast<std::int16_t>(std::max<std::int32_t>(1, raw));
}

// The profile a unit defends with: the weapon in hand, which is what
// `create_encounter` already resolved into unit state. A counter is struck with
// what the unit is holding and never searches the rest of what it carries.
// Final Fantasy Tactics' `Counter` says "your equipped weapon", and searching
// would make the gate depend on a choice no player made.
StrikeProfile equipped_strike(const UnitSnapshot& unit) noexcept {
    return {
        unit.power, unit.minimum_reach, unit.maximum_reach, unit.accuracy
    };
}

// Whether a defender strikes back at an attacker `separation` tiles away.
//
// The gate is the defender's own reach band and nothing else, which is Fire
// Emblem's rule and Final Fantasy Tactics' verbatim one. Three consequences are
// deliberate and each is a decision rather than a fallout:
//
//   * A felled defender does not answer. It is dead, and a killing blow being
//     safe is what makes "can I finish it this turn" the question a player
//     asks.
//   * The minimum of the band refuses as loudly as the maximum. An archer
//     whose band starts at two, struck from an adjacent tile, cannot shoot
//     back. It earns the same `target_out_of_range` too far earns, which every
//     client prints as OUT OF RANGE.
//   * Nothing about the defender's turn is consulted. A counter is free (see
//     the call site), so having acted, or being out of action points, does not
//     stop a unit defending itself.
//
// `health_after` is the defender's health once the provoking strike has landed,
// passed rather than read so the forecast can ask about a blow it has not
// struck without copying a unit: the query is noexcept and a snapshot owns
// vectors.
bool counters(
    const UnitSnapshot& defender,
    std::int16_t health_after,
    std::uint32_t separation
) noexcept {
    if (health_after <= 0) return false;
    return within_reach(
        separation, defender.minimum_reach, defender.maximum_reach
    );
}

const WeaponDefinition* find_weapon(
    const std::vector<WeaponDefinition>& weapons,
    ContentId id
) noexcept {
    const auto found = std::find_if(
        weapons.begin(),
        weapons.end(),
        [id](const WeaponDefinition& weapon) { return weapon.id == id; }
    );
    return found == weapons.end() ? nullptr : &*found;
}

bool carries_weapon(const UnitSnapshot& unit, ContentId id) noexcept {
    return std::find(unit.weapon_ids.begin(), unit.weapon_ids.end(), id) !=
           unit.weapon_ids.end();
}

// Resolves the weapon an attack names into the profile the rules read. A zero
// identity is the weapon in hand, which is exactly what the unit's own power
// and band already describe, so an attack that names nothing resolves to that.
// The two refusals are decided here, ahead of anything about the target, so the
// error names what the player got wrong.
CommandError resolve_strike(
    const UnitSnapshot& unit,
    const std::vector<WeaponDefinition>& weapons,
    ContentId weapon_id,
    StrikeProfile& strike
) noexcept {
    if (weapon_id == 0) {
        strike = equipped_strike(unit);
        return CommandError::none;
    }
    const WeaponDefinition* weapon = find_weapon(weapons, weapon_id);
    if (weapon == nullptr) return CommandError::unknown_weapon;
    if (!carries_weapon(unit, weapon_id)) {
        return CommandError::unavailable_weapon;
    }
    // The striker's own bonus goes on whichever weapon they drew, not only on
    // the one in their hand: an archer written to shoot further shoots further
    // with every bow she carries.
    strike = {
        weapon->power,
        weapon->minimum_reach,
        widened_reach(weapon->maximum_reach, unit.reach_bonus),
        weapon->accuracy
    };
    return CommandError::none;
}

const ItemDefinition* find_item(
    const std::vector<ItemDefinition>& items,
    ContentId id
) noexcept {
    const auto found = std::find_if(
        items.begin(),
        items.end(),
        [id](const ItemDefinition& item) { return item.id == id; }
    );
    return found == items.end() ? nullptr : &*found;
}

// Where in a unit's carried list an item sits, or the list's length when it is
// not carried at all. The index is what the count lives at, and the two lists
// are the same length by construction, so one search answers both questions.
std::size_t carried_slot(const UnitSnapshot& unit, ContentId id) noexcept {
    for (std::size_t i = 0; i < unit.item_ids.size(); ++i) {
        if (unit.item_ids[i] == id) return i;
    }
    return unit.item_ids.size();
}

// Resolves the item a use names into the definition the rules read, deciding
// every refusal that belongs to the item itself before anything about the
// target is looked at. That is the same order `resolve_strike` keeps, and for
// the same reason: the error names what the player got wrong. There is no
// zero-means-in-hand case here, because nothing in a pack is in hand.
//
// The four refusals are ordered the way a player asks the questions: does this
// item exist, am I carrying it, have I any left, and is it worth using. That
// puts `unusable_item` last, after the two that say something about this
// character's pack, because "you are not carrying that" is the more actionable
// answer to a use of a keepsake nobody holds.
CommandError resolve_use(
    const UnitSnapshot& unit,
    const std::vector<ItemDefinition>& items,
    ContentId item_id,
    const ItemDefinition*& item,
    std::size_t& slot
) noexcept {
    item = item_id == 0 ? nullptr : find_item(items, item_id);
    if (item == nullptr) return CommandError::unknown_item;
    slot = carried_slot(unit, item_id);
    if (slot == unit.item_ids.size()) return CommandError::unavailable_item;
    if (unit.item_counts[slot] == 0U) return CommandError::depleted_item;
    if (item->kind == ItemKind::none) return CommandError::unusable_item;
    return CommandError::none;
}

// Membership test for an enumerated area shape centred on `centre`.
bool covered_by(
    AreaShape shape,
    std::uint8_t radius,
    Position centre,
    Position candidate
) noexcept {
    const std::uint32_t separation = distance(centre, candidate);
    switch (shape) {
        case AreaShape::single: return separation == 0U;
        case AreaShape::cross: return separation <= 1U;
        case AreaShape::diamond: return separation <= radius;
    }
    return false;
}

const AbilityDefinition* find_ability(
    const std::vector<AbilityDefinition>& abilities,
    ContentId id
) noexcept {
    const auto found = std::find_if(
        abilities.begin(),
        abilities.end(),
        [id](const AbilityDefinition& ability) { return ability.id == id; }
    );
    return found == abilities.end() ? nullptr : &*found;
}

bool owns_ability(const UnitSnapshot& unit, ContentId id) noexcept {
    return std::find(unit.ability_ids.begin(), unit.ability_ids.end(), id) !=
           unit.ability_ids.end();
}

// Whether the cell at `slot` takes a unit carrying `crossings`. An empty
// terrain list is an all-open board: every package written before terrain had
// a meaning, and every caller that has none to give.
bool passable(
    const std::vector<Terrain>& terrain,
    std::size_t slot,
    std::uint8_t crossings
) noexcept {
    if (terrain.empty()) return true;
    if (slot >= terrain.size()) return false;
    return can_enter(terrain[slot], crossings);
}

// What the cell at `slot` charges, before the walker's own crossings are asked.
// An empty list is a board where every step costs one: every package written
// before ground had a price, and every caller that has none to give.
std::uint8_t authored_cost(
    const std::vector<std::uint8_t>& movement_cost,
    std::size_t slot
) noexcept {
    if (slot >= movement_cost.size()) return movement_cost_step;
    return movement_cost[slot];
}

}  // namespace

std::vector<std::uint32_t> movement_field(
    const EncounterSnapshot& snapshot,
    Position origin,
    std::uint8_t allowance,
    std::uint8_t crossings,
    Side mover
) {
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    std::vector<std::uint32_t> spent(cells, unreachable_cost);
    if (cells == 0U) return spent;
    if (!in_bounds(origin, snapshot.width, snapshot.height)) return spent;
    const auto index_of = [&snapshot](Position position) {
        return static_cast<std::size_t>(position.y) * snapshot.width +
               static_cast<std::size_t>(position.x);
    };
    spent[index_of(origin)] = 0U;

    // One bucket per payable total, swept in ascending order. Every entry costs
    // at least one and the allowance is a byte, so the totals worth reaching
    // are a short, dense, known range. That makes the queue an array of them
    // and the search a sweep, with no ordering structure to get wrong and no
    // comparator whose ties could decide anything.
    //
    // Sweeping totals in order is what makes the first price recorded for a
    // cell its cheapest: nothing found later can undercut a total already
    // passed, because no step is free. A cell reached again more cheaply is
    // reached from a *lower* bucket than the one it is queued in, so it
    // overwrites its entry and the stale one is skipped when its bucket comes
    // round. Neighbours are still visited in the same fixed order everything
    // else in this file uses, though nothing here depends on it: the least a
    // cell can cost is the least it can cost whatever order the board is walked
    // in, so this answer is a fact about the board and not about the traversal.
    std::vector<std::vector<Position>> buckets(
        static_cast<std::size_t>(allowance) + 1U
    );
    buckets[0].push_back(origin);
    constexpr std::pair<std::int16_t, std::int16_t> steps[] = {
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    };
    for (std::uint32_t total = 0; total <= allowance; ++total) {
        std::vector<Position>& bucket = buckets[total];
        for (std::size_t index = 0; index < bucket.size(); ++index) {
            const Position position = bucket[index];
            // Superseded: this cell was reached again for less after it was
            // queued here, and the cheaper arrival has already been walked out
            // of. Nothing is gained by walking out of it twice.
            if (spent[index_of(position)] != total) continue;
            for (const auto& [dx, dy] : steps) {
                const Position next{
                    static_cast<std::int16_t>(position.x + dx),
                    static_cast<std::int16_t>(position.y + dy)
                };
                if (!in_bounds(next, snapshot.width, snapshot.height)) continue;
                const std::size_t slot = index_of(next);
                // An opponent is a wall. An ally is walked through and priced
                // like any other cell, so a path may cross it. It is struck out
                // of the result below, so no walk may stop on it.
                if (blocks_passage(snapshot.units, next, mover)) continue;
                if (!passable(snapshot.terrain, slot, crossings)) continue;
                const std::uint32_t arrival =
                    total + entry_cost(
                                authored_cost(snapshot.movement_cost, slot),
                                crossings
                            );
                if (arrival > allowance) continue;
                if (arrival >= spent[slot]) continue;
                spent[slot] = arrival;
                buckets[arrival].push_back(next);
            }
        }
    }

    // Nobody finishes a walk on somebody else. An ally's tile was priced above
    // because a path is allowed to cross it; it is struck out here because
    // standing there is a different question, and the answer to that one is the
    // same for both sides. Struck out at the end rather than refused during the
    // sweep so that the cheapest way *past* an ally is still the cheapest way to
    // everything beyond it.
    //
    // Walking the roster rather than the board, because the roster is the
    // shorter list on every board this engine runs and because the tiles held
    // are exactly the tiles it names. `origin` is exempt: the moving character
    // is standing there and has always been in its own result.
    for (const UnitSnapshot& unit : snapshot.units) {
        if (!on_board(unit)) continue;
        if (unit.position == origin) continue;
        if (!in_bounds(unit.position, snapshot.width, snapshot.height)) {
            continue;
        }
        spent[index_of(unit.position)] = unreachable_cost;
    }
    return spent;
}

namespace {

// The nearest tile to `origin` that is free and this character could stand on,
// or `origin` itself when that already is.
//
// Breadth-first over the board in the same fixed neighbour order everything
// else here uses, so "nearest" is nearest by the board's own geometry and a tie
// is broken by that order rather than by anything an author could have typed
// differently. It follows passability, which is the point: a wave authored
// behind a wall comes in behind the wall or not at all, rather than stepping
// through it.
//
// Unbounded in steps, and deliberately blind to price: this is a search for
// somewhere to stand rather than a walk anybody has to make, and a wave marches
// onto the board from off it rather than paying its way across. Nearest is
// nearest in tiles, so a landing does not drift because the ground between two
// candidates happens to be slow. It answers false only for a board with no free
// standable tile within reach of the authored one at all.
bool nearest_standing_tile(
    const std::vector<UnitSnapshot>& units,
    std::uint16_t width,
    std::uint16_t height,
    const std::vector<Terrain>& terrain,
    Position origin,
    std::uint8_t crossings,
    Position& landing
) {
    const auto index_of = [width](Position position) {
        return static_cast<std::size_t>(position.y) * width +
               static_cast<std::size_t>(position.x);
    };
    const auto standable = [&](Position position) {
        return !occupied(units, position) &&
               passable(terrain, index_of(position), crossings);
    };
    if (standable(origin)) {
        landing = origin;
        return true;
    }
    std::vector<std::uint8_t> visited(
        static_cast<std::size_t>(width) * height, 0U
    );
    std::deque<Position> frontier;
    frontier.push_back(origin);
    visited[index_of(origin)] = 1U;
    constexpr std::pair<std::int16_t, std::int16_t> steps[] = {
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    };
    while (!frontier.empty()) {
        const Position position = frontier.front();
        frontier.pop_front();
        for (const auto& [dx, dy] : steps) {
            const Position next{
                static_cast<std::int16_t>(position.x + dx),
                static_cast<std::int16_t>(position.y + dy)
            };
            if (!in_bounds(next, width, height)) continue;
            const std::size_t slot = index_of(next);
            if (visited[slot] != 0U) continue;
            visited[slot] = 1U;
            if (!passable(terrain, slot, crossings)) continue;
            if (standable(next)) {
                landing = next;
                return true;
            }
            frontier.push_back(next);
        }
    }
    return false;
}

// A unit falling, and the drop roll that follows it, written once.
//
// The three places a unit can fall are a strike, the counterattack it provoked,
// and a cast's area. All come through here, so they cannot disagree about the
// order of the two events, about who claims the drop, or about when the stream
// is drawn from. `roll_drop` states the consumption order this call site
// realises.
void record_defeat(
    EncounterSnapshot& state,
    const UnitSnapshot& fallen,
    UnitId claimant,
    CommandResult& result
) {
    result.events.push_back(
        {
            EventType::unit_defeated,
            fallen.id,
            claimant,
            fallen.position,
            0,
            Outcome::ongoing
        }
    );
    // Nothing to leave, or nobody to claim it. Neither draws.
    if (fallen.drop_item_id == 0 || claimant == 0) return;
    if (!roll_drop(state.random, fallen.drop_chance)) return;
    state.drops.push_back({fallen.id, claimant, fallen.drop_item_id});
    result.events.push_back(
        {
            EventType::item_dropped,
            fallen.id,
            claimant,
            fallen.position,
            1,
            Outcome::ongoing,
            fallen.drop_item_id
        }
    );
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ULL;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(
            hash,
            static_cast<std::uint8_t>(bits >> (index * 8U))
        );
    }
}

// Which of a character's optional parts `canonical_hash` is about to fold.
//
// There are six parts and eight bits, because two of the six are pairs: a talk
// record and the departure it can end in, an arrival round and whether it has
// come. A bit apiece keeps the encoding injective over every snapshot rather
// than only over the ones content can currently reach. Eight is also
// exactly one byte, so the whole announcement costs one.
//
// Four of the eight are the whole of their part: a flag is one bit, and a bit
// saying the flag is set has already said it. The fold itself argues why the
// announcement is made rather than inferred.
constexpr std::uint8_t present_talk_record = 1U << 0U;
constexpr std::uint8_t present_departed = 1U << 1U;
constexpr std::uint8_t present_arrival_round = 1U << 2U;
constexpr std::uint8_t present_unarrived = 1U << 3U;
constexpr std::uint8_t present_endures = 1U << 4U;
constexpr std::uint8_t present_walk_spent = 1U << 5U;
constexpr std::uint8_t present_points_spent = 1U << 6U;
constexpr std::uint8_t present_reach_bonus = 1U << 7U;

}  // namespace

// Turns every authored recurrence into the characters it actually means, in
// definition order with a wave's arrivals adjacent, and answers false for an
// arrival nothing could honour.
//
// Done here in the rules rather than in either loader on purpose. The browser
// builds an `EncounterDefinition` in TypeScript and hands it to this same
// module; the consoles build one out of a package. Expanding in a loader would
// be one rule kept in two places that have to agree, which is the thing
// `engine/simulation/README.md` §conformance exists to forbid.
//
// The first arrival keeps the identifier the content gave it, so an objective,
// a talk or a campaign join that names the placement names the first of the
// wave. Every later arrival is a character the content did not name and takes
// **the lowest identifier the definition does not use**: lowest-unused rather
// than largest-plus-one because authored identities are content hashes and
// adding to the largest of them can wrap, while a scan cannot.
//
// **Those identifiers are handed out in identifier order, not in authored
// order**, and that is the load-bearing half of this function. A lowest-unused
// scan is a running state, so walking the waves in the order somebody happened
// to list them would give `[A, B]` and `[B, A]` different identifiers for the
// same two waves, and therefore different canonical hashes for the same
// content. Walking them by the identity the content gave them, a content hash
// and the order the board is kept in anyway, makes the assignment a fact about
// the waves rather than about the listing. It is what
// makes `engine/simulation/README.md`'s claim that a board is keyed by unit
// identity rather than by source insertion order true of the identifiers as
// well as of the walk, and it is what lets the browser's builder and the
// package loader emit their units in whatever order each finds natural without
// the two disagreeing about the battle.
bool expand_arrivals(
    const std::vector<UnitDefinition>& authored,
    std::vector<UnitDefinition>& expanded
) {
    // How many characters each authored placement means, judged before any
    // identifier is handed out. The assignment below walks the placements in a
    // different order than the emission does, and a rule that refused a board
    // would otherwise refuse it after having already spent identifiers on it.
    std::vector<std::uint16_t> times(authored.size(), 0U);
    for (std::size_t slot = 0; slot < authored.size(); ++slot) {
        const UnitDefinition& unit = authored[slot];
        const bool recurs = unit.arrival_every != 0 || unit.arrival_times != 0;
        if (unit.arrival_round == 0) {
            // A recurrence with no first arrival is a wave with nowhere to
            // start, which is a number an author meant something by and this
            // engine would otherwise drop on the floor.
            if (recurs) return false;
            times[slot] = 1U;
            continue;
        }
        // The first round is the round the battle opens in. See
        // `UnitDefinition::arrival_round`.
        if (unit.arrival_round < 2U) return false;
        times[slot] = 1U;
        if (recurs) {
            if (unit.arrival_every == 0U || unit.arrival_times < 2U ||
                unit.arrival_times > maximum_arrivals) {
                return false;
            }
            times[slot] = unit.arrival_times;
        }
    }

    // The placements that recur, in the order of the identities the content
    // gave them. Ties keep their authored order, which is the only thing left
    // to break them by; a board holding two placements under one identity is
    // refused as `duplicate_unit` a moment later regardless.
    std::vector<std::size_t> recurring;
    for (std::size_t slot = 0; slot < authored.size(); ++slot) {
        if (times[slot] > 1U) recurring.push_back(slot);
    }
    std::stable_sort(
        recurring.begin(), recurring.end(),
        [&](std::size_t lhs, std::size_t rhs) {
            return authored[lhs].id < authored[rhs].id;
        }
    );

    std::set<UnitId> used;
    for (const UnitDefinition& unit : authored) used.insert(unit.id);
    // Each recurring placement's later arrivals, by the slot they belong to.
    std::vector<std::vector<UnitId>> later(authored.size());
    for (const std::size_t slot : recurring) {
        for (std::uint16_t index = 1; index < times[slot]; ++index) {
            UnitId next = 1;
            while (used.find(next) != used.end()) ++next;
            used.insert(next);
            later[slot].push_back(next);
        }
    }

    for (std::size_t slot = 0; slot < authored.size(); ++slot) {
        const UnitDefinition& unit = authored[slot];
        for (std::uint16_t index = 0; index < times[slot]; ++index) {
            UnitDefinition copy = unit;
            // The recurrence is spent: each expanded character states the one
            // round it comes on, so nothing downstream has to expand anything
            // a second time.
            copy.arrival_every = 0U;
            copy.arrival_times = 0U;
            if (unit.arrival_round != 0U) {
                const std::uint64_t round =
                    static_cast<std::uint64_t>(unit.arrival_round) +
                    static_cast<std::uint64_t>(index) * unit.arrival_every;
                // `maximum_arrival_round` rather than the field's own ceiling:
                // the `unit_arrived` event reports the round in progress in an
                // `int16`, and a number a client cannot be told is a number an
                // author is told about here instead.
                if (round > maximum_arrival_round) return false;
                copy.arrival_round = static_cast<std::uint32_t>(round);
                // The ceiling is asked of arrivals rather than of placements,
                // because a wave is the one thing here that turns one authored
                // line into many characters. A board with more placements than
                // a board has cells is refused by the gate that counts cells.
                if (expanded.size() >= maximum_board_cells) return false;
            }
            if (index > 0) copy.id = later[slot][index - 1U];
            expanded.push_back(std::move(copy));
        }
    }
    return true;
}

std::string_view error_name(CreateError error) noexcept {
    switch (error) {
        case CreateError::none: return "none";
        case CreateError::invalid_map: return "invalid_map";
        case CreateError::invalid_unit: return "invalid_unit";
        case CreateError::duplicate_unit: return "duplicate_unit";
        case CreateError::occupied_position: return "occupied_position";
        case CreateError::missing_side: return "missing_side";
        case CreateError::invalid_ability: return "invalid_ability";
        case CreateError::invalid_objective: return "invalid_objective";
        case CreateError::invalid_weapon: return "invalid_weapon";
        case CreateError::invalid_item: return "invalid_item";
        case CreateError::invalid_deployment: return "invalid_deployment";
        case CreateError::invalid_arrival: return "invalid_arrival";
    }
    return "unknown";
}

std::string_view error_name(CommandError error) noexcept {
    switch (error) {
        case CommandError::none: return "none";
        case CommandError::encounter_complete: return "encounter_complete";
        case CommandError::unknown_unit: return "unknown_unit";
        case CommandError::defeated_unit: return "defeated_unit";
        case CommandError::wrong_side: return "wrong_side";
        case CommandError::invalid_command: return "invalid_command";
        case CommandError::invalid_destination: return "invalid_destination";
        case CommandError::occupied_destination:
            return "occupied_destination";
        case CommandError::unknown_target: return "unknown_target";
        case CommandError::target_defeated: return "target_defeated";
        case CommandError::friendly_target: return "friendly_target";
        case CommandError::target_out_of_range: return "target_out_of_range";
        case CommandError::unknown_ability: return "unknown_ability";
        case CommandError::unavailable_ability: return "unavailable_ability";
        case CommandError::activation_in_progress:
            return "activation_in_progress";
        case CommandError::no_action_points: return "no_action_points";
        case CommandError::unknown_weapon: return "unknown_weapon";
        case CommandError::unavailable_weapon: return "unavailable_weapon";
        case CommandError::unknown_item: return "unknown_item";
        case CommandError::unavailable_item: return "unavailable_item";
        case CommandError::depleted_item: return "depleted_item";
        case CommandError::unusable_item: return "unusable_item";
        case CommandError::wrong_phase: return "wrong_phase";
        case CommandError::undeployable_unit: return "undeployable_unit";
        case CommandError::outside_zone: return "outside_zone";
        case CommandError::not_talkable: return "not_talkable";
        case CommandError::target_departed: return "target_departed";
        case CommandError::target_unarrived: return "target_unarrived";
        case CommandError::unarrived_unit: return "unarrived_unit";
        case CommandError::already_acted: return "already_acted";
        case CommandError::already_moved: return "already_moved";
        case CommandError::departed_unit: return "departed_unit";
    }
    return "unknown";
}

Encounter::Encounter(EncounterSnapshot state) : state_(std::move(state)) {}

Encounter::CreateResult create_encounter(
    const EncounterDefinition& definition
) {
    EncounterSnapshot state;
    state.width = definition.width;
    state.height = definition.height;
    if (definition.width == 0 || definition.height == 0 ||
        definition.width >
            static_cast<std::uint16_t>(
                std::numeric_limits<std::int16_t>::max()
            ) ||
        definition.height >
            static_cast<std::uint16_t>(
                std::numeric_limits<std::int16_t>::max()
            ) ||
        static_cast<std::uint32_t>(definition.width) * definition.height >
            maximum_board_cells) {
        return {CreateError::invalid_map, Encounter(std::move(state))};
    }
    // Terrain is either absent, meaning an all-open board, or exactly one cell
    // per cell.
    // A partial board would make the rule depend on where a unit happened to
    // stand, so it is refused rather than padded.
    if (!definition.terrain.empty() &&
        definition.terrain.size() !=
            static_cast<std::size_t>(definition.width) * definition.height) {
        return {CreateError::invalid_map, Encounter(std::move(state))};
    }
    for (const Terrain cell : definition.terrain) {
        if (cell != Terrain::open && cell != Terrain::water &&
            cell != Terrain::heights) {
            return {CreateError::invalid_map, Encounter(std::move(state))};
        }
    }
    state.terrain = definition.terrain;
    // The board's price, on exactly the terms its passability is on: absent,
    // meaning a board where every step costs one, or one entry per cell. A
    // partial list is refused rather than padded, for the reason a partial
    // terrain list is.
    //
    // Zero is refused too, and it is the one value that has to be. A cell
    // charging nothing is not cheap ground, it is a walk with no end: a
    // character could cross any number of such cells inside one allowance, and
    // the straight-line refusal `Encounter::reachable` opens with, sound only
    // because no step is free, would start turning down tiles the fill could
    // actually reach. `entry_cost` floors what it is handed for the callers it
    // cannot vet; content this engine is given gets told instead.
    if (!definition.movement_cost.empty() &&
        definition.movement_cost.size() !=
            static_cast<std::size_t>(definition.width) * definition.height) {
        return {CreateError::invalid_map, Encounter(std::move(state))};
    }
    for (const std::uint8_t cell : definition.movement_cost) {
        if (cell < movement_cost_step) {
            return {CreateError::invalid_map, Encounter(std::move(state))};
        }
    }
    state.movement_cost = definition.movement_cost;

    // The deployment region, sorted row-major so that the order an author
    // happened to type the tiles in cannot reach the canonical hash, and
    // checked for duplicates so that "how many tiles are there" has one answer.
    // Terrain is deliberately not consulted: a region tile nobody could stand
    // on is refused by the compiler, where the author is.
    state.deployment_tiles = definition.deployment_tiles;
    std::sort(
        state.deployment_tiles.begin(),
        state.deployment_tiles.end(),
        [](Position lhs, Position rhs) {
            return lhs.y != rhs.y ? lhs.y < rhs.y : lhs.x < rhs.x;
        }
    );
    for (std::size_t index = 0; index < state.deployment_tiles.size(); ++index) {
        const Position tile = state.deployment_tiles[index];
        if (!in_bounds(tile, definition.width, definition.height) ||
            (index > 0 && tile == state.deployment_tiles[index - 1])) {
            return {
                CreateError::invalid_deployment,
                Encounter(std::move(state))
            };
        }
    }
    // Whether the encounter opens in the phase is decided at the very end of
    // this function, after the dice are seeded. See there for why.

    std::set<ContentId> ability_ids;
    for (const AbilityDefinition& ability : definition.abilities) {
        if (ability.id == 0 || !bounded_stat(ability.power) ||
            ability.minimum_reach > ability.maximum_reach ||
            !ability_ids.insert(ability.id).second) {
            return {CreateError::invalid_ability, Encounter(std::move(state))};
        }
    }

    std::set<ContentId> weapon_ids;
    for (const WeaponDefinition& weapon : definition.weapons) {
        if (weapon.id == 0 || !bounded_stat(weapon.power) ||
            weapon.minimum_reach == 0 ||
            weapon.minimum_reach > weapon.maximum_reach ||
            !weapon_ids.insert(weapon.id).second) {
            return {CreateError::invalid_weapon, Encounter(std::move(state))};
        }
    }

    std::set<ContentId> item_ids;
    for (const ItemDefinition& item : definition.items) {
        // A restoring item with no power restores nothing, which is an item
        // that exists to be wasted. Rejected here rather than at the point of
        // use, so the content is refused once instead of every battle.
        if (item.id == 0 || !bounded_stat(item.power) ||
            (item.kind != ItemKind::none && item.kind != ItemKind::restore) ||
            (item.kind == ItemKind::restore && item.power == 0) ||
            !item_ids.insert(item.id).second) {
            return {CreateError::invalid_item, Encounter(std::move(state))};
        }
    }

    // The waves, turned into the characters they mean, before anything is
    // judged, so that an arrival is validated by exactly the rules a placement
    // is, once, rather than by a second copy of them.
    std::vector<UnitDefinition> units;
    units.reserve(definition.units.size());
    if (!expand_arrivals(definition.units, units)) {
        return {CreateError::invalid_arrival, Encounter(std::move(state))};
    }

    std::set<UnitId> ids;
    bool first = false;
    bool second = false;
    for (const UnitDefinition& unit : units) {
        // Every stat that reaches the damage arithmetic is bounded above as
        // well as below. See `maximum_stat`: the sum of two of them is narrowed
        // back to the `int16` health is kept in, so an unbounded pair wraps
        // negative and a blow heals what it hits.
        if (unit.id == 0 || unit.unit_type_id == 0 ||
            !valid_side(unit.side) || unit.health <= 0 ||
            !bounded_stat(unit.strength) || !bounded_stat(unit.power) ||
            !bounded_stat(unit.defense) || !bounded_stat(unit.resistance) ||
            unit.skill < 0 || unit.luck < 0 ||
            unit.evasion < 0 || !bounded_stat(unit.magic) ||
            unit.action_points == 0 || unit.minimum_reach == 0 ||
            unit.minimum_reach > unit.maximum_reach ||
            !in_bounds(unit.position, definition.width, definition.height)) {
            return {CreateError::invalid_unit, Encounter(std::move(state))};
        }
        // A unit standing where it could never walk is authored nonsense.
        // Letting it stand there and step out would make the board's one
        // impassable cell a special case forever.
        if (!passable(
                state.terrain,
                static_cast<std::size_t>(unit.position.y) * definition.width +
                    static_cast<std::size_t>(unit.position.x),
                unit.crossings
            )) {
            return {CreateError::invalid_unit, Encounter(std::move(state))};
        }
        for (const ContentId ability : unit.ability_ids) {
            if (ability_ids.find(ability) == ability_ids.end()) {
                return {
                    CreateError::invalid_ability,
                    Encounter(std::move(state))
                };
            }
        }
        for (const ContentId weapon : unit.weapon_ids) {
            if (weapon_ids.find(weapon) == weapon_ids.end()) {
                return {
                    CreateError::invalid_weapon,
                    Encounter(std::move(state))
                };
            }
        }
        // A satchel is a list of slots, so a repeated identity would give one
        // item two counts and make "how many do I have" ambiguous. The counts
        // are either supplied one-for-one or absent, and absent means one of
        // each: what a unit type's authored list says, before a campaign
        // inventory has anything to say about it.
        if (!unit.item_counts.empty() &&
            unit.item_counts.size() != unit.item_ids.size()) {
            return {CreateError::invalid_item, Encounter(std::move(state))};
        }
        std::set<ContentId> carried_items;
        for (const ContentId item : unit.item_ids) {
            if (item_ids.find(item) == item_ids.end() ||
                !carried_items.insert(item).second) {
                return {CreateError::invalid_item, Encounter(std::move(state))};
            }
        }
        for (const std::uint16_t count : unit.item_counts) {
            if (count == 0U) {
                return {CreateError::invalid_item, Encounter(std::move(state))};
            }
        }
        // A drop is authored as a pair or not at all. A chance with nothing to
        // leave is a roll whose outcome was never written down; something to
        // leave with no chance of leaving it is an outcome nothing reaches.
        // Both are refused here rather than half-honoured, so that an author
        // who wrote one field and forgot the other hears about it once instead
        // of wondering why the picket never drops anything.
        //
        // The identity is deliberately not checked against `definition.items`.
        // A drop is recorded and handed to nobody, so the battle never reads
        // what it does; requiring the definition would force every board a
        // dropper can stand on to register an item nothing on it carries.
        if ((unit.drop_item_id == 0) != (unit.drop_chance == 0) ||
            unit.drop_chance > 100U) {
            return {CreateError::invalid_item, Encounter(std::move(state))};
        }
        std::vector<std::uint16_t> counts = unit.item_counts;
        if (counts.empty()) counts.assign(unit.item_ids.size(), 1U);
        if (!ids.insert(unit.id).second) {
            return {CreateError::duplicate_unit, Encounter(std::move(state))};
        }
        // Only the opening arrangement is judged against itself. A character
        // who comes in on the sixth round does not share the board with the
        // characters standing on it at the opening, and where its authored tile
        // is held when its round comes it takes the nearest one it can. So two
        // waves may be authored onto one tile, and a wave may be authored onto
        // a tile somebody starts on.
        if (unit.arrival_round == 0U && occupied(state.units, unit.position)) {
            return {
                CreateError::occupied_position,
                Encounter(std::move(state))
            };
        }
        // The first weapon a unit carries is the weapon in hand. Resolving it
        // here rather than trusting the caller to have flattened it means one
        // definition of "equipped" instead of one per caller; a unit that
        // carries nothing keeps the power and band it was defined with.
        StrikeProfile equipped{
            unit.power, unit.minimum_reach, unit.maximum_reach, unit.accuracy
        };
        if (!unit.weapon_ids.empty()) {
            const WeaponDefinition* in_hand =
                find_weapon(definition.weapons, unit.weapon_ids.front());
            if (in_hand == nullptr) {
                return {
                    CreateError::invalid_weapon,
                    Encounter(std::move(state))
                };
            }
            equipped = {
                in_hand->power,
                in_hand->minimum_reach,
                in_hand->maximum_reach,
                in_hand->accuracy
            };
        }
        // And then whatever the unit itself adds to what it is holding. Done
        // after the resolution rather than before it, because the band the
        // resolution produces is the one being widened: a bare-handed unit
        // widens the band it was defined with, and an armed one widens its
        // weapon's. This is the number `canonical_hash` folds.
        equipped.maximum_reach =
            widened_reach(equipped.maximum_reach, unit.reach_bonus);
        // A side counts as present only through somebody standing on the board
        // when the battle opens. See `CreateError::missing_side`: a side made
        // entirely of waves is a side that can take no command, and on a board
        // where that is true of either side nothing can ever be accepted, so no
        // round turns and no wave lands.
        if (unit.arrival_round == 0U) {
            first = first || unit.side == Side::first;
            second = second || unit.side == Side::second;
        }
        state.units.push_back(
            {
                unit.id,
                unit.unit_type_id,
                unit.side,
                // Nobody has spent a point of their own turn at the opening of
                // a battle. Stated here rather than beside the two flags below
                // because that is where the field stands. See the struct.
                0U,
                unit.position,
                unit.health,
                unit.health,
                unit.strength,
                equipped.power,
                unit.defense,
                unit.resistance,
                unit.skill,
                unit.luck,
                unit.evasion,
                unit.magic,
                unit.movement,
                unit.action_points,
                unit.speed,
                unit.acts_after_attacking,
                equipped.minimum_reach,
                equipped.maximum_reach,
                unit.ability_ids,
                // Nobody has acted and nobody has walked at the opening of a
                // battle.
                false,
                false,
                unit.weapon_ids,
                unit.crossings,
                equipped.accuracy,
                unit.item_ids,
                std::move(counts),
                unit.drop_item_id,
                unit.drop_chance,
                unit.reach_bonus,
                unit.talk_record_id,
                // Nobody has departed at the opening of a battle. Stated rather
                // than defaulted because the field beside it is authored and
                // this one never is: departure is something the battle does.
                false,
                unit.arrival_round,
                // And nobody with a wave to catch is here yet. A character who
                // authors no arrival is arrived from the opening, which is what
                // every character on every board written before waves says.
                unit.arrival_round == 0U,
                // Whether this character's health has a floor of one. Carried
                // across unchanged, like every other authored fact here: what
                // decides it is a campaign's business and the rules only obey it.
                unit.endures
            }
        );
    }
    if (!first || !second) {
        return {CreateError::missing_side, Encounter(std::move(state))};
    }

    std::set<ContentId> objective_ids;
    for (const ObjectiveDefinition& objective : definition.objectives) {
        if (objective.id == 0 || !valid_side(objective.side) ||
            !objective_ids.insert(objective.id).second) {
            return {
                CreateError::invalid_objective,
                Encounter(std::move(state))
            };
        }
        const bool needs_target =
            objective.kind == ObjectiveKind::defeat_target ||
            objective.kind == ObjectiveKind::protect_target;
        if (needs_target && ids.find(objective.target_unit_id) == ids.end()) {
            return {
                CreateError::invalid_objective,
                Encounter(std::move(state))
            };
        }
        // The count and the kind are one authored fact. A survive objective
        // with no count is a battle that was already over before it opened; a
        // count on a kind that cannot read one is a number nothing will ever
        // consult. Both are refused rather than half-honoured, on the standard
        // a half-authored drop already sets.
        const bool counts = objective.kind == ObjectiveKind::survive_rounds;
        if (counts != (objective.round_count != 0U)) {
            return {
                CreateError::invalid_objective,
                Encounter(std::move(state))
            };
        }
        state.objectives.push_back({objective.id, ObjectiveState::pending});
    }

    std::sort(
        state.units.begin(),
        state.units.end(),
        [](const UnitSnapshot& lhs, const UnitSnapshot& rhs) {
            return lhs.id < rhs.id;
        }
    );

    Encounter encounter(std::move(state));
    encounter.state_.turn_order = definition.turn_order;
    // Whether this battle gives the round count consequence. Under an ordered
    // turn order the count is kept regardless; under alternating order this is
    // what decides whether it is kept at all, and a board that authors neither
    // an objective that reads it nor a character who arrives on one therefore
    // runs exactly the code, folds exactly the bytes and prints exactly the
    // line it did before rounds were counted under that order. See
    // `EncounterSnapshot::round`.
    encounter.counts_rounds_ =
        std::any_of(
            definition.objectives.begin(),
            definition.objectives.end(),
            [](const ObjectiveDefinition& objective) {
                return objective.kind == ObjectiveKind::survive_rounds;
            }
        ) ||
        std::any_of(
            encounter.state_.units.begin(),
            encounter.state_.units.end(),
            [](const UnitSnapshot& unit) { return unit.arrival_round != 0U; }
        );
    encounter.abilities_ = definition.abilities;
    encounter.weapons_ = definition.weapons;
    encounter.items_ = definition.items;
    encounter.objectives_ = definition.objectives;
    std::sort(
        encounter.objectives_.begin(),
        encounter.objectives_.end(),
        [](const ObjectiveDefinition& lhs, const ObjectiveDefinition& rhs) {
            return lhs.id < rhs.id;
        }
    );
    std::sort(
        encounter.state_.objectives.begin(),
        encounter.state_.objectives.end(),
        [](const ObjectiveResult& lhs, const ObjectiveResult& rhs) {
            return lhs.id < rhs.id;
        }
    );
    // An ordered encounter names its first actor up front, so the caller never
    // has to guess who the turn belongs to. The exception is a battle that
    // opens in the deployment phase: there is no turn yet, and naming one would
    // be naming an activation nobody may spend. `begin_battle` names it at the
    // moment the phase closes, so an ordered board arranged by a player opens
    // on exactly the actor it would have opened on unarranged.
    if (encounter.state_.turn_order != TurnOrder::alternating &&
        encounter.state_.deployment_tiles.empty()) {
        // No wave can land here: the earliest arrival is the second round and
        // this is the opening of the first, so the events this advance is
        // handed are always none. Naming the sink rather than hiding it, so
        // that a future arrival with nowhere to be reported would not compile.
        CommandResult opening;
        encounter.begin_next_activation(Side::second, opening);
    }
    // The dice are seeded last, once the opening state is whatever it is going
    // to be, so that a derived seed is a function of the whole encounter: the
    // board, the units, the objectives and who acts first.
    //
    // A caller's seed is taken as given. Otherwise the seed is derived from the
    // encounter's own canonical hash, taken here while the random state is
    // still empty, which is the only source of variety a simulation forbidden
    // to read the clock has. Two identical encounters therefore roll identical
    // numbers; making a battle differ between playthroughs is the caller's job
    // and its seed is the field for it.
    encounter.state_.random.seed =
        definition.random_seed != 0
            ? definition.random_seed
            : core::derive_random_seed(encounter.canonical_hash());
    // And *then* the phase opens, which is the order rather than an accident.
    // A derived seed is a function of the board the dice will be rolled on,
    // and no die is rolled while that board is being arranged, so an encounter
    // with a region rolls exactly the numbers the same encounter without one
    // rolls. An encounter that authors none never enters the phase, and
    // every deployment command it is ever given is `wrong_phase`.
    encounter.state_.deploying = !encounter.state_.deployment_tiles.empty();
    return {CreateError::none, std::move(encounter)};
}

// Whether a walk from `origin` can afford `destination`: `movement_field`,
// which is the whole of the movement rule, asked about one cell. Terrain
// decides passage, the ground decides price, and `mover`'s side decides which
// characters are walls and which are squeezed past; no path is returned, so
// this answers what a move may reach rather than how it would get there.
//
// The straight-line refusal ahead of the search is not a second rule. No entry
// costs less than one, so no walk can be cheaper than its own length, and a
// destination further away than the allowance is unaffordable however the board
// is priced.
bool Encounter::reachable(
    Position origin,
    Position destination,
    std::uint8_t allowance,
    std::uint8_t crossings,
    Side mover
) const {
    if (origin == destination) return false;
    if (!in_bounds(destination, state_.width, state_.height)) return false;
    const std::uint32_t straight = distance(origin, destination);
    if (straight > allowance) return false;

    const std::vector<std::uint32_t> spent =
        movement_field(state_, origin, allowance, crossings, mover);
    const std::size_t slot =
        static_cast<std::size_t>(destination.y) * state_.width +
        static_cast<std::size_t>(destination.x);
    return spent[slot] != unreachable_cost;
}

// Objectives are evaluated in ascending identifier order after every accepted
// activation. The first decided objective sets the outcome.
void Encounter::evaluate_objectives(
    const UnitSnapshot& actor,
    CommandResult& result
) {
    Outcome decided = Outcome::ongoing;
    for (std::size_t index = 0; index < objectives_.size(); ++index) {
        const ObjectiveDefinition& objective = objectives_[index];
        ObjectiveResult& record = state_.objectives[index];
        if (record.state != ObjectiveState::pending) continue;

        const Side owner = objective.side;
        switch (objective.kind) {
            case ObjectiveKind::defeat_all_opponents:
                if (!has_living_unit(state_.units, other_side(owner))) {
                    record.state = ObjectiveState::satisfied;
                } else if (!has_living_unit(state_.units, owner)) {
                    record.state = ObjectiveState::failed;
                }
                break;
            case ObjectiveKind::defeat_target: {
                const UnitSnapshot* target =
                    find_unit(state_.units, objective.target_unit_id);
                if (target != nullptr && target->health <= 0) {
                    record.state = ObjectiveState::satisfied;
                } else if (target != nullptr && target->departed) {
                    // Talked off the board, so nobody can ever defeat them and
                    // this objective is not pending, it is lost. Deliberate,
                    // and the interesting half of the gesture: an author who
                    // writes both a kill objective and a talk mark on one
                    // character has authored a fork whose two routes exclude
                    // each other.
                    record.state = ObjectiveState::failed;
                }
                break;
            }
            case ObjectiveKind::protect_target: {
                const UnitSnapshot* target =
                    find_unit(state_.units, objective.target_unit_id);
                if (target != nullptr && target->health <= 0) {
                    record.state = ObjectiveState::failed;
                }
                // A departed character is left pending on purpose, and this is
                // the careful half. Their health will never reach zero, so the
                // objective's only transition can never fire and protecting
                // them has succeeded for good. Marking it *satisfied* would go
                // further and end the battle on the spot, because every
                // objective here is a win condition and the first one to
                // resolve decides the outcome. Talking to somebody you were
                // asked to protect would win the fight. Never failing is what
                // was earned; winning is not.
                break;
            }
            case ObjectiveKind::survive_rounds:
                // Satisfied the moment the authored round *completes*, which
                // is what `round` counts: a survive-seven objective resolves as
                // the seventh round closes, not at the start of the seventh and
                // not one command after the end of it. That boundary is why
                // `apply` asks the objectives again when a turn advance moved
                // the round.
                if (state_.round >= objective.round_count) {
                    record.state = ObjectiveState::satisfied;
                } else if (!has_living_unit(state_.units, owner)) {
                    // And failed when there is nobody left to survive,
                    // symmetrically with `defeat_all_opponents` above, and for
                    // a reason beyond the outcome: `campaign_runtime` steers
                    // its graph on objective *results*, so an objective that
                    // could only ever be satisfied or pending would be an edge
                    // an author could not write.
                    record.state = ObjectiveState::failed;
                }
                break;
        }

        if (decided != Outcome::ongoing) continue;
        if (record.state == ObjectiveState::satisfied) {
            decided = owner == Side::first ? Outcome::first_side_won
                                           : Outcome::second_side_won;
        } else if (record.state == ObjectiveState::failed) {
            decided = owner == Side::first ? Outcome::second_side_won
                                           : Outcome::first_side_won;
        }
    }

    // Backstop: a side with nothing left standing has lost, whatever its
    // objectives say. Without this an encounter whose objectives cannot be
    // decided by elimination would leave a wiped-out player on a board that
    // never ends.
    if (decided == Outcome::ongoing) {
        if (!has_living_unit(state_.units, Side::second)) {
            decided = Outcome::first_side_won;
        } else if (!has_living_unit(state_.units, Side::first)) {
            decided = Outcome::second_side_won;
        }
    }

    if (decided == Outcome::ongoing) return;
    state_.outcome = decided;
    result.events.push_back(
        {
            EventType::encounter_completed,
            actor.id,
            0,
            actor.position,
            0,
            state_.outcome
        }
    );
}

// Stands every character whose round has come on the board.
//
// The tile is the one the content asked for when it is free and this character
// could stand on it, and otherwise the nearest tile that is. See
// `nearest_standing_tile`.
//
// The three answers not taken: refusing at compile time is not available,
// because whether a tile is held in round six is a fact about a battle rather
// than about content; displacing whoever stands there needs a second movement
// rule with a second set of refusals; and skipping the arrival makes a wave
// something a player switches off by standing on a tile. In this engine a
// forecast is a promise and a danger tile is a promise, and a wave an author
// wrote is one too.
//
// A board with no free standable tile left, every passable cell held, makes the
// arrival wait, and it comes in on the first round it can. Waiting
// rather than being dropped because the alternative is a character the content
// authored quietly ceasing to exist, and because the case is self-clearing:
// a board that full is a board on which somebody is about to fall or step
// aside. So the round an arrival is authored for is the earliest it can come,
// not a deadline it can miss.
//
// Nothing here draws from any random stream.
void Encounter::land_arrivals(CommandResult& result) {
    const std::uint32_t in_progress = state_.round + 1U;
    for (UnitSnapshot& unit : state_.units) {
        if (unit.arrived || unit.arrival_round > in_progress) continue;
        if (!in_the_battle(unit)) continue;
        Position landing{};
        // Nowhere to stand at all: still marching, and asked again next round.
        // See above.
        if (!nearest_standing_tile(
                state_.units, state_.width, state_.height, state_.terrain,
                unit.position, unit.crossings, landing
            )) {
            continue;
        }
        unit.position = landing;
        unit.arrived = true;
        // The round in progress, in the `int16` every event reports a number
        // in. `maximum_arrival_round` is what makes this an exact report rather
        // than a hopeful one: no arrival may be authored past it, so the only
        // way `in_progress` could run beyond is a battle that has actually
        // played thirty-two thousand rounds with an arrival still waiting for a
        // free tile. The clamp is the backstop for that and nothing else: a
        // wrapped negative round would be a worse lie than a stuck one.
        result.events.push_back(
            {
                EventType::unit_arrived,
                unit.id,
                0,
                landing,
                static_cast<std::int16_t>(
                    in_progress > maximum_arrival_round ? maximum_arrival_round
                                                        : in_progress
                ),
                Outcome::ongoing
            }
        );
    }
}

// Opens the next activation.
//
// Under `initiative` that means picking the next actor, with ties breaking on
// the lowest unit identifier. Under `side_blocks` it means picking the next
// *side*, leaving the actor unnamed for whoever holds it to choose. Under both,
// a round ends when every living unit has acted.
void Encounter::begin_next_activation(Side previous, CommandResult& result) {
    if (state_.turn_order == TurnOrder::alternating) {
        state_.active_unit_id = 0;
        state_.remaining_action_points = 0;
        // A side with nobody standing hands the turn straight on. On a board
        // with no waves that cannot happen: a side with nobody left has lost
        // and the battle is over by the time this runs, so the loop settles on
        // its first pass. With a wave still marching the side is very much in
        // the battle and has simply nothing to spend this turn on, and leaving
        // a client holding a turn it cannot take would be the deadlock waves
        // would otherwise introduce.
        const std::size_t limit = state_.units.size() * 2U + 2U;
        Side next = other_side(previous);
        for (std::size_t attempt = 0; attempt <= limit; ++attempt) {
            state_.active_side = next;
            // A pass through an alternating turn order is one turn for each
            // side, so the round closes as the turn comes back round to the
            // side that opened the battle. Kept only where the content gives
            // the count consequence (see `EncounterSnapshot::round`), which is
            // why a board that authors neither an objective that reads it nor a
            // wave that arrives on it stands at zero.
            if (counts_rounds_ && next == Side::first) {
                ++state_.round;
                land_arrivals(result);
            }
            if (has_standing_unit(state_.units, next)) return;
            next = other_side(next);
        }
        return;
    }

    // Whose block it is, under `side_blocks`: the first side while anybody on
    // it still has a turn in hand, then the second, then the round turns. The
    // block order is read off `has_acted` alone rather than off who acted last,
    // so which block opens does not depend on the order the turns inside the
    // last one fell. And `has_acted` means *finished*, so a character who has
    // walked and not yet struck still holds the block open.
    const auto block_open = [this](Side side) {
        for (const UnitSnapshot& unit : state_.units) {
            if (unit.side == side && on_board(unit) && !unit.has_acted) {
                return true;
            }
        }
        return false;
    };

    for (int attempt = 0; attempt < 2; ++attempt) {
        if (state_.turn_order == TurnOrder::side_blocks) {
            for (const Side side : {Side::first, Side::second}) {
                if (!block_open(side)) continue;
                // The side, and deliberately not the character. Zero here is
                // what `apply` reads as "nobody has begun a turn yet, so
                // anybody eligible may", which is the whole of free selection:
                // the vocabulary was already there and this order simply stops
                // spending it.
                state_.active_side = side;
                state_.active_unit_id = 0;
                state_.remaining_action_points = 0;
                return;
            }
        } else {
            const UnitSnapshot* best = nullptr;
            for (const UnitSnapshot& unit : state_.units) {
                if (!on_board(unit) || unit.has_acted) continue;
                if (best == nullptr) {
                    best = &unit;
                    continue;
                }
                if (unit.speed > best->speed ||
                    (unit.speed == best->speed && unit.id < best->id)) {
                    best = &unit;
                }
            }
            if (best != nullptr) {
                state_.active_unit_id = best->id;
                state_.remaining_action_points = best->action_points;
                state_.active_side = best->side;
                return;
            }
        }
        // Everyone has acted: start a new round and look again. Whatever the
        // new round brings comes in before the second look, so a character who
        // arrives on this round takes its turn on it.
        for (UnitSnapshot& unit : state_.units) {
            unit.has_acted = false;
            unit.has_moved = false;
            unit.spent_action_points = 0U;
        }
        ++state_.round;
        land_arrivals(result);
    }
    // No living unit at all; the outcome check will have ended the encounter.
    state_.active_unit_id = 0;
    state_.remaining_action_points = 0;
}

CommandResult Encounter::apply(const Command& command) {
    CommandResult result;
    if (state_.outcome != Outcome::ongoing) {
        result.error = CommandError::encounter_complete;
        return result;
    }
    // The phase gate, before anything about the command's own subject, because
    // "you cannot do that yet" and "you cannot do that any more" are facts
    // about the battle rather than about a character. One refusal, symmetric:
    // an ordinary command while the phase is open and a deployment command once
    // it has closed are the same mistake seen from the two sides.
    const bool deployment_command =
        command.type == CommandType::deploy ||
        command.type == CommandType::begin_battle;
    if (deployment_command != state_.deploying) {
        result.error = CommandError::wrong_phase;
        return result;
    }
    if (command.type == CommandType::begin_battle) {
        state_.deploying = false;
        // The turn the opening deferred. An ordered board names its first actor
        // here instead of in `create_encounter`; an alternating one has nothing
        // to name, because a side's turn is whichever unit its player picks.
        if (state_.turn_order != TurnOrder::alternating) {
            // No wave can land here either: the phase closes into the first
            // round and the earliest arrival is the second.
            begin_next_activation(Side::second, result);
        }
        result.events.push_back(
            {
                EventType::deployment_ended,
                0,
                0,
                {},
                0,
                Outcome::ongoing
            }
        );
        return result;
    }
    if (command.type == CommandType::deploy) {
        UnitSnapshot* placed = find_unit(state_.units, command.unit_id);
        if (placed == nullptr) {
            result.error = CommandError::unknown_unit;
            return result;
        }
        // The same three facts the ordinary gate below asks, asked in the same
        // order and answered with the same three refusals, so that a client is
        // told which mistake it made here too. A wave is the live one: it is
        // not on the board, its `position` is the tile the content asked for
        // rather than a tile it holds, and arranging one would rewrite an
        // authored landing. `undeployable_unit` names one thing, a character
        // the content put outside the region, and would be the wrong answer
        // for any of these.
        if (placed->health <= 0) {
            result.error = CommandError::defeated_unit;
            return result;
        }
        if (placed->departed) {
            result.error = CommandError::departed_unit;
            return result;
        }
        if (!placed->arrived) {
            result.error = CommandError::unarrived_unit;
            return result;
        }
        if (placed->side != state_.active_side) {
            result.error = CommandError::wrong_side;
            return result;
        }
        if (!is_deployable(state_, *placed)) {
            result.error = CommandError::undeployable_unit;
            return result;
        }
        if (!in_bounds(command.destination, state_.width, state_.height)) {
            result.error = CommandError::invalid_destination;
            return result;
        }
        if (!in_deployment_zone(state_.deployment_tiles, command.destination)) {
            result.error = CommandError::outside_zone;
            return result;
        }
        if (occupied_by_other(
                state_.units, command.destination, placed->id
            )) {
            result.error = CommandError::occupied_destination;
            return result;
        }
        placed->position = command.destination;
        result.events.push_back(
            {
                EventType::unit_deployed,
                placed->id,
                0,
                placed->position,
                0,
                Outcome::ongoing
            }
        );
        return result;
    }
    UnitSnapshot* unit = find_unit(state_.units, command.unit_id);
    if (unit == nullptr) {
        result.error = CommandError::unknown_unit;
        return result;
    }
    // Who may act is `on_board`, spelled out here in the order a client is
    // owed: three separate facts about the character it named, each with its
    // own refusal, rather than one silent no. Under `initiative` and
    // `side_blocks` the engine picks the actor and picks only from the board,
    // so these answer a caller naming somebody the engine did not; under
    // `alternating` the caller picks every time, and these are the whole of
    // the gate.
    if (unit->health <= 0) {
        result.error = CommandError::defeated_unit;
        return result;
    }
    // Somebody who was talked off the board cannot act: they are not on it.
    // Beside the defeat check rather than folded into it, and in the same order
    // the target checks below take: a departure is not a defeat, and a
    // character who walked away must not be able to walk back, swing, be
    // countered and be buried.
    if (unit->departed) {
        result.error = CommandError::departed_unit;
        return result;
    }
    // Somebody who has not come in yet cannot act either, for the plain reason
    // that they are not standing anywhere to act from.
    if (!unit->arrived) {
        result.error = CommandError::unarrived_unit;
        return result;
    }
    if (unit->side != state_.active_side) {
        result.error = CommandError::wrong_side;
        return result;
    }
    // Somebody who has already had their turn this round cannot have another.
    // Above `activation_in_progress` because it is a fact about the character
    // the command named, and the refusals about the command's own subject are
    // answered in subject order; `activation_in_progress` is a fact about the
    // battle. Inert under alternating order, where nobody is ever marked.
    if (unit->has_acted) {
        result.error = CommandError::already_acted;
        return result;
    }
    // Under `alternating` and `initiative` a side commits to one unit per
    // activation: those orders hand out one turn at a time, and a second unit
    // acting inside somebody else's turn would make "two action points" mean
    // "two units".
    //
    // Under `side_blocks` it does mean that, and on purpose. A block is not an
    // activation but the whole side's turn, so turns inside it interleave
    // freely: walk one character, walk a second, come back and strike with the
    // first. Nobody is locked in by having started, so nothing here is claimed
    // and `activation_in_progress` cannot fire. What each character may still
    // do is its own state, counted below and refused by `no_action_points`,
    // `already_moved` and `already_acted`, refusals that name the character
    // the player picked instead of an actor they never chose.
    //
    // This order is deliberate, chosen over the classic model where moving
    // commits you to a unit until you attack or wait. The classic model is
    // what `alternating` is, and the reason it is not this order too is what
    // playing on a cartridge showed: a player who moved a character with a
    // point in hand found every other character refused, with nothing on
    // screen saying that attacking or waiting was the way out.
    if (state_.turn_order == TurnOrder::side_blocks) {
        if (unit->spent_action_points >= turn_budget(*unit)) {
            result.error = CommandError::no_action_points;
            return result;
        }
    } else {
        if (state_.active_unit_id != 0 && state_.active_unit_id != unit->id) {
            result.error = CommandError::activation_in_progress;
            return result;
        }
        if (state_.active_unit_id == unit->id &&
            state_.remaining_action_points == 0) {
            result.error = CommandError::no_action_points;
            return result;
        }
    }

    if (command.type == CommandType::move) {
        // One walk per activation, whatever the points say. Answered before
        // anything about the destination, because this is a fact about the
        // character rather than about the tile it named: the same tile would
        // be refused whichever one it was.
        if (unit->has_moved) {
            result.error = CommandError::already_moved;
            return result;
        }
        if (!in_bounds(command.destination, state_.width, state_.height)) {
            result.error = CommandError::invalid_destination;
            return result;
        }
        // Nobody lands on anybody, whichever side they are on. `movement_field`
        // says the same thing, and it is asked first here so that a tile a
        // character could have squeezed past earns the refusal that names what
        // is wrong with it rather than the one that says the walk was too far.
        if (occupied(state_.units, command.destination)) {
            result.error = CommandError::occupied_destination;
            return result;
        }
        if (!reachable(
                unit->position, command.destination, unit->movement,
                unit->crossings, unit->side
            )) {
            result.error = CommandError::invalid_destination;
            return result;
        }
        unit->position = command.destination;
        unit->has_moved = true;
        result.events.push_back(
            {
                EventType::unit_moved,
                unit->id,
                0,
                unit->position,
                0,
                Outcome::ongoing
            }
        );
    } else if (command.type == CommandType::attack) {
        StrikeProfile strike;
        const CommandError weapon_error =
            resolve_strike(*unit, weapons_, command.weapon_id, strike);
        if (weapon_error != CommandError::none) {
            result.error = weapon_error;
            return result;
        }
        UnitSnapshot* target = find_unit(state_.units, command.target_id);
        if (target == nullptr) {
            result.error = CommandError::unknown_target;
            return result;
        }
        if (target->health <= 0) {
            result.error = CommandError::target_defeated;
            return result;
        }
        // Somebody who was talked off the board cannot be struck either, and
        // the refusal says which of the two things happened to them. Ahead of
        // the friendly-fire question because it is a fact about the board
        // rather than about the sides.
        if (target->departed) {
            result.error = CommandError::target_departed;
            return result;
        }
        // And nobody can swing at somebody who has not come in yet, for the
        // same reason and with its own name.
        if (!target->arrived) {
            result.error = CommandError::target_unarrived;
            return result;
        }
        if (target->side == unit->side) {
            result.error = CommandError::friendly_target;
            return result;
        }
        const std::uint32_t separation =
            distance(unit->position, target->position);
        if (!within_reach(
                separation, strike.minimum_reach, strike.maximum_reach
            )) {
            result.error = CommandError::target_out_of_range;
            return result;
        }
        // The one roll this strike gets, taken here: after every refusal, so
        // an attack the engine would not accept never moves the stream, and
        // before any damage, so the roll cannot depend on its own outcome. The
        // chance is the weapon's accuracy with both units folded into it, and a
        // folded hundred draws nothing at all.
        if (!roll_hit(
                state_.random, hit_chance_for(*unit, *target, strike.accuracy)
            )) {
            result.events.push_back(
                {
                    EventType::attack_missed,
                    target->id,
                    unit->id,
                    target->position,
                    0,
                    Outcome::ongoing
                }
            );
        } else {
            const std::int16_t damage = attack_damage(*unit, *target, strike);
            take_damage(*target, unit->id, damage, result);
            if (target->health == 0) {
                record_defeat(state_, *target, unit->id, result);
            }
        }
        // The counter, gated on the target still standing, which a missed
        // strike guarantees. A blow that misses is still a blow you were in
        // range of, and Fire Emblem answers it; only a felled defender is
        // silent. Its own roll comes second, always, and only from here: a
        // counter that cannot happen never reaches this line and so never
        // moves the stream.
        if (counters(*target, target->health, separation)) {
            // The counter, resolved inside the same command that provoked it.
            //
            // It is the same arithmetic struck the other way, so it can kill:
            // clamping it short would be a second damage formula, and a
            // forecast promising a survivable exchange that could not fell you
            // is the more useful promise only if it is true.
            //
            // It provokes nothing in return. A counter is a consequence of an
            // attack command, not an attack command itself, so there is one
            // exchange per command and two units cannot annihilate each other
            // over a single press.
            //
            // It costs the defender nothing: no action point, no place in the
            // turn order. Charging for it would make defending yourself spend a
            // turn you have not been offered yet, and would put the turn order
            // at the mercy of who attacked whom.
            //
            // It can miss, on the same terms and against the accuracy of the
            // weapon the defender has in hand, the same weapon the rule
            // already says it answers with, folded the other way round, with
            // the defender now the striker and the attacker now the struck.
            const StrikeProfile answer = equipped_strike(*target);
            if (!roll_hit(
                    state_.random,
                    hit_chance_for(*target, *unit, answer.accuracy)
                )) {
                result.events.push_back(
                    {
                        EventType::attack_missed,
                        unit->id,
                        target->id,
                        unit->position,
                        0,
                        Outcome::ongoing
                    }
                );
            } else {
                const std::int16_t back =
                    attack_damage(*target, *unit, answer);
                take_damage(*unit, target->id, back, result);
                if (unit->health == 0) {
                    record_defeat(state_, *unit, target->id, result);
                }
            }
        }
    } else if (command.type == CommandType::ability) {
        // An ability provokes no counterattack, deliberately. Final Fantasy
        // Tactics' gate is "the unit which attacked you", and an ability is not
        // one unit attacking one unit: an area cast covering four opponents
        // would turn one command into five strikes, and a restoring cast would
        // have a healed ally answering its own medic. Casting is therefore the
        // safe way to spend an activation, which is a trade against a weapon's
        // damage rather than a loophole.
        const AbilityDefinition* ability =
            find_ability(abilities_, command.ability_id);
        if (ability == nullptr) {
            result.error = CommandError::unknown_ability;
            return result;
        }
        if (!owns_ability(*unit, command.ability_id)) {
            result.error = CommandError::unavailable_ability;
            return result;
        }
        if (!in_bounds(command.destination, state_.width, state_.height)) {
            result.error = CommandError::invalid_destination;
            return result;
        }
        if (!within_reach(
                distance(unit->position, command.destination),
                ability->minimum_reach,
                ability->maximum_reach
            )) {
            result.error = CommandError::target_out_of_range;
            return result;
        }

        // Units are already stored in ascending identifier order, so collecting
        // in place makes the affected order independent of definition order.
        const UnitId actor_id = unit->id;
        // A restoring cast can catch the caster, so `affected` can be the very
        // unit `unit` points at. Only health is written below, and the caster's
        // side, magic, skill and luck are read rather than written, so what it
        // casts with cannot change on the way round the loop.
        const UnitSnapshot& caster = *unit;
        for (UnitSnapshot& affected : state_.units) {
            // Nobody the area covers who is not standing in it. A cast names a
            // tile rather than a character, so a departed character earns no
            // refusal here: they are simply not there to be caught, which is
            // the same thing the tile-occupancy rule already says about them.
            if (!on_board(affected)) continue;
            if (!covered_by(
                    ability->area,
                    ability->radius,
                    command.destination,
                    affected.position
                )) {
                continue;
            }
            // **A damaging cast harms the caster's opponents and nobody else.**
            // An ally standing in the blast, and the caster standing in its own,
            // are covered by it and take nothing from it: the cast is aimed at
            // ground, so catching your own line is a thing the shape does rather
            // than a thing the rule does.
            //
            // Here, over the covered characters, rather than as a refusal of the
            // command. A cast names a tile and not a character, so there is no
            // aim to turn down. Refusing every centre whose cover happened to
            // include an ally would take away most of the board on a crowded one
            // and would make a diamond unusable in a line. Spared rather than
            // refused is also what keeps the shape honest: the tile is still
            // covered, `area_tiles` still draws it, and what changed is only who
            // the cover costs anything.
            //
            // It is asked of every shape alike, `AreaShape::single` included and
            // most of all. A one-tile damaging cast aimed next door is the thing
            // hardest to tell from a sword swing, and a sword swing already
            // refuses an ally by name, so the two gestures a player cannot
            // distinguish agree about the one thing they must.
            //
            // Ahead of the roll rather than after it: a cast that cannot harm
            // somebody does not roll to see whether it would have. That keeps
            // the hit stream's Nth number the Nth *uncertain* thing this battle
            // resolved, which is the audit `roll_hit` states and the reason a
            // cast's consumption is checkable at all.
            if (ability->kind != AbilityKind::restore &&
                affected.side == caster.side) {
                continue;
            }
            if (ability->kind == AbilityKind::restore) {
                // Mercy asks no side, and deliberately: a restoring cast mends
                // whoever is standing in it. There is nothing to protect
                // anybody from, so the reason the damaging half asks does not
                // arise, and an author who wants a cast that heals only one
                // side is asking for a shape rather than for a rule.
                const std::int16_t missing = static_cast<std::int16_t>(
                    affected.maximum_health - affected.health
                );
                const std::int16_t restored = static_cast<std::int16_t>(
                    std::min<std::int32_t>(missing, ability->power)
                );
                if (restored <= 0) continue;
                affected.health = static_cast<std::int16_t>(
                    affected.health + restored
                );
                result.events.push_back(
                    {
                        EventType::unit_restored,
                        affected.id,
                        actor_id,
                        affected.position,
                        restored,
                        Outcome::ongoing
                    }
                );
                continue;
            }

            // One roll per opponent the area damages, in the ascending
            // identifier order this loop already walks. Only opponents get
            // this far, so an ally in the blast neither misses nor lands: it
            // is not in the exchange at all, and it moves no stream.
            if (!roll_hit(
                    state_.random,
                    hit_chance_for(caster, affected, ability->accuracy)
                )) {
                result.events.push_back(
                    {
                        EventType::attack_missed,
                        affected.id,
                        actor_id,
                        affected.position,
                        0,
                        Outcome::ongoing
                    }
                );
                continue;
            }
            const std::int16_t damage =
                ability_damage(caster, *ability, affected);
            take_damage(affected, actor_id, damage, result);
            if (affected.health == 0) {
                // Ascending unit identifier order comes free: this loop already
                // walks `state_.units`, which is sorted. `roll_drop` states
                // that rule for the drop stream exactly as `roll_hit` states it
                // for the hit stream.
                record_defeat(state_, affected, actor_id, result);
            }
        }
        // `unit` may have been invalidated by the loop above only if the vector
        // reallocated, which it cannot: no element is inserted or removed.
        unit = find_unit(state_.units, actor_id);
        if (unit == nullptr) {
            result.error = CommandError::unknown_unit;
            return result;
        }
    } else if (command.type == CommandType::use_item) {
        // Everything about the item is decided before anything about the
        // target, exactly as a strike resolves its weapon first.
        const ItemDefinition* item = nullptr;
        std::size_t slot = 0;
        const CommandError refusal =
            resolve_use(*unit, items_, command.item_id, item, slot);
        if (refusal != CommandError::none) {
            result.error = refusal;
            return result;
        }
        // An item reaches the hand that holds it. A use that names somebody
        // else is out of range rather than invalid, so the day an item authors
        // a reach band this refusal keeps meaning what it means now.
        if (command.target_id != 0 && command.target_id != unit->id) {
            if (find_unit(state_.units, command.target_id) == nullptr) {
                result.error = CommandError::unknown_target;
                return result;
            }
            result.error = CommandError::target_out_of_range;
            return result;
        }

        // Spent first, and spent whatever it did: a draught drunk at full
        // health is a draught gone, which is the whole reason the forecast
        // shows the number before the player commits. The engine refusing to
        // let a player waste something would be a second rule about what a
        // good move is, and a restoring cast at full health already does not
        // have one.
        unit->item_counts[slot] =
            static_cast<std::uint16_t>(unit->item_counts[slot] - 1U);
        result.events.push_back(
            {
                EventType::item_used,
                unit->id,
                unit->id,
                unit->position,
                static_cast<std::int16_t>(unit->item_counts[slot]),
                Outcome::ongoing,
                item->id
            }
        );
        if (item->kind == ItemKind::restore) {
            const std::int16_t missing = static_cast<std::int16_t>(
                unit->maximum_health - unit->health
            );
            const std::int16_t restored = static_cast<std::int16_t>(
                std::min<std::int32_t>(missing, item->power)
            );
            if (restored > 0) {
                unit->health =
                    static_cast<std::int16_t>(unit->health + restored);
                result.events.push_back(
                    {
                        EventType::unit_restored,
                        unit->id,
                        unit->id,
                        unit->position,
                        restored,
                        Outcome::ongoing
                    }
                );
            }
        }
    } else if (command.type == CommandType::talk) {
        // Every refusal first, in the order that names what the player got
        // wrong, and only then the one consequence. Nothing below the last
        // refusal can fail, which is what makes `forecast_talk` a promise
        // rather than an estimate.
        UnitSnapshot* target = find_unit(state_.units, command.target_id);
        if (command.target_id == 0 || target == nullptr) {
            result.error = CommandError::unknown_target;
            return result;
        }
        if (target->health <= 0) {
            result.error = CommandError::target_defeated;
            return result;
        }
        if (target->departed) {
            result.error = CommandError::target_departed;
            return result;
        }
        if (!target->arrived) {
            result.error = CommandError::target_unarrived;
            return result;
        }
        if (target->talk_record_id == 0) {
            result.error = CommandError::not_talkable;
            return result;
        }
        // Adjacency, and nothing authored decides it. A talk is a conversation,
        // standing next to somebody is the genre's answer, and it is the band a
        // bare hand already uses. No number is authored, none is hashed, and
        // every client already knows how to draw the tiles it covers. A talk
        // aimed at the talker is refused here too, at distance zero, which is
        // the rule saying by itself that a conversation takes two.
        if (distance(unit->position, target->position) != talk_reach) {
            result.error = CommandError::target_out_of_range;
            return result;
        }

        // Off the board, alive, carrying the health they had. No
        // `unit_defeated` beside this and no health written to zero: see
        // `UnitSnapshot::departed` for why both of those are the whole point.
        target->departed = true;
        result.events.push_back(
            {
                EventType::unit_talked,
                target->id,
                unit->id,
                target->position,
                0,
                Outcome::ongoing,
                target->talk_record_id
            }
        );
    } else if (command.type == CommandType::wait) {
        result.events.push_back(
            {
                EventType::unit_waited,
                unit->id,
                0,
                unit->position,
                0,
                Outcome::ongoing
            }
        );
    } else {
        result.error = CommandError::invalid_command;
        return result;
    }

    ++state_.activation_count;

    // Waiting is an explicit "I am done", the deliberate way a player says
    // "nothing more from this one", and it finishes the character outright
    // rather than only closing off its action: a character who waits with a
    // walk still in hand is a character the player has said they are finished
    // with, and leaving it choosable would make the row mean nothing. Striking
    // ends the turn too unless the unit is authored to keep acting afterwards,
    // and spending an item is priced with the strikes rather than with the
    // move: it is the one thing the character does with its turn, so a draught
    // costs what a cast costs. Pricing it below a cast would make drinking free
    // and turn every considered turn into "drink, then act".
    const bool struck = command.type == CommandType::attack ||
                        command.type == CommandType::ability ||
                        command.type == CommandType::use_item ||
                        command.type == CommandType::talk;
    const bool closes_the_turn = command.type == CommandType::wait ||
                                 (struck && !unit->acts_after_attacking) ||
                                 unit->health <= 0;

    // Spend the point. Under `side_blocks` the character spends from its own
    // budget, because several characters on the open side may be part-way
    // through their turns at once and one side-wide counter cannot say what any
    // of them has left. Under the other two orders the side holds one
    // activation at a time, so the side-wide counter is the whole of it and
    // begins on the first accepted command. `initiative` has already named the
    // actor, so that claim only ever fires under `alternating`.
    bool finished = false;
    if (state_.turn_order == TurnOrder::side_blocks) {
        const std::uint8_t budget = turn_budget(*unit);
        if (unit->spent_action_points < budget) {
            unit->spent_action_points =
                static_cast<std::uint8_t>(unit->spent_action_points + 1U);
        }
        if (closes_the_turn) unit->spent_action_points = budget;
        finished = unit->spent_action_points >= budget;
    } else {
        if (state_.active_unit_id == 0) {
            state_.active_unit_id = unit->id;
            state_.remaining_action_points = unit->action_points;
        }
        if (state_.remaining_action_points > 0) {
            state_.remaining_action_points =
                static_cast<std::uint8_t>(state_.remaining_action_points - 1U);
        }
        if (closes_the_turn) state_.remaining_action_points = 0U;
        finished = state_.remaining_action_points == 0;
    }

    const UnitSnapshot actor = *unit;
    const Side opponent = other_side(actor.side);

    if (objectives_.empty()) {
        // No authored objectives means the v0 rule: the acting side wins when
        // the opposing side has no living unit. The second branch is new with
        // counterattacks, and it is not a nicety: an activation can now fell
        // the unit that made it, so for the first time the *acting* side can be
        // the one emptied, and without this the survivor would be left on a
        // board that never ends. Only one of the two can fire, because a
        // counter needs a defender who lived.
        Outcome decided = Outcome::ongoing;
        if (!has_living_unit(state_.units, opponent)) {
            decided = actor.side == Side::first ? Outcome::first_side_won
                                                : Outcome::second_side_won;
        } else if (!has_living_unit(state_.units, actor.side)) {
            decided = actor.side == Side::first ? Outcome::second_side_won
                                                : Outcome::first_side_won;
        }
        if (decided != Outcome::ongoing) {
            state_.outcome = decided;
            result.events.push_back(
                {
                    EventType::encounter_completed,
                    actor.id,
                    0,
                    actor.position,
                    0,
                    state_.outcome
                }
            );
        }
    } else {
        evaluate_objectives(actor, result);
    }

    if (state_.outcome != Outcome::ongoing) {
        state_.active_unit_id = 0;
        state_.remaining_action_points = 0;
        // A battle that ends mid-turn ends those turns with it, so the walk
        // allowance and the spent count go back the way they do on every other
        // path. A finished encounter takes no more commands and this changes
        // nothing a rule can read. It is here so that the state a save carries
        // and the hash it is checked against say the same thing about a closed
        // board however it closed.
        //
        // Everybody, not only the character who struck the last blow: under
        // `side_blocks` several characters can be part-way through their turns
        // when the board closes, and a closed board carrying half a dozen
        // half-spent turns would be a closed board that still had state in it.
        // Under the other two orders only the actor can be part-way through
        // one, so clearing them all is clearing exactly what the actor's line
        // cleared before.
        for (UnitSnapshot& resting : state_.units) {
            resting.has_moved = false;
            resting.spent_action_points = 0U;
        }
        return result;
    }
    if (finished) {
        result.events.push_back(
            {
                EventType::activation_ended,
                actor.id,
                0,
                actor.position,
                0,
                Outcome::ongoing
            }
        );
        // has_acted is documented as always false under alternating order,
        // where a round is a turn for each side rather than a pass over the
        // characters, so there is nothing per character to mark.
        if (UnitSnapshot* acted = find_unit(state_.units, actor.id)) {
            if (state_.turn_order != TurnOrder::alternating) {
                acted->has_acted = true;
            }
            // The walk allowance is per turn, so it is given back the moment
            // the turn closes, under every turn order, because every turn
            // order closes turns. Clearing it here, with the spent count beside
            // it, is what keeps a character between turns carrying no turn
            // state at all, which is the whole reason the canonical hash can
            // fold both only when they are set.
            acted->has_moved = false;
            acted->spent_action_points = 0U;
        }
        const std::uint32_t round_before = state_.round;
        begin_next_activation(actor.side, result);
        // The turn advance may have closed a round, and one of the objectives
        // may be about exactly that: `survive_rounds` resolves at a round
        // boundary rather than after a blow, so asking only before the advance
        // would report the win a command late.
        //
        // Asking again costs nothing where nothing is about a round: every
        // other kind reads health and departure, neither of which a turn
        // advance touches, and an objective already decided is skipped. So the
        // second answer is the answer just given, on every board that authors
        // no count. Guarded on the round having moved, which is a fact about
        // the battle rather than about what it authors.
        if (state_.round != round_before && !objectives_.empty()) {
            evaluate_objectives(actor, result);
            if (state_.outcome != Outcome::ongoing) {
                state_.active_unit_id = 0;
                state_.remaining_action_points = 0;
            }
        }
    }
    return result;
}

// Checks run in apply()'s order so the reported refusal is the one the real
// command would earn. tests/simulation lock the two together case by case.
AttackForecast forecast_attack(
    const EncounterSnapshot& snapshot,
    UnitId attacker_id,
    UnitId target_id
) noexcept {
    return forecast_attack(snapshot, attacker_id, target_id, {}, 0);
}

AttackForecast forecast_attack(
    const EncounterSnapshot& snapshot,
    UnitId attacker_id,
    UnitId target_id,
    const std::vector<WeaponDefinition>& weapons,
    ContentId weapon_id
) noexcept {
    AttackForecast forecast;
    if (snapshot.outcome != Outcome::ongoing) {
        forecast.error = CommandError::encounter_complete;
        return forecast;
    }
    // The phase, in the same position `apply` takes it and for the same
    // reason. There is no strike to price before the battle has begun, and a
    // number shown here would be a promise nothing was going to deliver.
    if (snapshot.deploying) {
        forecast.error = CommandError::wrong_phase;
        return forecast;
    }
    const UnitSnapshot* attacker = find_unit(snapshot.units, attacker_id);
    if (attacker == nullptr) {
        forecast.error = CommandError::unknown_unit;
        return forecast;
    }
    if (attacker->health <= 0) {
        forecast.error = CommandError::defeated_unit;
        return forecast;
    }
    // The rest of `apply`'s actor gate, in `apply`'s order. Somebody who is not
    // on the board cannot swing, so pricing a blow for them would be pricing a
    // command the engine is about to refuse, and the promise this whole query
    // makes is that it does not do that.
    if (attacker->departed) {
        forecast.error = CommandError::departed_unit;
        return forecast;
    }
    if (!attacker->arrived) {
        forecast.error = CommandError::unarrived_unit;
        return forecast;
    }
    if (attacker->side != snapshot.active_side) {
        forecast.error = CommandError::wrong_side;
        return forecast;
    }
    if (attacker->has_acted) {
        forecast.error = CommandError::already_acted;
        return forecast;
    }
    // Both refusals as `apply` asks them, and both order-agnostic without
    // needing to be told the order: a snapshot taken under `side_blocks`
    // carries no named actor, so the first can never fire, and `points_left`
    // reads the character's own count there and the side-wide one under the
    // orders that keep one.
    if (snapshot.active_unit_id != 0 &&
        snapshot.active_unit_id != attacker->id) {
        forecast.error = CommandError::activation_in_progress;
        return forecast;
    }
    if (points_left(snapshot, *attacker) == 0) {
        forecast.error = CommandError::no_action_points;
        return forecast;
    }
    StrikeProfile strike;
    const CommandError weapon_error =
        resolve_strike(*attacker, weapons, weapon_id, strike);
    if (weapon_error != CommandError::none) {
        forecast.error = weapon_error;
        return forecast;
    }
    const UnitSnapshot* target = find_unit(snapshot.units, target_id);
    if (target == nullptr) {
        forecast.error = CommandError::unknown_target;
        return forecast;
    }
    if (target->health <= 0) {
        forecast.error = CommandError::target_defeated;
        return forecast;
    }
    // In apply's order: a character who has walked away, and a character who
    // has not come in yet, cannot be aimed at, so the forecast says which
    // rather than pricing a blow nothing would land. A departed character keeps
    // the health it had and is marked arrived, so it falls through every other
    // test here and lands in the pricing unless this asks.
    if (target->departed) {
        forecast.error = CommandError::target_departed;
        return forecast;
    }
    if (!target->arrived) {
        forecast.error = CommandError::target_unarrived;
        return forecast;
    }
    if (target->side == attacker->side) {
        forecast.error = CommandError::friendly_target;
        return forecast;
    }
    const std::uint32_t separation =
        distance(attacker->position, target->position);
    if (!within_reach(
            separation, strike.minimum_reach, strike.maximum_reach
        )) {
        forecast.error = CommandError::target_out_of_range;
        return forecast;
    }
    // The chance, and then the numbers behind it. `hit_chance` is the weapon's
    // accuracy with both units folded into it, not scaled, not averaged
    // against a second roll, not rounded to something friendlier, because it
    // is the very number `Encounter::apply` hands to `roll_chance`, computed by
    // the very same function. Reporting the weapon's authored accuracy here
    // while apply rolled the folded number would be the two-roll dishonesty
    // this repository refused, reached by a different road. A surface that
    // draws this draws the rule.
    forecast.hit_chance = hit_chance_for(*attacker, *target, strike.accuracy);
    forecast.damage = attack_damage(*attacker, *target, strike);
    // The floor is asked for here rather than assumed to be zero, and it is
    // asked of the same function `take_damage` asks. A character who cannot be
    // reduced below one health is forecast as ending at one and not as lethal,
    // which is exactly what applying the same strike in the same state will do.
    // Clamping in apply alone would show a player a killing blow and then leave
    // the character standing, which is the promise this whole query is for.
    forecast.target_health_after = static_cast<std::int16_t>(
        std::max<std::int32_t>(floor_of(*target), target->health - forecast.damage)
    );
    forecast.lethal = forecast.target_health_after == 0;

    // The half a player is buying, not just the half they are selling. apply()
    // resolves the counter inside the same command, so a forecast that stopped
    // at the outgoing damage would be promising less than the command does,
    // and the promise is the whole reason this query exists.
    //
    // It is derived here exactly as apply() derives it: the same gate, with the
    // same formula struck the other way. What the miss adds is one word in the
    // gate. apply() asks the target's health *after* the strike, and a strike
    // that can miss leaves the target standing whenever it misses, so a
    // sub-certain lethal blow is still answered, on the rolls where it fails.
    // A *certain* lethal strike forecasts no counter, exactly as before, which
    // is the arithmetic saying what the rule says.
    forecast.attacker_health_after = attacker->health;
    const std::int16_t survivor_health =
        forecast.hit_chance >= hit_chance_bound
            ? forecast.target_health_after
            : target->health;
    if (counters(*target, survivor_health, separation)) {
        forecast.counter = true;
        const StrikeProfile answer = equipped_strike(*target);
        forecast.counter_chance =
            hit_chance_for(*target, *attacker, answer.accuracy);
        forecast.counter_damage = attack_damage(*target, *attacker, answer);
        forecast.attacker_health_after = static_cast<std::int16_t>(
            std::max<std::int32_t>(
                floor_of(*attacker), attacker->health - forecast.counter_damage
            )
        );
        forecast.counter_lethal = forecast.attacker_health_after == 0;
    }
    return forecast;
}

// The same discipline as `forecast_attack`, in apply()'s order again, and a
// shorter promise: nothing here rolls, so the number shown is the number
// delivered rather than the number delivered on the rolls that land.
ItemForecast forecast_item(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    UnitId target_id,
    const std::vector<ItemDefinition>& items,
    ContentId item_id
) noexcept {
    ItemForecast forecast;
    if (snapshot.outcome != Outcome::ongoing) {
        forecast.error = CommandError::encounter_complete;
        return forecast;
    }
    // The phase, exactly as the attack forecast takes it: nothing is spent
    // before the battle begins, so there is nothing to price.
    if (snapshot.deploying) {
        forecast.error = CommandError::wrong_phase;
        return forecast;
    }
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    if (unit == nullptr) {
        forecast.error = CommandError::unknown_unit;
        return forecast;
    }
    if (unit->health <= 0) {
        forecast.error = CommandError::defeated_unit;
        return forecast;
    }
    // The rest of `apply`'s actor gate, in `apply`'s order. Nobody off the
    // board drinks anything, so there is no draught to price for them.
    if (unit->departed) {
        forecast.error = CommandError::departed_unit;
        return forecast;
    }
    if (!unit->arrived) {
        forecast.error = CommandError::unarrived_unit;
        return forecast;
    }
    if (unit->side != snapshot.active_side) {
        forecast.error = CommandError::wrong_side;
        return forecast;
    }
    if (unit->has_acted) {
        forecast.error = CommandError::already_acted;
        return forecast;
    }
    if (snapshot.active_unit_id != 0 && snapshot.active_unit_id != unit->id) {
        forecast.error = CommandError::activation_in_progress;
        return forecast;
    }
    if (points_left(snapshot, *unit) == 0) {
        forecast.error = CommandError::no_action_points;
        return forecast;
    }
    const ItemDefinition* item = nullptr;
    std::size_t slot = 0;
    const CommandError refusal = resolve_use(*unit, items, item_id, item, slot);
    if (refusal != CommandError::none) {
        forecast.error = refusal;
        return forecast;
    }
    if (target_id != 0 && target_id != unit->id) {
        forecast.error = find_unit(snapshot.units, target_id) == nullptr
                             ? CommandError::unknown_target
                             : CommandError::target_out_of_range;
        return forecast;
    }
    forecast.kind = item->kind;
    forecast.remaining_after =
        static_cast<std::uint16_t>(unit->item_counts[slot] - 1U);
    forecast.target_health_after = unit->health;
    if (item->kind == ItemKind::restore) {
        const std::int16_t missing =
            static_cast<std::int16_t>(unit->maximum_health - unit->health);
        forecast.restored = static_cast<std::int16_t>(
            std::min<std::int32_t>(missing, item->power)
        );
        forecast.target_health_after =
            static_cast<std::int16_t>(unit->health + forecast.restored);
    }
    return forecast;
}

// apply()'s refusals in apply()'s order, and then the shortest promise in the
// engine: a talk that gets past them delivers one departure and nothing else.
// There is no number to roll, no clamp to apply and no second half to resolve,
// so `error == none` *is* the forecast. See `TalkForecast`.
TalkForecast forecast_talk(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    UnitId target_id
) noexcept {
    TalkForecast forecast;
    if (snapshot.outcome != Outcome::ongoing) {
        forecast.error = CommandError::encounter_complete;
        return forecast;
    }
    if (snapshot.deploying) {
        forecast.error = CommandError::wrong_phase;
        return forecast;
    }
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    if (unit == nullptr) {
        forecast.error = CommandError::unknown_unit;
        return forecast;
    }
    if (unit->health <= 0) {
        forecast.error = CommandError::defeated_unit;
        return forecast;
    }
    // The rest of `apply`'s actor gate, in `apply`'s order. A conversation
    // takes two people standing a tile apart, so somebody off the board starts
    // none, and this query's whole promise is that what it accepts, `apply`
    // accepts.
    if (unit->departed) {
        forecast.error = CommandError::departed_unit;
        return forecast;
    }
    if (!unit->arrived) {
        forecast.error = CommandError::unarrived_unit;
        return forecast;
    }
    if (unit->side != snapshot.active_side) {
        forecast.error = CommandError::wrong_side;
        return forecast;
    }
    if (unit->has_acted) {
        forecast.error = CommandError::already_acted;
        return forecast;
    }
    if (snapshot.active_unit_id != 0 && snapshot.active_unit_id != unit->id) {
        forecast.error = CommandError::activation_in_progress;
        return forecast;
    }
    if (points_left(snapshot, *unit) == 0) {
        forecast.error = CommandError::no_action_points;
        return forecast;
    }
    const UnitSnapshot* target = find_unit(snapshot.units, target_id);
    if (target_id == 0 || target == nullptr) {
        forecast.error = CommandError::unknown_target;
        return forecast;
    }
    if (target->health <= 0) {
        forecast.error = CommandError::target_defeated;
        return forecast;
    }
    if (target->departed) {
        forecast.error = CommandError::target_departed;
        return forecast;
    }
    if (!target->arrived) {
        forecast.error = CommandError::target_unarrived;
        return forecast;
    }
    if (target->talk_record_id == 0) {
        forecast.error = CommandError::not_talkable;
        return forecast;
    }
    if (distance(unit->position, target->position) != talk_reach) {
        forecast.error = CommandError::target_out_of_range;
        return forecast;
    }
    forecast.departing_id = target->id;
    forecast.record_id = target->talk_record_id;
    return forecast;
}

std::uint8_t action_points_left(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
) noexcept {
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    if (unit == nullptr) return 0U;
    return points_left(snapshot, *unit);
}

std::vector<Position> reachable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
) {
    std::vector<Position> tiles;
    if (snapshot.width == 0 || snapshot.height == 0) return tiles;
    // Nothing is reachable before the battle begins, because every move command
    // is refused for its phase and this query's whole contract is that a tile
    // is in it exactly when a move to it would not be refused. The rule that
    // *is* in force has its own query.
    if (snapshot.deploying) return tiles;
    // Nor by somebody who is not on the board. `on_board` rather than
    // `health > 0`, because `apply`'s actor gate is `on_board`: a character
    // talked off the board and a character whose wave has not landed each have
    // every move command refused by name, so painting a range for either would
    // offer a walk the board will not take.
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    if (unit == nullptr || !on_board(*unit)) return tiles;
    // Nor is anything reachable by somebody who has already walked this turn,
    // who has already had their whole turn, or who has no point left to spend
    // on a walk, for the same reason: this query's contract is that a tile is
    // in it exactly when a move to it would not be refused, and every one of
    // those moves is refused by name. A client painting a range here would be
    // offering a walk the board will not take.
    if (unit->has_moved || unit->has_acted) return tiles;
    if (points_left(snapshot, *unit) == 0) return tiles;

    const std::vector<std::uint32_t> spent = movement_field(
        snapshot, unit->position, unit->movement, unit->crossings, unit->side
    );
    for (std::uint16_t y = 0; y < snapshot.height; ++y) {
        for (std::uint16_t x = 0; x < snapshot.width; ++x) {
            const Position tile{
                static_cast<std::int16_t>(x),
                static_cast<std::int16_t>(y)
            };
            if (tile == unit->position) continue;
            const std::size_t slot =
                static_cast<std::size_t>(y) * snapshot.width + x;
            if (spent[slot] != unreachable_cost) tiles.push_back(tile);
        }
    }
    return tiles;
}

bool is_deployable(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& unit
) noexcept {
    // `on_board` rather than `health > 0`, and the arrival half of it is the
    // load-bearing one: a wave's `position` is the tile the content asked for
    // and not a tile it holds, so arranging one would rewrite an authored
    // landing. And since occupancy is `on_board` too, the tile it was authored
    // on stays offered to everybody else, so accepting one would put two
    // characters on one square.
    return snapshot.deploying && on_board(unit) &&
           unit.side == Side::first &&
           in_deployment_zone(snapshot.deployment_tiles, unit.position);
}

std::vector<Position> deployable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
) {
    std::vector<Position> tiles;
    if (!snapshot.deploying) return tiles;
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    if (unit == nullptr || !is_deployable(snapshot, *unit) ||
        unit->side != snapshot.active_side) {
        return tiles;
    }
    // The region is already sorted row-major, so walking it in order gives the
    // row-major result every other tile query in this engine gives without a
    // second sort. The only thing left to judge is who is standing where, asked
    // the way `apply` asks it, so the tile this character is already on is
    // offered back to it.
    for (const Position tile : snapshot.deployment_tiles) {
        if (occupied_by_other(snapshot.units, tile, unit->id)) continue;
        tiles.push_back(tile);
    }
    return tiles;
}

namespace {

// Marks every in-bounds tile whose separation from any marked stance falls
// inside one band. Factored out so a unit's own band, a carried weapon's band,
// and an ability's reach are all swept by the same code.
void mark_threatened_band(
    const EncounterSnapshot& snapshot,
    const std::vector<std::uint32_t>& stances,
    std::uint8_t minimum_reach,
    std::uint8_t maximum_reach,
    std::vector<std::uint8_t>& threatened
) {
    for (std::uint16_t y = 0; y < snapshot.height; ++y) {
        for (std::uint16_t x = 0; x < snapshot.width; ++x) {
            const std::size_t slot =
                static_cast<std::size_t>(y) * snapshot.width + x;
            if (stances[slot] == unreachable_cost) continue;
            const Position stance{
                static_cast<std::int16_t>(x),
                static_cast<std::int16_t>(y)
            };
            const auto maximum = static_cast<std::int32_t>(maximum_reach);
            for (std::int32_t dy = -maximum; dy <= maximum; ++dy) {
                const std::int32_t band = maximum - (dy < 0 ? -dy : dy);
                for (std::int32_t dx = -band; dx <= band; ++dx) {
                    const Position struck{
                        static_cast<std::int16_t>(stance.x + dx),
                        static_cast<std::int16_t>(stance.y + dy)
                    };
                    if (!in_bounds(struck, snapshot.width, snapshot.height)) {
                        continue;
                    }
                    if (!within_reach(
                            distance(stance, struck),
                            minimum_reach,
                            maximum_reach
                        )) {
                        continue;
                    }
                    threatened[
                        static_cast<std::size_t>(struck.y) * snapshot.width +
                        static_cast<std::size_t>(struck.x)
                    ] = 1U;
                }
            }
        }
    }
}

// How far past its aim tile an enumerated area reaches. The union of areas
// centred anywhere in a reach band is itself a band widened by this on both
// sides, which is why an area needs no separate sweep.
std::uint8_t area_spill(AreaShape shape, std::uint8_t radius) noexcept {
    switch (shape) {
        case AreaShape::single: return 0U;
        case AreaShape::cross: return 1U;
        case AreaShape::diamond: return radius;
    }
    return 0U;
}

// The points a unit will have when it next acts, before the turn comes back
// round to the side being warned: what it has left when it is part-way through
// its turn, none when it has already finished this round, and its whole budget
// otherwise. Under alternating order no unit is ever marked as having acted, so
// every living unit answers with its full budget, which is right: any one of
// them may be the unit its side picks.
//
// `points_left` is the same question `apply` and the forecasts ask, so a danger
// overlay drawn from this and a command the engine accepts cannot disagree
// about what a half-spent character can still do.
std::uint8_t coming_action_points(
    const EncounterSnapshot& snapshot,
    const UnitSnapshot& unit
) noexcept {
    return points_left(snapshot, unit);
}

// How far a unit may walk before it strikes: its movement allowance, once,
// and only if it has a point to spare from the strike. One point means it may
// move or strike but not both, so a striker never leaves its tile. A character
// who has already walked this turn cannot be anywhere but where it stands,
// however many points are left.
//
// One walk per turn is the rule, so three points threaten exactly what two do,
// and the warning says so rather than multiplying the allowance by every point
// past the first as it would if every point could buy another walk.
std::uint8_t stance_allowance(
    const UnitSnapshot& unit,
    std::uint8_t points
) noexcept {
    if (points <= 1U || unit.has_moved) return 0U;
    return unit.movement;
}

std::vector<Position> collect_threatened(
    const EncounterSnapshot& snapshot,
    const std::vector<std::uint8_t>& threatened
) {
    std::vector<Position> tiles;
    for (std::uint16_t y = 0; y < snapshot.height; ++y) {
        for (std::uint16_t x = 0; x < snapshot.width; ++x) {
            const std::size_t slot =
                static_cast<std::size_t>(y) * snapshot.width + x;
            if (threatened[slot] != 0U) {
                tiles.push_back(
                    {
                        static_cast<std::int16_t>(x),
                        static_cast<std::int16_t>(y)
                    }
                );
            }
        }
    }
    return tiles;
}

}  // namespace

std::vector<Position> danger_tiles(
    const EncounterSnapshot& snapshot,
    Side side
) {
    if (snapshot.width == 0 || snapshot.height == 0) return {};
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    std::vector<std::uint8_t> threatened(cells, 0U);

    for (const UnitSnapshot& unit : snapshot.units) {
        // `on_board` rather than a health test, because a warning is about
        // tiles and only somebody standing on one threatens anything: a
        // character talked off the board shades nothing, and neither does a
        // wave that has not come in: it stands nowhere, so it reaches nowhere.
        // The overlay is a promise about the board as it is, and drawing the
        // tiles a wave *will* threaten would be a promise about a board that
        // does not exist yet and whose occupancy decides where the wave lands.
        if (unit.side != side || !on_board(unit)) continue;
        const std::uint8_t points = coming_action_points(snapshot, unit);
        if (points == 0U) continue;
        // Everywhere the unit could be standing when it strikes: its own tile,
        // which `movement_field` prices at nothing, plus every tile it can
        // afford to walk to and still have a point left to strike
        // with. The same field a walk is judged against, so the warning and the
        // walk cannot come to different answers about what the ground costs.
        const std::vector<std::uint32_t> stances = movement_field(
            snapshot, unit.position, stance_allowance(unit, points),
            unit.crossings, unit.side
        );
        mark_threatened_band(
            snapshot, stances, unit.minimum_reach, unit.maximum_reach,
            threatened
        );
    }
    return collect_threatened(snapshot, threatened);
}

std::vector<Position> danger_tiles(
    const EncounterSnapshot& snapshot,
    Side side,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
) {
    if (snapshot.width == 0 || snapshot.height == 0) return {};
    const std::size_t cells =
        static_cast<std::size_t>(snapshot.width) * snapshot.height;
    std::vector<std::uint8_t> threatened(cells, 0U);

    for (const UnitSnapshot& unit : snapshot.units) {
        // `on_board` rather than a health test, because a warning is about
        // tiles and only somebody standing on one threatens anything: a
        // character talked off the board shades nothing, and neither does a
        // wave that has not come in: it stands nowhere, so it reaches nowhere.
        // The overlay is a promise about the board as it is, and drawing the
        // tiles a wave *will* threaten would be a promise about a board that
        // does not exist yet and whose occupancy decides where the wave lands.
        if (unit.side != side || !on_board(unit)) continue;
        const std::uint8_t points = coming_action_points(snapshot, unit);
        if (points == 0U) continue;
        const std::vector<std::uint32_t> stances = movement_field(
            snapshot, unit.position, stance_allowance(unit, points),
            unit.crossings, unit.side
        );
        // Bare-handed, or carrying only identities this caller cannot
        // resolve, the unit still threatens the band it snapshots with.
        bool any_weapon = false;
        for (const ContentId id : unit.weapon_ids) {
            const WeaponDefinition* weapon = find_weapon(weapons, id);
            if (weapon == nullptr) continue;
            any_weapon = true;
            // Widened by the unit's own bonus, exactly as the strike this tile
            // is a warning about would be. A danger overlay that drew the
            // authored band would understate the archer it is warning about.
            mark_threatened_band(
                snapshot, stances, weapon->minimum_reach,
                widened_reach(weapon->maximum_reach, unit.reach_bonus),
                threatened
            );
        }
        if (!any_weapon) {
            mark_threatened_band(
                snapshot, stances, unit.minimum_reach, unit.maximum_reach,
                threatened
            );
        }
        for (const ContentId id : unit.ability_ids) {
            const AbilityDefinition* ability = find_ability(abilities, id);
            if (ability == nullptr) continue;
            // Restoring is not a danger, whatever it can reach.
            if (ability->kind != AbilityKind::damage) continue;
            const std::uint8_t spill =
                area_spill(ability->area, ability->radius);
            const auto minimum = static_cast<std::uint8_t>(
                ability->minimum_reach > spill
                    ? ability->minimum_reach - spill
                    : 0U
            );
            const auto maximum = static_cast<std::uint8_t>(std::min<int>(
                255, static_cast<int>(ability->maximum_reach) + spill
            ));
            mark_threatened_band(
                snapshot, stances, minimum, maximum, threatened
            );
        }
    }
    return collect_threatened(snapshot, threatened);
}

namespace {

// Everything that refuses a gesture before it has been aimed at anything,
// asked in `Encounter::apply`'s own order so a character the board would turn
// down lights no tile at all. `forecast_strike` asks exactly these questions
// and in exactly this sequence; they are gathered here because all four
// gestures share them, and because sharing them is what makes "lit exactly when
// accepted" one sentence rather than four.
//
// The finished-battle gate is `apply`'s very first refusal and is honoured here
// even though `reachable_tiles` does not restate it. That is not a divergence
// worth smoothing over: a move on a completed board really is refused, so the
// stricter answer is the true one, and a client is not aiming on a board that
// has ended anyway.
[[nodiscard]] const UnitSnapshot* aiming_unit(
    const EncounterSnapshot& snapshot,
    UnitId unit_id
) noexcept {
    if (snapshot.width == 0 || snapshot.height == 0) return nullptr;
    if (snapshot.outcome != Outcome::ongoing) return nullptr;
    // Every gesture below is an ordinary command, and an ordinary command while
    // the board is being arranged is refused for its phase.
    if (snapshot.deploying) return nullptr;
    const UnitSnapshot* unit = find_unit(snapshot.units, unit_id);
    // `on_board` rather than a health test, for the reason it is stated once:
    // somebody who has walked out of the conversation, and somebody whose wave
    // has not landed, are each refused by name before anything they aimed at is
    // looked at.
    if (unit == nullptr || !on_board(*unit)) return nullptr;
    if (unit->side != snapshot.active_side) return nullptr;
    if (unit->has_acted) return nullptr;
    if (snapshot.active_unit_id != 0 && snapshot.active_unit_id != unit->id) {
        return nullptr;
    }
    if (points_left(snapshot, *unit) == 0) return nullptr;
    return unit;
}

// Sweeps the band around one stance, row-major within it, handing every
// in-bounds tile inside `[minimum, maximum]` to a predicate that says whether
// the gesture would be accepted there.
//
// Row-major within the band is row-major over the board, because the band is a
// contiguous run of rows and each row is swept left to right, so no second
// sort is needed to give the order every other tile query gives.
template <typename Accepts>
[[nodiscard]] std::vector<Position> band_around(
    const EncounterSnapshot& snapshot,
    Position stance,
    std::uint8_t minimum,
    std::uint8_t maximum,
    Accepts accepts
) {
    std::vector<Position> tiles;
    const auto span = static_cast<std::int32_t>(maximum);
    for (std::int32_t dy = -span; dy <= span; ++dy) {
        const std::int32_t width = span - (dy < 0 ? -dy : dy);
        for (std::int32_t dx = -width; dx <= width; ++dx) {
            const Position tile{
                static_cast<std::int16_t>(stance.x + dx),
                static_cast<std::int16_t>(stance.y + dy)
            };
            if (!in_bounds(tile, snapshot.width, snapshot.height)) continue;
            if (!within_reach(distance(stance, tile), minimum, maximum)) {
                continue;
            }
            if (!accepts(tile)) continue;
            tiles.push_back(tile);
        }
    }
    return tiles;
}

// Whoever is standing on a tile and can still be aimed at, or nothing. The same
// occupancy question `commit_aim` on every console asks of the board, answered
// here so no client has to.
[[nodiscard]] const UnitSnapshot* occupant_of(
    const EncounterSnapshot& snapshot,
    Position tile
) noexcept {
    for (const UnitSnapshot& unit : snapshot.units) {
        if (on_board(unit) && unit.position == tile) return &unit;
    }
    return nullptr;
}

}  // namespace

bool gesture_available(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    const AimedGesture& gesture,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
) noexcept {
    const UnitSnapshot* unit = aiming_unit(snapshot, unit_id);
    if (unit == nullptr) return false;
    switch (gesture.kind) {
        case Gesture::walk:
            // One walk per activation, whatever the points say. `apply`
            // answers it before anything about the destination, which is
            // exactly what makes it a fact about the gesture rather than about
            // the board.
            return !unit->has_moved;
        case Gesture::strike: {
            StrikeProfile strike;
            return resolve_strike(*unit, weapons, gesture.weapon_id, strike) ==
                   CommandError::none;
        }
        case Gesture::cast:
            return find_ability(abilities, gesture.ability_id) != nullptr &&
                   owns_ability(*unit, gesture.ability_id);
        case Gesture::talk:
            // Nothing about a talk is refused before its target is looked at.
            // A board with nobody to talk to still permits the gesture; who is
            // standing next to you is the aim, and `aimable_tiles` answers it.
            return true;
    }
    return false;
}

std::vector<Position> aimable_tiles(
    const EncounterSnapshot& snapshot,
    UnitId unit_id,
    const AimedGesture& gesture,
    const std::vector<WeaponDefinition>& weapons,
    const std::vector<AbilityDefinition>& abilities
) {
    // Structural rather than restated: a gesture the character cannot make
    // lights nothing, and this is the one line that says so.
    if (!gesture_available(snapshot, unit_id, gesture, weapons, abilities)) {
        return {};
    }
    const UnitSnapshot* unit = aiming_unit(snapshot, unit_id);
    if (unit == nullptr) return {};

    switch (gesture.kind) {
        case Gesture::walk:
            // The whole of the walk rule, including the second refusal this
            // query has no business restating: a character who has already
            // walked reaches nowhere, and `reachable_tiles` is where that is
            // written down.
            return reachable_tiles(snapshot, unit_id);
        case Gesture::strike: {
            StrikeProfile strike;
            if (resolve_strike(*unit, weapons, gesture.weapon_id, strike) !=
                CommandError::none) {
                return {};
            }
            return band_around(
                snapshot, unit->position, strike.minimum_reach,
                strike.maximum_reach,
                [&](Position tile) {
                    const UnitSnapshot* target = occupant_of(snapshot, tile);
                    // Only an opponent, and only one still standing. A strike
                    // names a character rather than a tile, so empty ground is
                    // not aimable however close it is, which is exactly the
                    // `unknown_target` a confirm there already earns.
                    return target != nullptr && target->health > 0 &&
                           target->side != unit->side;
                }
            );
        }
        case Gesture::cast: {
            const AbilityDefinition* ability =
                find_ability(abilities, gesture.ability_id);
            if (ability == nullptr) return {};
            if (!owns_ability(*unit, gesture.ability_id)) return {};
            // Every in-bounds tile in the band, occupied or not: a cast names
            // ground, and casting at empty ground is accepted. A client that
            // lit only the occupied tiles would be hiding the restoring cast
            // aimed to catch two allies and the blast aimed to spare one.
            return band_around(
                snapshot, unit->position, ability->minimum_reach,
                ability->maximum_reach, [](Position) { return true; }
            );
        }
        case Gesture::talk:
            return band_around(
                snapshot, unit->position,
                static_cast<std::uint8_t>(talk_reach),
                static_cast<std::uint8_t>(talk_reach),
                [&](Position tile) {
                    const UnitSnapshot* target = occupant_of(snapshot, tile);
                    // Whoever has something to say, on either side, because
                    // the talk rule asks nothing about sides. `on_board` has
                    // already turned away the fallen and the departed.
                    return target != nullptr && target->talk_record_id != 0;
                }
            );
    }
    return {};
}

std::vector<Position> area_tiles(
    const EncounterSnapshot& snapshot,
    ContentId ability_id,
    Position centre,
    const std::vector<AbilityDefinition>& abilities
) {
    const AbilityDefinition* ability = find_ability(abilities, ability_id);
    if (ability == nullptr) return {};
    if (ability->area == AreaShape::single) return {};
    const std::uint8_t spill = area_spill(ability->area, ability->radius);
    // The spill is how far the shape reaches, so sweeping a band of that width
    // and asking `covered_by` at each tile visits every covered tile and
    // nothing beyond it. `covered_by` is the membership test `apply` uses, so a
    // drawn splash and a caught character cannot disagree.
    std::vector<Position> tiles;
    const auto span = static_cast<std::int32_t>(spill);
    for (std::int32_t dy = -span; dy <= span; ++dy) {
        for (std::int32_t dx = -span; dx <= span; ++dx) {
            const Position tile{
                static_cast<std::int16_t>(centre.x + dx),
                static_cast<std::int16_t>(centre.y + dy)
            };
            if (!in_bounds(tile, snapshot.width, snapshot.height)) continue;
            if (!covered_by(ability->area, ability->radius, centre, tile)) {
                continue;
            }
            tiles.push_back(tile);
        }
    }
    return tiles;
}

EncounterSnapshot Encounter::snapshot() const {
    return state_;
}

std::uint64_t Encounter::canonical_hash() const noexcept {
    return simulation::canonical_hash(state_);
}

std::uint64_t canonical_hash(const EncounterSnapshot& state) noexcept {
    std::uint64_t hash = 14695981039346656037ULL;
    hash_integer(hash, state.width);
    hash_integer(hash, state.height);
    hash_integer(hash, static_cast<std::uint8_t>(state.active_side));
    hash_integer(hash, state.active_unit_id);
    hash_integer(hash, state.remaining_action_points);
    hash_integer(hash, state.round);
    hash_integer(hash, state.activation_count);
    hash_integer(hash, static_cast<std::uint8_t>(state.outcome));
    hash_integer(hash, static_cast<std::uint32_t>(state.units.size()));
    for (const UnitSnapshot& unit : state.units) {
        hash_integer(hash, unit.id);
        hash_integer(hash, unit.unit_type_id);
        hash_integer(hash, static_cast<std::uint8_t>(unit.side));
        hash_integer(hash, unit.position.x);
        hash_integer(hash, unit.position.y);
        hash_integer(hash, unit.health);
        hash_integer(hash, unit.maximum_health);
        hash_integer(hash, unit.strength);
        hash_integer(hash, unit.power);
        hash_integer(hash, unit.defense);
        hash_integer(hash, unit.resistance);
        hash_integer(hash, unit.movement);
        hash_integer(hash, unit.action_points);
        hash_integer(hash, unit.speed);
        hash_integer(hash, static_cast<std::uint8_t>(unit.acts_after_attacking ? 1U : 0U));
        hash_integer(hash, static_cast<std::uint8_t>(unit.has_acted ? 1U : 0U));
        hash_integer(hash, unit.minimum_reach);
        hash_integer(hash, unit.maximum_reach);
        hash_integer(hash, static_cast<std::uint32_t>(unit.ability_ids.size()));
        for (const ContentId ability : unit.ability_ids) {
            hash_integer(hash, ability);
        }
        // Carried weapons are canonical state: two units holding the same
        // weapon but carrying different ones can strike different tiles.
        hash_integer(hash, static_cast<std::uint32_t>(unit.weapon_ids.size()));
        for (const ContentId weapon : unit.weapon_ids) {
            hash_integer(hash, weapon);
        }
        // What a unit may cross is canonical state: two otherwise identical
        // units, one of which flies, do not have the same moves.
        hash_integer(hash, unit.crossings);
        // And how often what it holds lands, for the same reason: two
        // otherwise identical units, one holding a weapon that misses one
        // swing in ten, are not in the same battle. This is the accuracy of
        // the weapon in hand; the accuracies of the rest of what the unit
        // carries reach the hash through the strikes they roll.
        hash_integer(hash, unit.accuracy);
        // The four stats that decide whether a blow lands and what a cast is
        // worth in this unit's hands. Canonical for the same reason accuracy
        // is: two units differing only in how often they are hit are not in
        // the same battle, even though nothing on the board looks different.
        // Appended here, after everything that came before, so the reason a
        // hash moved is always the last thing added rather than a reshuffle.
        hash_integer(hash, unit.skill);
        hash_integer(hash, unit.luck);
        hash_integer(hash, unit.evasion);
        hash_integer(hash, unit.magic);
        // What the unit still carries in its pack, appended for the same
        // reason and in the same place: last. The counts matter as much as the
        // identities: two units carrying the same draught, one of which has
        // already drunk it, are not in the same battle. So a use moves the
        // hash exactly like a wound does, and a replay that drinks the same
        // draught at the same moment arrives at the same satchel.
        hash_integer(hash, static_cast<std::uint32_t>(unit.item_ids.size()));
        for (std::size_t i = 0; i < unit.item_ids.size(); ++i) {
            hash_integer(hash, unit.item_ids[i]);
            hash_integer(hash, unit.item_counts[i]);
        }
        // What this unit would leave behind, appended last for the same
        // reason everything before it was: two otherwise identical units, one
        // of which can leave a tonic, are not in the same battle: the one that
        // can draws from the drop stream when it falls and the one that cannot
        // does not.
        hash_integer(hash, unit.drop_item_id);
        hash_integer(hash, unit.drop_chance);
        // A character's six optional parts, announced by one byte before any of
        // them is folded.
        //
        // Each is absent from almost every board: nobody talks, nobody
        // arrives, nobody endures, and between turns nobody has walked or
        // spent a point of a turn. Folding all six unconditionally would spend
        // bytes stating six things no content is saying. Folding them behind
        // bare guards spends nothing, and states nothing either: bytes with
        // nothing ahead of them to say which part they came from are not an
        // encoding. `endures`, `has_moved`, one point spent of a turn and one
        // tile of bonus reach each fold a single one at a single offset, so
        // four different boards become one battle. And because
        // `derive_random_seed` takes this number, it is one battle with one
        // sequence of hits and misses. **A hash that cannot tell two battles
        // apart is not identifying a battle, which is the one job it has.**
        //
        // So the byte below says which parts are present and the parts follow
        // it. The mask fixes the width of everything after it, so two
        // characters differing in any of the six write different bytes, and it
        // is folded whether or not anything is present, which is the whole of
        // its value. A mask behind a guard of its own would be this same defect
        // one level up, with the absence of the mask now the thing the stream
        // does not say. The price is one byte per character on every board
        // there is, and therefore one move of every hash this repository pins.
        // `core::hash_random_state` pays the same price for the same reason,
        // folding a count ahead of a sparse list of stream positions.
        std::uint8_t present = 0U;
        // Who can be talked to, and who already has been. Two otherwise
        // identical battles, one of which has had the captain talked off the
        // board, are not the same battle, and a mid-battle save must resume
        // with him still gone.
        if (unit.talk_record_id != 0) present |= present_talk_record;
        if (unit.departed) present |= present_departed;
        // When this character comes in, and whether it has. Two otherwise
        // identical battles, one of which still has a wave marching, are not
        // the same battle: one of them is over and the other is not. A
        // mid-battle save must resume with the wave still coming.
        if (unit.arrival_round != 0U) present |= present_arrival_round;
        if (!unit.arrived) present |= present_unarrived;
        // Whether this character can be felled at all, which is canonical for a
        // stronger reason than most: two otherwise identical battles, one of
        // which nobody can be lost in, are not the same battle: the objectives
        // that end by elimination cannot end the same way, and no sequence of
        // commands makes the two agree.
        if (unit.endures) present |= present_endures;
        // Whether it has already spent this turn's one walk. Two otherwise
        // identical boards, one of whose acting characters has already walked,
        // are not the same battle: one can still be repositioned and the other
        // cannot. A save taken mid-turn must resume with the walk spent.
        if (unit.has_moved) present |= present_walk_spent;
        // How much of its budget this character's turn has spent. Non-zero only
        // under `side_blocks`, and only for somebody part-way through a turn;
        // where it is non-zero it decides what that character may still do, so
        // a save has to carry it.
        if (unit.spent_action_points != 0U) present |= present_points_spent;
        // And what this character adds to the reach of whatever it holds.
        //
        // The bonus is already inside `maximum_reach` above, for the weapon in
        // hand, and it would be tempting to let that stand for it. That is the
        // standard the accuracies of the weapons *not* in hand are held to, and
        // it is a real standard rather than a saving. It fails here on one
        // case, and one is enough: `widened_reach` saturates, so a character
        // whose in-hand band already reaches 250 snapshots the same 255 with a
        // bonus of ten as with a bonus of twenty, while a second carried weapon
        // resolves a band ten tiles apart between them. Two battles telling
        // themselves apart by a strike one may make and the other may not are
        // two battles, so the number is folded rather than inferred from a
        // field that cannot always carry it.
        if (unit.reach_bonus != 0U) present |= present_reach_bonus;
        hash_byte(hash, present);
        // And then the four parts a single bit cannot carry.
        if ((present & present_talk_record) != 0U) {
            hash_integer(hash, unit.talk_record_id);
        }
        if ((present & present_arrival_round) != 0U) {
            hash_integer(hash, unit.arrival_round);
        }
        if ((present & present_points_spent) != 0U) {
            hash_integer(hash, unit.spent_action_points);
        }
        if ((present & present_reach_bonus) != 0U) {
            hash_integer(hash, unit.reach_bonus);
        }
    }
    hash_integer(hash, static_cast<std::uint32_t>(state.objectives.size()));
    for (const ObjectiveResult& objective : state.objectives) {
        hash_integer(hash, objective.id);
        hash_integer(hash, static_cast<std::uint8_t>(objective.state));
    }
    // The board itself, for the same reason: the same units on the same tiles
    // with a river between them are not the same encounter. An all-open board
    // hashes its empty list, so a package that says nothing about terrain
    // still hashes to one definite value.
    hash_integer(hash, static_cast<std::uint32_t>(state.terrain.size()));
    for (const Terrain cell : state.terrain) {
        hash_integer(hash, static_cast<std::uint8_t>(cell));
    }
    // And what the ground charges, for the same reason once more: the same
    // units on the same tiles with a marsh between them are not the same
    // encounter, and no sequence of commands makes the two agree.
    //
    // Behind the guard the fields above `state.terrain` keep, and here the
    // guard is a statement rather than a saving. **A board that charges one for
    // every cell is a board with no price on it**, and it folds nothing, so a
    // board painted entirely in fast ground hashes exactly what the identical
    // board with no cost list at all hashes. The two are the same battle in
    // every answer this engine can give about them, so they are one hash.
    const bool priced = std::any_of(
        state.movement_cost.begin(),
        state.movement_cost.end(),
        [](std::uint8_t cell) { return cell != movement_cost_step; }
    );
    if (priced) {
        hash_integer(
            hash, static_cast<std::uint32_t>(state.movement_cost.size())
        );
        for (const std::uint8_t cell : state.movement_cost) {
            hash_integer(hash, cell);
        }
    }
    // What has fallen, in the order it fell. It is state a rule produced from a
    // roll, so it belongs in the hash as much as a health total does: two
    // battles that rolled differently over the same commands are two different
    // battles, and a mid-battle save that resumes must resume with what has
    // already dropped still dropped.
    hash_integer(hash, static_cast<std::uint32_t>(state.drops.size()));
    for (const DropRecord& drop : state.drops) {
        hash_integer(hash, drop.unit_id);
        hash_integer(hash, drop.claimant_id);
        hash_integer(hash, drop.item_id);
    }
    // The dice, because they were the newest field and appending keeps every
    // earlier field at the offset it already had. A stream nothing has drawn
    // from contributes nothing, so naming a new purpose in
    // `core::RandomStream` moves no hash here; only a seed or an actual draw
    // does.
    hash = core::hash_random_state(hash, state.random);
    // And the deployment phase, behind a guard that is the whole point of
    // it: **the region is hashed while it is a rule, and not afterwards.**
    //
    // While the phase is open it is very much state. A battle waiting to be
    // arranged and the same battle already arranged are two different things,
    // a save taken in the first resumes in the first, and the region decides
    // which commands the first will accept, so the marker and the tiles are
    // both here.
    //
    // The moment `begin_battle` is accepted the region can no longer influence
    // anything: no rule reads it, no command consults it, and the board is
    // whatever the player left. From there this function stops before the
    // block, and an arranged board hashes exactly as the identically-arranged
    // board an author could have written by hand. That equality is deliberate
    // and it is worth more than the distinction it gives up: it makes the
    // compatibility claim total rather than partial. A zoneless encounter's
    // hash is computed from bytes this code cannot reach; a zoned encounter
    // whose player takes the line the content authored produces the very same
    // battle, the very same derived seed, and therefore the very same rolls.
    // Nothing downstream of a region (a golden, a growth roll, a drop) moves
    // because a region was added and nobody moved.
    //
    // Who is arrangeable is not hashed, because it is not stored: it is read
    // off the region and the positions, both of which are already here.
    if (state.deploying) {
        hash_integer(
            hash, static_cast<std::uint32_t>(state.deployment_tiles.size())
        );
        for (const Position tile : state.deployment_tiles) {
            hash_integer(hash, tile.x);
            hash_integer(hash, tile.y);
        }
    }
    // And who acts next, appended last and behind a guard of its own.
    //
    // The guard is safe where it sits rather than by announcement, which is
    // what separates it from a character's optional parts above: this is the
    // very last thing folded, so an absent byte here cannot be read as the
    // start of anything, and the deployment block above it is the same.
    //
    // **This one is the rule every advance reads**, which makes it the field
    // with the least excuse for being left out and the easiest to leave out:
    // nothing about a board's opening arrangement looks different for it. Two
    // boards laid out identically, one under `side_blocks` and one under
    // `alternating`, hand the turn to different characters and accept opposite
    // commands from the very first activation, and no sequence of commands
    // makes them agree. Unfolded they would share an opening hash and, since
    // the seed is derived from it, their dice. `initiative` would be
    // told apart incidentally, by naming its first actor into `active_unit_id`;
    // `side_blocks` names none, so nothing would tell it apart at all. A hash
    // that cannot tell two battles apart is not identifying a battle, which is
    // the one job it has.
    //
    // `alternating` is the default every board that never mentions an order
    // has, and such a board folds nothing here; the other two fold their own
    // value, so all three are distinct.
    if (state.turn_order != TurnOrder::alternating) {
        hash_integer(hash, static_cast<std::uint8_t>(state.turn_order));
    }
    return hash;
}

}  // namespace grandleon::simulation
