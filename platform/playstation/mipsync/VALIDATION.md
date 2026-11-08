# Mipsync playable-slice validation

Validated 2026-11-08 with real two-player Steel and Cedar encounters. The
target reconstructs each exported encounter and calls the shared simulation;
it is not a hand-authored Mipsync rules demo.

## Exact Bell Orchard artifact

- input JSON SHA-256:
  `5a3c3549cc53adfb0b8124c46fc2628385c037e030b5bc6030560a15db5c538a`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 2,944,704 bytes, SHA-256
  `5b887c1e45fa73131ab311b2616a311751b75223f6d679c2646d817cecf15bdd`
- build manifest SHA-256:
  `aec41ab2593e9ddf6926706da28ae3dd5c9f6f429b4e92470a76b85c01aca230`
- scripted-turn 320x240 frame SHA-256:
  `0abecfd65b36ea7fbd0ba855dedfad768cb36366d81ad066f8de17ca05907980`
- scripted-turn emulator log SHA-256:
  `cd78b0d9857076173f581ade0968436b21aa923939b2d2717c0402f2bb812251`

The input proof selects four units, moves one, waits with all four player units
and runs all four opposing waits. The final canonical simulation hash is
`cec012b51530befc`. Over 600 measured frames it renders 591 frames and holds
the already-complete front buffer for nine bounded rule-mutation slices. Render
work averages 245 HBlanks and peaks at 248; ready-to-ready work averages 247
and peaks at 255. The slowest mutation is 111 HBlanks. No VBlank is missed and
the maximum elapsed interval is one VBlank.

Bell Orchard uses 562,560 resident bytes. With 64 KiB reserved for the future
platform audio seam, 32 KiB for stack and 512 KiB for gameplay heap, committed
RAM is 1,185,152 of 2,097,152 bytes. The scene uses 280 cached vertices and 152
triangles. Texture VRAM uses 122,880 bytes and preserves a bounded 73,728-byte
atlas reserve.

## Exact Cedar Deepwood scrolling artifact

- input JSON SHA-256:
  `e6cb3459be56f2519eede7245ad664e1c37219f3d939426485b7d78b2ee73047`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 2,944,704 bytes, SHA-256
  `6844d410ce1ea69b21d6358a00d3cb9eef64be74ef8c1e04b92d714119aa4f4f`
- build manifest SHA-256:
  `f982ba442cd668482f5554e3805d15ca096fa2c82071411d25a0cc31b113362c`
- scrolled 320x240 frame SHA-256:
  `2efa0ac15b39f8e98137680d305dc548be33536a3c8b18d38218e5386ac1b9ee`
- scrolling emulator log SHA-256:
  `8a23a9d421a4bef155673f788fc9867db7406230b75edd05ba67ca32d49d9081`

This is the real 14x9, 11-unit Cedar Deepwood encounter with canonical initial
hash `72216135b6c8e9d3`. The scripted proof moves the cursor across both axes and
requires camera extrema `x=-6..6` and `z=11..17`. The viewport activates one
of four overlapping 8x6 terrain pages; unit and prop culling changes only
presentation. A page transition gets one uncluttered cache-fill frame while the
cursor remains visible, with no rules or input deferred.

All 600 measured frames render. Render work averages 134 HBlanks and peaks at
246; ready-to-ready work averages 136 and peaks at 248. No VBlank is missed and
the maximum elapsed interval is one VBlank. Resident RAM is 591,232 bytes, or
1,213,824 bytes including the same audio, stack and gameplay-heap reserves.
The four generated pages contain 672 cached vertices and 368 triangles in
total, but only one page is active. Texture VRAM uses 139,264 bytes and
preserves a 57,344-byte atlas reserve.

## Reproduction and boundary

Both discs were built by the checked-in wrapper from Mipsync
`866f405a0e34b5925a8d832272fe52c657688430` plus the checked-in four-patch
series. The PS-EXE and BIN contain `GRANDLEON_PS1_RENDERER=MIPSYNC`. The probes
boot the exact CUE through the pinned BIOS-style PCSX-Redux interpreter with
the software GPU and OpenBIOS. Controller edges are injected only at the public
update boundary; the executable owns input interpretation, rules, scene
synchronization and opposing-turn behavior.

The Grandleon compiler uses Debian's o32 hard-float ABI metadata because its
hosted C++ headers do not include the o32-soft stubs. Generated R3000A code
contains no floating-point operations, and calls into the PSn00bSDK renderer
are integer- or pointer-only. The linker reports a metadata warning; exact disc
boot and both runtime proofs exercise the boundary. A dedicated bare-metal C++
toolchain remains the long-term clean fix.

## Visual and surroundings policy

The tactical rectangle remains finite and the viewport-aware camera clamps to
its authored bounds, centering maps smaller than the viewport. Bell Orchard and
Cedar Deepwood select `natural_blend`: deterministic edge-influenced terrain
continues into a noninteractive apron with no tactical grid or highlights. The
portable adapter also exposes `extend_edges` and `fixed_terrain`, with an
optional terrain, seed and variation. Larger maps generate only bounded,
overlapping camera pages rather than one monolithic terrain mesh.

Forest cells use low-poly trunks and crossed/layered canopies with deterministic
variation. Far props can use single-card LODs. Unit cutouts come from the Godot
graphics profile and team masks. Houses and other landmarks retain authored
texture faces and simple depth geometry.

## Gates still open

This proves the real simulation/input boundary, opposing turn, bounded
mutation schedule, viewport-aware scrolling and paging, asset conversion, disc
and memory shape. It is not a cutover build. Production still needs complete
controls and UI, saves and campaign flow, close combat, the Grandleon platform
audio seam, remaining rule actions, and real-hardware validation. Until then
the current player is an explicitly named baseline, never a fallback.
