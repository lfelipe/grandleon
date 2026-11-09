# PlayStation renderer validation

Validated on 2026-11-08 with the real two-player Steel and Cedar campaign.
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

Both measured campaign windows rendered all 600 frames with zero missed
VBlanks and a maximum interval of one VBlank. Cedar Deepwood averaged 178
HBlanks and peaked at 179; Twin Gate averaged 193 and peaked at 194. A separate
two-axis Deepwood scrolling stress pass averaged 163 HBlanks and peaked at 253,
again with zero missed VBlanks. These figures include the terrain relief,
native forms, seam fill, and live unit rendering described below.

The separate combat probe waits for routed movement to settle, performs a real
melee attack and healing ability with Player 1, spends Player 2's units through
the second controller, and observes the authored opposing turn. Board frames
keep their 60 Hz budget; the optional close view remains bounded to two VBlanks
per presented frame.

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
| Bell Orchard | 1,460,944 / 2,097,152 | 116,736 / 196,608 | 214 vertices, 212 triangles |
| Cedar Deepwood | 1,493,712 / 2,097,152 | 133,120 / 196,608 | 193 vertices, 140 triangles |
| Cedar Watch | 1,549,008 / 2,097,152 | 149,504 / 196,608 | 199 vertices, 158 triangles |
| Gate Village | 1,549,008 / 2,097,152 | 165,888 / 196,608 | 183 vertices, 128 triangles |
| Twin Gate Keep | 1,510,096 / 2,097,152 | 124,928 / 196,608 | 237 vertices, 182 triangles |

Larger maps use overlapping 8x6 terrain pages, with only the nearest page
active. Unit and prop culling changes presentation only; the full encounter
continues to run in the shared simulation.

## Visual checks

Unit cutouts come from the Godot graphics profile and retain authored figure
and team-colour differences. Terrain uses Godot-derived palettes on a
continuous low-poly surface whose shared corners keep neighbouring materials
connected. Units and the cursor use the generated surface height. Native
conifers, outcrops, reeds, crops, bamboo, rubble, and ice forms provide sparse
volume, while clustered surroundings, terrain-matched fog, and opt-in seam
fill remove the previous flat-card and floating-board appearance. The full
campaign capture is produced by the checked-in campaign probe so every stage
can be compared from the same build.

The optional close-combat presentation keeps attacker and defender on fixed
sides during attacks and counterattacks. Forecasts, results, objectives, and
story screens use the compact in-game font rather than Sony's debug font. All
interface labels are generated live in the selected language; the old baked
English label texture is no longer shipped.

## Exact artifact

- CUE SHA-256: `29d16c06436b8a5210108dcf67bbbd29ef0c78753e18cceed2e506a8f9ffecff`
- BIN SHA-256: `9aa16c5a9f5b9732a8d666cb1782cdefc2f40aa837af85e73996c4da542d628c`
- final memory-card SHA-256: `c0a525c5d9318ad4b88a629549168261a43be9948fb6c2902716e4656a4e1e26`

The exact build manifests, emulator logs, card, and hashes remain outside
source control at
`/home/lstrano/.cache/grandleon-mipsync-release05-campaign/`.

The executable and BIN embed `GRANDLEON_PS1_RENDERER=MIPSYNC`. Mipsync is
pinned to `866f405a0e34b5925a8d832272fe52c657688430` and receives the five
checked-in upstream patches before each clean build.

## Remaining platform work

The remaining platform work is Grandleon's audio implementation, testing on
real PlayStation hardware, and continued visual and control polish. Mipsync is
the only PlayStation renderer and is never a silent fallback.
