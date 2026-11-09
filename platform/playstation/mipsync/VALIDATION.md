# PlayStation closing review

Reviewed on 9 November 2026. Mipsync is the only PlayStation renderer;
missing dependencies or unsupported exports fail rather than selecting an
older renderer. Nine runtime patches and a Windows build-helper fix apply to
the pinned upstream revision `866f405a0e34b5925a8d832272fe52c657688430`.

## Corrected in this review

- Terrain pages overlap beyond the nominal 8×6 camera region, so scrolling
  does not expose missing floor. Only used grid corners are exported, and
  raised scenery uses a narrower guard than the floor.
- Authored castle-wall identities survive export. Twin Gate now has stone
  walls and battlements instead of mountain cones.
- Hidden-room covers use upward-facing geometry. The castle interior is
  visibly covered before entry, then revealed by normal doorway movement.
- Menus with fewer than five actions no longer underflow their first row.
- Disabled terrain pages release their static renderer-cache reservations.
  This is a standalone upstream patch, with no campaign-specific engine code.
- Units remain visible through terrain-page changes, removing a one-update
  blink. The scrolling probe can capture these transitions as a video.
- Safe projected polygons use native GPU clipping at screen edges, while
  depth, saturation and polygon-size guards retain the CPU path where needed.
  Bounded vertex stamps also reduce visibility-cache construction cost.
  Both changes are separate upstream patches; the clipping patch includes
  a passing native test for GPU bounds and depth safety.

## What was checked

The clean production export builds all five Steel and Cedar encounters and
passes the renderer identity and budget gates. Its 2P launch reaches the
authored Bell Orchard simulation through the normal title and player chooser.
The same disc persists the FPS preference and restores it in a new emulator
process. FPS is off on a fresh card.

The audio export has six CD music tracks and 21 resident ADPCM effects.
The audio probe verifies CD initialization and acknowledged title playback,
changes Music and Effects independently through both title and battle menus,
then restarts the emulator and checks the saved values. It captures both menus.
This verifies transport and settings; listening to the final mix on hardware
remains part of acceptance.

Focused captures boot the real Deepwood 2P and Twin Gate 1P encounters directly
and inject controller input at the normal update boundary. Deepwood exercises
both scrolling axes. Twin Gate advances Rowan to the approach, ends both
friendly turns, allows the authored opposing turn, then crosses the doorway.
The test requires the 18-cell reveal between the approach and crossing logs.
Before/after framebuffers and a reveal clip were inspected, including the
post-move action menu. These probes use no test-only reveal or victory hook.

The current separate campaign/card probe passed all 31 existing assertions
for fresh co-op, resumed co-op and solo routes. All five encounters load in
both modes, Player 2 retains unit ownership, and the co-op save resumes in a
new emulator process. Every captured framebuffer has the expected size and
both memory cards are complete. This probe uses a test-only stage-completion
control; it is not evidence that a player can win every battle. The production
disc still needs that full playthrough and hardware acceptance.

## Memory and performance

Committed RAM includes the implemented audio code and 51 KiB effect bank,
an additional 64 KiB platform reserve, 32 KiB for stack, and 512 KiB for the
gameplay heap. Music remains on the CD.

| Encounter | Committed RAM / 2,097,152 | Texture VRAM / 720,896 |
| --- | ---: | ---: |
| Bell Orchard | 1,790,860 | 211,744 |
| Cedar Deepwood | 1,842,060 | 167,200 |
| Cedar Watch | 2,036,636 | 262,976 |
| Gate Village | 2,057,132 | 281,440 |
| Twin Gate Keep | 2,007,948 | 242,432 |

**The corrected graphics do not yet pass the strict 30 fps gates.** Before
the audio integration, across
600 rule ticks, Deepwood records 300 renders with 13 missed retraces during
camera movement; completed work averages 288 HBlanks and peaks at 593.
Twin Gate records 297 renders, three deferred updates and 55 missed
retraces; completed work averages 380 HBlanks and peaks at 762.
Before the clipping/cache fixes these windows missed 57 and 257 retraces.
Those are the current measurements. Earlier zero-miss results used smaller
terrain coverage and must not be applied to this build. Keep the strict
scroll/combat tests and reduce rendering cost before calling performance ready.

## Artifacts and remaining gates

The review export and manifests are in
`~/.cache/grandleon-ps1-indexed-build.mOZON2/review-release05-audio-final/`.
The BIN SHA-256 is
`0978d0d10cf4a5d7416e0d02e8a90500e437228499ab82c4e53f89be8d2ae3c3`.
The audio menu evidence is in `review-release05-audio-final-controls-v2/`.
The earlier graphics evidence is in `review-accepted-deep/` and
`review-accepted-gate/` beside it; repeat performance checks with audio enabled.
Campaign/card evidence is in `review-route05-corrected-evidence/`; that disc
is a separate probe and must not be distributed as the playable release.

Before release: resolve the frame-budget misses, rerun the final campaign and
card checks on the resulting disc, test pose transfers and pacing on hardware,
and play both campaign modes for balance. Check music across stage loads,
the final mix and combat-effect timing on hardware.
