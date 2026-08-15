# Grandleon demo game

`source/project.json` is the maintained canonical source for the first playable
vertical slice. It deliberately uses two generic teams and placeholder prose so
art and story can be replaced without changing the engine or package boundary.

The slice proves this path:

1. validate the canonical source project;
2. compile it into a separately built, versioned `.gpk`;
3. load the encounter definition from the package;
4. replay a deterministic command list through the headless simulation; and
5. finish the defeat-all-opponents objective with a canonical state hash and
   follow its unconditional campaign edge to the terminal node.

The rules the slice exercises are intentionally small: sides alternate, and a
side's turn is one character spending the action points it has, which on this
board is two: enough to walk and then strike, and the reason a rider that
crosses the water is not finished by having crossed it. Attacks target an enemy
inside the weapon's band, damage is `max(1, strength + power - defense)`, a
defender that survives strikes back inside the same command when its own band
covers the attacker, and defeated units remain as zero-health snapshots but no
longer occupy or act. Moving is a cheapest-path fill, and the map's forest and
sand charge two a step where its grass, road and bridge charge one. Nobody can
harm their own side: a strike at an ally is refused and a damaging area spares
one. This ctest slice draws nothing; the same game opened in the editor as
**The Bridge at Dawn** draws with the generated art like any other.

Both classes carry seven health and two action points, and they are the same
stat line on purpose. Two points are what a turn means: a walk is a command and
a command spends a point, so a single-point character may move or strike and
never both, and a board whose first lesson is that stepping forward forfeits
the swing teaches the wrong game. Seven health is what makes striking first the
right move rather than the wrong one. Four strength against one defence is a
blow of three either way and a counterattack is free, so whoever opens an
exchange takes three back from the counter and three more on the other side's
turn: at four health that is a character dead before its second swing, and a
board where the winning line is to stand still and let the other side come.
Seven buys three exchanges instead of one, and the opener wins them.

The Dawn Bridge is a bridge. The water down the middle of the map is terrain a
walker cannot enter, so the River Watch, who are guards, hold the only crossing
on foot, and the Dawn Guard ride herons: their class declares
`"traversal": { "flying": true }`, which is the shipped example of an authored
character crossing what others cannot. The mountains along the far edge are
closed to the guards for the same reason and open to the heron.

## The second campaign

`source/project.json` carries a second campaign beside the conformance slice:
**The Muster Road**, two riders and two maps on the same Dawn Bridge. It exists
because the slice above cannot express one thing the engine has to prove: that
a permanently dead character does not come back because a later map lists them.
One rider a side is a side that empties the moment anybody falls, and a board
one side cannot field is refused rather than published, so nothing about
exclusion can be shown on it.

The two riders are authored people rather than a convention: **Vanguard Rilla**
and **Outrider Bevan** are written into the campaign's `roster`, each placement
on the road says which of them stands on it, and the crossing `recruits` a
third, **Torvald the Ferryman**, who belongs to no company until it is won and
rides the second map with the survivor. That is the demo's example of the last
of the four roster verbs: a character obtained for later use.

The Muster Road is played by `tests/campaign_runtime/demo_permadeath_test.cpp`,
which fights its first encounter with the real simulation, turns what the
simulation reported into a campaign outcome batch (the death, the level, the
drop and the recruitment together), writes the campaign through the save
envelope into a storage slot, reads it back, and asks the second map for a
board. The rider who fell is not on it. The ferryman, who was nobody when the
campaign was founded, is. The terminal and the browser play the same road, and
`tests/desktop/terminal_campaign_test.cpp` has the recruit finish the second
battle himself.

The Muster Road leaves `demo_campaign` alone: its board, its four commands, its
activation count and its canonical hash are the conformance run's own, and the
same test re-plays them on its way past so that a content addition which moved
them could not go unnoticed.

The demo is a package consumer, not part of the engine library. Its build and
playthrough are exercised from the repository CTest suite.

From a configured repository build, run:

```sh
cmake --build build
ctest --test-dir build -R grandleon.game_demo --output-on-failure
```

The test installs the public SDK to its build workspace, configures this
directory as a separate project, compiles `source/project.json` with the
installed CLI, validates the resulting package, and runs the four-command
headless playthrough, and verifies the campaign cursor reaches `demo_complete`.
The cursor holds no campaign state, so it takes only unconditional edges and
rejects a conditional one explicitly; `engine/campaign_runtime` is what
evaluates conditions against a campaign.
