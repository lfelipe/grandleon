# Playing on Nintendo 64 and PlayStation

Grandleon projects use the same campaign and combat rules on PC, Nintendo 64,
and PlayStation. Console builds adapt the controls, graphics, saves, and file
format to each machine; authors do not maintain a separate console version of
their game.

## Current 0.5 transition

The console players are in the middle of an engine transition. Nintendo 64 is
moving to Pyrite/Tiny3D and PlayStation is moving to Mipsync. Grandleon keeps
one path per console: these engines are not optional experiments selected at
export time, and the removed renderers are not kept as fallbacks.

The transition is complete for a feature only when the exported ROM or disc
can perform the same player-facing behavior as Godot. For 0.5 that includes
the full campaign flow, saves, one- and two-player ownership, tactical actions,
objectives and reinforcements, scrolling, help and settings, authored
deployment, close combat, localization and player names, and the castle's
hidden-area reveal. Visual polish may differ by machine and will continue to
improve after functional parity.

## Language during play

A translated game adds **Language** to the live Settings menu on both consoles.
Changing it immediately updates the compact interface and package-authored
title, stage, objective, dialogue, company, and content names; missing or
outdated translations continue to use the current source copy. The selected
language is stored with presentation settings, while an older settings save
still loads safely. The Nintendo 64 reads the catalog directly from the GPK;
the PlayStation exporter writes the same reviewed strings into each campaign
executable. No retired renderer or separate console translation file is used.

Common title, launch, pause, Settings, and outcome copy is available in English,
Spanish, and Brazilian Portuguese. The remaining uncommon help, tactical-result,
deployment, and error labels are still under compact-layout review.

## Showcase campaigns

**Sevenfold Gate** is the compact 0.4 showcase built to make the art range easy
to inspect. Its seven stages include:

- all seven character themes and all eight unit roles;
- male and female characters on both sides;
- every terrain kind across maps with different shapes and routes;
- one-player and local two-player play; and
- movement, conversation, attacks, counterattacks, misses, damage, and defeat.

Review builds include a Stage picker. The picker lets a reviewer jump between
gates without replaying the entire campaign; it does not change the content of
the stages. Both console battle menus can show live FPS. The counter begins off
in every ROM or disc image and stays a session-local testing preference.

Choosing **1 Player** leads to the save choice and then the first playable
stage. Founding or resuming a campaign no longer pauses on a separate roster
summary. A fixed ready-to-play company, including characters carrying their
normal equipment, does not stop at a roster or deployment screen; management
appears only for a real shared-store, deployment, or recovery decision.
**Help** is available from the title screen instead of appearing as a mandatory
page. Choosing **2 Players** retains each unit's authored local owner, with one
independent cursor and menu state per controller.

## Graphics

Each console has one renderer. It uses smaller versions of the curated Godot
character sheets, applies the authored faction colour through the same team
masks, and draws those characters over shaped, textured tactical terrain.
Movement and attack poses come from the same animation sheets rather than a
separate pixel roster. Tactical figures use about one and a half cells of
height, keeping the silhouettes readable without hiding as much of the
neighbouring terrain.
Steel and Cedar's authored 2×2 village houses and ancient groves have their
own connected silhouettes on both consoles rather than appearing as ordinary
mountains. They remain impassable mountain terrain in the game rules. The
console drawing is made from a few primitives over the existing terrain cell,
so these landmarks add no texture upload or VRAM allocation. On PlayStation,
the terrain textures follow a continuous low-poly surface and units, trees,
and the cursor follow its height rather than floating over a flat board.

The tactical interface uses one compact UI grammar on both machines. Action
menus have a framed header, a visible focus row, and the real controller's
confirm and back buttons. Unit status uses an owner accent and a health meter
instead of relying on an unstructured line of numbers. Panels move to the side
opposite the selected character, and the PlayStation status panel keeps the
same 32-pixel reservation, so this work does not shrink the board. The kit is
flat geometry plus the already-resident font: a full action panel has a fixed
seven-box base, focus adds three boxes, and a meter adds at most two. It
allocates no texture, palette, or frame-time memory.

The same grammar continues outside battle. Title and launch, save slots, help,
story, company and inventory, aftermath, Stage selection, character details,
warnings, confirmations, and endings use a shared bounded page made of header,
body, and footer regions. Save, company, inventory, and Stage rows use the same
focus treatment as battle actions. Footers call the Nintendo 64 buttons A, B,
Z, and START, and the PlayStation buttons Cross, Circle, Triangle, and START.
Story pages size their text panel to the lines on that page so unused panel
space does not cover the backdrop. A complete page costs the same fixed seven
flat primitives as a complete action panel and adds no texture or allocation.

During combat, the renderer can show the attacker and defender against a
terrain backdrop before returning to the unchanged tactical board. Characters
keep the same left/right positions for an attack and its counterattack. The
close view now establishes the matchup before impact and holds the damage or
miss afterward, so an exchange remains readable on hardware.

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
