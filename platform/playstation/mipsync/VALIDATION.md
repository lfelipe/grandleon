# PlayStation renderer validation

The private castle review now dismisses the authored opening through normal
input, waits for the opposing turn and combat camera to finish, walks Rowan
through the doorway, and records the revealed room with its post-move action
menu. Optional motion capture records the same input sequence. The test
requires the reveal between the settled approach and doorway-crossing logs.
Panorama visibility is driven by the active combat view rather than the
saved preference; actors remain staged through the return camera so the
board is restored only after the camera has pulled back.
The latest Bell combat window has zero missed deadlines (316 HBlanks average,
511 maximum). The larger one-player castle run has one missed deadline during
the opposing turn (625 HBlanks maximum), which remains a performance-polish
item and is disclosed on the private comparison page.

## Graphics pipeline work in progress

The indexed-texture work restores all nine character poses to 64×64, shares
one 256-colour unit palette per stage, gives terrain its own 16-colour
palettes, and pages the six mutually exclusive walking
frames through one VRAM slot. Idle and combat poses retain independent slots.
Terrain forms now have bark, foliage, and stone textures and material-grouped
triangle pairs. The full campaign/card probe passed on the indexed and paged
build; the subsequent material pass passed the focused movement/combat probe.
The latter performed an attack, healing, and opposing turns with zero missed
30 fps deadlines across its 600-tick timing window. Completed frame work
averaged 348 HBlanks and peaked at 511, leaving little worst-case headroom.

The subsequent Godot-panorama close view, grounded camera, and larger actors
also passed the focused attack/healing/opposing-turn probe. The 600-tick
window recorded 300 renders, zero missed deadlines, and completed frame work
averaging 316 HBlanks with a 512 maximum. Review video and stills are in the
`final-film` and `final-combat` subdirectories of the evidence root below.

The material build retains all audio, stack, and heap reserves:

| Encounter | Committed RAM | Texture VRAM |
| --- | ---: | ---: |
| Bell Orchard | 1,718,264 / 2,097,152 | 211,744 / 720,896 |
| Cedar Deepwood | 1,759,224 / 2,097,152 | 167,200 / 720,896 |
| Cedar Watch | 1,923,064 / 2,097,152 | 262,976 / 720,896 |
| Gate Village | 1,947,640 / 2,097,152 | 281,440 / 720,896 |
| Twin Gate Keep | 1,878,008 / 2,097,152 | 242,432 / 720,896 |

Evidence and experimental discs are under
`/home/lstrano/.cache/grandleon-ps1-indexed-build.mOZON2/`.
These are review builds; the measurements below describe the earlier release
candidate. Compatible stages now include Godot panorama plates. Landmark
silhouettes, additional backdrop variations, and hardware testing of pose
transfers remain open.

Validated on 2026-11-09 with the real two-player Steel and Cedar campaign.
This is package content running through Grandleon's shared campaign and battle
rules, not a hand-authored renderer demo.

## What the proof covers

The exact CUE/BIN boots through the pinned BIOS-style PCSX-Redux interpreter,
starts a new two-player campaign, saves it to a PlayStation memory card, and
continues it in a second emulator process. The probe visits live board views
for all five encounters:

- Bell Orchard, 8x6;
- Cedar Deepwood, 14x9;
- Cedar Watch, 16x12, including the survival objective and reinforcements;
- Gate Village, 18x12;
- Twin Gate Keep, 20x14.

The disc follows the campaign graph compiled into the game package. Story and
terminal nodes stay in that graph, and each encounter is a separate PS-EXE on
the same disc. The probe verifies every memory-card handoff before running the
real Twin Gate simulation. The same build supports one or two local players.

## Performance and memory

The player deliberately presents at 30 fps while input and tactical rules
continue at 60 Hz. The two-axis Cedar Deepwood scrolling pass recorded 300
renders and 300 scheduled holds across 600 rule ticks, with no deferred frames
or missed 30 fps deadlines. Its render work averaged 194 HBlanks and peaked at
313; completed frames averaged 195 and peaked at 314. The two-retrace maximum
is inside the intended 30 fps cadence and includes a live terrain-page change.
These figures include terrain relief, native forms, seam fill, and live units.

The separate Bell Orchard combat probe waits for routed movement to settle,
performs a real melee attack and healing ability with Player 1, spends Player
2's units through the second controller, and observes the authored opposing
turn. It also recorded 300 renders and 300 scheduled holds with no missed 30
fps deadlines. Render work averaged 310 HBlanks and peaked at 445; completed
frames averaged 311 and peaked at 447, inside the two-retrace close-view
budget. A separate mixed-axis capture verifies both walk poses, left/right
facing, north/south art, and the clear board while movement is still active.

Two focused action probes cover behavior that the release campaign does not
force into its short automated route. One selects Talk through the normal menu
and requires the exact package-authored conversation. The other moves and
attacks with the demo's item carrier, takes counter and opposing-turn damage,
then requires the newly usable field tonic to restore two health through the
shared `use_item` command.

A third focused probe covers authored tactical deployment. It boots the real
Muster Road encounter, shows its six authored tiles, selects the opening Dawn
Guard, places it on a different legal tile through the ordinary Cross-button
path, and presses Start. The probe requires both accepted simulation changes
and captures the live deployment board; fixed Steel and Cedar formations still
enter battle without this screen.

The focused Twin Gate probe boots the real one-player 20x14 encounter and uses
ordinary controller input. Rowan advances to the keep, both friendly units end
their turns, the authored opposing side takes its turn, and Rowan then crosses
the door. The log records the first settled approach before
`MIPSYNC_VISIBILITY_REVEAL cells=18`, followed by the settled doorway move.
Captured frames show the room covered before entry and the same board revealed
after entry. Evidence is kept outside source control at
`/home/lstrano/.cache/grandleon-mipsync-release05-castle/`.

The build keeps the planned 64 KiB audio reserve, 32 KiB stack reserve, and
512 KiB gameplay heap on every stage:

| Encounter | Committed RAM | Texture VRAM | Active cache |
| --- | ---: | ---: | ---: |
| Bell Orchard | 1,759,992 / 2,097,152 | 403,456 / 720,896 | 395 vertices, 352 triangles |
| Cedar Deepwood | 1,893,112 / 2,097,152 | 501,760 / 720,896 | 447 vertices, 336 triangles |
| Cedar Watch | 2,001,656 / 2,097,152 | 559,104 / 720,896 | 411 vertices, 320 triangles |
| Gate Village | 2,038,520 / 2,097,152 | 616,448 / 720,896 | 361 vertices, 268 triangles |
| Twin Gate Keep | 1,923,832 / 2,097,152 | 493,568 / 720,896 | 430 vertices, 324 triangles |

Larger maps use overlapping 8x6 terrain pages, with only the nearest page
active. Unit and prop culling changes presentation only; the full encounter
continues to run in the shared simulation.

## Visual checks

Unit cutouts come from the Godot graphics profile and retain authored figure
and team-colour differences. Board movement now cycles both authored side-view
walk poses and uses compact front/back frames when a route turns north or
south; close combat keeps the larger 64x64 pose bank. A dithered contact cue is
baked into each existing cutout, so grounding adds no entity, mesh, texture
page, or draw call. Terrain uses Godot-derived palettes on a
continuous low-poly surface whose shared corners keep neighbouring materials
connected. Units and the cursor use the generated surface height. Native
conifers, outcrops, reeds, crops, bamboo, rubble, and ice forms provide sparse
volume, while clustered surroundings, terrain-matched fog, and opt-in seam
fill remove the previous flat-card and floating-board appearance. The full
campaign capture is produced by the checked-in campaign probe so every stage
can be compared from the same build.

The close view repeats the encounter's dominant material across a 24-cell
floor extending beyond the camera frustum. It removes the visible edge of the
old single quad without adding another texture allocation or exceeding the
measured two-retrace budget.

The optional close-combat presentation keeps attacker and defender on fixed
sides during attacks and counterattacks. Forecasts, results, objectives, and
story screens use the compact in-game font rather than Sony's debug font. All
interface labels are generated live in the selected language; the old baked
English label texture is no longer shipped.

## Exact artifact

- CUE SHA-256: `1175e80086decd95d63c584f4375d420e64a5b609aec8876cf680bce6b912ae3`
- BIN SHA-256: `5f219fcd874bc05ffc48c508198722ffdf26666d2579bf62030a74d4e38b6b70`

The exact disc, build manifests, hashes, emulator logs, and captured
framebuffers remain outside source control at
`/home/lstrano/.cache/grandleon-release05-20261109/playstation/`.

The executable and BIN embed `GRANDLEON_PS1_RENDERER=MIPSYNC`. Mipsync is
pinned to `866f405a0e34b5925a8d832272fe52c657688430` and receives the six
checked-in upstream patches before each clean build.

## Remaining platform work

The remaining platform work is Grandleon's audio implementation, testing on
real PlayStation hardware, and continued visual and control polish. Mipsync is
the only PlayStation renderer and is never a silent fallback.
