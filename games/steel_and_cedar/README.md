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
| **Cedar Watch** (16×12) | Survive 20 rounds and keep Commander Rowan standing. Only two enemies begin on the board; seven readable wave groups arrive from announced approaches over time. | Rowan holds the western command court while Akari owns the eastern steps. Extra waves make both fronts consequential, with room to warn and reinforce one another. |
| **Gate Village** (18×12) | Rowan pushes from a closer start through the gate, alleys, and village square to defeat Castellan Vere without losing the commander. | The March takes the north gate while Cedar takes the south lane. The companies fight separate street battles before meeting in the square and closing Vere's escape. |
| **The Twin Gate** (20×14) | Rowan's high stair route and Tomas's healing are tested against guard posts, ranged pressure, reserves, and Lord Masatora. | Akari takes the lower court while Rowan takes the high stair. Neither player is a helper: both routes must break their own defenses before the armies converge on the boss. |

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

The current Godot grouping is a campaign prototype. These should become named,
portable map features that an author can place and preview in one action and
that every renderer draws consistently; the console fallback still reads the
same blocked footprints as high ground until that portable feature work lands.

## Current status

The source parses, compiles, and opens every encounter as both a one-player and
a two-player campaign. Automated checks walk the real terrain to ensure both
armies have routes to every opponent and reinforcement, keep each co-op player
within two moves of a consequential opening front, wait through every wave to
prove it lands on a unique legal cell, and play the 20-round defense to a real
simulation victory. A deterministic pursue-versus-pursue smoke run also makes
all ten 1P/2P encounters reach a result instead of deadlocking. Finally, the
campaign cursor follows the objective results through all five victories and
the ending; keeping Rowan alive correctly remains a pending protection result,
while the stage's survival or boss objective advances the story.

That unattended smoke run is a legality and deadlock check, not a balance
proof. The War Host currently wins all ten simple pursuit traces. A human can
use terrain, ranges, healing, target priority, and split-route coordination in
ways that policy does not, so this does not prove any battle is unwinnable. It
does make difficulty and attrition the clearest current balance risks,
especially for Rowan and Tomas on the late solo maps.

It is not ready to replace the current included campaigns until real 1P and 2P
playtests cover difficulty, duration, instructions, retries, saves,
continuation, dead turns, and dominant strategies. Regenerate its repeatable
source with:

```sh
python3 games/steel_and_cedar/generate_project.py
```
