# Steel and Cedar

Steel and Cedar is Grandleon's built-in demonstration campaign. Its maps,
scenery and pacing are being reworked for 0.6; the current layouts remain
playtest material.

A false treaty signal sends Commander Rowan Vale to stop the March and Cedar
from stumbling into war. The one-player campaign is Rowan's complete story,
with Tomas keeping his small company alive. In two-player mode, Akari Mori and
Ren Sato arrive with a separate Cedar army and story thread. They do not simply
become two more pieces in Rowan's army: the two companies begin on different
routes, face additional opposition, and must decide when to hold their own
front and when to rendezvous.

## The five battles

| Battle | One player | Two players |
|---|---|---|
| **The Bell Orchard** (8×6) | A short Rowan-and-Tomas opening that teaches moving, attacking, counter-attacking, terrain, and ending the turn. | Rowan and Tomas take the upper orchard road while Akari and Ren answer the signal on the lower road, with another enemy covering the gap. |
| **Cedar Deepwood** (14×9) | Rowan begins farther along the old track so the forest does not become a long empty walk. Ambushes guard clearings, rocks, and narrow paths. | The March starts on the northern trail and Cedar on the southern trail. Each army must win its route before meeting at the watch road. |
| **Cedar Watch** (16×12) | Survive 20 rounds and keep Commander Rowan standing. Only two enemies begin on the board; explicit arrivals pressure the west, east, and north without asking two units to cover every edge. | Rowan holds the western command court while Akari owns the eastern steps. Additional eastern, western, and southern arrivals use all four approaches, with room to warn and reinforce one another. |
| **Gate Village** (18×12) | Rowan pushes from a closer start through the gate, alleys, and village square to defeat Castellan Vere without losing the commander. | The March takes the north gate while Cedar takes the south lane. The companies fight separate street battles before meeting in the square and closing Vere's escape. |
| **Twin Gate Keep** (20×14) | Rowan's outer approach and Tomas's stronger field healing are tested before the company opens the keep and fights Lord Masatora inside its throne room. | Akari takes the lower court while Rowan takes the high approach. Both companies start beyond the empty outer apron; a second guard line and two reserves pressure their separate routes before they converge at the keep doors. |

Every board after the tutorial is larger than one console view. Enemies are
placed as route guards, flank threats, ranged pressure, command posts, and wave
entry groups rather than one central blob. The same authored placements can
also give Rowan a closer solo start and a more separated two-player start
without creating a second copy of the character.

## Environment direction

The campaign follows one road from orchard country through Cedar forest and
its old watch fort, then into Gate Village and the keep above it. See the
[map direction](MAP_DIRECTION.md) for each place's layout, scenery and solo/co-op
role.

Gate Village is the first composition prototype: homes face connected streets,
a market square surrounds the well, cottage gardens adjoin the southern lane,
and the tower joins the wall at the eastern gate. Deepwood retains its ancient
groves. New Godot landmark models and editor stamps are available; their console
versions are being prepared. The other maps still need the planned composition
pass. More detail alone does not make those layouts finished.

Twin Gate's connected 6×6 keep covers a playable chamber. Entering reveals that
room without moving the board or units; it stays open once explored.

## Current status

The source parses, compiles, and opens every encounter as both a one-player and
a two-player campaign. The four protagonists now use campaign-specific class
numbers and equipment while keeping the same shared Commander, Archer, Healer,
and Rogue art roles. They have a measured durability and mobility edge over
rank-and-file enemies; Tomas restores eight health so the late solo route can
recover from a real exchange rather than requiring a flawless run.

Automated checks walk the real terrain to ensure both armies have routes to
every opponent and reinforcement, make every larger conflict extend beyond a
single console view, distribute both solo and co-op opposition across at least
three map regions, reject opening enemy blobs, keep each co-op player within
two moves of a consequential opening front, and prove that every arrival lands
on a unique legal cell. Bell Orchard also checks its exact upper/lower 2P
deployments against the route named in its co-op dialogue. The
Watch authors each arrival separately, so every wave keeps its intended
hold-or-advance order instead of becoming an inert recurring copy. A
deterministic player-pursuit run against those authored orders gives all ten
1P/2P encounters a repeatable completion baseline. It reaches combat by round
two, never records more than one quiet round in a row, and gives each co-op
player at least one quarter of their side's combat actions. The active Watch
run survives to round twenty, while a company that only waits loses on round
seventeen in 1P and round twelve in 2P. Finally, the
campaign cursor follows the objective results through all five victories and
the ending; keeping Rowan alive correctly remains a pending protection result,
while the stage's survival or boss objective advances the story.

A separate Godot smoke imports this exact source through the normal project
library, opens all five battles in Campaign Editor, compiles every isolated
direct-test project, and then opens Cedar Watch in Map Editor. It proves that
the 1P preview excludes Akari's company, the 2P preview uses the separated
western/eastern starts without duplicating roster members, and all 23 staggered
arrivals remain visible and editable with their intended player scaling.

The unattended traces are a balance baseline, not proof that a map is fun.
The redesigned village currently finishes in thirteen solo rounds and six
co-op rounds. Its earlier solo pacing and attrition assertions flag the change
for review; the map has not been tuned to force those old results. Human
playtests should judge the approaches, enemy pressure, retreats and whether
both co-op companies have worthwhile jobs.

The campaign still needs real solo and co-op playtests for difficulty,
instructions, retries, saves and dominant strategies. Maintainers can regenerate
its repeatable source below; authored campaign translations are retained:

```sh
python3 games/steel_and_cedar/generate_project.py
```

To run the real-editor readiness check:

```sh
platform/godot/scripts/smoke-steel-cedar-editor.sh
```
