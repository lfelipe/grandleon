# Tactics

`grandleon_tactics` decides what a unit nobody is steering should do. It is one
function, `decide`, and it returns a `Plan`: a command to propose, or a flag
saying the unit cannot act at all.

**This is policy, not rules.** Nothing here decides whether a command is legal.
`engine/simulation` remains the sole authority and will refuse a proposal it
does not like, so every caller has to be prepared for a refusal and fall back to
waiting. That separation is the whole design: the opposing side gets no
privileged knowledge and no privileged path into the state, and it plays through
exactly the command surface a player does.

It depends on `grandleon::simulation` and on nothing else. It never mutates
anything: a snapshot goes in, a command comes out. It asks the simulation what
the ground allows rather than keeping its own copy of the answer.

**Who is on the board is one of those answers.** `simulation::on_board` decides
which characters hold a tile, which may be walked at, which may be struck, which
an area covers, and whether the actor has a plan at all. It is the same
predicate `Encounter::apply` gates every command with, called rather than
re-derived. A policy that spelled it `health > 0` would count a character
talked off the board and a wave that has not landed, and both are refused the
moment the proposal reaches the engine: a driver answers a refusal by waiting,
so such a unit stands still for whole turns while a target it could really hit
walks past. The engine has the last word on the strike itself: a candidate
whose `forecast_attack` refuses is not scored zero and kept, it is not a
candidate.

## Behaviours

| | |
|---|---|
| `hold` | Never moves. Strikes anything that comes into reach. |
| `patrol` | Walks its patrol points in order, striking anything in reach on the way. |
| `pursue` | Closes on the nearest opponent standing on the board and strikes when it can. |

Movement toward a goal is chosen out of `simulation::movement_field`. That is
the engine's one definition of where a character can go, bounded by the unit's
movement allowance and paying what the ground charges. Terrain therefore
constrains the opposing side exactly as it constrains the player, without this
module knowing what water is or what a marsh costs. What this module decides is
only which of the offered tiles to take: the one that gets closest to the goal
in a straight line, ties to the lowest cell index.

## One currency

Casting, striking, and choosing between carried weapons are all priced in the
same unit: **health swung in the acting side's favour.**

A candidate is scored as the health it would actually strip from opponents on
the board, plus the health it would actually return to allies on it, minus the
health a restoring cast would hand an opponent. Nothing is counted past a
target's last point, so overkill and overhealing score zero.

An ally under a damaging area is worth nothing either way, and that is the rule
rather than a shortcut: a damaging cast takes health only off the caster's
opponents, so an ally in the blast and the caster in its own lose nothing and
there is nothing to charge the cast for. Charging one anyway would have a side
walk the long way round its own line to avoid a splash that never touches it.

A basic attack is scored the same way, and the unit casts only when the best
cast beats the best strike outright. So a plain attack stays the default, an
area lands wherever it catches the most of the other side, and finishing a
wounded opponent beats chipping a healthy one. All of it falls out of the
arithmetic rather than out of special cases.

Counterattacks are part of the price. A strike is read off `forecast_attack`,
which returns both halves of the exchange, so shooting from outside the
defender's reach band is preferred without this module containing a rule about
bows.

## Determinism

Every tie has a stated winner, because the same snapshot must produce the same
command on a host, a VR4300 and an R3000A:

- destinations come from `simulation::movement_field`, whose answer is a fact
  about the board rather than about the order it was walked in, and are
  considered in row-major order with ties to the lowest board index;
- target tiles are considered in row-major order;
- opponents tie to the lowest unit identifier;
- abilities are considered in the order the unit lists them;
- weapons are considered with the one in hand first, then in carried order; and
- a candidate replaces the incumbent only on a **strictly** higher score, so the
  earliest-considered candidate wins every tie.

When the winning weapon is the one in hand, the plan names no weapon at all. A
unit carrying a single weapon therefore proposes the identical command it would
have proposed with no weapon vocabulary in the engine.

## The three overloads

`decide` comes in three forms, taking progressively more of the encounter's
registries:

1. snapshot only: the unit strikes, walks, or waits, and never casts;
2. plus `AbilityDefinition`s: the unit can cast;
3. plus `WeaponDefinition`s: the unit can also choose among what it carries.

They are overloads rather than optional arguments so that a caller with nothing
to resolve identities against is never silently handed a different answer than
one that has the registries. `platform/client` passes both registries;
`Encounter::abilities()` and `Encounter::weapons()` are where they come from.

## Tests

```sh
cmake --build build
ctest --test-dir build -R grandleon.tactics --output-on-failure
```
