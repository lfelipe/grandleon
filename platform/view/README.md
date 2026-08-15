# Board presentation model

The arithmetic three renderers would otherwise write three times: where a cell
lands on screen, which sheet variant its neighbours call for, and what covers
what.

Header only, integer arithmetic, no allocation. It includes no engine header,
no SDL and no libdragon, so the Nintendo 64 ROM, the SDL presenter and the host
test suite all compile the same file, and the whole of it is verified in
`tests/view/` with no console, window or browser in the loop. `camera.h` proved
the idea on the console; this is the rest of it.

```cpp
#include "grandleon/view/board_view.hpp"   // target: grandleon::view
#include "grandleon/view/motion.hpp"       // the same, in time
```

`board_view.hpp` says where a cell is. `motion.hpp` says when a token is
between two of them: the frame counts every client animates at, the route a
slide is drawn along, the cursor's pulse phase and the flinch offset. Same
rules (integer arithmetic, no allocation, no engine header), plus one more that
matters most: **no clock**. Everything there is counted in drawn frames,
because the console checks assert framebuffer pixels at named checkpoints and a
picture that depended on how fast the machine happened to be is a picture
nobody can pin. The numbers themselves live in `motion.hpp`, pinned by
`tests/view/motion_test.cpp`, and are mirrored in
`editor/src/domain/board-motion.ts` for the browser.

## Where it sits

**Above the three renderers, below the presenter seam.**
`platform/client` neither links this nor knows it exists: the session keeps
passing terrain to `battle_begins` and snapshots to `draw`, which is already
everything a renderer needs. Teaching the session about pixels would make
every future renderer the session's problem.

Nothing here is state. An elevation, a depth key and a pixel rectangle are
drawing concerns: they never enter a snapshot, they are never hashed, and a
client that ignores them draws a correct board.

## What it owns

| | |
|---|---|
| `Camera` | the top-left visible cell of a board larger than the screen, clamped to the map and following a cursor with a margin |
| `Projection` | cell → pixel, with a per-level vertical lift and the matching centre-sampling positions |
| `max_lift_for` | the bound on how far up the screen a cell may be drawn |
| `headroom` | the room a board needs above its first row so the tallest lift stays in frame |
| `neighbour_mask` / `autotile_variant` | the generator's eight-neighbour bit order and its `mask_to_variant` table, looked up in one place |
| `DrawList` / `depth_key` | a fixed-capacity, caller-owned list of items in back-to-front order |
| `ListWindow` / `scroll_legend` | `Camera` in one dimension: a window over a list longer than the screen, following a caret with a margin, and the legend that says which rows are up |
| `SlotMenu` / `slot_name_at` | the save-slot screen: its rows, what each slot is holding, the caret, the verbs the buttons carry, and the arming that stands between a held slot and being written over |
| `slide_frames_per_tile` / `slide_between` | how long a token spends crossing a tile, and where it is drawn part-way, landing exactly on the last frame |
| `plan_route` | the tiles a slide is walked along, breadth-first over the tiles the walk may be on: the simulation's reachability query plus the tiles the mover's own side holds, which a walk crosses without stopping. Allocation-free, caller-owned scratch, no knowledge of terrain |
| `cursor_emphasised` / `cursor_emphasis_inset` | the cursor's pulse phase, at rest at phase zero, and a ring that never reaches the centre pixel a probe samples |
| `flinch_frames` / `flinch_offset` | how far and for how long a struck token is knocked away from whoever swung, always ending at rest |
| `walk_cell` / `strike_cell` / `cast_cell` | which cell of a four-cell sequence sheet a token is drawn as, on a given frame of a walk, a blow or a cast. Every one starts and ends standing |
| `sequence_cell_ceiling` | why there is no fifth cell: 512 bytes a 32x32 CI4 cell against 2,048 texel bytes of the Nintendo 64's TMEM. A `static_assert` rather than a comment, because nothing else in the tree refuses one |
| `attack_gesture` / `reach_covers` | which of swing, shot and cast an attack is drawn as, derived from the separation and the striker's own reach bands. Never from anything the simulation was asked to report |
| `miss_frames` / `cast_hold_frames` / `gesture_frames` | how long each gesture runs, striker's half and target's half together |
| `projectile_frames_for` / `rise_and_fall` | a bolt's flight, and the one shape a projectile's arc and a spell's flare share: nothing at both ends, a peak in the middle |

Storage for the draw list comes from the caller, so the console holds one for
the life of the presenter and never allocates during a frame.

`board_view.hpp` is the board's. `list_view.hpp` and `slot_menu.hpp` belong to
two screens, the company and the save menu. They are here for the same reason
the camera is: both consoles draw them, a rule written twice is a rule that can
disagree with itself, and a list that scrolls wrongly fails *silently*.
Neither includes an engine header or knows what a campaign is: `SlotMenu` is
handed a base name and four booleans and has never seen a `SlotStorage`.

## The two rules worth reading before changing anything

**The projection is axis-aligned by decision, not by omission.** A rotated
diamond grid would re-open the art generator: the generated tiles are top-down
squares, and no runtime transform makes a top-down tree read as an isometric
one. A diamond grid also turns every tile into two triangles: 216 for a 12x9
board, measured at about 5.4 ms of Nintendo 64 submission. That is 30 fps
territory for a look rather than a capability. So this model offers a
vertical lift and deliberately nothing that could grow into a camera matrix.

**The draw order is `layer`, `elevation`, `batch`, row, column.** The middle
term is the whole occlusion rule rather than a heuristic: in an axis-aligned
top-down projection a cell's art overhangs only the cells *behind* it, and two
cells overlap exactly when the nearer one is drawn higher. Everything at one
elevation shares one offset and so cannot overlap anything else at that
elevation. That is what makes `batch` free: a renderer may group items by
whatever it pays for (a TMEM upload on the console, a texture binding on the
desktop) without changing the picture. Row and column come last only to make
the order total, so every client produces the same list whatever order it
walked the board in.

**The store column does not move while the roster scrolls.** That is a property
of how the two windows are configured rather than of `ListWindow` itself. A
company screen is two lists: the roster, whose window follows the caret, and the
store, whose window follows nothing and whose `top` is always zero. The roster's
window is a *fixed* seven rows whether the company is four or four hundred, so
the store's heading lands on the same page row and the same scanline whichever
machine draws it. The caret is on the roster; only the roster moves.

The store's own overflow is named by its legend rather than dropped, which was
the whole of the original complaint. Its stacks are reached where they have
always been reached, on a member's menu, which scrolls under its own caret.

**A margin wider than half a window is honoured as far as the window allows.**
`Camera::follow` has never been handed one, because a board's viewport is always
wider than its margin, and its two-sided arithmetic would answer such a request
by scrolling the cursor off the screen. A list is a few rows and a caller is
entitled to ask for context without knowing how few, so `ListWindow::follow`
clamps the margin to `(rows - 1) / 2`: the caret staying visible outranks the
context. This is the one place the two deliberately differ.

## Consumers, and the one place they legitimately differ

`platform/desktop`, `platform/nintendo64`, `platform/playstation`, and the
editor board, a TypeScript restatement in
`editor/src/domain/board-view.ts`. The C++ header is the specification; when
the two must change, they change together.

`list_view.hpp` and `slot_menu.hpp` have two consumers each and both are
consoles: `platform/client/src/turn_client.cpp` and
`platform/nintendo64/src/play_rom.cpp`, under their campaign defines.

`glyphs.hpp` is the odd one here and is honest about it: sixty-four letters, one
bit per pixel, and no arithmetic at all. It is in this library because a console
with no font of its own has to be given one, and a second copy of a letter is
how two machines come to spell a refusal differently. What is shared is the
*shape*; an expansion belongs to the machine that makes it and to nothing else.
The PlayStation expands the whole sheet into a 4bpp texture page.
`tests/view/glyph_test.cpp` pins the shapes, and the box most of all, because
every expansion walks a fixed rectangle per glyph and the PlayStation's pixel
claims name a particular ink pixel of a particular letter. The
desktop and the browser have their own idea of a list and their own save
picker, and neither is a fixed-height window over a fixed-width row, so neither
would gain anything from being made to share this one.

The console does **not** use `autotile_variant`. It embeds the four-variant
base sheets, whose whole point is that all four variants sit in TMEM at once;
the 47-blob sheet the mask table indexes is 24 KB and can never be resident.
So it keeps its own deterministic interior-variant choice, documented at the
call site. The desktop and the editor, which do hold the blob sheets, both go
through the shared lookup.

## Elevation

How high a terrain kind stands is art-library data, generated beside that
kind's keywords, flat colour and mark: `grandleon_terrain_elevation` in
`tools/placeholder_art/assets/themes.h`, and `TERRAIN_ELEVATION` in the
editor's generated board-art module. Mountains are two levels, hills one,
everything else zero.

It is deliberately not authorable per cell. Per-cell height would need a schema
field, a wire encoding and a compiler path. The moment the simulation sees it,
it is hashed and it is gameplay. Keeping elevation a property of a kind
keeps the whole feature on the drawing side of the seam. Content whose terrain
is all level draws pixel-identically to what it drew before any of this
existed, which is a property the clients preserve by reserving zero headroom
for it rather than by rounding to it.

## The lift is bounded; the elevation is not

How high a cell is **drawn** stops at `max_lift_for(tile)`: three eighths of a
cell. The number is geometry, not taste. A cell drawn `L` higher than the cell
behind it covers that cell's bottom `L` rows, and a centre row sits `tile / 2`
below its own top edge, so the centre behind survives exactly while
`L < tile − tile / 2`. Half a cell is therefore the *boundary*; a level is a
quarter of a cell, so two levels of that step land precisely on it. The art
library's tallest terrain, a mountain, is exactly two levels, so an unbounded
projection would sit on the boundary rather than inside it, and every "a cell's
centre is visible" argument in the tree would be a near-miss rather than a fact.
Three eighths is the halfway line less half a step, so a raised cell stops an
eighth of a cell short of the centre behind it, which makes "a cell's centre is
always visible" a fact every probe may rely on.

How high a terrain **is** stops nowhere. The level count is content: authored,
generated, carried whole, and read by anything that can afford real relief: a
3D exporter, a console with a depth buffer. Terrain taller than the bound keeps
its number and draws at the bound. Clamping the table instead would look
identical on every screen this repository draws today and would throw that
number away, which is the one form of the bound that could not be undone.

What the bound does *not* promise is that a whole cell rectangle is a cell's
own. Two rows at different heights share the band between them, and the unit
drawn there belongs to whichever cell the draw order put last. A reader that
scans a rectangle rather than a centre, like the desktop probe's faction-ramp
scan, excludes those rows. The bound keeps each band under half a cell, so
what remains is always the middle of the cell.
