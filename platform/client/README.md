# Client

`grandleon_client` is the part of a game client that is the same everywhere: the
campaign walk, the battle loop, the refusal handling, and the side nobody is
steering. A port supplies a `Presenter` and nothing else.

It links `package_format`, `package_runtime`, `simulation` and `tactics`, and it
holds no pixels, no text, no key codes, and no platform header.

## The seam

```
Session  ──asks for──▶  Intent      ("move unit 3 to 1,3")
Session  ──tells────▶  Presenter    (draw, report, refuse, end)
```

Input crosses that boundary as **intents, not key codes**. A terminal reads a
line, a window reads a click, and a console reads a stick and two buttons; all
three produce the same small vocabulary (`move_to`, `attack`, `wait`,
`ability`, plus the housekeeping kinds), and nothing below the seam knows which
produced it.

That is not abstraction for its own sake. It is the shape two recorded
constraints need: a controller is another source of intents rather than another
set of key bindings, and a second player in co-operative play is another source
of them again. "Which seat controls which units" is a question this seam is
where to answer.

`Intent` carries only choices, never consequences. An `ability` intent names a
target tile; which tiles the area covers is the engine's business. An `attack`
intent may name a carried weapon; zero means the weapon in hand, so a front end
that offers no weapon choice says nothing.

## What a presenter must provide

`present_dialogue`, `battle_begins`, `draw`, `report`, `refused`, `show_state`,
`battle_ended`, `campaign_ended`, and `next_intent`. Two details are worth
calling out:

- **`battle_definitions` is not pure virtual.** It hands over the encounter's
  weapon and ability registries once, before the first frame, because a snapshot
  names only the identities a unit holds. A front end that draws a danger zone
  needs them to ask what *everything* a unit could use threatens, rather than
  only what is in its hand. A front end with no such surface overrides nothing.
- **The canonical hash is passed in.** It belongs to the encounter, which the
  session owns, so a presenter cannot reach for it independently and cannot
  print a hash the session did not agree to.

`Roster` sits beside the presenter interface and gives every unit a stable
one-based label, so a player never types a 64-bit identifier and a renderer has
something short to draw.

## What the session does

`run_campaign(package, campaign_id, player_side, presenter)` loads the campaign,
walks its cursor, and returns a `SessionError`. Story nodes present their
dialogue; encounter nodes are played out; a terminal node ends it.

Inside a battle: the session asks the presenter for an intent, translates it
into a `simulation::Command`, applies it, and reports the result or the refusal.
The engine's refusal is passed through verbatim. The client never pre-judges a
command to avoid one, so what a player sees is the rule's own word.

**Who is standing on the board is `simulation::on_board`, and no client
spells it.** The predicate folds alive, arrived, and not talked off, and
every client here calls it: `TurnClient::unit_at`, the cursor's opening
tile, the transcript's occupant rows, both consoles' draw loops and pixel
censuses. Spelling it `health > 0` fails quietly in both directions: a
character talked off the board keeps every point of health, so a health test
draws and hovers it while the engine answers `target_departed`; a character
still marching in fails the other way, on a tile the content asked for
rather than one anybody holds. Three sites here deliberately ask about
health and not the board (a defeated-character roll, a permanent-death
record, and an assertion that a cast killed), and each says so where it
stands.

The unattended side is driven through `engine/tactics`, with both registries
passed so it can cast and choose weapons. It is **bounded rather than looped**:
a behaviour that stops proposing anything surfaces as a stuck board rather than
a hang. The campaign walk carries the same kind of guard, and a dead end in the
flow comes back as `SessionError::flow_stalled` instead of spinning.

## The campaign a player keeps

`run_campaign` above walks an authored flow through one sitting: a cursor, an
outcome, the next node, and nothing that survives the process. A campaign that
outlives it (a roster, permanent deaths, levels, a store, a graph position,
bytes in a save slot) is `run_persistent_campaign`, in
`campaign_session.hpp`, and it is **a second library**:

```
grandleon_client            package_runtime, tactics
grandleon_client_campaign   grandleon_client + campaign_runtime + storage
```

Two targets rather than one because both console ROMs link the first and neither
has a storage adapter yet. Folding the persistent session into
`grandleon_client` would put `engine/campaign`, `engine/campaign_runtime` and
`platform/storage` into every console link for a loop those consoles cannot run.
A console links `grandleon_client_campaign` the day it has somewhere to put a
save, and nothing else has to move.

Both sessions play a battle through the same `play_battle`, so the two cannot
come to disagree about what a battle is. What the persistent one adds around it:

1. Found the company the campaign authors, or resume it from the slot.
   Founding is one `recruit_unit` per member the campaign begins with, in
   authored order, each with the kit their unit type says they start with.
   Resuming runs `campaign::load_campaign_migrated_into`, which decodes,
   migrates, interprets and validates a candidate in full before the live
   campaign is touched, so a slot this build cannot honour costs the player
   nothing they were holding.
2. Load each board through
   `campaign_runtime::load_encounter_for_campaign`, which leaves off everyone
   the roster says cannot take the field.
3. Play it, then hand the finished battle to
   `campaign_runtime::derive_battle_progression` and commit the result through
   `campaign::complete_node`.
4. Narrate through `CampaignNarrator`, and write the campaign to its slot.

**A battle's consequences commit in one order, and the order is a rule.**
What the characters did, then who did not come back, then the objectives,
then whoever the node brought in. `campaign::apply_outcome` refuses every
operation against a permanently dead member, so a character who drinks their
last draught and then falls must be charged while still alive, and
`record_permanent_death` then returns to the store only what is left of the
kit.

**What a member carries is the campaign's, not their unit type's.** A member
is stocked once, in the batch that recruits them, and
`campaign_runtime::join_campaign_roster` hands their kit onto every board
they take. `RosterEntry` carries the kit; `CampaignBoard` and
`BattleAftermath` carry the company's store; both are read out of
`campaign::CampaignState`, neither derived here.

**The company is managed between battles, and the stage stands before every
board.** After a battle, after a story node, on a resume, and before the very
first board of a fresh campaign: one placement, and the other three fall out of
it. `CampaignSession::management()` says what the company is and which members
the next board has a place for; `give_item`, `take_item` and `set_fielded` each
build exactly one `campaign::CampaignOutcomeBatch` and commit it through
`campaign::apply_outcome`. A move is `consume_item` against the owner it leaves
and `add_item` against the owner it reaches; benching is
`set_availability(member, retired)`, which the exclusion pass already honours.
No engine vocabulary was added for any of it.

**A management batch is identified by where the company is standing.** Its
`OutcomeSource` is the campaign node as its content reference, a zero battle
hash because no battle produced it, and the number of outcomes already committed
as its sequence. The category of the reference is part of the id, so a
management batch at a node can never collide with the battle fought at its
encounter; the count is what makes two identical gestures two moves rather than
one committed twice, and what makes a genuine retry (the same gesture against
the same campaign) recognised and ignored.

**The slot is written after every gesture that commits**, which is what leaves
the stage with no pending state at all. There is no basket and no apply step: a
screen shows the campaign the slot holds, and a campaign resumed in the middle
of arranging resumes in the middle of arranging.

**A board the roster refuses sends the player back to the company.**
`prepare_board` publishes nothing and commits nothing when it refuses, so a
company that benched everybody (`side_emptied`) or benched the character an
objective protects (`unavailable_objective_target`) is told the roster's own
word for it and stands where it stood. `run_persistent_campaign` runs the stage
and the board in one loop for exactly that reason.

**A player checking a game can stand on any Stage without playing the ones
before it, and only if the project said so.** `CampaignSession::stages()`
publishes the campaign's encounter nodes in the order the flow reaches them
(breadth first from the entry, edges in `select_transition`'s own order, which
for a campaign without branches is the order the author wrote them), each marked
with whether this playthrough has stood there. `jump_to_stage` commits an empty
batch through `campaign::jump_to_node` and writes the slot at once. It is
offered on two screens and both draw from the same list: in a battle, through
`IntentKind::jump_to_stage` and `BattleReport::jump_to_stage`, which the battle
loop carries out without reading; and between battles, through
`ManagementVerb::jump`.

**The list is empty unless this was built with `GRANDLEON_STAGE_PICKER`**, and
that is the whole gate. `CampaignSessionOptions::stage_picker` defaults from that
define, so an image carries what it was built with. A front end offers the row
when the list it was handed is not empty and never reads the define itself, so
one place decides whether an image has the picker.

It is a build option and not a project field on purpose. A project that could
carry it could be shared with it left switched on, and a testing aid should not
move the bytes of anything anybody plays. `grandleon.stage_picker_absent` is
compiled without the define and holds the default down.

**What a jump does not do is the half that matters.** It moves the campaign and
changes nothing else: no objective recorded, no world flag set, nobody recruited
on behalf of the Stages passed over. So a transition out of a jumped-to Stage
can match nothing, a battle there can be unwinnable, and a board can refuse to
open at all. Tarnholt is the worked example: its last board carries "keep
Captain Mirea alive", and Mirea joins at a cutscene after the first battle, so
jumping straight there is `unavailable_objective_target`. **That is why the
picker is on the company screen as well as the pause menu** — a refused board
sends the player to the company, nothing they can do there recruits anybody, and
a jump has already written the slot. Without the second surface a jump could
leave a saved campaign standing at a Stage nothing can open. Inventing the
author's facts instead would be wrong differently at every branch;
`CampaignStage::reached` is what lets a screen tell a safe jump from a risky one
before the player takes it.

**The session derives nothing and a front end derives less.** Experience,
levels, growth rolls, what a battle left behind and what it spent are all one
function's answer, carried to the narrator exactly as that function returned it.
Who the player starts with is not the client's either: the campaign says so,
and a campaign that says nothing is refused by name rather than played with a
company the client made up. A member the author wrote onto a
node joins in that node's own completion batch, so a recruitment is one more
consequence of a battle rather than a second kind of thing.

## Who uses it

- `platform/desktop`: the terminal client and the SDL window, and the terminal
  again as the first front end to run a persistent campaign.
- `platform/nintendo64`: the play ROM's `N64Presenter`.
- `platform/playstation`: through `src/turn_client.cpp` below, which is *one*
  presenter a console compiles, and the host tool that derives that console's
  expected behaviour by replaying the same intents against the real engine.
- `platform/web`: the editor's Play mode, which links
  `grandleon_client_campaign` and drives `CampaignSession` step by step across
  the WebAssembly ABI.

The browser is the reason `CampaignSession` exists as an object: a terminal
owns its loop and calls `run_persistent_campaign`, a browser's battle is
clicks over many event-loop turns, so the sequence is a re-entrant object
and `run_persistent_campaign` is a thin driver over it. One thing genuinely
differs: where boards come from. A compiled game loads them out of a
package; an authoring session plays content that was never compiled.
`CampaignBoards` is that one seam, `PackageBoards` the answer every client
with a package uses, and everything above it (founding, exclusion, growth,
drops, the commit, the envelope) is one implementation.

## `turn_client.cpp`, the console presenter a console is

`src/turn_client.cpp` and `include/grandleon/client/turn_client.hpp` are the
interactive half of a console client: the cursor, the selection, the engine
queries a selection asks for, the unit action menu, the information sheet, the
aiming state, the route a slide is drawn along, and the transcript a checked run
reports. It draws no pixel, reads no port and names no machine.

**It is a source of each target, not a member of this archive**, and it has to
be: its campaign half is behind `GRANDLEON_TURN_CLIENT_CAMPAIGN`, which only
some targets set, and each console compiles it under its own cross toolchain.
One file, a compiler per machine, no archive that could hold only one of them.
The PlayStation compiles it twice, once with the macro and once without, which
is the clearest statement of why: the two builds are the same source and
different programs.

**The Stage picker is one screen and two ways in.** START over a battle opens
the board menu, which grows a fourth row — `GO TO ANOTHER STAGE - TESTING`,
under the way out, because it is an aid and not one of the two questions that
menu exists to answer. C on the company screen opens the same picker, on a
button rather than a row because that screen's caret walks the company. Both
build the row only when the list they were handed is not empty, which is only
in a `GRANDLEON_STAGE_PICKER` build; neither reads the define. The screen
itself lists the Stages under the author's own names, marks the one the campaign
is standing on `HERE` and every one it has stood on `SEEN`, and says once, under
its heading, that a Stage with neither mark may not open.

**A board that scrolls is shown across before it is played on.** When the window
is narrower than the board, the camera opens at the column showing its right edge
and travels to the left, so a player is told how much board there is rather than
finding out by walking off the screen. It ends where play begins, reaching the
left edge first and coming back if play begins further in, because the width is
what the gesture is for. Three frames a column, capped at ninety, on the first
drawn frame of a board and no later one — so a Stage reopened by the picker is
shown again, and a board redrawn mid-turn is not. A board that fits asks for no
frames and none are drawn.

It is the one gesture this client counts the frames for itself, and the reason is
ownership: the other three move a token, which is the platform's own pixel state,
while this moves `camera_`, which a platform can only read through the const
`camera()`. So the client walks the columns and asks the platform for one frame at
a time through `sweep_frame`, rather than handing out a mutable camera and the
ability to scroll the board at any other moment. It is deliberately not
interruptible: a press read inside it and then not acted on would desync every
recorded pad script in this repository.

A machine supplies `paint` and `next_press`, plus `paint_screen` in a campaign
build, and then four animation hooks, a checkpoint hold and an after-screen
hook, all with do-nothing defaults. The
three that are required are required because a client that defaulted them would
be a client that could pass without drawing anything. That last
part is the verification argument: a host tool that implements `paint` as an
empty function and reads a scripted table walks every other line of the file, so
what a console is checked against is derived from the rules by a different
compiler for a different architecture, ahead of the run. Adding an animation to
a console changes no expectation, because the host build overrides none of them.

The button names are the client's rather than any machine's (`pad_a`, `pad_b`,
`pad_c`, `pad_start`), and each console documents which of its own it maps onto
each. `platform/client/autopilot/fordlight_pad.h` is the controller script both
consoles play, and their two independent host derivations produce byte-identical
transcripts.
