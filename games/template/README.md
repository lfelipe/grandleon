# Grandleon game template

An independently built mock game that keeps the package SDK honest: configured
separately from the engine, it discovers an installed `GrandleonPackageSdk`,
compiles one definition in every required category, and writes a package
without reaching into engine sources. The authoring source under `src/` is C++
against that SDK; the schema-backed source format a real game is written in is
`tools/source_schema/SOURCE_FORMAT.md`.

Most of this file is the contract a game author works against, not a tour of
the mock: identity, save compatibility, and the authored fields below apply to
any game built on this engine.

## Contents

- [Build](#build)
- [Ownership](#ownership)
- [Identity ownership and compatibility](#identity-ownership-and-compatibility)
- [The settings that shape the whole game](#the-settings-that-shape-the-whole-game)
- [What a company owns, and how many of it fights](#what-a-company-owns-and-how-many-of-it-fights)
- [What makes a character more than their class](#what-makes-a-character-more-than-their-class)
- [Save compatibility](#save-compatibility)
- [Who can be talked to](#who-can-be-talked-to)
- [Holding out, and the waves that come](#holding-out-and-the-waves-that-come)

## Build

```sh
cmake --install build --prefix build/package-sdk
cmake -S games/template -B build/game-template \
  -DCMAKE_PREFIX_PATH="$PWD/build/package-sdk"
cmake --build build/game-template
build/game-template/grandleon_template_builder build/template.gpk
build/package-sdk/bin/grandleon_package_check build/template.gpk
```

The last command validates the artifact through the installed runtime reader.
CTest exercises the same commands.

## Ownership

- `src/`: the host-side package builder.
- `content/`: schema-backed gameplay source.
- `assets/`: source presentation assets.
- Generated `.gpk` files belong in a build directory, never in source.

To start another mock game, copy this directory and replace the game ID,
title, stable definition IDs, and content. No engine changes are required.

## Identity ownership and compatibility

A save file is a list of your identities: the record that a player's captain
died names your package, your category and your stable id, across every later
build of your game. This is the contract that makes that safe.

| Thing | Owner | Changing it |
| --- | --- | --- |
| Package id (16 bytes) | you, once, per game | never; a new id is a new game |
| Content revision | you, per build | freely; saves record which one wrote them |
| Source key (`"tarnholt_line/fordlight_battle"`) | you, per record | only with a rename mapping |
| Stable content id | derived, never chosen | not yours to set |
| Content category | the engine | append-only, never reused |
| Persistent entity id | the player's campaign | never; it is not in your package |

**Stable ids are derived from your source keys** by FNV-1a-64
(`stable_content_id_v1`, `engine/core/README.md`). You choose the key; the
compiler computes the number. A key is namespaced by package and category and
by nothing else. Two packages may both author `"captain"`, and one package
may author `"captain"` as a unit type and again as an item; two unit types
called `"captain"` in one package is a conflict the compiler refuses. The
arithmetic can never change: a different mapping would be
`stable_content_id_v2` beside it.

**Renaming a source key is a migration, not an edit.** The derived id changes
with it, so every save that referred to the old key refers to nothing. Ship an
explicit rename mapping from the old reference to the new one; the engine
never guesses, because a loader that matched on names would silently repoint a
dead character's record. A stored reference that survives your mappings and
still names nothing mounted is a refused load with a diagnostic.

**Deleting a record is the case with no mapping.** If a save may still refer
to it, keep the record or map it to a replacement. A retired record is better
kept and made unobtainable than removed.

**A persistent entity id is not yours.** Your package names *kinds* of things;
which instance a player recruited, and whether it lives, is the campaign's,
and survives every rename. Identify a character by a definition reference plus
the campaign's identity, never by a position in a list. Two instances of one
unit type are two people; if your story needs one particular soldier, give
that soldier their own definition.

`engine/campaign/README.md` describes the three identity levels these rules
protect.

## The settings that shape the whole game

Four project fields say things about the game rather than any one record.
`schemas/source/v1/project.schema.json` describes each one; the editor gathers
them on its **Game settings** page.

```jsonc
{
  "gameId": "your_game",
  "title": "Your Game",
  "contentRevision": "0.1.0",
  "defaultTurnOrder": "initiative", // how battles are ordered
  "characterLoss": "recoverable",   // what a fallen character costs you
  "characterStyleId": "scifi",      // what the characters are drawn as
  "themeId": "winter"               // what season the ground is
}
```

**Every one costs nothing when you leave it out.** Omitted, `defaultTurnOrder`
is `alternating`, `characterLoss` is `permanent`, `characterStyleId` is
`medieval`, `themeId` is `temperate`. A project that omits them compiles to a
package with no byte spent on any of them. Nothing writes a default in for you.

**`characterLoss`** decides what your company is left with, not how a battle
runs: `permanent` means a fallen character is never fielded again;
`recoverable` means they rejoin the company after the battle, still holding
what they carried. Either way they leave the battlefield when they go down.

**`characterStyleId`** names an entry on the art library's style menu:
`medieval`, `scifi`, `mythical`, `nature`, `sengoku`, `undead`, `pirates`; the
schema's enum is the authority. It changes only the picture: every character
keeps its role, numbers and faction colour, and a console build embeds the
named style's art and no other. The editor's "Make a character" panel offers a
shelf of units per style. `characterFigureId` sits beside it on the same terms:
the body a character that names none of its own is drawn with.

**`defaultTurnOrder`** is a default a battle may override with its own
`turnOrder`. A battle that states its own keeps it; one that states nothing
follows the game and moves when the game's setting changes. A battle that should
follow the game therefore states nothing rather than restating the default.
The three orders:

- **`alternating`.** The sides take turns; the player picks any unit on the
  active side. A round is one turn for each side.
- **`sideBlocks`.** Every unit on the first side acts, then every unit on the
  second, the player choosing the order inside their own block.
- **`initiative`.** Every unit interleaved by speed, regardless of side. The
  engine names the next unit; ties break on the lowest unit identifier.

The art choices are closed menus the art library owns; a value off any menu is
refused rather than reinterpreted.

**`invulnerableForTesting`** is a fifth field and not a way to play: while it
is on, nobody in your company can be brought below one health. It exists so
you can walk your own campaign through without dying on the way. It is
compiled into the file you export and changes what the battles do, so turn it
off before you share. It is deliberately not a third value of `characterLoss`,
which asks what becomes of a character who falls; "nobody falls" is not an
answer to that question.

## What a company owns, and how many of it fights

Three campaign fields, in `schemas/source/v1/campaign.schema.json`.

**`campaign.startingStore`** is what the company owns before it has fought:

```jsonc
"startingStore": [
  { "itemId": "field_tonic", "quantity": 3 },
  { "itemId": "whetstone", "quantity": 1 }
]
```

The *store*, not anybody's hands: a member's satchel is their unit type's
`startingItemIds`; the store is the company's, and a store entry a member can
use is one somebody gives them at the management screen. Omitted and empty
mean the same thing. State one item identity once. Two entries for one item
are two answers to "how many".

**`campaignNode.grants`** is what passing a node puts in the store. It is
allowed on every node kind: a gift on a story node, a battle's worth on an
encounter node.

```jsonc
{
  "id": "the_abbey",
  "kind": "story",
  "grants": [{ "itemId": "field_tonic", "quantity": 2 }],
  "transitions": [{ "id": "onward", "targetNodeId": "the_ford", "priority": 0 }]
}
```

A grant is an occasion, not a stock level: a node your flow returns to grants
again on every pass. There is no "grant once" flag. If you want a one-off, do
not make the node reachable twice. A *retry* is handled for you: re-deriving
the same node's batch after an interrupted write produces the identical batch,
and the campaign answers that it was already applied.

**`campaignNode.deployment.capacity`** is how many of the company may take a
board's field:

```jsonc
"deployment": { "id": "the_narrow_stair", "capacity": 2 }
```

`tiles` says where the player arranges troops, `capacity` how many; state
either or both, but a `deployment` you typed and left empty is refused. The
rules:

- **It is a maximum, not a quota.** Nothing requires the player to fill it.
- **The engine never benches anybody to make a party fit.** An over-cap
  company is refused `over_deployment_capacity` and the player benches
  somebody.
- **A member the cap keeps off is left off the board entirely**, by the same
  pass that leaves off the dead and the unrecruited. The two things that can
  go wrong are the two that already could: `side_emptied` and
  `unavailable_objective_target`. An objective naming a specific member on a
  capped board can be made unanswerable by a legal choice.
- **The cap must be able to bind.** A capacity at or above the encounter's
  first-side placement count is refused; "everybody who has a place goes" is
  spelled by omitting the capacity.

## What makes a character more than their class

A member of `campaign.roster`, or one a node `recruits`, may carry a
`specificity`, which writes a person without inventing a class for them:

```jsonc
{
  "id": "wren",
  "name": "Wren",
  "unitTypeId": "archer",
  "specificity": {
    "stats": { "health": 3, "skill": 2, "speed": -1 },
    "rangeBonus": 1
  }
}
```

Omit `specificity` and the character is exactly their class. `{}` is refused:
it claims a character is specific without saying how. So is any zero delta,
because leaving a stat out is how a member says nothing about it.

- **Every stat is a delta, not a total.** `"health": 3` means three more than
  the class has, so a rebalanced class moves the character with it.
- **A specificity stacks with what the character earns**, applied one step
  before level-up gains, by the same addition. The save is untouched: what you
  wrote lives in the package, what the player earned lives in their file.
- **`speed` is adjustable here although `growthRates` refuses it.** A growth
  roll that reshuffles turn order mid-campaign surprises somebody; an authored
  delta is fixed before the campaign is founded and on the info sheet from the
  start.
- **`rangeBonus` raises a ceiling, never lowers a floor.** One to thirty-two,
  added to the *maximum* of every weapon band this character strikes with; the
  minimum is untouched, so an archer still cannot answer somebody standing on
  top of her.
- **The bonus is on the character, not the weapon.** Two archers carrying one
  authored bow have one bow between them; only the one you wrote the bonus on
  shoots further.
- **Abilities are unaffected.** An ability's reach is the ability's own shape;
  the bonus follows everything that reads a weapon's band.
- **A delta that lands outside what a class could hold is refused by name.** A
  stat may end anywhere its own `statBlock` field admits, nothing is clamped,
  and a refusal names the member, the stat, the base, the delta and where it
  landed.

One shape serves `roster` and every node's `recruits`.

## Save compatibility

A save records five things about the build that wrote it, all checked before
a byte of campaign data is believed:

| Recorded | Mismatch means |
| --- | --- |
| Envelope version | the file's own layout moved; the engine's concern |
| Writing engine version | a save from a newer build than the reader |
| Rules contract | the vocabulary changed meaning |
| A schema version per section | that one section's layout moved |
| Every package's id and content revision | *your* content moved |

**Nothing is ever half-loaded.** A save is decoded, checked, migrated,
interpreted and validated as a complete campaign before anything replaces what
the session holds; a failed load leaves the player where they were, with a
named diagnostic.

What the player sees, and what fixes it:

| The player's save | What they see | Recovery |
| --- | --- | --- |
| bit-rotted, truncated | `checksum_mismatch` or `truncated`, naming the section | a backup slot; slots are cheap for this reason |
| written by a newer build | `incompatible_engine`, or an unreached schema | install the newer build; downgrades are refused by name |
| older build, migration shipped | loads silently, upgraded on the way in | nothing (the working case) |
| older build, no migration | `missing_step`, naming the version nothing leads out of | ship the migration |
| naming content you no longer have | `missing_definition` | ship a rename mapping, or restore the record |
| carrying a section this build does not know | refused if marked required; otherwise carried through untouched | nothing (an old build hands a new build's data back unharmed) |

**The rename recipe**, which is the case you will actually hit:

1. Rename the source key in your project.
2. Raise your `contentRevision`. The revision is how the engine knows a
   migration is owed.
3. Register the mapping for the revision you moved *from*: renames are
   declared one revision at a time and chained on load.
4. Keep the mappings. A player four revisions behind loads through all four.

Two old identities renamed onto one new one refuse the load rather than merge:
which value survives is a content question, not the loader's. Raising the
revision without renaming anything is free: no migration is owed and nothing
runs. A save written by a *newer* build cannot be fixed from the package side:
there is no honest transform downwards, so it is refused by name.

**A save is the player's file.** The checksums catch corruption, not
tampering; no content decision should depend on a save being unedited.

## Who can be talked to

A placement may say its character can be talked to, and name what talking
records:

```json
{
  "id": "line-captain",
  "unitTypeId": "veteran",
  "side": "second",
  "x": 6,
  "y": 2,
  "talk": { "flagId": "captain-heard-out" }
}
```

A character standing next to him may spend an action point talking instead of
striking, which takes him **off the board alive**. He has departed, deliberately
not defeated: no defeat event, no experience, no permanent death, and his tile
is free. If he was the last of his side, the battle is won without a kill.

Author beside it deliberately: an objective demanding his **defeat** fails
once he walks away; one demanding his **protection** stays pending and can
never fail (satisfied would end the battle on the spot).

The `flagId` is a world flag the campaign holds afterwards. Read it from a
transition to open a branch, and let a second edge name the same node to
reconverge:

```json
{
  "targetNodeId": "the-hidden-pass",
  "priority": 0,
  "when": { "kind": "worldFlagEquals", "flagId": "captain-heard-out", "value": true }
}
```

Omitting `talk` costs nothing: a package authoring none spends no byte on the
gesture and no client shows a TALK row. Not authorable yet: what is said, a
condition on who may talk, or a chance of failure. An accepted talk always does
exactly one thing, which is what lets the forecast promise it.

## Holding out, and the waves that come

Two knobs for a battle about the clock rather than the body count.

**An objective that outlasts a number of rounds:**

```json
{
  "id": "hold-the-ford",
  "name": "Hold the ford",
  "kind": "surviveRounds",
  "side": "first",
  "rounds": 7
}
```

The named side wins the moment the seventh round **completes**; it loses if it
has nobody left before then, and a transition can read the result either way.
A round is one completed pass through the turn order: everybody still standing
having acted under `sideBlocks` and `initiative`, one turn per side under
`alternating`. The count a client shows is the round in progress, one-based.
`rounds` belongs to `surviveRounds` and no other kind; both halves of that are
refused rather than defaulted.

**A placement that arrives later:**

```json
{
  "id": "second-wave-outrider",
  "unitTypeId": "raider",
  "side": "second",
  "x": 11,
  "y": 0,
  "behavior": "pursue",
  "arrival": { "round": 3, "every": 3, "times": 4 }
}
```

This character enters as the third round begins, and again on the sixth and
ninth. That is four arrivals: the first is this placement, every later one a
character of the same shape whose identifier the engine allocates. `behavior`
is how a wave comes at the player rather than standing where it spawned.

- **The earliest arrival is the second round.** Somebody there at the open is
  an ordinary placement, so no wave lands during deployment.
- **`every` and `times` are authored together or not at all**; `round` alone
  is a single arrival.
- **A tile is a request, not a claim.** Judged in bounds and passable like any
  placement's, but not against tiles others hold; where it is taken, the
  character stands on the nearest tile it could, and the event says which.
- **A side with a wave still to come is not beaten.** `defeatAllOpponents`
  cannot be won by killing the opening garrison first.
- **Nothing is shaded before it lands.** The danger overlay is a promise about
  the board as it is; tell the player a wave is coming in dialogue if you want
  them told.
- **A roster member cannot arrive.** A placement naming both `memberId` and
  `arrival` is refused: a member is fielded or held back, and there is no
  third state.

Omitting `arrival` costs nothing: a package authoring none carries no
arrivals section at all.
