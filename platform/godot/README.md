# Godot PC client

This is the PC presentation layer for Grandleon. Godot draws and accepts input;
the existing C++ simulation, campaign loader, package format, and tactics policy
remain authoritative. The SDL client stays as a small reference surface, and
the Nintendo 64 and PlayStation builds consume the same portable projects.

See [PC_ROADMAP.md](PC_ROADMAP.md) for the current review, SteamOS test
checklist, known risks, and the ordered path from this test bundle to a
production PC release.
[CREATOR_ROADMAP.md](CREATOR_ROADMAP.md) defines the native create/play
architecture, standalone-game exports, Windows support, and console target
builder direction.
[UI_GUIDE.md](UI_GUIDE.md) defines the battlefield information budget and the
fun, quick, intuitive interaction rules used by the PC play surface.

The separation is deliberate:

| Layer | Owns | Does not own |
|---|---|---|
| `.gpk` project | maps, characters, dialogue, rules, presentation choices | PC paths or machine settings |
| C++ extension | package loading, campaign saves, legal ranges, deterministic commands and enemy turns | rendering or controller layout |
| Godot project | rendering, animation, and two-player input | combat rules or campaign outcomes |
| `runtime/local/controls.local.json` | one machine's controller and stick profile | project content or portable rules |

The application opens on one small home screen. It lists the included game and
locally created or imported games by title, followed by **Create / Edit Games**
and **Exit**. No path, project identifier, or machine name is
shown. The footer names only the directional control and the configured confirm
button. Automated captures and integration smokes can enter play directly, but
ordinary launches always begin at this screen.

Each portable PC bundle also has a private, local file-control channel under
`runtime/automation`. It lets test and support tooling inspect visible UI text,
send a bounded keyboard command, and ask Godot to capture its own viewport,
without depending on Steam or compositor screenshots. Use
`control-pc.sh inspect`, `control-pc.sh screenshot NAME`,
`control-pc.sh press KEY`, or `control-pc.sh pad DEVICE BUTTON` while the game
is running. Inspection reports only numeric connected-pad slots, never device
names or identifiers. Commands and JSON results stay inside the persistent
runtime directory, paths are never embedded in a project or bundle, and no
network listener is opened. Set `GRANDLEON_AUTOMATION=0` to disable the channel
entirely.

## Create and play projects

Choose **Create / Edit Games** on the home screen. The native Projects
screen starts with one primary **New Campaign** action. Name the adventure and
**Create & Open First Map** makes a complete small campaign—with heroes,
enemies, a goal, and a story scene—then opens its map immediately. **Empty
Project** remains an advanced option. Existing project cards expose only
**Continue Creating**, **Test Campaign**, **Write Story**, and **More**;
rename, copy, and export stay behind **More** until needed. Projects live under
Godot's per-user data directory and open focused native map or story creators.
**Export** creates a
`.grandleon.zip` containing only canonical source and project-owned assets;
local settings, recovery files, and machine paths are
excluded. Import accepts both that versioned form and compatible archives from
the earlier editor. It preserves portable identifiers unless they already exist
in the library, in which case the import becomes a clearly labelled copy with
fresh project and game identifiers. A normal duplicate also receives fresh
portable identifiers, while a rename preserves stable IDs and all authored
source content. Exported archives are placed in the per-user `exports`
directory. The controller-native import inbox lists archive names and sizes
from its sibling `imports` directory, can refresh while the application is
running, and never displays an absolute path. A desktop file browser remains
available as an optional mouse-and-keyboard route. Neither location is inside
the Steam installation. The map creator
previews the same replaceable terrain atlas as play mode, provides brush,
contiguous fill, and two-corner rectangle tools,
supports bounded command-level undo/redo, and resizes from the top-left origin
with an explicit clipping preview. The selected encounter's allied and enemy
placements are overlaid on the terrain and can be added, moved, or removed with
compact controller actions. Allied additions use unused campaign members;
enemies use portable unit types. Deployment regions have a visible board
overlay and an atomic draft/apply flow, so several cells can be toggled before
canonical source changes. **P1 / P2** assigns every allied placement to one of
the two co-op armies, with matching cyan and amber board badges; solo play
normalizes both armies under Player 1. The separate **2P** tool toggles an
opposing placement between always present and a two-player addition; scaled
enemies are labelled on the board.
**Talk & Story Choices** marks an opposing character whom players may talk off
the board alive. Selecting one opens a focused Write/Edit Conversation, Remove
Talk, and Done screen. Writing creates a portable in-battle moment and opens its
exact ordinary Story scene; generated scene, moment, and campaign-memory
identities remain hidden. The outline later offers **Talked to** and **Did Not
Talk to** conditions using visible character and battle names. A referenced
Talk choice cannot be removed, an unreferenced removal also clears its unshared
scene, and Copy Battle Setup does not duplicate chapter-specific conversation
memory.
The **Army** screen keeps the company and join timing readable, while **Tune
This Hero** opens bounded role-relative controls only when needed. Returning a
modifier to zero removes the override, and the same portable result is consumed
by PC, Nintendo 64, and PlayStation. **Role Gear & Skills** summarizes the
selected role and uses one named choice plus one Add/Remove action to share
compatible weapons, items, and abilities across every character with that
role. During **Add a Character**, **Make a New Role** copies an existing valid
role after asking only for a visible name and a named starting point.
The **Goals** surface attaches existing objectives or authors complete defeat,
protect, and survive conditions. **Outside** chooses how exposed space beyond
the tactical map is presented: one RGB colour, an automatic non-playable edge,
or a manually painted terrain perimeter. The creator reports the exact manual
cell count and recommends three cells of depth for complete PC camera coverage;
these cells never enlarge the board or become reachable. Edits that collide,
leave the map, remove the last ally, create an invalid deployment region, or
make a previously playable project invalid are refused. Shrinks are blocked
while any encounter placement or deployment tile would fall outside the new
board, and resizing preserves matching manual-perimeter coordinates while
filling newly exposed ones. Saves pass through the validating native source
layer, and the edited draft plays immediately. A draft automatically proceeds
through the native company gate, uses the same roster and deployment
validation as normal play, and has campaign persistence disabled so testing an
edit cannot resume or replace an installed game's checkpoints.
Compilation and scene handoff stay in-process; draft package bytes are not
written to the installation directory. **Main Menu** returns from play to the
same small game-and-creator front door.

**Story** opens a focused dialogue editor rather than adding writing controls
to the map canvas. A controller can choose scenes, lines, and speakers; a
keyboard enters the words. It can also create a complete scene with a portable
cast identity, backdrop, and opening line, then attach it atomically to an
existing campaign story moment. **Add Next Line** creates the frequent next
step beside line navigation; reorder and delete live under **More**, and the
last line cannot be deleted. **Speaker / Character** chooses a named project
character, or deliberately chooses no character art, while maintaining the
scene cast atomically. The scene panel changes the shared backdrop and previews
the same resolved portrait asset and backdrop wash used during play. Visible
line changes pass through the native integer-preserving source boundary
automatically when the author saves, navigates, changes the line list, tests,
or leaves; there is no separate Apply step. Bounded undo/redo, recoverable
save, and immediate draft play remain available. Campaign routing outside that
explicit attachment, console content, and unrelated source fields remain
unchanged.

The current creator creates and resizes maps, edits playable and surrounding
terrain, arranges both sides of existing encounters, and authors deployment
regions, allied player ownership, player scaling, objectives, dialogue lines,
and new dialogue scenes attached to existing story moments. **Grow the Army**
lists starting heroes and later recruits, previews their portable art, renames
them without changing identity, and adds an existing role at campaign start or
after a chosen chapter. A new named role can begin as a copy of any working
role without exposing source identities. **Tune This Hero** adjusts focused
portable traits; **Tune Role** owns the complete portable baseline, side,
traversal, post-attack rule, and gear-family allowances. **Growth & Rewards**
keeps experience, level growth, drops, and per-role appearance behind a
separate advanced action. **Campaign Supplies** gives the shared company named
starting items or weapons and chapter rewards. **Role Gear & Skills** assigns
compatible named weapons, items, and abilities to every character using a
role, while **Gear & Abilities** creates those reusable choices from safe
defaults or a working copy. **Look & Feel** chooses
the shared world theme and character style while previewing the actual terrain
and character assets used by the ports. Placing a character in a battle stays
an explicit Map → Battle Setup action. **Add Next Chapter** inserts a new story
moment, cloned playable encounter, and fresh map after a selected linear battle
as one undoable edit. **Continue Creating** opens a readable campaign outline;
story, battle, and ending rows open their exact focused action. **Outcomes**
edits common goal, inventory, and named Talk-choice routes, ordering, and
destinations. **Multiple Requirements** combines those named facts with
portable all, any, or single-not rules on a separate controller-focused screen.
**Add Route** connects an existing chapter, creates a named ending, or creates
an alternate playable story/map/battle by copying the current working setup
and rejoining the selected destination as one compiler-checked undoable edit.
**Change Chapter Order** moves a complete Story → Battle pair on one focused
screen while preserving its map, outcomes, alternate routes, and stable
identities. Imported general world-value comparisons remain preserved and
advanced because the source schema has no general operation that sets them.
Replacement art-pack import remains in the increments defined by
[CREATOR_ROADMAP.md](CREATOR_ROADMAP.md).

Godot is pinned to **4.7.2**, `godot-cpp` to **10.0.0-stable**, and the generated
bindings to the **4.7** API. The project uses the compatibility renderer to
cover a wider range of SteamOS hardware.

## Build a Linux PC bundle

The portable bundle includes the official Godot binary, the native extension,
imported assets, and the compiled player-facing campaign. It does not need system-wide Godot or
export templates.

```sh
platform/godot/scripts/build-steamos.sh
GRANDLEON_WINDOWED=1 out/grandleon-pc/launch-pc.sh
```

The first command downloads the pinned Linux editor binary when necessary and
verifies its SHA-512 checksum. Its output directory must not already exist, so
an old PC build is never silently overwritten. Pass another source project
or destination when needed:

```sh
platform/godot/scripts/build-steamos.sh \
  --project games/emberbound/source/project.json \
  --output out/emberbound-test
```

Before transferring a release, exercise its actual launcher, bundled Godot,
home menu, frame diagnostic, default graphics profile, and two overlapping
co-op routes while confirming the icon and privacy boundary:

```sh
platform/godot/scripts/smoke-pc-bundle.sh \
  --require-clean out/emberbound-test
```

The smoke attaches a temporary runtime symlink and removes it on exit; the
bundle remains ready for the target PC's persistent runtime directory.

Before an installed-test release, run the aggregate gate as well. It builds and
runs the native test suite, then serially exercises every Godot integration
smoke for graphics, controls, co-op, campaigns, checkpoints, project editing,
and project portability before repeating packaged acceptance:

```sh
platform/godot/scripts/smoke-pc-release.sh \
  --bundle out/last-signal-test
```

Every Godot child in these smoke scripts has a bounded runtime, so an
interrupted or regressed check cannot silently leave an indefinitely running
test process behind.

After the Projects screen has opened the inbox once, archives can be copied to
`runtime/data/godot/app_userdata/Grandleon PC/imports/` relative to the portable
bundle. Native exports appear in the sibling `exports/` directory. These are
runtime locations and are never compiled into a project or release manifest.

Campaign progress is runtime data as well. A completed battle is committed
through the shared portable campaign layer and written atomically below
Godot's `user://campaigns` directory; in the portable bundle that remains
below `runtime/data/`. Package identity and campaign identity scope every
checkpoint, so two imported games cannot silently resume one another. The next
launch resumes a compatible checkpoint through the engine's standard save
migrations. Leaving during an unfinished battle preserves the last completed
checkpoint instead of recording a partial outcome. The session menu reports
only checkpoint state, never its path. **Save / Load** exposes three manual
checkpoint slots. A manual save copies the current complete campaign state
while the Company screen is open between battles; loading a populated slot is
available from anywhere and restarts from that complete state. **New Campaign**
requires two confirms, resets the automatic resume campaign, and leaves manual
slots available until the player replaces them. No unfinished turn, animation,
or partial battle is represented as a save state.
Project archives, player-only exports, and presentation settings never include
campaign saves.

Before each persisted battle, the controller-native **Company** screen shows
the portable roster, fielded count and authored deployment capacity, each
member's availability and carried items, and the shared store. A player can
field or bench a placeable member, move one item at a time between a member and
the store, and then start the battle. Every change is committed and
checkpointed immediately by the shared campaign layer; the screen does not
keep a PC-only draft or reinterpret roster rules. Stick or d-pad navigation,
confirm, cancel, and Start cover the complete flow. Dead, unrecruited,
unplaceable, and over-capacity choices are disabled before submission, while
the engine remains authoritative if campaign state changes underneath the
presentation.

Copy that whole directory to the SteamOS machine. In desktop mode, add
`launch-pc.sh` as a non-Steam game, then return to gaming mode. The launcher
keeps Godot's data, config, and cache in the bundle's `runtime/` directory and
starts the two-player `emberbound` campaign full-screen. That package contains
one campaign: five battles whose two-person form assigns a separate allied army
to each player and whose solo form gives the whole company to player one. No home-directory or
host-specific path is compiled into the extension; the bundler checks the
native binary and refuses it if the checkout path remains.

Machine-only overrides belong in `runtime/local/`. When `runtime/` is linked
to persistent PC storage, control and graphics settings
survive atomic release upgrades without entering an immutable release. The
launcher prefers that private directory and retains bundle-root override files
only as a compatibility fallback. Packaged acceptance installs a temporary
control profile there and proves that the real launcher applies it to both the
home screen and gameplay.

The bundle root also contains `grandleon-icon.png`, a 512-pixel launcher image.
Use that file for the Steam shortcut and any desktop entry. Its stable location
inside the bundle means an atomic release-symlink upgrade can retain one icon
path instead of copying machine-specific shortcut metadata into the project.

The same builder can package one project as a player-only Linux PC game:

```sh
platform/godot/scripts/build-steamos.sh \
  --project path/to/project.json \
  --campaign campaign_id \
  --player-only \
  --output out/my-game
```

That form contains the compiled game but removes creator scenes and canonical
source templates. Its home screen has no creator entry. The ordinary bundle
retains both player and creator, so this is a packaging choice over the same
project format rather than a separate kind of game.

`BUILD-INFO.txt` identifies product release version, source commit, clean/dirty
build state, pinned Godot version, and default campaign/profile without
recording the checkout, user, hostname, endpoint, or other machine-specific
values. `LICENSE.txt`, `NOTICE.txt`, and the `licenses/` directory carry the
project, Godot, godot-cpp, and Godot third-party notices needed by the bundled
runtime.

Environment overrides are intentionally generic:

```sh
GRANDLEON_CAMPAIGN=emberbound GRANDLEON_PLAYERS=2 ./launch-pc.sh
GRANDLEON_GRAPHICS=diorama ./launch-pc.sh
GRANDLEON_WINDOWED=1 ./launch-pc.sh
```

The selected PC default is `voxel-sprites`. `pixel` remains the stable 2D
fallback, while `diorama` and `voxel` stay available for comparison.

Graphics profile, camera impulse, interface text size, and the three-level
sound-effects volume persist after they are changed in the session menu. The
versioned document contains only those four presentation values and lives
under Godot's `user://` data directory;
in the portable bundle that remains below `runtime/data/`. It cannot contain a
service endpoint, token name, controller identity, machine path, project field,
or campaign state. `GRANDLEON_GRAPHICS` remains an explicit per-launch
override and does not silently replace the stored choice. Remove
`pc-presentation.json` and its optional `.previous` recovery file to reset the
display choices.

## Local co-op

Each player has a cursor, selection, colour, and request queue. A lone
controller is Player 1 and can command the whole friendly team. Keyboard/mouse
may join the vacant Player 2 seat on its first gameplay input; a second
controller takes Player 2 directly. Connected pads are detected again after
startup, and a reconnect preserves every still-connected pad's player seat and
both cursor positions. The side panel and a short toast show which player
became ready, fell back to keyboard, received a ready spare controller, or is
waiting; unassigned extra pads cannot control either player or the session
menu. Controller model names are deliberately not displayed or stored.

Every essential flow is intentionally playable with one directional control
and five buttons: stick or d-pad moves the cursor, A confirms, B cancels, Y
selects the next action, X ends a character's turn, and Start begins play or
opens the session menu. Shoulder buttons and Back remain optional shortcuts,
not requirements. Configurable remapping is a production requirement so the PC
build can support compact controllers and other limited-button installations
without creating a separate game mode. The current build implements that
requirement through an optional local control profile while retaining the
stable default mapping above.

The play HUD does not keep that complete control list on screen. It follows the
last player to interact and shows only the current goal and usable buttons:
select a character, place during deployment, choose the active action's target,
wait for the opposing side, or choose somebody else. While one submitted action
is animating, the prompt explicitly says that another character can be
commanded, matching the concurrent team-phase rules instead of implying a turn
lock. After a move resolves, a small owner-coloured menu opens beside that
character and lists only actions that currently have a legal target, plus
**Do Nothing / Finish**. Each player navigates their own menu independently, so
one player choosing an attack never blocks the other player's cursor. Player
readiness remains two compact identity-free lines above it.

To remap buttons, copy `controls.example.json` to
`runtime/local/controls.local.json` and change values under `buttons`. The
optional `labels` object
maps physical buttons to the short legends shown throughout play, dialogue,
and the session menu; a five-button setup may use `1`, `2`, `3`, `4`, and
`MENU` without pretending its controls are lettered like a gamepad. Labels are
limited to eight visible characters and cannot contain markup or control
characters. Essential actions must remain
bound to distinct supported buttons; omitted values retain their defaults. Set
`previous_unit`, `next_unit`, or `menu_alt` to `none` when the controller lacks
those optional inputs. An unknown, disabled essential, or ambiguous mapping
activates the complete default profile instead of leaving the game partly
controllable. Supported button values are `a`, `b`, `x`, `y`, `start`, `back`,
`left_shoulder`, and `right_shoulder`; `none` is valid only for optional
actions. Invalid labels reject the complete profile just like invalid bindings.
The local file is ignored by Git, excluded from immutable releases, and
discovered only at launch time; controller names and identities are never
written into it. Because it lives below the persistent runtime directory, a
release upgrade does not replace it.

The optional `stick` object adjusts navigation without changing a project. Its
`pressThreshold` defaults to `0.78`: raise it toward `0.95` when a stick should
require a more deliberate tilt. `releaseThreshold` defaults to `0.30` and must
remain at least `0.15` below the press threshold. The left stick produces one
cardinal focus or cursor step for each complete deflection and must recenter
before another; a diagonal cannot become two steps. D-pad buttons remain
immediate and do not use these thresholds. Unsafe, mistyped, or overlapping
thresholds reject the complete local profile and restore safe defaults.
The Settings page exposes only two plain-language choices over that safe base:
**Deliberate** uses the profile threshold, while **Extra Firm** raises it by
`0.10` up to the safe `0.95` maximum. This presentation-only choice persists
locally and applies to both the next home screen and gameplay; it never changes
portable project content or records a controller identity.

Outside deployment and dialogue, the configured menu button (Start by default)
or optional alternate (Back by default) opens a controller-native session menu.
The stick or d-pad chooses an action and the configured confirm button (A by
default) or Enter confirms it. The root contains only **Resume**, **Save /
Load**, **Settings**, **Main Menu**, and **Exit Game**. Settings contains the
four graphics profiles, camera-impulse comfort switch, Normal/Large text,
Deliberate/Extra Firm stick input, Full/Low/Off sound effects, and the rolling
five-second FPS, p95 frame-time, peak frame-time, window-size, and internal-
canvas summary. Save / Load contains
the three manual checkpoint slots and the two-confirm new-campaign action.
Cancel or Escape goes back one page and resumes only from the root; exiting
requires focusing **Exit Game** and confirming it. There is no one-button quit
shortcut to trigger accidentally during play. The diagnostic values exist only
in memory and are never written to a report; the readout deliberately omits
graphics-device, display, controller, user, path, and network identities.

A completed or defeated campaign replaces the battle controls with a terminal
result page. It offers checkpoint loading or a new campaign through the same
save page, plus Main Menu and Exit Game. Cancel cannot dismiss the result back
onto a finished battle.

Between battles, a fixed authored formation starts automatically after its
story scene; it does not stop at an empty Company screen. A board that authors
a deployment region still opens Company with **Start Battle** focused, and a
build with the optional stage picker keeps that choice visible too. Move to a
roster member only to change the lineup or equipment; confirm then enters that
member's orders, and B returns to the roster or session menu. Giving,
returning, fielding, and benching each save before the screen accepts another
input.

Co-op campaigns should author `sideBlocks`. That engine rule carries no
exclusive unit activation: one character can move, a second can move, and the
first can then act. The Godot bridge accepts both players' commands while the
previous visual action is still resolving. Commands are serialized only at the
deterministic simulation boundary, where collisions and conflicts have one
portable answer. The board keeps one animation track per unit, so two accepted
moves received in the same rendered frame animate together.

The configured finish button (X by default) marks the selected character done.
With no character selected it finishes every ready character not currently
claimed by the other player, so a team can hand control to the opposition
without hunting across the board. The live help spells out both meanings.

The first local control uses cyan, the second uses amber, and the opposition
uses red. Remaining-health bars and cursor outlines carry those colours; no
`P1` or `P2` labels compete with the character art. When no character is
selected, the side panel shows the colour-to-control legend. Inspecting or
selecting a character replaces it with only that character's useful numbers.

Encounter difficulty can be authored visibly rather than inferred from a stat
multiplier. An opposing placement with `minimumPlayers: 2` is omitted from a
one-player session and fielded unchanged when two local players participate.
The in-game map editor authors this with its **2P** tool and does not offer a
conditional placement as an objective target. The rule lives in the project
and compiled package; local paths, controller identifiers, and machine details
never enter it.

This is not a second real-time simulation. It is concurrent local control of a
team phase, preserving replays and package compatibility on every target. The
Nintendo 64 and PlayStation clients consume the same co-op maps and now keep an
independent cursor and selection for each of their first two controller ports;
only the richer overlapping presentation differs on PC.

## Replaceable graphics

[project/scripts/art_library.gd](project/scripts/art_library.gd) is the current
PC art boundary. The package publishes
only portable menu indices—terrain kind/theme and character
archetype/colour/style/figure. The art library maps those values to resources.
Rules and source projects do not name Godot files.

The checked-in project does not duplicate generated art. CMake stages
`tools/placeholder_art/assets/modern` into `res://assets`, including its
manifest. A replacement pack can preserve the manifest vocabulary, or a new
renderer can implement the same lookups for `.glb` models, voxel chunks, or a
different sprite atlas. See [GRAPHICS_DIRECTIONS.md](GRAPHICS_DIRECTIONS.md) for
the evaluated paths. The comparison set is generated by the runnable Godot
profiles—not image-generation prompts—and is reproduced outside the repository
with `scripts/capture-graphics-prototypes.sh`.

Four launchable profiles exercise the choice without changing the
package or rules: `pixel` uses the existing sheets, `diorama` uses lit 3D
terrain with the real sheets as billboard characters, `voxel-sprites` uses
box-built terrain with those same characters, and `voxel` builds both terrain
and characters in 3D. The full-voxel profile binds imported, skinned commander,
rogue, stormcaller, and drone GLBs with in-place idle, run, attack, ability,
hurt, and defeat clips. Per-binding display scales keep each humanoid and the
wider quadruped inside a tactical cell without modifying the source GLB; other
archetypes retain their procedural fallback. Select one with
`--graphics=PROFILE` in a developer run or `GRANDLEON_GRAPHICS=PROFILE` in the
SteamOS bundle. During play, the session menu can replace only the renderer and
rebind the current board, cursors, selections, and highlights so the four
directions can be compared on the target PC without restarting the campaign.

`content/graphics_profiles.json` is the versioned PC art contract for camera,
lighting, sprite scale, renderer choice, and the terrain/material palette. To
tune one PC without changing a project or committing machine details, copy
`graphics.example.json` to `runtime/local/graphics.local.json` and edit only
the values to override. `cameraImpulse` and bounded `textScale` values
supply the session's initial accessibility settings; the controller menu may
then change either for the current run. The launcher discovers that ignored
persistent file at run time; its path and contents are never compiled into the
bundle.

An authored pack may also bind `assets.terrainScenes` by terrain name and
`assets.characterScenes` by archetype to project resources. A character
binding may set its display `scale`, animation-player path, and named clip map,
so replacement proportions do not become renderer assumptions. Imported `.glb`
files and `.tscn` scenes both arrive as `PackedScene` resources in Godot. Each
scene uses a one-cell origin and the manifest's `sceneScale`; surface heights
remain explicit in the same profile. Missing or invalid bindings fall back per
asset to procedural terrain or the existing sprite, so an artist can replace
the roster incrementally instead of delivering an all-or-nothing pack.
The recommended profile exercises this boundary with authored low-poly water,
road and bridge, forest, mountain, sand, ruins, and grass scenes. Together they
cover every terrain used by The Last Signal; procedural fallback remains
available for the wider terrain vocabulary. Scene animation, shaders, and
detail meshes belong to the scenes, not the board renderer, so a replacement
may remove or redesign them freely.

A terrain binding may be a direct scene path or a context-aware binding. The
current road selects and rotates a bridge scene when a cardinal neighbour is
water, without teaching the renderer an art filename:

```json
{
  "road": {
    "scene": "res://art/terrain/relay_road.tscn",
    "whenAdjacent": {
      "water": {
        "scene": "res://art/terrain/relay_bridge.tscn",
        "rotateToAdjacent": true
      }
    }
  }
}
```

An absent contextual scene still falls back to the binding's base scene; an
absent or invalid binding falls back to the procedural tile.

A binding may also provide a `variants` array. Each entry accepts the same
scene form plus an optional `rotationYDegrees`; a stable hash of map coordinate
and terrain kind selects one without saving randomness into the project. The
relay pack rotates its asymmetric forest, mountain, sand, and grass detail to
break repetition. A later pack can point those entries at entirely different
scenes while retaining the same portable map.

Profiles may name another profile with `inherits`; dictionaries are merged
before the selected profile is applied over `common`. The full-voxel profile
uses this to share the complete relay terrain pack with `voxel-sprites` while
replacing its character renderer and bindings. This keeps terrain replacement
in one manifest block instead of requiring artists to update two copies.

Authored character bindings may additionally name an `AnimationPlayer` and map
semantic `idle`, `move`, `attack`, `ability`, `hurt`, and `defeat` states to the
clips exported by that asset. Locomotion follows the engine-derived multi-cell
route, faces its current leg, and uses portable per-cell timing; the resulting
snapshot remains authoritative. See [ANIMATION_PIPELINE.md](ANIMATION_PIPELINE.md)
for the complete presentation-event contract.

Sprite bindings may set `facesRight` to describe the sheet's native direction
and `directionalFlip` to permit automatic horizontal mirroring. Both default to
`true`. Pixel 2D and hybrid billboards share the same behavior: movement
mirrors a directional sprite relative to the screen, while an attack or
ability first faces its target. A mirrored off-centre pivot remains anchored
to the same tactical cell. Set `directionalFlip` to `false` for a deliberately
front-facing or asymmetric sheet. These values are renderer metadata only and
never enter a portable project or save.

The checked-in `relay_commander.glb`, `relay_rogue.glb`,
`relay_stormcaller.glb`, and `relay_drone.glb` prove normal skinned-mesh,
armature, material, and animation imports. Commander, rogue, and stormcaller
share the exact sixteen-bone humanoid hierarchy while presenting different
meshes, materials, equipment, and clips; the drone uses its own quadruped rig.
Explicit profile maps translate exported action names such as `Relay_Run`,
`Kestrel_Sprint`, `Morrow_Arc`, and `Drone_Scamper` into engine semantics. The
earlier `relay_drone_3d.tscn` and `relay_commander_3d.tscn` scenes remain
procedural references. The board still owns destination, facing, health,
shadows, effects, and removal. Run `scripts/generate-imported-characters.sh`
with Blender to regenerate all four GLBs; ordinary builds consume the committed
assets and do not require Blender. Run `scripts/smoke-animated-characters.sh` to
import all active replacements and sample every required clip and skeleton
response headlessly.

The generated pack currently supplies four 32×32 pose cells plus a separate
standing image. Their size is not a renderer rule: the art manifest publishes
each sheet's cell dimensions and columns, a normalised ground pivot, and named
idle, move, attack, ability, item, talk, hurt, and defeat sequences with their
frame indices and timing. Both sprite renderers consume that data. Idle units
use the standing image, moving units loop only the two walk poses, and combat
events hold the attack or cast pose while their procedural or replaceable
effects play. A higher-resolution pack may change cell dimensions without a
package, simulation, or renderer change.

The recommended profile now proves that contract with four 1772×886 sheets for
Rook, Kestrel, Morrow, and the relay beast. Each sheet supplies eight 443×443
cells with independently timed idle, walk, weapon, cast, hurt, and defeat
poses. `assets.characterSprites` keys each replacement by
`archetype|faction-colour|style|figure`; its path, grid, pivot, and named frame
sequences live entirely in the graphics profile. A missing path, invalid grid,
or absent identity falls back to the generated character independently. Copy
the example entry in `graphics.example.json` to exercise a different sheet
without touching package data or simulation code.

## Developer build

```sh
cmake -S . -B build-godot \
  -DGRANDLEON_BUILD_GODOT=ON \
  -DGRANDLEON_GODOT_API_VERSION=4.7 \
  -DGRANDLEON_WERROR=ON
cmake --build build-godot --target grandleon_godot_project
```

`scripts/smoke-presentation-settings.sh` verifies atomic save/recovery, the
strict presentation-only whitelist, restart persistence, and command-line
override precedence under an isolated user-data directory.

`scripts/smoke-pc-audio.sh` verifies the complete built-in cue set, bounded cue
lengths, safe volume levels, mute behavior, and semantic move, weapon, magic,
impact, restoration, and defeat mappings. The PC autoload supplies these
deterministic defaults to every native screen and game; later audio assets can
replace that one presentation layer without changing a project or its rules.

`scripts/smoke-campaign-persistence.sh` completes a real battle, verifies its
opaque checkpoint, resumes it in a second Godot process, then exercises the
in-app reset and proves the next campaign starts fresh.

`scripts/smoke-checkpoint-slots.sh` commits a real between-battle company
change, copies it into manual slots, loads one through the native Godot bridge,
and verifies that empty and out-of-range slots are refused.

`scripts/smoke-campaign-management.sh` renders a real two-member company, moves
an item to the store and back, benches and fields the same member, starts the
battle, and verifies that the gestures produced one non-empty checkpoint.

`scripts/smoke-home-menu.sh` verifies that the packaged entry scene publishes
the included game and the minimal controller-first home actions.

`scripts/smoke-frame-timing.sh` verifies the rolling frame-time calculation,
its deliberately narrow data shape, and the session-menu integration.

`scripts/smoke-control-profile.sh` also verifies that the contextual play HUD
uses remapped labels for action selection and simultaneous-command guidance,
rather than falling back to hard-coded gamepad letters.

The runnable project is staged at
`build-godot/platform/godot/project/app`. Compile a package into that directory,
run one editor import to generate the extension/import cache, then launch Godot
with `--path`.

To build and capture all four graphics profiles under the same campaign state:

```sh
platform/godot/scripts/capture-graphics-prototypes.sh
```

To record the paced campaign reel used for remote visual review:

```sh
platform/godot/scripts/record-demo-reel.sh
```

To validate default, remapped, disabled-optional, and rejected ambiguous
controller profiles—including their live interface labels:

```sh
platform/godot/scripts/smoke-control-profile.sh
```

To verify stable controller seats through connect, disconnect, reconnect,
keyboard-plus-pad, and extra-pad cases:

```sh
platform/godot/scripts/smoke-controller-seats.sh
```

To validate the route sampler and then require two concurrent accepted moves
to publish legal paths and timing through both the 2D and 3D renderers:

```sh
platform/godot/scripts/smoke-motion-path.sh
```

To validate manifest-driven sprite dimensions, pivots, frame selection, timing,
legacy defaults, and a non-32-pixel replacement sheet:

```sh
platform/godot/scripts/smoke-art-library.sh
```

To create a project, paint and undo/redo a real map cell, save and compile the
result, play it, and return to the same editor session:

```sh
platform/godot/scripts/smoke-map-editor.sh
```

To edit, undo/redo, save, reopen, and compile a portable dialogue line through
the focused native story screen:

```sh
platform/godot/scripts/smoke-story-editor.sh
```

To add, rename, schedule, undo/redo, save, reopen, and compile portable campaign
characters through the focused Army screen:

```sh
platform/godot/scripts/smoke-army-editor.sh
```

To validate the campaign outline, common and combined outcome editing, named
endings, portable alternate playable chapters, hidden stable identities,
persistence, and one-step undo/redo:

```sh
platform/godot/scripts/smoke-campaign-editor.sh
```

To regenerate the native creator screens used by the user guide from fresh
per-capture project data and the actual rendered UI:

```sh
platform/godot/scripts/capture-creator-guide.sh
```

The reel is an actual 1280x720 Godot capture. It advances authored dialogue,
queues two friendly moves without serial player turns, and lets the built-in
opposition respond. Its two automation controls use the same cyan and amber
cursors and health bars as physical local co-op, so review footage proves the
current two-player presentation as well as its overlapping commands. Set
`GRANDLEON_DEMO_SECONDS` or pass build and output paths as the first two
arguments when a different duration or destination is useful.
Each invocation uses and removes a fresh capture-only save directory, so an
earlier reel cannot advance the campaign seen by a later recording.
Set `GRANDLEON_DEMO_GRAPHICS` to `pixel`, `diorama`, `voxel-sprites`, or
`voxel` to record the same sequence through another replaceable renderer.
