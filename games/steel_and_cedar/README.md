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

## The main route

| Battle | One player | Two players |
|---|---|---|
| **The Bell Orchard** (8×6) | A short Rowan-and-Tomas opening that teaches moving, attacking, counter-attacking, terrain, and ending the turn. | Rowan and Tomas take the upper orchard road while Akari and Ren answer the signal on the lower road, with another enemy covering the gap. |
| **Cedar Deepwood** (14×9) | Rowan begins farther along the old track so the forest does not become a long empty walk. Ambushes guard clearings, rocks, and narrow paths. | The March starts on the northern trail and Cedar on the southern trail. Each army must win its route before meeting at the watch road. |
| **Cedar Watch** (16×12) | Survive 20 rounds and keep Commander Rowan standing. Only two enemies begin on the board; explicit arrivals pressure the west, east, and north without asking two units to cover every edge. | Rowan holds the western command court while Akari owns the eastern steps. Additional eastern, western, and southern arrivals use all four approaches, with room to warn and reinforce one another. |
| **Gate Village** (18×12) | Rowan pushes from a closer start through the gate, alleys, and village square to defeat Castellan Vere without losing the commander. | The March takes the north gate while Cedar takes the south lane. The companies fight separate street battles before meeting in the square and closing Vere's escape. |
| **Twin Gate Keep** (20×14) | Rowan's outer approach and Tomas's stronger field healing are tested before the company opens the keep and fights Lord Masatora inside its throne room. | Akari takes the lower court while Rowan takes the high approach. Both companies start beyond the empty outer apron; a second guard line and two reserves pressure their separate routes before they converge at the keep doors. |

The main battles after the tutorial are larger than one console view. Enemies are
placed as route guards, flank threats, ranged pressure, command posts, and wave
entry groups rather than one central blob. The same authored placements can
also give Rowan a closer solo start and a more separated two-player start
without creating a second copy of the character.

## Optional battles

- **Orchard Storehouse** opens if you stop on the glint beside the orchard
  bell. Follow the dispatch it reveals to a walled storehouse.
- **Courier Ford** opens if you talk to the courier in Cedar Deepwood and ask
  him to show you the way. The two bridges give each army its own approach in 2P.

Both detours rejoin the main story. The end screen shows the battles you played,
the share of enemies defeated, allied falls, hidden finds, and conversations.

A separate [castle study](prototypes/README.md) explores a 32×24 layout for
Twin Gate Keep with three hidden chambers and split co-op approaches.

## Environment direction

The campaign follows one road from orchard country through Cedar forest and
its old watch fort, then into Gate Village and the keep above it. See the
[map direction](MAP_DIRECTION.md) for each place's layout, scenery and solo/co-op
role.

The orchard has cultivated rows and a bell court. Deepwood follows a brook
around old groves. Watch has a ruined command court, and Village has staggered
houses, a market and a loading yard. The keep has connected fortifications
with separate approaches.

Both consoles use the Godot house and grove drawings, simplified branching
orchard trees, bell, watch details, village props and stone keep. The PS1 floor
continues across the whole map independently of its nearby scenery pages.
Nintendo 64 uses a stronger ground palette and textured surrounding terrain.
The console versions still need visual and performance refinement.

Twin Gate's connected 6×6 keep covers a playable chamber. Entering reveals that
room without moving the board or units; it stays open once explored.

## Playtesting and editing

All seven battles support 1P and 2P. Automated checks cover legal routes,
reinforcements, and both companies contributing to the main campaign.

This demo update adds stages and changes the campaign flow. Start a new
playthrough to use the new routes and collect statistics from the first battle.

Maintainers can regenerate the source and its overhead plans with:

```sh
python3 games/steel_and_cedar/generate_project.py
python3 games/steel_and_cedar/render_plans.py
```

Authored translations are retained. To check the real editing workflow:

```sh
platform/godot/scripts/smoke-steel-cedar-editor.sh
```
