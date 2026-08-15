# Simulation

`grandleon_simulation` owns authoritative, deterministic encounter state. It
has no filesystem, clock, input, rendering, audio, or package-decoding
dependency. Callers create an encounter from an already validated
`EncounterDefinition`, submit commands, and consume immutable snapshots and
semantic events.

## Contents

- [The core loop](#the-core-loop)
- [Rounds, and outlasting them](#rounds-and-outlasting-them)
- [Waves: characters who arrive on a round](#waves-characters-who-arrive-on-a-round)
- [Deployment: the phase before the first activation](#deployment-the-phase-before-the-first-activation)
- [Read-only queries](#read-only-queries)
- [What a character carries in its pack](#what-a-character-carries-in-its-pack)
- [Talking somebody off the board](#talking-somebody-off-the-board)
- [Counterattacks](#counterattacks)
- [What the ground allows](#what-the-ground-allows)
- [The dice](#the-dice)
- [What a defeated character leaves behind](#what-a-defeated-character-leaves-behind)
- [A character who cannot be felled](#a-character-who-cannot-be-felled)
- [The hash that names a battle](#the-hash-that-names-a-battle)
- [Browser/native conformance](#browsernative-conformance)

## The core loop

- The first side begins; turn order is alternating activations, whole-side
  blocks, or by speed.
- An activation is one move, attack, wait, use, talk, or ability command by
  one living unit, spending from that unit's action points.
- Movement is a cheapest-path traversal out to the unit's allowance, four
  neighbours, into in-bounds tiles the unit may enter, paying what each cell it
  enters charges. **A character of the mover's own side is walked through and
  an opposing one is a wall**, and no walk may finish on anybody either way.
  `movement_field` says all three and is the only place any of them is said.
- An attack strikes a living hostile unit whose Manhattan separation falls
  inside the striking weapon's reach band.
- The chance a strike lands is
  `clamp(accuracy + striker skill + striker luck - struck evasion - struck
  luck, 0, 100)`, rolled once against that one number. A miss takes nothing.
- Damage is `max(1, strength + weapon power - defense)`, health clamped to
  zero; a magical ability costs `max(1, magic + power - resistance)` and a
  physical one `max(1, power - defense)`. Every stat is bounded to
  `maximum_stat` at creation. The sum is computed wide and narrowed to the
  `int16` health is kept in, and two unbounded stats would wrap negative and
  heal what they hit. Both gates that build an encounter, `create_encounter`
  and the package loader, refuse the number by asking the same constant.
- **Nobody harms their own side.** A strike aimed at an ally is refused
  `friendly_target`, and a damaging ability covers whoever stands in its area
  but takes health only off the caster's opponents. An ally under the blast,
  and the caster under its own, are spared and roll nothing. A restoring
  ability asks no side and mends whoever is standing in it.
- A surviving defender strikes back (see [Counterattacks](#counterattacks)).
- Objectives decide the outcome: defeat all opponents, defeat a named unit,
  protect one, or outlast a number of rounds. With no objective authored, the
  acting side wins when the opposing side has no living unit, and a side with
  no living unit has lost even when it just acted.

## Rounds, and outlasting them

A round is one completed pass through the turn order:

| Turn order | A pass is |
|---|---|
| `side_blocks` | every character still on the board has acted, the first side's block then the second's; the engine names the side, never the actor |
| `initiative` | every character still on the board has acted, interleaved by speed |
| `alternating` | one turn for each side: a side's block is one activation, so the pass closes as the turn returns to the side that opened |

`EncounterSnapshot::round` counts rounds that have **completed**: a battle
opens with the count at zero, and the round in progress is one more than the
count. Every authored round number is a round in progress, one-based.

Under an ordered turn order the count is always kept. Under alternating order
it is kept only where the content gives it consequence: an objective reads it,
or a character arrives on a round. A board that authors neither is zero, folds
no byte for it into the hash, and prints no round on a console's status line.

`ObjectiveKind::survive_rounds` is satisfied the moment the authored round
completes, and failed when its own side has nobody left in the battle. The
round advances after an activation ends, so the objectives are asked once more
when a turn advance moved the round. The win lands on the command that closed
the round, not the one after it.

## Waves: characters who arrive on a round

A unit definition may state the round it enters on, how many rounds separate
its arrivals, and how many arrivals it makes. `create_encounter` expands the
recurrence in the rules, not in a loader, so the browser and the consoles
cannot disagree about what "every three rounds, four times" means. The first
arrival keeps the definition's identifier; every later one takes the lowest
identifier the definition does not use, and those identifiers are handed out
**in identifier order, not listed order**: a lowest-unused scan is a running
state, and walking waves as listed would give `[A, B]` and `[B, A]` different
identifiers, hashes and dice for the same content.
`two_waves_listed_either_way_are_one_battle` holds it.

The earliest arrival is the **second** round, because somebody there when the
battle opens is an ordinary placement. A wave only ever lands on a turn
advance, never during deployment, never before the dice are seeded.

**Not arrived is not absent.** An unarrived character holds no tile, is never
chosen to act, can be aimed at by nothing (`unarrived_unit` and
`target_unarrived` name the two mistakes), is not arrangeable, reaches no
tile, and shades no danger tile. All of that is `on_board`, asked once. But
it is still in the battle: `defeat_all_opponents` and the elimination backstop
count it, so killing the opening garrison before the second wave lands ends
nothing.

Each side must have somebody standing at the opening: a side whose every
character is a wave is refused `missing_side`, the same refusal a side with no
members earns. (Accepted, such a board is unplayable: under `alternating` no
command is ever accepted so no wave ever lands; under `initiative` the first
round completes inside `create_encounter` and a `survive_rounds` objective
resolves a round early.)

A wave takes its authored tile when it is free and standable, otherwise the
nearest tile that is: breadth-first from the authored tile, following the
board's passability and deliberately blind to its price, ties broken by the
fixed neighbour order every search here uses. Nearest is nearest in tiles: a
wave marches on from off the board rather than paying its way across it, so a
landing does not drift because the ground between two candidates is slow. With
no free standable tile the arrival waits and lands on the first round it can.
`unit_arrived` names the tile actually taken. Nothing about an arrival draws
from any random stream.

## Deployment: the phase before the first activation

An encounter may carry a **deployment region**, a set of tiles. With one, the
battle opens in a phase where the player stands their own characters on those
tiles before anybody acts. Without one there is no phase at all: nobody
arrangeable, nothing added to the canonical hash, the board opens straight
onto the first activation.

**The region says where; the placements say who.** A first-side character
whose defined position is a region tile is deployable; everybody else stays
where the content put them. An author pins a character by placing it outside
the region. `deployable` is read off the authored board once, at creation.

**Two commands.** `CommandType::deploy` stands one deployable character on one
region tile: no action point, no activation, repeatable in any order.
`CommandType::begin_battle` closes the phase and takes no character. The
engine never closes the phase on its own: every character is already standing
somewhere, so there is no moment it could detect as finished.

**Refusals, in the order `apply` takes them:** `wrong_phase` first. An ordinary
command while the phase is open and a deployment command after it closes are
the same mistake, one name. Then `unknown_unit`, `defeated_unit` and
`wrong_side`; then `undeployable_unit` for a pinned character;
`invalid_destination` for a tile off the board; `outside_zone` for one on the
board but outside the region; `occupied_destination` for a region tile
somebody else holds. Putting a character down where it already stands is
accepted and changes nothing.

Terrain is not consulted here: a region tile nobody could stand on is content
the compiler refuses at authoring time. Nothing in the phase draws from any
random stream, nothing in it can fell anybody, and objectives are not
evaluated until the battle begins.

**The phase is canonical state, hashed while it is a rule and not
afterwards:** `canonical_hash` folds the region only while `deploying`. A
battle waiting to be arranged and the same battle arranged are two different
things and a save in the first resumes in the first; once `begin_battle` is
accepted no rule reads the region, and an arranged board hashes exactly as the
identically arranged board an author could have written by hand.
`create_encounter` derives its seed **before** the phase opens, so the streams
roll the same numbers either way. No die is rolled while the board is being
arranged. Who is arrangeable is not hashed because it is not stored:
`is_deployable` reads the region, the positions and `on_board`.

A wave authored inside the region is not arrangeable: its position is a tile
it asked for, not one it holds, and the tile stays offered to everybody else.
A `deploy` naming one is refused `unarrived_unit`, not `undeployable_unit`.

The region is sorted row-major at creation, so authored tile order cannot
reach the hash. A tile off the board or named twice is `invalid_deployment`.

## Read-only queries

Pure queries over a snapshot; none changes state or any hash.

- `forecast_attack` prices one attack with the same checks and formula `apply`
  uses. Because an attack can miss, it is a stated chance and the exact
  numbers behind it: `hit_chance` is the very percentage `apply` rolls
  against, unrounded and not a second opinion. On a miss exactly zero is
  taken. It prices both halves, the strike and the counter, each with its own
  chance, and computes the health **floor** rather than clamping after the
  fact: both halves call `floor_of`, so a unit that `endures` is forecast at
  one and not lethal.
- `reachable_tiles` lists every tile a unit could occupy after one accepted
  move: the same traversal the move rule judges with, exposed so no client
  re-implements it. Empty for anybody not `on_board`.
- `deployable_tiles`: a tile is in the result exactly when a `deploy` naming
  it would be accepted. Empty once the phase closes, empty for anybody not
  arrangeable.
- `danger_tiles` is the danger zone: every tile a side's living units could
  strike in their coming activation, movement plus weapon band, honouring
  minimum reach, budgeted by what each unit can still do (see
  [What the ground allows](#what-the-ground-allows)). The overload taking the
  weapon and ability registries unions the band of every carried weapon and
  every *damaging* ability. A restoring ability is not a danger. The
  registry-free overload answers for one band per unit.

During the deployment phase: `reachable_tiles` returns nothing (every move
would be refused, and the query's contract is that a tile is listed exactly
when a move would be accepted); `forecast_attack` and `forecast_item` return
`wrong_phase`, the refusal `apply` would give; `danger_tiles` answers as
always, over the board as currently arranged. It is the one query the phase
makes more useful.

`Encounter::abilities()` and `Encounter::weapons()` are the definitions the
encounter was created with, read-only, in declaration order. A snapshot names
only identities, so anything choosing a cast or a strike (an action menu,
`engine/tactics`) resolves them here.

A unit carries every weapon its type lists, in that order. The first is the
weapon in hand, and `create_encounter` resolves that unit's power and reach
band from it; a unit carrying nothing keeps the power and band it was defined
with. An attack command names which carried weapon it uses; zero means the one
in hand. `unknown_weapon` (not defined by the encounter) and
`unavailable_weapon` (not carried by the actor) are refused before the target
is looked at.

## What a character carries in its pack

A unit carries every item its type lists, in that order, with a count beside
each. `Encounter::items()` is the third registry on the terms of the other
two. There is no item in hand: a `use_item` naming nothing is `unknown_item`,
not a guess. This is the one place the item vocabulary does not mirror the
weapon one.

`ItemKind::restore` gives back `power` health, clamped to what is missing;
`ItemKind::none` (the default) authors no effect and spending one is
`unusable_item`. The refusals run before anything about the target:
`unknown_item`, `unavailable_item` (defined but not carried), `depleted_item`
(carried down to nothing), `unusable_item`.

The count is canonical state: spending decrements it, `canonical_hash` reads
it at the tail of the per-unit block, and a replay finds the same satchel. A
count at zero keeps its slot, so a client can draw what ran out.

A use costs one action point and closes the activation unless the unit is
authored `acts_after_attacking`, the same gate an attack and a cast pass
through. It provokes no counterattack and draws nothing from any random
stream. `forecast_item` runs the same refusals in the same order and, with no
roll, the number it shows is the number `apply` delivers. A character at full
health is forecast as restoring nothing, and spending the item anyway spends
it.

`EventType::item_used` names the character, the character it was spent on, the
item's `content_id`, and how many are left; the restoring half arrives as the
`unit_restored` event a cast already emits, absent when nothing was restored.
Exactly one is consumed per event, which is what lets
`campaign_runtime::derive_battle_progression` derive inventory consequence
from events alone: one `consume_item` per event. The simulation does not know
that layer exists.

## Talking somebody off the board

`CommandType::talk` is the gesture for the enemy who turns out not to be one.
Shaped after `use_item`: one action point, the activation closed as a strike
closes it, no counterattack, and not one number drawn from any random stream.
The test asserts the whole `core::RandomState`, seed and every stream's
position.

It reaches one adjacent character through `talk_reach`, the band a bare hand
already uses, so nothing new is hashed. Distance zero is refused: a
conversation takes two.

Who can be talked to is authored on a placement as
`UnitDefinition::talk_record_id`; zero, the default, is somebody no talk may
reach. The identity is opaque here: the rules copy it into the event and stop.
Reading it as a world-flag key is the campaign layer's job.

**Departure is not defeat.** The talked-to character is marked
`UnitSnapshot::departed` and keeps its health. Every place that asks who is
there asks one predicate, `on_board`, which is `in_the_battle && arrived`,
where `in_the_battle` is `health > 0 && !departed`. So a departed character
holds no tile, cannot act, cannot be arranged, reaches nothing, and is not a
living character of its side. A battle whose last opponent walks away ends by
the same elimination backstop as one whose last opponent falls. `on_board` is
public, in the header beside the struct it reads; `engine/tactics` calls it
too.

No `unit_defeated` event is emitted, so nobody is paid experience for a
conversation and nobody dies of one. A strike aimed at a departed character is
`target_departed`; a command asked *of* one is `departed_unit`, needed
because under `alternating` the caller picks the actor every time.

Objectives naming a departed character: `defeat_target` **fails**;
`protect_target` stays **pending** and can never fail (every objective is a
win condition and the first to resolve decides, so marking it satisfied would
end the battle on the spot); `defeat_all_opponents` is satisfied when every
opponent has been defeated *or* departed.

Refusal order: the actor gate every command takes (`defeated_unit`,
`departed_unit`, `unarrived_unit`), then `unknown_target`, `target_defeated`,
`target_departed`, `target_unarrived`, `not_talkable`, `target_out_of_range`.
`friendly_target` is deliberately absent: a talk aimed at an ally nobody
authored is already `not_talkable`. `forecast_talk` runs those refusals in
that order and what is left cannot fail: `error == none` is the forecast. All
three forecasts take the actor gate as well as the target gate; a forecast
missing one would promise a command the engine then refuses.

`EventType::unit_talked` names who was talked to, who talked, where they
stood, and the authored record in `content_id`. The authored identity is the
only one that can cross the battle boundary, since battle-local identifiers
differ on every appearance. `campaign_runtime::derive_battle_progression`
turns each such event into exactly one `set_world_flag`.

The hash folds the talk record and the departure behind two bits of a
character's presence byte, so an encounter authoring no talk folds no record
and no marker for the gesture, only the two clear bits that say so.

## Counterattacks

A basic attack against a target still standing when it resolves is answered,
immediately, inside the same command, whether the attack landed or missed.
The gate is the **defender's own reach band** and nothing else: whether an
answer comes is certain and unpurchased; whether it connects is the weapon's
own accuracy, rolled like any other strike. That one comparison makes
`minimum_reach` and `maximum_reach` the most important numbers on a unit. A bow
that outranges a sword is a different weapon, not a longer one.

The edges, each decided:

- **A counter can kill.** Same formula struck the other way; it can take the
  last point.
- **A felled defender does not answer.** A certain killing blow is safe; a
  killing blow that can miss is not, and the forecast reports what a miss
  would earn and how often.
- **A counter provokes nothing.** One exchange per command; two units cannot
  annihilate each other over a single press.
- **A counter is free.** No action point, no place in the turn order, no
  `has_acted`. A unit that has acted still defends itself.
- **An ability provokes nothing.** An area cast covering four opponents would
  turn one command into five strikes, and a restoring cast would have a healed
  ally answering its own medic. Casting is the safe way to spend an
  activation: a trade against a weapon's damage, not a loophole.
- **The minimum of the band refuses as loudly as the maximum.** An archer
  whose band starts at two, struck from an adjacent tile, cannot shoot back.
  Too close and too far are one refusal, `target_out_of_range`, so clients
  keep one word for it.
- **A counter is struck with the weapon in hand**, never a second carried
  weapon.

An activation can fell the unit that made it, so the acting side can be the
one emptied. The outcome check answers that. `engine/tactics` prices a strike
as a trade, health taken less health the counter takes back, read off the
forecast rather than re-derived.

## What the ground allows, and what it charges

Two questions, two answers, and the board carries both per cell.

**What it allows** is `Terrain::open`, `Terrain::water` or `Terrain::heights`. A
unit carries `crossing_water`, `crossing_heights`, and `crossing_every` (flight,
admits everything). `can_enter` is the whole rule. An empty terrain list is an
all-open board; any other length is `invalid_map`, and a unit standing where it
could never walk is `invalid_unit`.

**What it charges** is `movement_cost`, one number per cell, paid on entering.
The tile a character starts on charges nothing, because it is not entered. An
empty list is a board where every step costs one, which is what content written
before ground had a price says and what it has always meant; any other length is
`invalid_map`, and so is a cell charging zero, which is not free ground but an
unbounded walk. A flier pays one everywhere, on the same terms `crossing_every`
crosses everything. `entry_cost` is the whole rule.

Impassable is not "very expensive". An unreachable tile and a dear one are
different answers, and keeping them different is why cost is a second list
rather than a large number in the first.

`movement_field` puts the two together and is **the single definition of where a
character can go**: the move rule, both read-only queries, the danger overlay
and `engine/tactics` all consume it and none of them walks the board itself. It
is a cheapest-path fill, because reaching a tile *cheaply* is the question, and
its answer is a fact about the board rather than about the traversal.

The board, its price and each unit's crossings are canonical state, with one
guard: a board charging one for every cell is a board with no price on it, and
hashes exactly what the same board hashes with no cost list at all.

`danger_tiles` budgets by what a unit can still do:

- a unit that has already acted this round contributes nothing;
- a unit part-way through an activation is measured by the points it has left;
- the stances it may strike from are the tiles reachable with the points it
  can spare from the strike: one point threatens only the band around where
  it stands, two buy a walk and a strike;
- walking into a stance comes off the same `movement_field` a walk is judged
  against, so a river narrows the zone and so does a marsh. A warning that
  counted steps while a walk counted price would shade tiles the board does not
  threaten and leave threatened tiles bare.

Under alternating order no unit is ever marked as having acted, so the zone is
the union over the side's living units, the truthful answer to "who could
reach me before I act again".

## The dice

An encounter carries a `core::RandomState`: a seed, and how many numbers each
stream has drawn. It is authoritative state: hashed, carried in the snapshot,
and complete enough that a save resumed mid-battle rolls the number it was
going to roll. `engine/core/include/grandleon/core/random.hpp` is the substrate.

**Two rules draw from it: a strike can miss (an attack, its counter, or a
damaging cast) and a defeated character can leave something behind.** Nothing
else does. Growth is the third named stream, drawn *outside* any battle by
`campaign_runtime::derive_battle_progression` from a state seeded off the
completed battle's canonical hash, because a level is a campaign's business.

A weapon and an ability each carry an `accuracy`, a whole percentage in
`[0, 100]`; a hundred, the default, always lands. `roll_chance` returns true
at a hundred and false at zero without drawing, so certainty and impossibility
are free.

The chance is the accuracy with both units folded in. `hit_chance_for` is the
only place that computes one:

```
chance = clamp(accuracy + striker skill + striker luck
                        - struck evasion - struck luck, 0, 100)
```

Integer addition with one clamp: no multiplication, no rounding, no second
roll. `skill` helps only when you swing, `evasion` only when you are swung at,
and `luck` sits on both sides of the same roll. **There are no critical hits
in this engine**, so luck has no other role. At zero on both units the
expression is the authored accuracy. The fold costs no extra draw: a folded
hundred and a folded zero each draw nothing. The counterattack rolls the same
expression with the roles swapped, against the accuracy of the weapon the
defender has in hand.

**Hit-stream consumption order**, within one accepted command:

1. the attack, after every refusal, so a refused command never moves the
   stream;
2. then the counterattack, only when one actually occurs (target standing,
   separation inside its band); a missed strike is still answered;
3. for an ability, one draw per unit the area *damages*, in ascending unit
   identifier order. A restoring cast never draws.

`the_hit_stream_is_consumed_in_one_fixed_order` in
`tests/simulation/encounter_test.cpp` holds each clause.

The forecast states the **folded** chance, never the weapon's authored
accuracy, and the exact on-hit numbers; `apply` delivers exactly those on a
hit and exactly zero on a miss, advancing the stream by one per sub-certain
strike and none per certain one.
`forecasts_a_chance_and_the_numbers_behind_it` and
`attacks_land_or_miss_by_the_stated_chance` hold the line. The chance is
computed here and only here.

Further properties:

- **The seed comes from the caller or the encounter itself.**
  `EncounterDefinition::random_seed` is taken when non-zero; zero, the
  default, means `create_encounter` derives one from the encounter's opening
  canonical hash. Two identical encounters roll identically; making a battle
  differ between playthroughs is the caller's job, because this layer has no
  clock.
- **One stream per purpose.** A stream's Nth number does not depend on any
  other stream's draws, so a new purpose cannot reroll every hit in a battle.
- **Adding a purpose moves no hash.** A stream nothing has drawn from is not
  encoded at all; a seed or an actual draw does move the goldens, which is
  when a moved hash means something.
- **A miss is an event.** `EventType::attack_missed` carries whoever was swung
  at, whoever swung, and an amount of zero. Every client draws it.
- **A package says nothing about the dice.** The seed is not a package field;
  snapshots are in-process values, so nothing about the dice is written to
  disk.

## What a defeated character leaves behind

A unit type authors a pair, `drop_item_id` and `drop_chance`. When one of its
characters falls, the chance is rolled to decide whether the thing is left.
Both fields are on `UnitDefinition` and `UnitSnapshot`, and both are canonical:
the character that can leave a tonic draws from the drop stream when it falls
and one that cannot does not.

- **The pair is authored together or not at all.** A chance with nothing to
  leave, or something to leave with no chance, is refused `invalid_item`,
  along with a chance above a hundred. "Leaves nothing" has exactly one
  spelling: both zero, the default.
- **The chance is a whole percentage.** `roll_drop` divides by a hundred and
  nothing else. A hundred always drops and draws nothing.
- **The identity is not resolved against the encounter's items.** A drop is
  recorded and handed to nobody; no battle rule reads what the thing does. The
  compiler resolves it, where an author naming a tonic that does not exist is
  cheapest to tell.
- **A drop goes nowhere during the battle.** It enters no pack and lies on no
  tile: there is no ground-tile inventory in this engine. It is recorded
  twice: `EncounterSnapshot::drops` holds a `DropRecord` naming whose body it
  came off, who felled them, and what fell; and an `item_dropped` event with
  the same three is emitted immediately after the `unit_defeated` event that
  caused it. The list is canonical state, hashed like a health total.
- **Where it ends up is a campaign's business.**
  `derive_battle_progression` turns each `item_dropped` into one `add_item`
  against the campaign's shared store.
- **There is no forecast for a drop.** The forecast promise prices choices,
  and nothing a player can decide changes a drop.

**Drop-stream consumption order:**

1. exactly one draw per defeated character whose type authors a drop below a
   certainty, immediately after its `unit_defeated` event, so draws fall in the
   order defeats resolve;
2. within one attack command that fells twice, the strike's kill before the
   counterattack's;
3. within one ability command that fells several, one roll per felled
   character in ascending unit identifier order, the same rule the hit stream
   states for the same loop;
4. a defeat with no cause would draw nothing; there is none today, because
   every defeat this engine produces names the character that caused it.

All three places a character can fall route through one `record_defeat`.
`the_drop_stream_is_consumed_in_one_fixed_order` holds each clause;
`a_drop_cannot_move_the_hit_stream` pins that a battle fought against droppers
rolls exactly the hit numbers of the same battle against non-droppers;
`a_half_authored_drop_is_refused` holds the refusals.

## A character who cannot be felled

`UnitDefinition::endures` and `UnitSnapshot::endures` say a unit's health may
not fall below one; it is therefore never defeated. It exists so that somebody
walking their own campaign through does not have to lose anybody. It is a
testing aid, declared on a project and carried into a battle by the campaign's
roster join. It is in the rules because a floor applied by a client would be a
client whose battles nobody else could reproduce.

- **The floor is one function.** `floor_of(unit)` returns one for a unit that
  endures, zero otherwise. `take_damage` clamps with it and `forecast_attack`
  predicts with it, so a strike that would take a three-health character to
  minus nine forecasts `target_health_after == 1`, `lethal == false`, and
  delivers exactly that.
- **The counter follows without a special case:** `counters` is gated on the
  defender's health after the blow, computed the same way in both halves, so a
  defender who cannot be felled is always standing to answer.
- **A client is told.** `EventType::unit_endured` is emitted immediately after
  the `unit_damaged` event whose damage the floor caught, only where it caught
  something. A consequence a surface must show is an event, never the absence
  of one.
- **The tactical policy needed no special case.** It scores a strike by
  `target.health - forecast.target_health_after`, so a target it cannot get
  past scores nothing; a losing trade still beats standing still, so two
  armies do not stand in front of each other forever.
- **Canonical only where authored.** The hash carries it as one bit of a
  character's presence byte and folds nothing further, so a board nobody
  endures on spends nothing on it beyond that cleared bit.

`a_character_who_endures_is_left_standing`,
`a_blow_the_floor_did_not_catch_says_nothing`,
`the_forecast_computes_the_floor_rather_than_clamping_after_it`,
`enduring_is_canonical_only_where_it_is_authored` and
`a_target_that_cannot_be_hurt_loses_to_one_that_can` hold all of it.

## The hash that names a battle

`canonical_hash` folds every byte of canonical state, in a fixed order, into
the sixty-four bits that identify a battle. The conformance ROM proves
cross-platform determinism by comparing it, a battle that authors no seed
derives one from it, and a replay that arrives at it has arrived at the same
board.

It is a function of the snapshot and nothing else, declared as a free
function over `EncounterSnapshot`, with `Encounter::canonical_hash` that
function over its own state. Who acts next is in the snapshot for exactly that
reason: state the hash reads that the snapshot does not carry would be a rule
that can go missing from both at once.

Every field of a battle takes one of two verdicts:

- **Folded.** Two battles differing only in it are two battles. Most fields
  are folded unconditionally; a character's six optional parts (a talk record
  and the departure it can end in, an arrival round and whether it has come,
  an enduring character, a spent walk, a turn part-way spent, a reach bonus)
  are folded only where they are not their default, and are **announced before
  they are folded**.

  The announcement is the point. Two of the six are pairs, so eight bits
  announce them, and one byte per character carries all eight. A part's bytes
  are never bytes with nothing to say which part they are. Unannounced,
  `endures`, `has_moved`, one spent point and one tile of bonus reach are all a
  single one at a single offset, and four different boards become one battle
  with one derived seed and one sequence of rolls. Four of the eight bits are
  the whole of their part, a flag being one bit already. The byte is folded
  whether or not anything is present, because a mask behind a guard of its own
  would be the same defect one level up.
  `core::hash_random_state` folds a count ahead of a sparse list of stream
  positions for the same reason.

  The board itself keeps three plain guards: a movement-cost list that prices
  nothing, a deployment region that is no longer a rule, and the default turn
  order. They are safe where they sit rather than by announcement: the last two
  are the final things folded, so an absent one cannot be mistaken for anything
  that follows it, and the price list's equality with an unpriced board is a
  claim rather than an economy. A board charging one for every cell
  *is* a board with no price on it.
- **Not folded, with an argument.** The ability, weapon and item registries
  are content the battle names by identity, not state it holds. The identities
  are folded; the records behind them are not, except the numbers the
  arrangement cannot restate: the band, power and accuracy of the weapon in
  hand, which are resolved onto the character at creation and folded there.

The failure this is written against is silence: a forgotten field looks
exactly like a decision, and the hash quietly stops telling two battles apart.
`tests/simulation/canonical_hash_test.cpp` takes every structure a battle is
made of apart by name, into exactly as many names as each has fields. A field
added to any of them **stops the build** until whoever added it writes the
line saying which verdict it takes. The verdicts are checked, not asserted in
a comment.

## Browser/native conformance

The browser runs *this* implementation: `platform/web` compiles it to
WebAssembly and `editor/src/domain/encounter-simulation.ts` binds it, so there
is one set of rules rather than two agreeing sets. JavaScript identifiers and
counters are `bigint`; the JSON-safe `canonicalState()` projection renders
them as decimal strings. Conformance is therefore about the boundary: the
reference vectors below are replayed through the WebAssembly build and must
produce the same canonical hashes as the native build. A change to the rules
is incomplete until both the native simulation test and
`editor/src/domain/encounter-simulation.test.ts` agree.

The shared reference vector is embedded in both test suites:

- map: 4 by 3;
- first unit: id 10, type 100, position (0,1), HP 8, strength 4, defense 0;
- second unit: id 20, type 200, position (2,1), HP 5, strength 2, defense 1;
- commands: first moves to (1,1), second waits, first attacks, second waits,
  first attacks;
- initial hash: `0e41227fef2c075f`;
- final hash: `9090072b2c0a69c5`, activation count 5, first-side victory.

A second vector covers a battle that counts rounds and a battle a wave
arrives in:

- map: 5 by 3, alternating order;
- first unit: id 10, type 100, position (0,1), HP 6, strength 4, defense 1;
- second unit: id 20, type 200, position (3,1), HP 6, strength 3, defense 1;
- a third, id 30, type 200, position (4,0), the same numbers, arriving on
  round 2 and every 2 rounds twice over;
- one objective: the first side outlasts 3 rounds;
- commands: each side waits in turn, six times;
- initial hash: `31f90d9772d39bed`;
- final hash: `03377a446b1b5ac3`, round 3, first-side victory.

A rejected command makes no state change, emits no event, advances no
activation count, and leaves the canonical hash unchanged. Units and canonical
hashes use stable unit-ID order rather than source insertion order. The walk
is over a board sorted by identifier, and the one place an identifier is
invented rather than read, the later arrivals of a wave, is sorted by
identifier too. Neither side of the boundary promises the other an order.

Run the module tests:

```sh
cmake --build build
ctest --test-dir build -R grandleon.simulation --output-on-failure
```
