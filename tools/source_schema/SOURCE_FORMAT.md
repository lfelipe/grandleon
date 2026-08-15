# The authoring source contract

**`schemas/source/v1/` is the field reference.** Every field's type, bounds and
meaning is a `description` string in the schema, and the editor renders those
strings as the author's own help text. This document holds only what a schema
cannot state: identities, cross-field rules, the diagnostics that enforce them,
and the handful of places where a rule lives in the compiler rather than in the
schema.

Schema version `1.0.0` is pre-release. Authors normally use the editor; the
public files exist so projects stay portable, scriptable, source-controlled and
buildable headlessly.

## Identity

Three components: an immutable 128-bit `packageId` in lowercase canonical text;
a content category (class, unit type, weapon, item, map, dialogue, …); and a
stable source key, a lowercase identifier mapped to a 64-bit compiled ID by
`stable_content_id_v1`. The fully qualified compiled reference is
`(packageId, category, stableId)`, so equal keys in different categories or
packages do not collide. **A compiler must detect and reject a hash collision
within one package and category.** A display name or filename is never identity.

Renaming `packageId` creates a different package. Renaming a stable source key
changes its compiled identity and therefore needs an explicit migration map for
saves, portable rosters and any other durable reference. A content revision
does not change identity.

Persistent entity IDs, battle-local IDs, save envelopes and roster mappings are
separate contracts. They may carry a fully qualified content reference but must
never redefine one or infer it from save layout.

## Versions

`CONTRIBUTING.md` states the obligation on a schema change, and
`engine/campaign/include/grandleon/campaign/migration.hpp` argues the migration
rules at length. Two things belong here:

- **A reader places a document before it validates it.** The schemas describe
  the one version that reader writes, so validating first turns every other
  version into `must be equal to constant`, which is true and useless to the
  person holding the file. Reading `schemaVersion` first is the difference
  between a refusal and an offer.
- **The native compiler migrates nothing.** It reads exactly the current
  version and names both versions and the way up when it meets any other.

Unknown ordinary fields are errors, which catches misspellings. Unknown values
inside the bounded `extensions` object are preserved. Preserving bytes is not
understanding them, so required extension behaviour must also appear in the
manifest capability list.

## Typed weapon and item compatibility

Weapon types and item types are distinct categories so each can gain rules
without overloading a generic type identity. Source `1.0.0` keeps the links
optional as an additive bridge:

- omitted `weaponTypes`, `itemTypes`, `weaponTypeId` or `itemTypeId` is legacy
  unclassified content, and stays omitted through a no-edit round trip;
- omitted `allowedWeaponTypeIds` is legacy unrestricted access; present but
  empty permits no weapon type at all;
- a weapon with no type has **unknown** compatibility, not known compatibility.

New typed content should select an explicit type even though legacy imports
stay valid. A future major version may require type assignments, through a
migration.

## What a cell's name decides

A map's `terrain` is a list of authored names, one per cell. What a cell is
drawn as, what it lets through, and what it charges all follow from that name
by one keyword convention, resolved in `tools/game_content/src/compiler.cpp`
(`terrain_kind_index`). Two properties an author has to know:

**Each kind answers to two keywords, and the match is a substring.**

| Kind | Keywords | Passability | Cost |
|---|---|---|---:|
| water | `water`, `river` | swimmers only | 1 |
| road | `bridge`, `road` | anyone | 1 |
| forest | `forest`, `wood` | anyone | **2** |
| heights | `mountain`, `rock` | climbers only | 1 |
| sand | `sand`, `desert` | anyone | **2** |
| snow | `snow`, `ice` | anyone | 1 |
| marsh | `swamp`, `marsh` | anyone | **2** |
| hill | `hill`, `highland` | anyone | **2** |
| ruin | `ruin`, `rubble` | anyone | **2** |
| grass | `grass`, `plain` | anyone | 1 |
| farm | `farm`, `field` | anyone | 1 |
| bamboo | `bamboo`, `thicket` | anyone | **2** |
| paving | `pave`, `cobble` | anyone | 1 |

**The order of that table settles ambiguity, first match winning.** A cell named
`"mountain road"` is a **road**, because road is tried before heights. Grass
sits late because it is also open ground's own name, and the three kinds after
it were appended, which is why adding a kind cannot change how a name authored
before it existed resolves. A name matching nothing is open ground at cost 1, so
a game acquires neither a wall nor a tax by naming its own ground.

The prices are two, not a scale, and they are built into the compiler: a
project cannot state its own yet.

A class says what its characters cross that open ground is not. `flying` admits
every terrain there is, including any added later. `crossings` names terrain the
class enters on its own account, from the closed vocabulary `water` and
`heights`. Omitting `traversal` is a class that walks: open ground and nothing
else. A placement standing on terrain its class cannot enter is
`invalid_placement` at that placement's path.

## Rules the schema cannot express

Everything below is enforced by the analyzers, the compiler, or both; the schema
cannot state any of it.

**Campaign flow.** Membership array order never defines execution order.
Conditional transition priorities are unique per node and the **lowest matching
value wins, independently of array order**. At most one conditionless fallback
per node, considered only when no condition matches. Cycles are structurally
valid but advance at most once per completed-node event. Conditions are closed
typed data over objective results, inventory quantities and typed world flags,
never expression strings, so a campaign's shape is provable by a validator that
runs no game code. An `objectiveResult` asks about exactly `victory` or
`defeat`: the schema types `result` as a free identifier because it was written
before the runtime had a vocabulary, and
`tests/fixtures/source_projects/invalid/campaign-flow-routing.json` pins both
analyzers and the compiler to the same two. `flow` is optional to the schema so
a campaign can be a portable registry, and **not** optional to a playable
package. A campaign holding no node has no node to enter, and the compiler
refuses it by name.

**Placements and objectives.** Placement identifiers are unique within their
encounter, and coordinates fall inside the referenced map. A `defeatTarget` or
`protectTarget` names its placement through `targetPlacementId`, which must be
on the board of every encounter naming that objective. **A placement that fields
a roster member is named by the member, not by the tile.** The character is who
the objective is about, and is the same character on every board. A placement
fielding nobody is named by its own `id`.

**The company.** Two members naming one unit type are two people: a member's
identity, not its type, carries a level, a wound and a death between battles. A
member's `id` is unique across `roster` and every node's `recruits` together.
**Persistent identities are assigned in authored order: `roster` first, then
each node's recruits in flow order.** The same content therefore always founds
the same company, and a save written by one run is read by the next. A campaign
stating no founding member is `empty_roster` and is refused rather than played
with an invented company.

**Specificities.** A `specificity` holds signed `stats` deltas over the class's
line and a `rangeBonus`. Deltas apply where earned level-up gains apply, one step
earlier, so an authored specificity and an earned gain stack rather than
replacing each other. A delta must land inside the range that stat's own
`statBlock` field admits:

| Diagnostic | When |
|---|---|
| `SOURCE_CAMPAIGN_STAT_DELTA_OUT_OF_RANGE` | the delta left the stat's range. Reported **only** when the member's unit type and its class both resolve: a delta whose base nothing states is an unresolved reference and is reported as one |
| `SOURCE_CAMPAIGN_STAT_DELTA_ZERO` | a delta of nought; leaving the stat out is how a member says nothing about it |
| `SOURCE_CAMPAIGN_SPECIFICITY_EMPTY` | a `specificity` holding neither a stat nor a `rangeBonus`. It claims a character is specific without saying how |

**The store.** `startingStore` is the company's shared store at founding; what
one character carries is their unit type's `startingItemIds`. A node's `grants`
is an **event**, not a statement of how much the store should hold: a node
reached again round a cycle grants again. One item identity appears once per
list; a second is `SOURCE_CAMPAIGN_GRANT_ITEM_DUPLICATE` at that entry's
`itemId`. Two nodes may grant the same item.

**Deployment.** A `deployment` states a region, a `capacity`, or both; one
stating neither is `SOURCE_CAMPAIGN_DEPLOYMENT_EMPTY`. The region states
*where*; ***who*** falls out of the placements already written. A first-side
placement inside the region is a default the player may move, one outside it is
fixed, so an author pins a character by placing them outside. Tiles must be on
the map, appear once, and hold at least one first-side placement
(`SOURCE_CAMPAIGN_DEPLOYMENT_TILE_DUPLICATE`, `…_OUT_OF_BOUNDS`,
`…_UNOCCUPIED`). `capacity` is a maximum rather than a quota: fewer is legal and
the engine never benches anybody to make a party fit. A cap at or above the
number of first-side placements can never refuse anybody and is
`SOURCE_CAMPAIGN_DEPLOYMENT_CAPACITY_UNREACHABLE`.

One deployment rule is **the compiler's alone**: every tile of the region must
be ground that *every* character the region arranges could stand on, because the
phase offers each of them every tile. The analyzers do not resolve a unit type
through its class to its crossings, and half of that rule in two places would be
worse than the whole of it in one.

**Naming.** A placement's `name` is what every client calls that character on
that board, and it is the only way anybody on the **second side** is named at
all. A character nobody named is called after their unit type, with an ordinal
appended only when the board holds more than one of that kind. Three
`line-captain` placements read `LINE CAPTAIN 1`, `2`, `3`; a lone one reads
`LINE CAPTAIN`. **The ordinal runs in ascending placement identity**, so a
console and a browser number a crowd the same way. A first-side placement naming
a `memberId` takes the roster's name; a `name` beside it is accepted and ignored.

**Talking.** A `talk` on a placement makes that character talkable on that board
and names the world flag raised when somebody talks them off it. Talking takes
them off the board **alive**: no defeat event, no experience, nobody recorded
dead. So an objective demanding their defeat **fails**, one demanding their
protection stays pending and can never fail, and one demanding the defeat of
every opponent is satisfied when the last has been defeated *or* has departed.
A kill objective and a talk mark on one character is a deliberate fork whose two
routes exclude each other.

**Arrivals.** `round` is at least two. The first round is the one the battle
opens in, and somebody there when it opens is an ordinary placement. `every` and
`times` are authored together or not at all. An arriving tile is judged in
bounds and passable, but **not** against the tiles other placements hold: a
character arriving on the sixth round does not share the board with the opening
arrangement, so two waves may name one tile. Where the tile is held when the
round comes, the engine stands the character on the nearest one it could stand
on. A placement naming a `memberId` may not also arrive
(`SOURCE_CAMPAIGN_ARRIVAL_MEMBER`): who takes the field counts against the
deployment capacity, and somebody absent at the opening is neither fielded nor
withheld.

## Omission is not a default

Across every optional field (presentation choices, `defaultTurnOrder`,
`characterLoss`, `deployment`, `talk`, `arrival`), omitted, empty and explicitly
written are three different states, and readers never write a default into a
project merely by opening it. A no-edit round trip leaves an absent field
absent. A value a menu does not hold is **refused** rather than replaced, with
`invalid_value` at that field's own path, because a reader that quietly drew the
default would hide the mistake.

The compiled package follows: a section is written only when some record
authors the thing, so a project naming nothing compiles to the bytes it always
did, down to the records not being there. Two consequences worth knowing:

- **Required versus optional sections.** Talks and arrivals are *required*
  sections: a runtime that skipped one would play a different battle, so it
  declines the package instead. Placement names are optional, because a runtime
  skipping them draws the same battle under a derived name.
- **Positional tails.** `characterLoss` and `invulnerableForTesting` ride one
  two-byte tail on the campaign record, appended after the member
  specificities. Because the tails before it are positional, a project that
  states a loss rule and authors no specificities writes a specificity count of
  zero to hold the tail's place.

## Script bindings are inert

A binding is typed authoring-time data: an owner-local slot, an expected future
API version, a lexically validated relative path, an entry-point name and closed
typed parameter values. It is not source code, a module, a command line or an
expression. Editors and validators may display, preserve, index, rename and
validate bindings, and **must never execute them** or interpret parameter
strings as HTML, JavaScript, URLs, templates, filesystem paths or shell
commands. `apiVersion` records an expectation for a future contract, not current
executability.

**Version `1.0.0` defines no package signing, author identity, provenance,
sandboxing, permission grants or native-plugin loading, and no security
guarantee for future capability implementations.** Those need separate threat
models and durable contracts first. Until then script bindings must not be
presented as signed, authenticated, safe-to-execute, or as an extension
mechanism for arbitrary project code.
