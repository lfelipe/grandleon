# PlayStation closing review

Reviewed on 9 November 2026. Mipsync is the only PlayStation renderer;
missing dependencies or unsupported exports fail rather than selecting an
older renderer. Eleven runtime patches and a Windows build-helper fix apply to
the pinned upstream revision `866f405a0e34b5925a8d832272fe52c657688430`.

## Accented text

The current font preserves Portuguese and Spanish accents, including `Aço`,
`ação`, `então`, `é`, `ñ` and inverted punctuation. UTF-8 is decoded once;
paged text retains its Latin-1 glyph codes. The 64×144 Spleen atlas uses a
separate 4-bit palette, reducing font storage despite adding accented glyphs.
The production Portuguese title and all three opening speakers were visually
checked. Language selection persists across an emulator restart.

All five memory gates pass; the largest stage commits 2,089,428 of 2,097,152
bytes, leaving 7,724 bytes above the unchanged platform, stack and heap reserves.

## Hardware feedback follow-up

The current build aligns digital and analog directions with the screen,
shows reachable movement, and only offers legal attacks. Targeting starts on
an available enemy and shows health and the combat forecast. Panels avoid the
units involved; enemy attacks use close-ups, counterattacks keep their sides,
and defeated characters remain until their animation completes.

The title uses the Grandleon mark. Story and battle conversations have speaker
portraits and complete paging. Export preserves every authored story line and
its cast identity, including off-board speakers. Resident character art is
reused; absent speakers need only a small portrait. Static dialogue arrays
avoid per-node vector initialization and keep the largest stage inside the
unchanged memory budget.

Music and Sound Effects have independent saved sliders. Reduced motion is no
longer a visible setting. Steel and Cedar includes English, Spanish and
Brazilian Portuguese; language selection appears only for translated games.

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

The ambient-motion comparison uses fresh discs with the new Spleen bitmap
font and distinct blade, bow, magic and healing effects. Idle breathing and
weight shifts reuse the visible unit quad; off-screen units skip art updates.
Pose changes are sampled at 7.5 Hz to retain cached projection between changes.
Reduced motion restores a neutral pose. Scenery remains static on PS1.

| Probe | Static completed work, avg/max HBlanks | Idle motion, avg/max | Missed retraces, both |
| --- | ---: | ---: | ---: |
| Bell Orchard combat | 211 / 327 | 213 / 339 | 0 |
| Deepwood scrolling | 260 / 537 | 261 / 538 | 1 |
| Twin Gate solo | 326 / 634 | 326 / 634 | 46 |

Bell combat and Twin Gate doorway reveal checks pass. The strict Deepwood
scrolling check still fails; its threshold has not changed. The solo castle
fixtures regenerate both scene and simulation data from the one-player
manifest. Evidence: `review-live-baseline/` and `review-live-ambient/` inside
`~/.cache/grandleon-ps1-indexed-build.mOZON2/`; castle evidence uses `evidence-solo/`.

The production export includes the audio code and 57 KiB effect bank,
an additional 64 KiB platform reserve, 32 KiB for stack, and 512 KiB for the
gameplay heap. Music remains on the CD.

| Encounter | Committed RAM / 2,097,152 | Texture VRAM / 720,896 |
| --- | ---: | ---: |
| Bell Orchard | 1,823,220 | 213,312 |
| Cedar Deepwood | 1,850,316 | 168,224 |
| Cedar Watch | 2,070,996 | 264,544 |
| Gate Village | 2,089,428 | 281,984 |
| Twin Gate Keep | 2,040,292 | 242,976 |

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
performance is not yet ready for a claim of steady 30 fps throughout.
The remaining frame-pacing work is deferred until after 0.5. Preserve the
strict checks and their failing results as the follow-up baseline; they are
no longer a release blocker. Functional, memory and hardware acceptance
remain required.

Follow-up profiling identifies static scenery drawing as the main CPU cost.
Isolated prototypes for splitting the unfogged quad path and packing colour
conversion leave the strict scrolling and castle failures unresolved. Smaller
hot-loop code worsens the castle misses to 52. Cyclic index normalization for
native unit/cursor quads gives a small reduction to 45 castle misses, but still
fails scrolling. None of these prototypes is in the production build or the
upstream patch series. Evidence is beside the review export in
`review-closeout-profile/`, `review-unfogged-quad/`, `review-packed-colors/`,
`review-hot-loop-size/` and `review-native-quad-exports/`.

## Artifacts and remaining gates

The review export and manifests are in
`~/.cache/grandleon-ps1-indexed-build.mOZON2/review-release05-accents-v9/`.
The BIN SHA-256 is
`40323d98a9ae6f35a22e99f3325ae4bd6118bac9b3228220aa93e3068c43eeec`.
It includes the current effects, font and idle motion. Normal co-op launch,
CD playback, independent volumes, held-button handling and saved settings
pass before the final portrait-depth-only correction. The final disc also
passes complete story/cast and executable-identity checks. It is installed
on MiSTer, with the remote BIN
hash checked; the CUE reference is adjusted to the installed BIN filename.
The audio menu evidence is in `review-release05-hardware-ux-v8-audio-final/`.
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

Before release: disclose the frame-budget misses, rerun the final campaign and
card checks on the resulting disc, test pose transfers and pacing on hardware,
and play both campaign modes for balance. Check music across stage loads,
the final mix and combat-effect timing on hardware.
