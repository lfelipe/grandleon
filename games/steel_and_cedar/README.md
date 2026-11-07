# Steel and Cedar

Steel and Cedar is the candidate campaign for Grandleon's future shared demo.
It is intentionally separate from the included games while its pacing and
difficulty are playtested. Nothing launches it by default yet.

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
| **The Bell Orchard** (8×6) | A short Rowan-and-Tomas opening that teaches moving, attacking, counter-attacking, terrain, and ending the turn. | Akari and Ren answer the signal on a second orchard road, with another enemy covering the gap. |
| **Cedar Deepwood** (14×9) | Rowan begins farther along the old track so the forest does not become a long empty walk. Ambushes guard clearings, rocks, and narrow paths. | The March starts on the northern trail and Cedar on the southern trail. Each army must win its route before meeting at the watch road. |
| **Cedar Watch** (16×12) | Survive 20 rounds and keep Commander Rowan standing. Only two enemies begin on the board; three wave groups pressure the west, east, and north without asking two units to cover every edge. | Rowan holds the western command court while Akari owns the eastern steps. Seven groups use all four approaches, with room to warn and reinforce one another. |
| **Gate Village** (18×12) | Rowan pushes from a closer start through the gate, alleys, and village square to defeat Castellan Vere without losing the commander. | The March takes the north gate while Cedar takes the south lane. The companies fight separate street battles before meeting in the square and closing Vere's escape. |
| **Twin Gate Keep** (20×14) | Rowan's outer approach and Tomas's stronger field healing are tested before the company opens the keep and fights Lord Masatora inside its throne room. | Akari takes the lower court while Rowan takes the high approach. A second guard line and two reserves pressure both routes before the armies converge at the keep doors and enter together. |

Every board after the tutorial is larger than one console view. Enemies are
placed as route guards, flank threats, ranged pressure, command posts, and wave
entry groups rather than one central blob. The same authored placements can
also give Rowan a closer solo start and a more separated two-player start
without creating a second copy of the character.

## Environment direction

Cedar Deepwood and Gate Village deliberately reserve coherent tactical
footprints with the terrain art that exists today. They are playable layouts,
but not the final environment presentation.

- Deepwood now uses two-cell-by-two-cell blocked ancient-grove footprints,
  connected clearings, and separate winding trails. The Godot player groups
  those footprints into large trees instead of four unrelated obstacles.
- Gate Village now uses six deliberate two-cell-by-two-cell house footprints,
  two cross streets, narrow alleys, a paved square, a wall with three entries,
  and a defended court. The houses occupy real impassable cells, so an archer
  cannot shoot a visual prop that everybody else walks through.

Twin Gate Keep adds a connected 6×6 castle footprint: blocked outer walls,
two passable doors, a walkable chamber and a boss dais. Entering the keep
reveals the room beneath its exterior cutout without changing any tactical
cells or unit positions.

The current Godot grouping is a campaign prototype. These should become named,
portable map features that an author can place and preview in one action and
that every renderer draws consistently; the console fallback still reads the
same blocked footprints as high ground until that portable feature work lands.

## Current status

The source parses, compiles, and opens every encounter as both a one-player and
a two-player campaign. The four protagonists now use campaign-specific class
numbers and equipment while keeping the same shared Commander, Archer, Healer,
and Rogue art roles. They have a measured durability and mobility edge over
rank-and-file enemies; Tomas restores eight health so the late solo route can
recover from a real exchange rather than requiring a flawless run.

Automated checks walk the real terrain to ensure both
armies have routes to every opponent and reinforcement, keep each co-op player
within two moves of a consequential opening front, wait through every wave to
prove it lands on a unique legal cell, and play the 20-round defense to a real
simulation victory. A deterministic player-pursuit run against each enemy's
authored hold-or-pursue behaviour also gives all ten 1P/2P encounters a
repeatable completion baseline. Finally, the
campaign cursor follows the objective results through all five victories and
the ending; keeping Rowan alive correctly remains a pending protection result,
while the stage's survival or boss objective advances the story.

That unattended run is a repeatable balance baseline, not proof that a map is
fun. It now completes all ten 1P/2P traces for the Concord instead of losing all
ten to the War Host. The opening preserves every deployed protagonist and
finishes by round four; Deepwood takes seven to nine rounds; both Watch defenses
reach round twenty; and Village and Twin Gate take eight to twelve rounds. Those
late traces finish with only one Concord survivor, while Masatora keeps his
original 27 health. That is intentional evidence that the tuning removed the
automatic loss without erasing attrition, but it is also the clearest remaining
warning: human players still need to judge whether the late battles feel tense
or merely punishing.

It is not ready to replace the current included campaigns until real 1P and 2P
playtests cover difficulty, duration, instructions, retries, saves,
continuation, dead turns, and dominant strategies. Regenerate its repeatable
source with:

```sh
python3 games/steel_and_cedar/generate_project.py
```
