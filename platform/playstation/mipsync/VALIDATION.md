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
VBlanks and a maximum interval of one VBlank. Cedar Deepwood averaged 165
HBlanks and peaked at 166; Twin Gate averaged 169 and peaked at 170. A separate
two-axis Deepwood scrolling stress pass averaged 163 HBlanks and peaked at 253,
again with zero missed VBlanks. These figures include the terrain relief,
native forms, seam fill, and live unit rendering described below.

The separate combat probe waits for routed movement to settle, performs a real
melee attack and healing ability with Player 1, spends Player 2's units through
the second controller, and observes the authored opposing turn. Board frames
keep their 60 Hz budget; the optional close view remains bounded to two VBlanks
per presented frame.

The build keeps the planned 64 KiB audio reserve, 32 KiB stack reserve, and
512 KiB gameplay heap on every stage:

| Encounter | Committed RAM | Texture VRAM | Active cache |
| --- | ---: | ---: | ---: |
| Bell Orchard | 1,375,608 / 2,097,152 | 126,976 / 196,608 | 214 vertices, 212 triangles |
| Cedar Deepwood | 1,406,328 / 2,097,152 | 143,360 / 196,608 | 193 vertices, 140 triangles |
| Cedar Watch | 1,457,528 / 2,097,152 | 159,744 / 196,608 | 199 vertices, 158 triangles |
| Gate Village | 1,459,576 / 2,097,152 | 176,128 / 196,608 | 183 vertices, 128 triangles |
| Twin Gate Keep | 1,420,664 / 2,097,152 | 135,168 / 196,608 | 165 vertices, 146 triangles |

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
- BIN SHA-256: `f5c6c37b266f134d4e716fffe6d2509c1579c1052f3c197344865ec8329e1d89`
- final memory-card SHA-256: `48fc152503401d53e177aaf1b60898ec904d0c09a1534a0b459bfd65c0a7cf5a`

The executable and BIN embed `GRANDLEON_PS1_RENDERER=MIPSYNC`. Mipsync is
pinned to `866f405a0e34b5925a8d832272fe52c657688430` and receives the five
checked-in upstream patches before each clean build.

## Still required before cutover

The remaining platform gates are the Grandleon audio implementation, testing
on real PlayStation hardware, continued visual and control polish, and the
final removal of the explicitly named comparison renderer. Mipsync is never a
silent fallback.
