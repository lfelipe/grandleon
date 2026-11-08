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

Both measured gameplay windows rendered all 600 frames with zero missed
VBlanks and a maximum interval of one VBlank. Cedar Deepwood averaged 199
HBlanks and peaked at 200; Twin Gate averaged 117 and peaked at 118.

The build keeps the planned 64 KiB audio reserve, 32 KiB stack reserve, and
512 KiB gameplay heap on every stage:

| Encounter | Committed RAM | Texture VRAM | Active cache |
| --- | ---: | ---: | ---: |
| Bell Orchard | 1,383,800 / 2,097,152 | 135,168 / 196,608 | 292 vertices, 146 triangles |
| Cedar Deepwood | 1,414,520 / 2,097,152 | 151,552 / 196,608 | 304 vertices, 152 triangles |
| Cedar Watch | 1,461,624 / 2,097,152 | 167,936 / 196,608 | 252 vertices, 126 triangles |
| Gate Village | 1,461,624 / 2,097,152 | 184,320 / 196,608 | 156 vertices, 78 triangles |
| Twin Gate Keep | 1,420,664 / 2,097,152 | 143,360 / 196,608 | 160 vertices, 80 triangles |

Larger maps use overlapping 8x6 terrain pages, with only the nearest page
active. Unit and prop culling changes presentation only; the full encounter
continues to run in the shared simulation.

## Visual checks

Unit cutouts come from the Godot graphics profile and retain authored figure
and team-colour differences. Terrain is not only a flat colour layer: the
paged board meshes contain deterministic low-poly outcrops, shrubs, reeds,
crops, bamboo, rubble, and other forms, while forests add trunks and crossed
canopies. The full campaign capture is produced by the checked-in campaign
probe so every stage can be compared from the same build.

The optional close-combat presentation keeps attacker and defender on fixed
sides during attacks and counterattacks. Forecasts, results, objectives, and
story screens use the compact in-game UI rather than Sony's debug font.

## Exact artifact

- CUE SHA-256: `29d16c06436b8a5210108dcf67bbbd29ef0c78753e18cceed2e506a8f9ffecff`
- BIN SHA-256: `f55e3613d0e081fb84fc5745cf63ab24428a1d0ebf7fda1bd971ad3496e1c8ba`
- final memory-card SHA-256: `48fc152503401d53e177aaf1b60898ec904d0c09a1534a0b459bfd65c0a7cf5a`

The executable and BIN embed `GRANDLEON_PS1_RENDERER=MIPSYNC`. Mipsync is
pinned to `866f405a0e34b5925a8d832272fe52c657688430` and receives the four
checked-in upstream patches before each clean build.

## Still required before cutover

The remaining platform gates are the Grandleon audio implementation, testing
on real PlayStation hardware, continued visual and control polish, and the
final removal of the explicitly named comparison renderer. Mipsync is never a
silent fallback.
