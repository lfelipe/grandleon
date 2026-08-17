# The Tarnholt Line

A six-map campaign with a small story, a branch that reconverges, and an end.
`games/demo` is the maintained vertical slice; this is the project's **worked
campaign**: the place where every authoring capability the engine offers is
used by shipped content rather than only by a fixture. The section at the end
names what it found inexpressible.

## The campaign

Two rivers, one road, and forty people who promised to keep it open. The
Ashen Coil take the ford, and it turns out they are not raiding: they are
clearing a road for the Iron Vow, whose Marshal is a week behind them.

| # | Node | Kind | What happens |
|---|---|---|---|
| | `prologue` | story | A runner brings word that the ford has fallen. |
| | `the_valley` | story | What the valley is, and what the guard promised it. |
| | `the_muster` | story | The order to march. |
| **1** | `fordlight_battle` | encounter | **Fordlight Crossing**, 32×8. Clear the far bank. |
| | `interlude` | story | Grants a tonic. |
| | `marching_order` | story | **Recruits** Captain Mirea and Sister Nemet. |
| | `harrow_road` | story | Old smoke on a village burned in the spring. |
| **2** | `harrow_burn_battle` | encounter | **The Harrow Burn**, 11×8. Clear it, or **talk** to the levy. |
| | `colls_word` | story | *Branch only.* **Recruits** Coll Rankin. |
| **3** | `sunken_mill_battle` | encounter | *Branch only.* **The Sunken Mill**, 9×7. |
| | `mill_burned` | story | *Branch only.* **Grants** three tonics. |
| | `the_long_way` | story | *Other thread.* Two days over the shoulder of the valley. |
| | `emberhall_road` | story | **Both threads rejoin here.** |
| **4** | `emberhall_battle` | encounter | **The Yard at Emberhall**, 12×9. **Survive six rounds.** |
| | `dark_falls` | story | Morning, and the count. |
| | `the_climb` | story | The road up to the Watch. |
| **5** | `ashen_watch_battle` | encounter | **The Ashen Watch**, 12×9. Fell Warden Kesh, keep Mirea alive. |
| | `watch_falls` | story | The tower is taken. |
| | `keshs_orders` | story | A second seal. Kesh was clearing somebody's road. |
| **6** | `coldgate_battle` | encounter | **The Coldgate**, 13×9. **Fell Marshal Vorne.** |
| | `tower_cost` | story | The names, all of them, in one list. |
| | `valley_held` | terminal | Two rivers, both theirs. For now. |
| | `valley_falls` | terminal | The losing end, reachable from four boards. |

Five boards on the common road, six on the branch.

## The branch

On the Harrow Burn, one of the four Ashen Coil soldiers is not an enemy. Coll
Rankin's placement carries a `talk` mark naming the world flag
`coll_rankin_heard`, and talking to him takes him off the board alive. He has
departed, not been defeated: no defeat event, no experience, nobody recorded
dead. The board's clearance objective is satisfied either way.

- `worldFlagEquals coll_rankin_heard = true` at priority zero opens
  `colls_word` (Coll joins the company), the Sunken Mill (no other route
  reaches it), and three tonics out of its cellar.
- The unconditional fallback takes `the_long_way`.

Both edges end at `emberhall_road`. A reconvergence is two edges naming one
node. The branch pays in a person (Coll carries the campaign's only
reach-one-to-two melee weapon) and in supplies, and the extra body is felt
immediately because the next board caps who may stand on it.

Coll is authored twice (`ashen_levy` on the burn, `dawn_levy` on the roster)
because a placement is not a member: a character talked off a board has
departed, and what the story does next is *recruit* him, a different act with
a different record.

## The yard at Emberhall

The middle board is won by **outlasting six rounds**, and it is the board the
game's turn order is chosen for, for arithmetic reasons: a round under
`alternating` is one activation per side, so "survive six rounds" would end
before the second wave landed. Under `sideBlocks` a round is everybody still
standing having acted, and `ROUND 3 OF 6` means what a player thinks it means.
The project states that order and `emberhall_battle` states nothing of its own,
so the board the count belongs to takes the same order as every other.

Two waves keep arriving: the east gate on the second round and every second
round after, three times; the north gap on the third, twice. Both are authored
`behavior: pursue`, so they come at the company.

It is also the campaign's only **deployment capacity**: the muster region
around the well holds **five**, and six members can be there on the common
road, seven on the branch, so somebody sits it out. The region is the centre
of the yard rather than a corner: a company backed into a wall is a company
only one gate can reach. The Coldgate carries the campaign's other deployment
region and deliberately no capacity, so the two cases are both authored
somewhere.

## The Coldgate

Marshal Vorne stands at the head of a pass whose mountain spur is broken by
exactly three tiles of road. He is a `marshal` class and an `ashen_marshal`
unit type that exist for one character, because an enemy-side placement
cannot name a roster member and therefore cannot carry a member specificity.
Both are named for the rank, and the type reads **Ashen Marshal**. *Marshal
Vorne* is a `name` on the placement, which is where a person's name belongs: a
unit type is a kind of soldier, and this is the one soldier of that kind.

| | health | move | str | def | res | skill | luck | evasion | AP |
|---|---|---|---|---|---|---|---|---|---|
| `commander` (Kesh, Mirea) | 14 | 4 | 6 | 3 | | | 3 | | 2 |
| `marshal` (Vorne) | 24 | 4 | 5 | 5 | 3 | 4 | 4 | 1 | 2 |

Five defence means a Guard Sword takes three off him and a Power Strike six,
so twenty-four health has to be spent rather than burned through. Three
resistance is the campaign's first, aimed at its own balance note: with
nothing resisting it, magic was strictly better than steel; against the
Marshal, Ember Bolt lands three where a Power Strike lands six. Two action
points let him step and strike in one activation, and the Vow Glaive answers
from two tiles as well as one, so stepping back does not solve him. He
carries no `talk`, so no client offers a talk row and the engine refuses one
as `not_talkable`.

## The company

`roster` names the four who ride to the ford: Ser Halvard, Ser Ondrey, Wren
Ashdown and Emrik Vayle. `marching_order` recruits Captain Mirea (whom two
objectives protect) and Sister Nemet, the healer. `colls_word` recruits Coll
Rankin, on the branch only. The campaign is founded with a starting store of
two Field Tonics; two story nodes grant more.

Three members carry a specificity and nobody else does. A specificity
everybody has is a class:

| Member | Specificity | Why |
|---|---|---|
| Wren Ashdown | `skill +2`, `rangeBonus 1` | Her Long Bow's band is two to four where every other archer's is two to three; the bonus travels with her and stays behind when somebody else picks the bow up. |
| Captain Mirea | `health +2`, `luck +1` | Losing her ends the campaign. |
| Coll Rankin | `health +2`, `defense -1` | A miller's son: harder to put down than a knight, wearing nothing that stops a blade. |

### Roles

| Class | Role | Reach | Health / Move / Str / Def |
|---|---|---|---|
| `knight` | melee line | 1 | 12 / 3 / 5 / 3 |
| `archer` | ranged | 2–3 | 8 / 4 / 4 / 1 |
| `mage` | single-target magic | 1–2 | 7 / 3 / 5 / 1 |
| `stormcaller` | area effect | 1–2 | 8 / 3 / 4 / 1 |
| `healer` | support | 1–2 | 8 / 3 / 2 / 1 |
| `commander` | named leader | 1 | 14 / 4 / 6 / 3 |
| `marshal` | the last board | 1–2 | 24 / 4 / 5 / 5 |

The Dawn Levy is a `knight` carrying a **Boat Hook**: the only weapon in the
guard's hands that strikes a tile away and a tile beyond it, and the reason
beyond the story to take the road that brings it.

## What the boards are shaped by

- **Fordlight Crossing.** The water is terrain nobody in this campaign can
  enter, so the two road rows across the middle are the ford. The far bank runs
  out into open country, which makes the board wider than either console
  screen: the first board anybody plays is one that scrolls, and so is the
  first one shown across before it is played on. Everything the battle uses is
  inside the window either console opens at.
- **The Harrow Burn.** Nothing is closed; the only thing deciding how it goes
  is who the guard walks up to.
- **The Sunken Mill.** The millrace runs the whole height of the board and the
  mill floor is the one row over it.
- **The Yard at Emberhall.** Mountain corners, four gaps, a company in the
  middle.
- **The Ashen Watch.** Mountains are closed, including the pair in the middle
  of its road.
- **The Coldgate.** Mountain on every edge and a spur down the middle, broken
  by three tiles of road.

The marsh is not closed, it is slow: a swamp cell costs two of a character's
allowance to walk into where road, grass and snow cost one. That is the
difference between ground that shapes a walk and ground that ends it, and the
Watch is the board that leans on it hardest: a ring of marsh around one lane
of hard road, so the lane is worth holding.

## The two boards that carry no later authoring

The Fordlight Crossing and the Ashen Watch are the plainest boards in the
campaign, and their golden canonical hashes are pinned:
`f0e720363089e7d3` and `bc4cee0e403c3366`. Everything the later chapters
author belongs on a board that serves it, and none of those boards is these
two; a canonical hash is a fact about an arrangement of characters, so the
package growing around them does not reach it.

What does reach them is the order they are played in, and what every character
on them is. A turn order is part of the battle rather than of the file it
arrived in, so the project's `sideBlocks` is folded into both of these hashes
exactly as it is into every other board's, and the two values above are the
values under that order. A class stat travels the same way and reaches further
still: the two action points every class here carries are canonical on every
character placed, so that one number moves both values above and every other
board in the game at once. Nothing else about
either board is free to move: a placement, an objective, a tile or an identity
changing here is a golden failing, which is the point of pinning them.

## Running it

```sh
node tools/source_schema/validate.mjs games/tarnholt/source/project.json
./build/grandleon_content_compile games/tarnholt/source/project.json out.gpk
ctest --test-dir build -R grandleon.game_tarnholt --output-on-failure
ctest --test-dir build -R grandleon.tarnholt_branch --output-on-failure
```

- `games/tarnholt/src/play_tarnholt.cpp` walks the campaign end to end and
  pins the two golden hashes. It drives `package_runtime::CampaignCursor`,
  which holds no campaign state, so it deliberately takes the unconditional
  thread every time.
- `tests/client/tarnholt_branch_test.cpp` plays **both threads** through
  `client::CampaignSession` and proves the flag survives the board and the
  save.
- `editor/src/domain/tarnholt-campaign.test.ts` covers the browser's reading
  of the same source.

## Playing it

Play mode steers the first side only; the Ashen Coil acts from the
`behavior` authored on each placement: `hold`, `patrol`, `pursue`.
Behaviour is policy, not rules: it lives in `engine/tactics`, and the
simulation validates every proposal, so a bad policy can make a bad move but
not an impossible one. Closing the distance still costs the side that has to
do it. It strikes second, and it is standing in reach when the answer comes,
which is why the yard is the board it is: the guard stands still and the Coil
walks.

**Every class here carries two action points**, which is one walk and one
strike in a single activation. It is stated on all seven rather than left to
the format's default of one, because one is a character who is finished the
moment it takes a step: walking is a command and a command costs a point, so a
single-point character can move or fight and never both. Nothing about a board
says that, nothing on the console shows it, and a player who walks a knight
into reach and is then refused an attack has met a rule they had no way to
read. Two is what the genre means by a turn and what the editor's own help text
promises, so it is what the campaign is tuned against. Both sides carry it: a
class is shared by both armies here (`knight`, `archer` and `commander` each
field Dawn Guard and Ashen Coil) and a unit type overrides no stat, so giving
the guard a second point without giving the Coil one would mean minting a
second copy of three classes and splitting Coll Rankin, who is a `knight` on
both sides of his talk, into two different characters.

Turn order, action points and the rest of the vocabulary are the engine's;
`games/template/README.md` and `tools/source_schema/SOURCE_FORMAT.md` state
them. What this game states is the order: `defaultTurnOrder` is **`sideBlocks`**
and no board overrides it, so all six play the same way. Every character of one
side acts, in whatever order the player picks, then every character of the
other. The engine names the side and never the actor, so nobody is locked in by
having started: walk one, walk a second, come back and strike with the first.

One order across six boards is a decision about the game rather than about one
of its boards. The yard is the board that cannot be played any other way, and a
player who learned on the ford what a round is should not have to learn a second
answer when they reach the yard. The order is therefore authored once, where a
statement about the whole game belongs, and no board here spends a line
restating it. A company of seven handing the turn back after every single step
would read as a broken board rather than as a rule.

## Saves

The campaign declares `contentRevision` `0.2.0`. A save written against
`0.1.0` is refused by name rather than silently replaced: the migration
registry's content axis is deliberately empty, every client shows the
refusal, and the browser offers "Start fresh" rather than doing it unasked.

## What is still not expressible

- **Inventory conditions.** `inventoryAtLeast` is schema-valid and the
  compiler refuses it by name; `worldFlagEquals` is what the branch runs on.
- **Multiple dialogues per node.** A node carries one dialogue; extra
  `dialogueIds` are ignored.
- **A campaign-member reinforcement.** A placement naming a `memberId`
  cannot also author an `arrival`; the yard's waves are all Ashen Coil.

## A decision worth knowing about

An objective names a placement, but a placement bound to a roster member is
registered under the member, the identity that outlasts one board. That is
why `keep_mirea_alive` is one objective used by two boards whose placements
for her are called different things, and why an objective naming a member's
*placement* identifier on a board where the two differ will not resolve.

## In the editor

The Tarnholt Line is one of the editor's bundled samples: choose it beside
**Open this example**. It opens as an unsaved working copy, imported directly from
`games/tarnholt/source/project.json`, so editing it cannot damage the file
and nothing can drift from what the tests compile.
