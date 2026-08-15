# Unit sheet

The same words about a character on every client.

```cpp
#include "grandleon/sheet/unit_sheet.hpp"   // target: grandleon::sheet
```

A player asks for a character's sheet from the unit action menu. It answers what
the character *is*, not what it would do to the enemy under the cursor. This
library is what the menu row opens: it takes the snapshot the engine produced
and the weapon, ability and item registries the encounter was created with, and
returns fixed lines of text.

## Why it is not a panel per client

Left to themselves, clients say different amounts about a character: six rows
that follow the cursor on one, a status line on another, a sentence in the
browser. Each decides for itself which of a character's numbers are worth
saying. That is how a client comes to disagree with the rules about what a
character is: not by computing something wrong, but by quietly never mentioning
the stat that decided the last fight.

So the sheet is built in one place and drawn everywhere. A console blits the
lines into a framebuffer, the terminal prints them, and neither chooses the
content.

## What it never does

**It computes nothing.** Every number in a line is read off the snapshot or off
the registry. The one number a reader might expect and will not find is the
chance a strike lands: that folds the striker's skill and luck against the
*target's* evasion and luck, so it needs a target, and it belongs to
`forecast_attack` and the panels that show a forecast. The sheet states the
weapon's authored accuracy and the four unit stats that move it. That is where
the roll starts, and all a reader needs to read a forecast when one appears.

**It allocates nothing.** `UnitSheet` is a fixed block of characters and the
formatter is written here rather than taken from the C library, because these
lines are compared byte for byte between a console and a host, and because a
client that allocated to draw a panel would perturb the heap census the ROM
takes beside it.

## The width, and the words that fit it

Forty columns. Both consoles are 320 pixels across, which is forty characters
of an eight-pixel font, and a line that fits the narrowest surface fits every
surface. Sixteen lines of capacity is
four more than the shipped vocabulary fills.

Everything fits on one screen because the labels are three letters. That is the
whole of how the console budget was paid. There is no paging: the widest line
the vocabulary can produce is thirty columns of forty, and the tallest sheet is
twelve lines of sixteen.

| | | |
|---|---|---|
| `HP` | health / maximum | `AP` | action points |
| `MOV` | movement | `SPD` | speed |
| `STR` | strength | `DEF` | defence |
| `RES` | resistance | `MAG` | magic |
| `SKL` | skill | `LCK` | luck |
| `EVA` | evasion | | |
| `RNG` | a reach band | `HIT` | a weapon's accuracy |
| `x` | how many of an item are left | `HEAL` | what spending one gives back |

The three stat lines are groups rather than a wrap, and each answers one
question: what a character can spend this turn (`HP AP MOV SPD`), what it deals
and what it takes (`STR DEF RES MAG`), and whether a blow lands either way
(`SKL LCK EVA`). Four labels is the widest line; a fifth stat on a line splits
the line rather than shortening the words. Browser Play has no pixel budget and
still draws the same three groups in the same order, with the words spelled out.

Reach is not among them, and deliberately. A character's reach is a property of
what they are holding rather than of the character, so it is stated once per
weapon row and never on a stat line, where it would be that fifth label.
What a character adds to a weapon's reach is theirs all the same: a weapon row's
`RNG` is the authored weapon's floor and the *character's* ceiling, widened by
`simulation::UnitSnapshot::reach_bonus` and saturating where the engine
saturates. An ability row is not widened, because an ability's reach comes from
the ability rather than from the caster.

A stat added later takes three letters by the same rule and joins the line that
answers its question: a second defensive stat on line 2, a second targeting
stat on line 3. Four labels per line is the bound the widest line sets; a fifth
would need the line split rather than the labels shortened further. Paging is
worth its presses only when a character's weapons and abilities take the sheet
past `unit_sheet_capacity`, and the shape is already cut for it: the header and
the three stat lines are page one, the two lists are page two, and the caret
keys are free while the sheet is up because nothing on it is selectable.

## Display names

The engine knows stable content identities and nothing else, so a name is the
client's business. It is one table rather than one per client, because three
byte-identical copies are three chances to disagree. `unit_type_name`,
`weapon_name`, `ability_name` and `item_name` are that table, once. Both
consoles' action menus read it too, which is what stops a menu row and a sheet
row from calling one weapon two things.

## Who a character is

`character_name` answers it, and every client asks rather than deciding. It
tries three sources in order: what the campaign calls the roster member standing
in this character, what the author wrote on this placement, then the character's
unit type with an ordinal when the board fields more than one of that kind. So
nobody is ever nameless, only unnamed. The ordinal is taken from ascending
placement identity, which is the same order on every machine, and counted over
everybody the board holds rather than over whoever is still standing, so a name
does not change when the character in front of it falls.

Only the first of the three needs a client: the join from a board character to a
roster member is the session's, and a client passes the answer in. The rest is
read here, out of the package.

`class_name` is the row underneath: the class a character's unit type belongs
to, which is not the unit type. `DAWN ARCHER` and `ASHEN ARCHER` are two unit
types wearing two factions' colours and one class, `ARCHER`, whose stats and
growth they both take. The name says which character this is; the class says
what a character of that kind can do.

Neither is a number. A character's place in the board's list is the terminal
client's addressing scheme, for `move 3 5 2`, and it belongs on the surface a
player types at and nowhere else.

## Where it sits

Above the presenter seam and beside `platform/view`. It links `simulation` for
the snapshot's shape and `core` for content identities, and nothing else: no
window, no console header, no allocation. `tests/sheet/` pins every line against
a snapshot built by hand, with no console in the loop.
