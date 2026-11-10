# PlayStation closing review

Reviewed on 9 November 2026. Mipsync is the only PlayStation renderer;
missing dependencies or unsupported exports fail rather than selecting an
older renderer. Ten runtime patches and a Windows build-helper fix apply to
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
- Disabled terrain pages release their static renderer-cache reservations
  when a new draw needs space. Idle hidden pages no longer invalidate all
  active projection, lighting and topology caches every frame. The generic
  fixes are separate upstream commits, with no campaign-specific engine code.
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
The separate routing fixture also crosses every disc executable with audio
active, exercising CD ownership handoff during stage changes and save/resume.
A raw controller-packet check also verifies that holding a volume button
changes it once, even across the memory-card write. The combat probe observes
real cue calls at launch/contact ticks 28/37 and counterattack ticks 68/77.
These verify transport, settings and presentation timing; listening to the
final mix on hardware remains part of acceptance.

Focused captures boot the real Deepwood 2P and Twin Gate 1P encounters directly
and inject controller input at the normal update boundary. Deepwood exercises
both scrolling axes. Twin Gate advances Rowan to the approach, ends both
friendly turns, allows the authored opposing turn, then crosses the doorway.
The test requires the 18-cell reveal between the approach and crossing logs.
Before/after framebuffers and a reveal clip were inspected, including the
post-move action menu. These probes use no test-only reveal or victory hook.

The audio-enabled separate campaign/card probe passes its existing assertions
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
| Bell Orchard | 1,790,884 | 211,744 |
| Cedar Deepwood | 1,844,116 | 167,200 |
| Cedar Watch | 2,038,692 | 262,976 |
| Gate Village | 2,059,204 | 281,440 |
| Twin Gate Keep | 2,007,972 | 242,432 |

**The corrected graphics do not yet pass the strict 30 fps gates.** With
music and effects enabled, the 600-tick Deepwood window renders 300 frames
with one missed retrace. Completed work averages 259 HBlanks and peaks at 537.
Twin Gate renders 297 frames with three deferred updates and 46 missed
retraces; completed work averages 326 HBlanks and peaks at 634. Bell Orchard
now passes its strict zero-miss combat gate, including camera return and cue
timing.

Foreground haze correction and pressure-based cache eviction reduced the
scrolling and castle misses from 13/55 to 1/46 without removing terrain.
The cache change alone leaves the settled scroll and both castle captures
byte-identical. The strict gates remain unchanged. Scrolling still fails;
reduce the remaining scrolling/castle cost before calling performance ready.

## Artifacts and remaining gates

The review export and manifests are in
`~/.cache/grandleon-ps1-indexed-build.mOZON2/review-release05-audio-v3/`.
The BIN SHA-256 is
`66112237fcc2260600eda022be7a7ece71be048a332abbabf567856f138bff46`.
The audio menu evidence is in `review-release05-audio-v3-controls/`.
Graphics/performance evidence is in
`review-audio-performance/deep-verified-disc-evidence/` and
`review-audio-performance/gate-verified-disc-evidence/` beside it.
`review-audio-combat/verified-motion-evidence/` records the passing combat frame and sound-timing checks.
Each probe verifies that the disc's PSX.EXE matches its linked build before
starting the emulator and records the tested BIN hash. Default builds also
rebuild the disc after executable changes. These direct-battle fixtures use
the same runtime and authored encounters;
they are not the distributable campaign disc.
Campaign/card evidence is in `review-audio-route-evidence/`; that disc
is a separate probe and must not be distributed as the playable release.

Before release: resolve the frame-budget misses, rerun the final campaign and
card checks on the resulting disc, test pose transfers and pacing on hardware,
and play both campaign modes for balance. Check music across stage loads,
the final mix and combat-effect timing on hardware.
