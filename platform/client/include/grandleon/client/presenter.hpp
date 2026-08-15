// SPDX-License-Identifier: MIT
#pragma once

#include <grandleon/package_runtime/dialogue.hpp>
#include <grandleon/simulation/encounter.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace grandleon::client {

// What a player asked for, independent of how they asked for it.
//
// This is the seam the whole client is built around. A terminal reads a line, a
// window reads a click, and a console reads a stick and two buttons; all three
// produce the same small vocabulary. Nothing below this point knows which.
//
// It is also what controller support and co-operative play will need: a
// controller produces intents, and a second player is another source of them.
enum class IntentKind : std::uint8_t {
    none = 0,
    quit,
    redraw,
    list_units,
    show_state,
    help,
    move_to,
    attack,
    wait,
    // Cast one of the unit's abilities at a target tile. Which tile an area
    // covers is the engine's business; the intent only names the choice.
    ability,
    // Spend one of the unit's carried items. There is no tile and no target to
    // name: an item reaches the hand that holds it, so this commits on the
    // press rather than handing the player back a cursor to aim with.
    use_item,
    // Stand one arrangeable character on one tile of the deployment region.
    // Only meaningful while the phase is open, and the engine says so rather
    // than the front end: a front end that offers it late is refused by name.
    deploy_to,
    // Close the deployment phase and open the battle. Deliberately a separate
    // intent rather than a `wait` reused: a player who has finished arranging
    // has done something no ordinary turn can express.
    begin_battle,
    // Talk to the adjacent character `target_id` names. Unlike a use, this one
    // does name somebody: a talk reaches a neighbour rather than the hand that
    // holds it, so the front end has a target to collect before it commits.
    talk,
};

struct Intent final {
    IntentKind kind{IntentKind::none};
    simulation::UnitId unit_id{};
    simulation::UnitId target_id{};
    simulation::Position destination{};
    simulation::ContentId ability_id{};
    // Which carried weapon an attack uses. Zero means the weapon in hand, so a
    // front end that offers no choice needs to say nothing.
    simulation::ContentId weapon_id{};
    // Which carried item a use spends. There is no in-hand default here: a
    // pack has no first entry the rules read, so a use naming nothing is
    // refused by the engine rather than guessed at here.
    simulation::ContentId item_id{};
};

// Which of `side`'s characters still owes the board an activation, or zero
// when the side owes none.
//
// This is what "end the side's turn" is made of, and it is one function rather
// than one per client because the answer is a fact about the board. A front end
// that offers the gesture asks this, sends the `wait` it names, and asks again
// after the engine has answered; when it answers zero, the side has nothing
// left to spend and the turn has already passed on.
//
// It is not a rule and adds none. Every field it reads is the engine's own, and
// the order it walks the roster in is the order the engine itself picks an
// actor in under `side_blocks`. A front end draining a side therefore commands
// exactly the characters the engine would otherwise have waited for, and never
// one the engine would refuse.
//
// Zero on a board that is not this side's to act on, on a board still being
// arranged, and on a finished one. It also honours an activation already open:
// under the orders that name an actor, only that actor may be commanded, so
// only that actor is ever named here.
[[nodiscard]] simulation::UnitId unfinished_unit(
    const simulation::EncounterSnapshot& snapshot, simulation::Side side
) noexcept;

// Whether this character's turn should be closed for it, because the board has
// nothing left to offer it.
//
// One function rather than one per front end, for the reason `unfinished_unit`
// is one: the answer is a fact about the board, and four clients each deciding
// what "nothing left to do" means is four games. Every clause below is the
// engine's own answer, and none of them is a rule stated a second time here.
//
// **What counts as something to do.** A gesture is something to do when
// `aimable_tiles` is non-empty, which is exactly "a command committing this
// gesture there would be accepted":
//
//   * a walk still to take, which is the clause that keeps this from firing on
//     a character the player has merely not moved yet;
//   * a strike, with the weapon in hand or with any other the character
//     carries, that reaches somebody;
//   * a talk somebody beside it would answer;
//   * a cast that would change anybody (see below).
//
// And an item is something to do when `forecast_item` accepts spending it *on
// the character itself*, which is the only place an item reaches. A restoring
// item that would restore nothing is not: the engine forecasts that zero, and
// says in `ItemForecast::restored` that the number is there to stop the item
// being spent for it.
//
// **A cast is judged by whether it would change anybody, and that is the one
// clause that needs more than an empty list.** A strike's aimable tiles are the
// tiles somebody strikeable is standing on, so an empty list is already "nobody
// in reach". A cast's are the whole band, occupied or not. That is deliberate,
// so a player can see where a spell would land before choosing. Reading that
// list alone would make every character who knows a spell permanently busy. So
// each tile of the band is put to `area_tiles`, the engine's own membership
// test, and the cast counts when somebody the cast would change is standing in
// the cover: one of the caster's opponents for a damaging cast, which harms
// nobody on its own side, and somebody short of full health for a restoring
// one, which is the `restored <= 0` clause `apply` itself skips on. A blast
// whose only company is the caster's own line changes nobody and is not
// something to do.
//
// **It is deliberately about "no action at all" and not "the obvious one is
// unavailable".** Ending a turn takes a decision away from a player, so
// everything the engine would accept counts, including a strike the player
// would never make. False for a character the player could not command anyway,
// because there is no turn of its to close: the other side's, one already
// finished, one locked out by somebody else's open activation, one still being
// arranged.
[[nodiscard]] bool nothing_left_to_do(
    const simulation::EncounterSnapshot& snapshot,
    simulation::UnitId unit_id,
    const std::vector<simulation::WeaponDefinition>& weapons,
    const std::vector<simulation::AbilityDefinition>& abilities,
    const std::vector<simulation::ItemDefinition>& items
);

// ---------------------------------------------------------------------------
// The aiming cursor
//
// A pick that names a character turns the cursor into a target chooser: it
// rests only on tiles the engine lit, and a press moves it to another of them.
// A player who has taken ATTACK out of the menu is answering "which of these",
// and a cursor that could wander onto open ground is a cursor that has to be
// swept across the board to find the one square the engine will accept.
//
// The three functions below decide where it lands, and they decide it once for
// every front end that aims. **None of them re-derives what a gesture can
// reach**: they are handed the engine's own `aimable_tiles` and choose among
// its entries. A front end that spelled the choice itself would be the second
// copy of a rule, which is the drift this file exists to prevent.
//
// They are pure arithmetic over a tile list, so a host test pins them off the
// hardware and every console inherits the answer.
// ---------------------------------------------------------------------------

// Whether an aimed gesture names a character rather than a place, and so
// whether its cursor chooses from a list instead of moving over ground.
//
// **The engine draws the line and this only reads it.** A strike lights the
// tile of every opposing character inside its band and a talk lights the tile
// of every neighbour with something to say. Their lit sets *are* the
// candidates, and stepping between them is the whole of the choice. A walk
// lights `reachable_tiles` and a cast lights its whole band whether or not
// anybody is standing in it. Their lit sets are ground, a destination is
// somewhere rather than someone, and a cursor that jumped between tiles would
// be answering a question the player is not being asked.
//
// Both kinds still refuse to commit anywhere the engine would refuse; what
// differs is only what a press on the pad does between the row and the confirm.
[[nodiscard]] constexpr bool gesture_names_a_character(
    simulation::Gesture kind
) noexcept {
    return kind == simulation::Gesture::strike ||
           kind == simulation::Gesture::talk;
}

// Where an aim opens: the lit tile nearest `from`, on the engine's own
// orthogonal metric, and the first of them in the engine's row-major order when
// two are equally near. `from` itself when nothing is lit, because a gesture
// that reaches nobody must still leave the cursor somewhere a player can see.
[[nodiscard]] simulation::Position nearest_aim_tile(
    const std::vector<simulation::Position>& tiles,
    simulation::Position from
) noexcept;

// Where an aim goes when the player pushes the stick one way. `dx`/`dy` are the
// press, exactly one of them non-zero.
//
// The lit tile the press moves toward: nearest along the pressed axis, and
// nearest across it where two lie the same distance along. Tiles level with the
// cursor on that axis are not candidates. They are what the other axis is for.
// With nothing further along, the press wraps to the furthest lit tile the other
// way, which is what makes two targets cycle under repeated presses; with
// nothing either way it stays put.
[[nodiscard]] simulation::Position next_aim_tile(
    const std::vector<simulation::Position>& tiles,
    simulation::Position from,
    int dx,
    int dy
) noexcept;

// Stable one-based labels for units, so a player never types a 64-bit
// identifier and a renderer has something short to draw.
class Roster final {
public:
    void rebuild(const simulation::EncounterSnapshot& snapshot);
    [[nodiscard]] std::string label(simulation::UnitId id) const;
    [[nodiscard]] simulation::UnitId resolve(const std::string& token) const;
    [[nodiscard]] simulation::UnitId at(std::size_t index) const;
    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }

private:
    std::vector<simulation::UnitId> order_;
};

// Everything a client has to provide. The session owns the rules; a presenter
// owns pixels, text, and input, and nothing else.
class Presenter {
public:
    Presenter() = default;
    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;
    virtual ~Presenter() = default;

    virtual void present_dialogue(const package_runtime::Dialogue& dialogue) = 0;
    // `terrain` is the map's row-major terrain identities, width x height.
    // Purely presentational: the simulation neither sees nor validates it.
    virtual void battle_begins(
        const simulation::EncounterSnapshot& snapshot,
        const Roster& roster,
        simulation::Side player_side,
        const std::vector<std::uint64_t>& terrain
    ) = 0;
    // The weapon, ability and item definitions the encounter was created
    // with, handed over once before the first frame. A snapshot names only the
    // identities a unit holds, so a front end that draws a danger zone needs
    // these to ask the engine what everything a unit could use threatens,
    // rather than only the weapon in its hand, and a front end offering a row
    // per carried item needs them to say what spending one would do. Not pure:
    // a front end with no such surface needs none of it and says so by not
    // overriding this.
    //
    // The objectives are here for the same reason and answer the same lack: a
    // snapshot names an objective by identity and result and never says what it
    // is, so a front end that must draw "round 3 of 7" cannot know the seven
    // without the definition behind the identity.
    virtual void battle_definitions(
        const std::vector<simulation::WeaponDefinition>& weapons,
        const std::vector<simulation::AbilityDefinition>& abilities,
        const std::vector<simulation::ItemDefinition>& items,
        const std::vector<simulation::ObjectiveDefinition>& objectives
    ) {
        (void)weapons;
        (void)abilities;
        (void)items;
        (void)objectives;
    }
    // The battle opens in the deployment phase, and here is the region. Handed
    // over once, before the first frame of it, for the reason the definitions
    // are handed over once: a front end that lights the region should not have
    // to read it out of every snapshot. Not pure: a front end that does not
    // steer the phase says so by not overriding this, and plays a board with a
    // region exactly as it plays one without.
    virtual void deployment_begins(
        const simulation::EncounterSnapshot& snapshot,
        const Roster& roster,
        const std::vector<simulation::Position>& zone
    ) {
        (void)snapshot;
        (void)roster;
        (void)zone;
    }
    virtual void draw(
        const simulation::EncounterSnapshot& snapshot,
        const Roster& roster
    ) = 0;
    virtual void report(
        const simulation::CommandResult& result,
        const Roster& roster
    ) = 0;
    virtual void refused(simulation::CommandError error) = 0;
    // The canonical hash belongs to the encounter, which the session owns, so
    // a presenter cannot reach it on its own. So do the objectives, which a
    // snapshot names by identity and result without saying what any of them
    // is. A presenter that must draw "round 3 of 7" needs the definition behind
    // the identity, and this is where it gets it.
    virtual void show_state(
        const simulation::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash,
        const std::vector<simulation::ObjectiveDefinition>& objectives
    ) = 0;
    virtual void battle_ended(
        const simulation::EncounterSnapshot& snapshot,
        std::uint64_t canonical_hash
    ) = 0;
    virtual void campaign_ended() = 0;

    // Blocks until the player expresses an intent. Returning `quit` ends the
    // session; returning `none` is a no-op the session simply re-asks after.
    [[nodiscard]] virtual Intent next_intent(
        const simulation::EncounterSnapshot& snapshot,
        const Roster& roster
    ) = 0;

    // The same, while the deployment phase is open. A separate hook rather
    // than a mode flag on `next_intent`, so that a front end cannot serve a
    // battle menu to a player who is still arranging, and so that a front end
    // which does not steer the phase needs to say nothing at all. The default
    // begins the battle at once, which opens the board exactly as the content
    // authored it, which is the board every non-interactive surface opens on.
    [[nodiscard]] virtual Intent next_deployment_intent(
        const simulation::EncounterSnapshot& snapshot,
        const Roster& roster
    ) {
        (void)snapshot;
        (void)roster;
        return {IntentKind::begin_battle};
    }
};

}  // namespace grandleon::client
