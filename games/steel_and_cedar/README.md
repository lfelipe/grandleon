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

A separate [larger castle study](prototypes/README.md) explores a 32×24 map
with three hidden chambers and split co-op approaches. It does not replace
the five-battle campaign.

## Environment direction

The campaign follows one road from orchard country through Cedar forest and
its old watch fort, then into Gate Village and the keep above it. See the
[map direction](MAP_DIRECTION.md) for each place's layout, scenery and solo/co-op
role.

The composition pass now covers all five maps. The orchard has cultivated rows
and a bell court; Deepwood follows a brook around old groves; Watch has one
ruined command court; Village has staggered houses, a market and loading yard;
and the keep has connected fortifications and separate approaches.

Both consoles use the Godot house and grove drawings, simplified branching
orchard trees, bell, watch details, village props and stone keep. The PS1 floor
continues across the whole map independently of its nearby scenery pages.
Nintendo 64 uses a stronger ground palette and textured surrounding terrain.
The console versions still need visual and performance refinement.

Twin Gate's connected 6×6 keep covers a playable chamber. Entering reveals that
room without moving the board or units; it stays open once explored.

## Playtesting and editing

All five battles load for solo and co-op in the editor and players. Automated
checks cover legal routes, arrivals, real command-driven completions, both
players contributing, and campaign progression. They do not require casualties
or equal workloads from different roles. Human playtests still need to judge
difficulty, instructions, retreats, retries, saves and dominant strategies.

Layout revision `0.3.0` changes terrain and routes. Start fresh rather than
resuming a checkpoint from `0.1.0` or `0.2.0`, whose positions may now be blocked.

Maintainers can regenerate the source and its overhead plans with:

```sh
python3 games/steel_and_cedar/generate_project.py
python3 games/steel_and_cedar/render_plans.py
```

Authored translations are retained. To check the real editing workflow:

```sh
platform/godot/scripts/smoke-steel-cedar-editor.sh
```
