# PlayStation closing review

Reviewed on 9 November 2026. Mipsync is the only PlayStation renderer;
missing dependencies or unsupported exports fail rather than selecting an
older renderer. Seven separate engine patches apply to the pinned upstream
revision `866f405a0e34b5925a8d832272fe52c657688430`.

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

## What was checked

The clean production export builds all five Steel and Cedar encounters and
passes the renderer identity and budget gates. Its 2P launch reaches the
authored Bell Orchard simulation through the normal title and player chooser.
The same disc persists the FPS preference and restores it in a new emulator
process. FPS is off on a fresh card.

Focused captures boot the real Deepwood 2P and Twin Gate 1P encounters directly
and inject controller input at the normal update boundary. Deepwood exercises
both scrolling axes. Twin Gate advances Rowan to the approach, ends both
friendly turns, allows the authored opposing turn, then crosses the doorway.
The test requires the 18-cell reveal between the approach and crossing logs.
Before/after framebuffers and a reveal clip were inspected, including the
post-move action menu. These probes use no test-only reveal or victory hook.

The earlier campaign/card probe covers stage handoff and save/resume with a
test-only stage-completion control. It is not evidence that a player can win
every battle. The final production disc still needs that full playthrough and
hardware acceptance.

## Memory and performance

Committed RAM includes 64 KiB reserved for audio, 32 KiB for stack, and
512 KiB for the gameplay heap. Reserved space is not implemented PS1 audio.

| Encounter | Committed RAM / 2,097,152 | Texture VRAM / 720,896 |
| --- | ---: | ---: |
| Bell Orchard | 1,728,992 | 211,744 |
| Cedar Deepwood | 1,780,192 | 167,200 |
| Cedar Watch | 1,974,752 | 262,976 |
| Gate Village | 1,995,232 | 281,440 |
| Twin Gate Keep | 1,946,080 | 242,432 |

**The corrected graphics do not yet pass the strict 30 fps gates.** Across
600 rule ticks, Deepwood records 300 renders with 55 missed retraces during
camera movement; completed work averages 422 HBlanks and peaks at 869.
Twin Gate records 297 renders, three deferred updates and 257 missed
retraces; completed work averages 530 HBlanks and peaks at 1,245.
Those are the current measurements. Earlier zero-miss results used smaller
terrain coverage and must not be applied to this build. Keep the strict
scroll/combat tests and reduce rendering cost before calling performance ready.

## Artifacts and remaining gates

The review export and manifests are in
`~/.cache/grandleon-ps1-indexed-build.mOZON2/review-release05/`.
The BIN SHA-256 is
`73f823e016719d017d3fcaca7d21dc95a4b4c8a1ae7dc1c16c2362463fb9f1e5`.
Focused evidence is in `review-final-deep/` and `review-final-gate/` beside it.

Before release: resolve the frame-budget misses, rerun the final campaign and
card checks on the resulting disc, test pose transfers and pacing on hardware,
and play both campaign modes for balance. The PS1 audio implementation remains
separate unfinished platform work.
