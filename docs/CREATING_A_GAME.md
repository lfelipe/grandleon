# Creating a game

A game is characters, ground to fight on, **Stages** fought on that ground, and
the order the Stages happen in. One pass through it, in the order you would
do it.

Every picture is taken by `scripts/guide-shots.mjs` driving the real editor
through exactly these steps, and the gate fails if one stops matching.

## Open it

```sh
npm ci --prefix editor
npm run --prefix editor dev
```

![The start screen: Start a new game, Start from an example offering The Tarnholt Line and The Bridge at Dawn, and Open a project file](screenshots/guide/01-start.png)

To see a finished game instead of building one, open an example and press
**▶ Play**. It opens as your own copy; the original is not touched.

## Name it

**Start a new game** opens on Game.

![The game settings form: What the game is called reading The Salt Road, then Turn order, If a character falls, Character style and Season, and a closed Advanced fold below them](screenshots/guide/02-game.png)

Five questions, and every one of them can be answered before anything else
exists. The three under the turn order (what happens when a character falls,
the style the characters are drawn in, the season the ground is drawn in) are
game-wide defaults, and none is a restriction: a medieval campaign can field an
undead enemy.

**Advanced** holds the two names that are for a machine rather than for a
player: the game id, which the exported file is named after and which follows
the title until you write one yourself, and the content revision, which you
raise when you change content a saved game reads. Neither needs an answer to
make a game.

## Make somebody

Characters → **New character**. Whose side, what kind, what they are called.

![The wizard's second step: seven settings across the top with Medieval chosen, and eight role cards below (Knight, Archer, Mage, Stormcaller, Healer, Commander, Rogue, Wolf), each a picture, a name and its reach, Commander selected](screenshots/guide/03-wizard-kind.png)

A card says the one number that changes which role you pick. The rest of a
character's numbers are read on the character. A setting changes the names and
the picture, never what you may make.

![The wizard's third step: a Name field reading Wren, and a line saying the press also makes their class and an officer's blade](screenshots/guide/04-wizard-name.png)

Writing it is one act: one undo takes all of it back.

## Draw ground

Maps → **Create map**. Click or drag to paint, or arrow keys and Enter.

![Undo and Redo above the terrain brush, reading 14 edits can be undone; fourteen terrains with rock selected and a closed Invent a terrain fold below them; and an eight by six grid painted with a river, a bridge, forest and rock](screenshots/guide/05-map.png)

Maps can be reused in several Stages.

Fourteen brushes cover everything the art library draws. **Invent a terrain**
takes a name of your own, and says how it will be drawn and walked over before
you use it. A name matching none of the fourteen is drawn flat and walked like
open ground.

A drag repaints every cell it crosses, so one careless stroke is dozens of
edits. The undo depth is bounded, and the row above the grid says how much is
still held.

## Set up a Stage

A **Stage** is a fight on a map. Pressing **Make a Stage on this ground** on the
map you just drew carries the ground across, so it is not asked for twice.

Then fill the board. You do not have to have made anybody first.

![The placement palette: Wren in a solid box, then Knight, Archer, Mage, Stormcaller, Healer, Commander, Rogue and Wolf in dashed boxes, Rogue chosen, a name field reading Bandit, and The enemy chosen as the side](screenshots/guide/06-find-or-make.png)

The dashed entries are characters this game has not got. Press a tile holding
one and it is made and standing there: one act, one undo. The next press puts
down another of the same Bandit rather than a second Bandit.

![The stage board: Wren on the left below a rock outcrop, and three bandits on the far side of the river, two of them standing in forest](screenshots/guide/07-board.png)

Four presses.

![Two character cards: Wren, one of yours, Commander class, marches with a campaign's company and stands in one Stage; Bandit, an enemy, Rogue class, stands in one Stage three times in all and nothing depends on which of them is which](screenshots/guide/08-standing.png)

Nobody was asked who matters. Wren is held by name in a company, so she is a
person and stands in one place. The bandits are only ever placed, so they are
extras, and three of them are three placements of one.

## Say how it ends

![Winning and losing: four buttons (beat everyone on the other side, beat one particular character, keep one particular character alive, last a number of rounds), the first ticked and reading that your side wins by defeating every opposing character](screenshots/guide/09-winning.png)

A Stage can use more than one. Anything left unticked belongs to a different
Stage and does nothing here.

## Put them in order

Flow draws the campaign as stops on a road.

![The flow graph: The crossing leads to Stage at The salt flats, which leads to the ending; After The Ford sits apart in red saying nothing leads here, and a warning below repeats it](screenshots/guide/10-flow.png)

Making each Stage also made the stop after it, so the road is joined up before
you touch it. Dragging is for changing it: here the first Stage has been sent
straight at the second, and the story beat between them is now reached by
nothing. The graph says so, in the stop and again underneath.

## Play it

**▶ Play**, at the top of every screen.

![The Stage running: Wren picked up on the left, every tile she can reach lit across the near bank, three bandits waiting beyond the river, and a terrain key under the board](screenshots/guide/11-play.png)

The engine the Nintendo 64 and PlayStation builds run, compiled to
WebAssembly: the same rules, the same numbers, the same board.

## Take it away

- **Save** keeps the project in this browser. No account, and nothing leaves the
  machine.
- **Export** downloads it as a `.grandleon.zip`, which **Open a project file**
  reads back.
- **Nintendo 64 ROM** builds a `.z64`, and **PlayStation disc** builds a `.bin`
  and the `.cue` that goes with it, downloaded together as one zip. Both need
  the local build service running (`node tools/rom_service/serve.mjs`) and the
  editor opened at a `localhost` address rather than at the machine's network
  name; the port does not matter. Without either, the buttons are disabled and
  say why.

  **A disc is not the same offer as a cartridge, and the difference is worth
  reading before you burn one.** The image carries no licence sector, because
  that data is Sony's and none of it is fetched or vendored here. It boots in
  PCSX-Redux, which is what the gate proves; a stock PlayStation reads the
  licence area, finds nothing, and refuses the disc. Nothing in this repository
  has ever run on real hardware of either console, so what any other machine
  that reads a disc does with it is not something this project says.

[FROM_EDITOR_TO_CONSOLE.md](FROM_EDITOR_TO_CONSOLE.md) is the rest of that road.

## Refreshing these pictures

```sh
npm run --prefix editor build
node scripts/guide-shots.mjs            # rewrite them
node scripts/guide-shots.mjs --check    # or compare, which is what the gate runs
```

The walk is deterministic, so the check is byte for byte. It fails two ways, and
both matter: a click that no longer lands means this page describes an editor
that is gone, and a picture that no longer matches means it shows one.
