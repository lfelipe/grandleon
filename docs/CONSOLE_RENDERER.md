# Playing on Nintendo 64 and PlayStation

Grandleon projects use the same campaign and combat rules on PC, Nintendo 64,
and PlayStation. Console builds adapt the controls, graphics, saves, and file
format to each machine; authors do not maintain a separate console version of
their game.

## Sevenfold Gate

The 0.4 console showcase is **Sevenfold Gate**, a short campaign built to make
the current range easy to inspect. Its seven stages include:

- all seven character themes and all eight unit roles;
- male and female characters on both sides;
- every terrain kind across maps with different shapes and routes;
- one-player and local two-player play; and
- movement, conversation, attacks, counterattacks, misses, damage, and defeat.

Review builds show live FPS and include a Stage picker. The picker lets a
reviewer jump between gates without replaying the entire campaign; it does not
change the content of the stages.

Choosing **1 Player** leads to the save choice and then the first playable
stage. A fixed ready-to-play company does not stop at a roster or deployment
screen. **Help** is available from the title screen instead of appearing as a
mandatory page. Choosing **2 Players** retains each unit's authored local
owner, with one independent cursor and menu state per controller.

## Graphics

Each console has one renderer. It uses smaller versions of the curated Godot
character sheets, applies the authored faction colour through the same team
masks, and draws those characters over textured tactical terrain. Movement and
attack poses come from the same animation sheets rather than a separate pixel
roster.

During combat, the renderer can show the attacker and defender against a
terrain backdrop before returning to the unchanged tactical board. Characters
keep the same left/right positions for an attack and its counterattack.

Console exports contain this renderer and its project-selected artwork only.
There is no renderer selector or alternate character-art path in an exported
game.

Console graphics remain deliberately lighter than Godot's presentation. The
current priorities are readable units and walkable cells, stable performance,
clear selection and movement ranges, and smooth transitions. Terrain variety,
effects, animation, close scenes, and interface artwork will continue to
improve before 1.0.

## Export a game

Open **Finish & Export** in the Godot creator. With the matching optional
builder installed, Grandleon can produce:

- a Nintendo 64 `.z64` ROM; or
- a PlayStation `.bin` file with its matching `.cue` file.

The builders are separate downloads because their pinned toolchains are large.
Once installed, they work offline. A battle can field at most 48 characters on
either console; the creator warns about that console limit without preventing
the project from being played on PC.

The PlayStation image intentionally omits Sony's licence sector. It works with
compatible emulators and FPGA systems but does not boot on an unmodified retail
PlayStation. Keep each `.bin` and `.cue` together and open the `.cue`.

Platform-specific controls, build commands, and verification are in the
[Nintendo 64 guide](../platform/nintendo64/README.md) and
[PlayStation guide](../platform/playstation/README.md).
