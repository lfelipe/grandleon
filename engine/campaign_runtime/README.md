# Campaign runtime

Where the compiled content and the persistent campaign meet. `engine/campaign`
holds what the player accumulated; `engine/package_runtime` holds what the
author wrote. Two questions need both at once: which node is next, and who may
take the field. This library is those two answers and nothing else.

## Contents

- [Why here and not inside either module](#why-here-and-not-inside-either-module)
- [The graph is translated, not newly authored](#the-graph-is-translated-not-newly-authored)
- [The roster decides who appears](#the-roster-decides-who-appears)
- [Growth: where experience is earned, and where the dice are](#growth-where-experience-is-earned-and-where-the-dice-are)
- [Inventory: what a battle did to what the campaign owns](#inventory-what-a-battle-did-to-what-the-campaign-owns)
- [The satchel: what a campaign character carries](#the-satchel-what-a-campaign-character-carries)
- [What the author made of them](#what-the-author-made-of-them)
- [The store: what a campaign is given](#the-store-what-a-campaign-is-given)
- [The cap: how many of a company take a field](#the-cap-how-many-of-a-company-take-a-field)
- [Tests](#tests)

## Why here and not inside either module

`engine/campaign/README.md` states the constraint: `CampaignCursor` is not
durable state, it walks an *authored* flow decoded out of a package section,
and that is content; what lives in `engine/campaign` is what the player
accumulated. Putting the meeting inside `engine/campaign` would make the
roster link the package format, undoing the property that a save layer and
every console storage adapter reach a list of names without a rules engine
attached. Putting it inside `engine/package_runtime` would make every client,
ROM and conformance target carry the campaign, the outcome machinery and the
save envelope in order to read a map, and would make "no campaign attached" a
branch inside the loader rather than a separate entry point that cannot
accidentally consult a roster that is not there.

So: a third target above both, linking both, adding a dependency to neither.
The simulation is below all of it and learns nothing. A battle is
battle-local, and the day the rules read a roster is the day a canonical hash
depends on a save file.

## The graph is translated, not newly authored

`build_campaign_graph` turns one decoded `CampaignDefinition` into a
`campaign::CampaignGraph`. The source schema already authors the graph (nodes,
an entry node, prioritised transitions, `all`/`any`/`not` conditions, three
predicate kinds) and the compiler encodes it. What a package cannot carry is
the semantics: what a priority means when two edges match, what state
a predicate reads, what is written down so a branched route can be resumed.
That is what this module supplies, with no schema field, compiler output or
package hash involved.

Known edges of the translation:

- **`inventoryAtLeast` has no package encoding.** `worldFlagEquals` does (the
  compiled predicate's result byte is a kind tag); `campaign::predicate_holds`
  can evaluate `inventoryAtLeast`, but the compiler refuses it by name.
  Teaching it bytes means a new kind tag, a schema version step
  (`CONTRIBUTING.md`), and a fixture the command-line and browser analyzers
  both consume. A runtime that does not know a tag answers `unsupported_flow`
  rather than evaluating it as something else.
- **The compiled flow can express an ambiguity the graph refuses.** Two
  conditional transitions out of one node may share a priority in a package;
  `campaign::validate_graph` refuses that and `build_campaign_graph` answers
  `invalid_graph` naming `duplicate_priority`. Compiler-side validation, where
  the author would see it, is left with the item above.
- **`CampaignCursor` exists beside the graph** and walks the flow against
  battle objectives, for a campaign played in a single sitting. Converging
  them is a client change, not an engine one.

## The roster decides who appears

`load_encounter_for_campaign` never spawns a uniquely identified character
that is dead, retired, unrecruited, or otherwise unavailable. A campaign
member is joined to an authored placement by the placement's **source-key
identity** (`PlacementIdentity::source_key_id`), the only identity that is the
same character across two encounters. The instance id is derived from the
encounter and differs every time. Who is on the roster is campaign state and
never content, so the assignment table is supplied by the caller.

Three refusals:

- an **objective whose target was excluded**: protecting or defeating
  somebody who is not there is not a rule with an answer;
- a **side emptied entirely** (`side_emptied`): a battle decided before it
  began is not a battle;
- an **assignment table naming one placement twice or one member twice**,
  checked before a byte of the encounter is read.

`members_a_board_places` answers the separate question of which members a
board has a place for: the board's own placement identities against the
caller's table, no campaign state, no rule. A between-battle screen must not
offer "field this member" for somebody the next map never placed.

A member set to `Availability::retired` is left off exactly as the dead and
the not-yet-recruited are, and appears in `excluded` the same way. The
simulation never learns that somebody chose.

`package_runtime::load_encounter` means: no campaign attached, no exclusion,
the board the package describes. A campaign whose roster is full loads a
board that hashes identically to the one the package alone produces. The tests
assert it, and it is why the demo, Tarnholt and every console target load the
same board with a campaign as without one.

## Growth: where experience is earned, and where the dice are

**Experience is derived at battle end, never carried through the battle.** A
counter inside `UnitSnapshot` would put a campaign concept inside
battle-local canonical state. The simulation carries none of it: no field, no
event, no forecast, no hash. What a battle contributes is what it already
records: a `unit_defeated` event names who fell and, in `related_unit_id`, who
felled them. `derive_battle_progression` turns that plus the board's unit
types into typed operations in the outcome batch beside the deaths and
objective results. One commit, one atomicity guarantee, one idempotent retry.

**The growth stream is seeded from the completed battle, not drawn inside
it.** `campaign::derive_growth_seed` folds the `OutcomeSource` (encounter,
battle's canonical hash, which completion this is) through
`core::derive_random_seed`. The numbers are a function of canonical state, so
they reproduce on replay and on every platform, and a retry of the same
completion rolls the same numbers. Growth stays out of the encounter's own
`RandomState`, so a mid-battle save resumes a battle and nothing else.

The authored rule:

- `experienceAward`: what defeating one of them is worth to whoever struck
  the felling blow. Absent is zero.
- `experiencePerLevel`, defaulting to 100. Level is
  `1 + lifetime experience / experiencePerLevel`, capped at 99. Experience is
  never spent; a level is a threshold a running total crosses.
- A character at the cap earns nothing; one the battle buried earns nothing.
  In both cases the batch contains nothing about them at all.
- `growthRates`: a whole percentage per stat. Each level gained rolls each
  one once; a success adds one point permanently. The authored number is the
  rolled number.
- The points are stored on the roster member as gains, and added to the
  authored unit type whenever the member takes the field. Rebalancing a class
  still moves every character built on it.

**The growth stream's consumption order**, fixed once (the hit stream's is in
`engine/simulation/src/encounter.cpp`):

1. Members in ascending persistent id. Who earned what decides the order,
   never the order the events arrived in.
2. A member who reached no new level draws nothing; so does one at the cap
   and one the battle buried.
3. For each level gained, in order, every growable stat is rolled once, in
   `campaign::GrowableStat` order: health, strength, defense, resistance,
   movement, action points, skill, luck, evasion, magic. Two levels in one
   battle is twenty rolls, not ten doubled.
4. A chance of zero or a hundred draws nothing, as
   `core::RandomState::roll_chance` defines. A unit type with no authored
   growth moves the stream not at all.
5. The bound is a hundred; the chance is a whole percentage.

Every stat grows except `speed`: speed orders a whole turn rather than
pricing one blow, and growing it would silently reshuffle who acts when. The
order of `GrowableStat` *is* the consumption order, so a stat is appended and
never inserted. Inserting one would shift every draw after it under a seed
already drawn from.

## Inventory: what a battle did to what the campaign owns

`derive_battle_progression` returns an inventory consequence beside the
progression, derived from the finished battle's events alone:

- **`item_used`**: one is spent per event; each becomes one
  `campaign::consume_item`.
- **`item_dropped`**: one thing falls per event; each becomes one
  `campaign::add_item`.

**A spend lands on the spender's kit; a drop lands in the shared store.** A
campaign character takes the field carrying their own kit and nothing else,
so charging the store for a spend would claim the army paid for something it
never held; a thing picked off a battlefield belongs to whoever is marching.
`record_permanent_death` makes the same judgement when it returns a dead
member's kit to the store.

**The binding decides whether the consequence is the campaign's at all.** The
acting unit (the spender for a use, the claimant for a drop) must bind to a
roster member. An enemy drinking its own tonic is not the campaign's
business; a drop nobody on the roster claimed is recorded by the battle and
ignored here.

**Order.** The inventory half sits after every progression operation, so it
moves no operation and no growth draw. Within it, what fell is added before
what was spent is consumed, so a batch that both gains and spends the same
identity is never refused for an ordering. And a spend is committed before
the death that ended the spender: `apply_outcome` refuses every operation
against a permanently dead member, and the draught was drunk while they were
alive. `record_permanent_death` then returns to the store only what is
actually left of the kit.

The save format carries all of it without a field of its own: a drop and a
kit are both quantities of item identities, which is what `InventoryStack`
holds. `consume_item` is refused when the owner holds fewer. That is
unreachable in ordinary play, since what a character can spend is what the
campaign put in their hands.

## The satchel: what a campaign character carries

`join_campaign_roster` fills every bound board unit's `item_ids` and
`item_counts` from that member's `carried` stacks, in the same pass that adds
level-up points. A unit no member stands in is untouched and carries exactly
what its type lists.

**The order is the type's authored order first.** A campaign's collections
are sorted by item identity; an authored list is not, and `item_ids` is
hashed in the order written. The satchel walks the type's own list taking
each identity the kit still holds, then appends anything else the kit holds
in ascending identity. A member holding one of each authored item reproduces
the authored board exactly, canonical hash and all. `tests/campaign_runtime`
asserts it by hash.

**A kit is stocked once, when the member joins.** `starting_kit` reads the
type's authored `startingItemIds` (through
`package_runtime::load_unit_starting_items`, for a caller with no board) and
returns one `add_item` per authored item, put in the batch that recruits the
member. After that the type's list is never read for that member again, so a
draught drunk in one battle is gone from the next.

`startingItemIds` therefore means: without a campaign, the satchel carried
into every battle (`load_encounter` reads it directly, so every ROM and
rosterless playtest fields the authored kit); with a campaign, what a member
is given the day they join. One authoring surface, two readings.

A kit holding something the board does not register is carried through, and
`create_encounter` refuses the unregistered item by name. That is unreachable,
since a kit is always a subset of the type's list, and the loud failure is
right for the day it is not. A campaign whose members were never given
anything deploys them with empty packs; no migration regenerates a kit nobody
was handed.

## What the author made of them

`apply_roster_join` also adds what the author made of a character: a signed
delta per stat and a reach bonus (`campaignRosterMember.specificity`), applied
only at the join.

**Authored first, earned second.** The two are addends over the same line, so
the order only matters at a ceiling, where the first addend can saturate and
swallow the second; it is stated and pinned anyway, because two clients that
added in different orders could disagree about a reachable character.
`apply_authored_specificity` runs immediately before the `gain_in` adds, and
`add_signed_saturating` saturates at the floor each stat's own authored field
admits, so a delta that somehow evaded the compiler still cannot produce a
unit with no health.

The reach bonus is *carried* onto `simulation::UnitDefinition::reach_bonus`
rather than added to a band here, because `create_encounter` resolves the
band from the weapon in hand and would overwrite it. The bonus belongs to the
unit, not the weapon record: two archers carrying one authored bow have one
bow between them.

**A specificity rides in beside the board**, in
`package_runtime::EncounterLoadResult::member_specificities`, exactly as
`deployment_capacity` does. The join takes a board, a campaign state and an
assignment table and deliberately no package, because the editor's Play mode
plays content that has never been compiled. The lookup key is the placement's
`source_key_id`, the same identity the assignment table joins on. A
specificity cannot land on somebody the exclusion pass thought was somebody
else. `load_encounter` does not fill the table, so every conformance replay,
rosterless playtest and console ROM loads a board byte-identical to the one
the package alone produces. `member_specificities` is the function that fills
it: content in, table out, no rule, no state, no stream. Every caller that
hands out a board attaches the same table.

## The store: what a campaign is given

`node_item_grants` is what the company holds, in
`campaign::CampaignState::store`, addressed by the reserved zero owner
`campaign::add_item` reads as "the army". Two authored fields, one function:
`campaign.startingStore` is what the company is founded with, compiled with a
join node of zero; `campaignNode.grants` is what passing a node puts there,
compiled with that node. One table in the package.

**A grant is an occurrence, not an assertion about how much the store should
hold.** "Passing this node put three tonics in the store" composes with
everything else that ever touched the store, survives a retry by identity,
and can be narrated straight off the batch. So a route that loops grants
again: a second pass commits under a moved sequence and a different id. A
retry does not: a batch recomputed from an unchanged campaign folds the same
source over the same operations, the same id, and `apply_outcome` answers
`already_applied`. A client derives the sequence at the moment it builds the
batch and never caches it across a commit.

The save format carries a grant without a field of its own, like a kit. The
founding stock rides in the founding batch after the members, so a starting
store can never answer the roster's question of whether a campaign authors a
company at all. A node's grants ride last in that node's own batch.

## The cap: how many of a company take a field

`deployment` says where a player arranges troops; `deployment.capacity` says
how many. The tiles are a rule of the battle and travel inside
`EncounterDefinition`, hashed while the phase is open. The capacity is not a
rule of the battle at all: who is allowed out of a company is a campaign
judgement, so it travels beside the board in
`package_runtime::EncounterLoadResult::deployment_capacity` and the
simulation never learns it.

A placement nobody stands in is not a placement: it is removed before
`create_encounter` is called, by the same exclusion pass that removes a
withheld member's placement. The simulation is never handed a character with
no position: no unit id, nothing for an objective to name, nothing for
`canonical_hash` to fold, nothing to draw. A cap is benching with a different
reason, decided one layer up, so it invents no third failure: the two
consequences a smaller board can have are `side_emptied` and
`unavailable_objective_target`, which benching already has.

- **Enforced above the board.** `join_campaign_roster` counts the members who
  would take the field and answers `over_deployment_capacity` before it
  publishes anything; a published board is always within its cap.
- **A maximum, not a quota.** A capacity of three over a board one member
  takes is a board with one member on it.
- **A reading, published rather than re-derived.** `members_a_board_fields`
  is `members_a_board_places` filtered by `campaign::is_deployable`, exposed
  on the terms `simulation::deployable_tiles` is, so no screen offers a
  gesture the engine would refuse.
- **It never chooses.** Over-cap is a refusal the player answers by benching
  somebody; an automatic trim would have to be deterministic across
  platforms, which means picking by persistent id, which means silently
  preferring whoever was recruited first.

A board with no campaign attached is unaffected, and a capacity no company
reaches changes no board, no binding, no hash and no seed.
`a_capacity_nobody_reaches_costs_nothing` asserts it by hash.

## Tests

```sh
cmake --build build
ctest --test-dir build -R "grandleon.campaign" --output-on-failure
```
