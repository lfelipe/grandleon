# Desktop client

The reference front end for the presenter seam, in a terminal or an SDL window.
It plays a real compiled package: walks the campaign, presents authored
dialogue, runs battles, and lets the opposing side act for itself through
`engine/tactics`. The session it drives is `platform/client`, which the console
front ends drive too. See
[platform/client/README.md](../client/README.md).

## Running

```sh
cmake --build build --target grandleon_play
./build/grandleon_content_compile games/tarnholt/source/project.json game.gpk
./build/grandleon_play game.gpk --campaign=tarnholt_line
./build/grandleon_play game.gpk --campaign=tarnholt_line --sdl   # windowed
```

`--no-colour` disables ANSI colour. `--sdl` opens a window; without a display it
says so and exits rather than hanging.

## Campaign mode: a campaign the player keeps

A named save slot turns the run above into a persistent campaign.

```sh
./build/grandleon_content_compile games/demo/source/project.json demo.gpk
./build/grandleon_play demo.gpk --campaign=muster_road --slot=road
./build/grandleon_play demo.gpk --campaign=muster_road --slot=road --resume
```

`--slot=<name>` is where the campaign is written after every battle, as
`<name>.gls` under `--saves=<dir>` (default `saves`). `--resume` reads it back
first. A slot name is what every platform could carry: lowercase letters,
digits, `_` and `-`, up to thirty-one characters. The same name is a file here
and a save block on a cartridge later.

What changes when a slot is given:

- **The roster is joined to every board.** A character the campaign has
  permanently lost does not stand on a later map, however plainly that map lists
  them, and the client says who was left off and why.
- **Between battles the campaign is narrated.** Who fell for good and which
  numbered unit they were, who reached which level and what each level actually
  granted per stat, what the battle put into the shared store and what it took
  out, and where the campaign went next.
- **`roster` is a verb.** It lists the company: level, experience, the
  permanent points every level-up granted, and who is gone. `units` cannot
  answer that, because `units` is about a board.
- **`info <n>` grows a campaign row.** Level and experience, for characters the
  campaign holds. A battle with no campaign behind it prints the plain sheet
  with no campaign row on it.
- **The company is managed before every board.** A `Manage>` prompt with its
  own verbs and its own numbering stands between one battle and the next,
  before the first, and after a resume:

  ```
    roster            the company, its packs, and the store
    give <n> <k>      hand member n the kth thing in the store
    take <n> <k>      put the kth thing in member n's pack back
    field <n>         member n takes the next board
    bench <n>         member n sits the next board out
    proceed           take the board with the company as it stands
    help              this list
    quit              leave; nothing you did here is lost
  ```

  Members and their packs are numbered exactly as the battle prompt numbers
  units and their packs, so `give 2 1` reads like `use 2 1` and means the
  neighbouring thing. Every gesture is one committed outcome batch, written to
  the slot as it is made, so quitting here loses nothing. The one thing the
  prompt refuses on its own is fielding somebody the next board has nowhere to
  stand; everything else is the campaign's refusal, printed under the
  campaign's own name for it (`insufficient_items`, `unit_is_dead`,
  `unknown_unit`). A line the roster will not publish (everybody benched, or
  the character an objective protects benched) says so in the roster's word and
  leaves the stage standing.

Every one of those numbers comes from the engine.
`campaign_runtime::derive_battle_progression` decides what a battle did to the
roster's numbers and its inventory, `campaign::complete_node` decides where the
campaign goes, and this client adds nothing up. See
[platform/client/README.md](../client/README.md) and
[engine/campaign_runtime/README.md](../../engine/campaign_runtime/README.md).

A slot that cannot be honoured is refused in the words of whichever layer
refused it, and the campaign the session was already holding is what gets
played:

```
  slot 'road' refused: save truncated
  the campaign you were holding is untouched.
```

The device says `storage <name>`, the migration registry says
`migration <name>`, the envelope says `save <name>`, and the campaign's own
validation says `state <name>`. None is paraphrased, because a refusal a bug
report cannot be read out of is a refusal that costs somebody an afternoon.

Campaign mode is terminal-only: the SDL presenter draws a battle and
says nothing about a campaign, so `--slot` with `--sdl` is refused rather than
silently overwriting a slot while narrating none of itself.

## The presenter seam

Everything about rules, campaign flow, and the opposing side lives in
`session.cpp`. A client is a `Presenter` and nothing else:

```
Session  ──asks for──▶  Intent        ("move unit 3 to 1,3")
Session  ──tells────▶  Presenter     (draw, report, refuse, end)
```

Input crosses that seam as **intents, not key codes**. A terminal reads a
line, a window reads a click, and a console reads a stick and two buttons;
all three produce the same small vocabulary and nothing below the seam knows
which. A controller is another source of intents, and a second player in
co-operative play would be too. "Which seat controls which units" is a question
this seam is where to answer. Adding a front end means implementing `Presenter`
and one line in `main.cpp`.

## What each front end does

| | Terminal | SDL |
|---|---|---|
| Board | ANSI text grid | autotiled terrain and character sprites |
| Author's theme and colours | ignored | drawn |
| Dialogue | inline | console (no font dependency yet) |
| Input | one line at a time | mouse click, Escape, Space |
| Available | always | when SDL2 is found at configure time |

The terminal client reads a line at a time rather than using raw terminal mode.
That costs a little immediacy and buys a client that can be driven from a file,
which is why `grandleon.desktop_client` can assert a playthrough rather than a
person having to watch one.

## Verifying the SDL front end without a display

SDL's `offscreen` video driver renders into memory, so the renderer can be
checked on a machine with no screen:

```sh
SDL_VIDEODRIVER=offscreen ./build/grandleon_play game.gpk \
    --campaign=tarnholt_line --probe
PROBE board=32x8 blue=4 red=4 terrain=248 unknown=0 firstblue=1,2 firstred=8,2 \
    theme=temperate firstcolours=blue secondcolours=red ground=7150f872f9c71b51
PROBE step=16 tile=64 headroom=0 raised=0
```

`--probe` draws one real frame through the ordinary session, reads the
framebuffer back, samples the centre of every board cell, classifies each
against the palette, and exits. `grandleon.sdl_presenter` asserts that
summary: the board is the right size, each side drew the right number of
units in the authored positions, and no cell is an unrecognised colour.

`blue` and `red` count the first and second sides, not the colours; `theme`,
`firstcolours` and `secondcolours` report what was actually drawn. `ground`
is a digest of every terrain cell's sampled colour, never compared against a
constant: the test draws one campaign twice, as written and with a season
and two colours chosen, and asserts the ground differs while every count and
position is identical.

The second line is the 2.5D geometry, asserted differently: the counts are
read *out of* the framebuffer, while `step`, `tile`, `headroom` and `raised`
are what the presenter decided *before* drawing. A probe that samples through
the same projection the renderer drew with cannot catch the projection being
wrong. So the test authors mountains across a copy of the Fordlight and
re-derives the reserved headroom from the model's own rules, while the counts
stay what a level board reported; every shipped map is level, so without that
run the lift would be code no gate executes. A third
run steps the relief row by row, which is the board that proves the ramp
scan reads only the rows a cell owns rather than the whole rectangle. The
check was confirmed non-vacuous by shifting every unit forty pixels,
dropping the theme substitution, and ignoring the authored colour: each
made it fail.

## What the author chose

A project names the season its ground is drawn in and the colour each faction's
characters wear. Both travel in the compiled package's presentation section, and
this client reads them through `package_runtime`'s `load_presentation`:

- **Terrain** is expanded through the theme's palette substitution, so a project
  that chose winter draws winter ground with no separate art and no extra
  drawing code.
- **Units** draw in their faction's colour, and fall back to the side they fight
  on only when no faction claims them.
- **A package with no presentation section** resolves to the default theme and
  to no faction colours, which is the plain blue-and-red board.

Character art is deliberately *not* put through the theme table. A theme
recolours ground; a faction's colour is the author's other choice and the two
must stay independent. Today the two never overlap in the master palette, so
this costs nothing and buys the rule.

## What is deliberately absent

- **No archetype-aware character art.** Every unit is a knight in its faction's
  colour. The package says which archetype each unit type wears, so the
  lookup is available; what is missing here is the second sprite set and the
  code that picks from it, which the console and the editor both have.
- **No text rendering in the SDL front end at all**, so no character is named
  in the window: a token is identified by its faction colour and by where it
  stands, and it is clicked rather than typed at. A font is a system dependency
  for the sake of a whole alphabet. The terminal presenter is legible
  meanwhile, and is where a character *is* named and where a player types the
  number that addresses one.
- **No audio and no animation.**
- **No save menu.** One slot, named on the command line. Browsing, previewing,
  renaming and deleting slots is a front-end surface nobody has built;
  `storage::SlotStorage::slots()` already answers the question it needs.
- **No mid-battle save.** A battle is committed when it ends, so quitting in the
  middle of one leaves the slot holding the campaign as it stood after the last
  one. That is what an unfinished fight honestly is.

Terrain is deliberately not on that list. The package's presentation section
carries the terrain kind the compiler resolved for every identity, and this
client reads it, so a map named `Fordlight River` draws as water rather than as
the fallback sheet. Matching a cell's identity against the art library's own
terrain names is kept only as the fallback for a package that carries no
presentation section. `grandleon.sdl_presenter` redraws the Fordlight with
every terrain renamed to a word the art library is not named after (`Open
Plain`, `Fordlight River`, `Stone Bridge`, `Birch Wood`), and requires the
ground to come out byte-identical to the authored board's.
