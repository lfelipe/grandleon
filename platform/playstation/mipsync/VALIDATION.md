# Mipsync playable-slice validation

Validated 2026-11-08 with real two-player Steel and Cedar encounters. The
target reconstructs each exported encounter and calls the shared simulation;
it is not a hand-authored Mipsync rules demo.

## Bell Orchard combat

- input JSON SHA-256:
  `20e000c8166082acba527e38938cf1af63824b3b4753e2b695fe631a465aaaa3`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 2,982,336 bytes, SHA-256
  `684620a687f27569a2edf99dc9a4964f184c9cb31383f37a0632ae7874074d27`
- build manifest SHA-256:
  `6cbc91773c00ac1dbd7dd9df4474f53cc3424c039c979cbee4446676e9849a6d`
- forecast frame SHA-256:
  `a0d1c70495c251979cba042b56127ac3757aba32c36989075ebe0a9251cdef2d`
- close-combat frame SHA-256:
  `e42082b4de216fb73a6c6a1b3952742d07406d561efd169c8ac056f9d0479451`
- completed-board frame SHA-256:
  `ec58a6df0b7e1e23aa63ab3a92f587e496d3790188a58c8c674a6c6f458e0398`
- emulator log SHA-256:
  `689445bb5b9fe899b94da58e6bd5ae71fe974a4b3b24610d3fd60a4ed8602c2f`

The scripted player turn selects four units, moves two, performs an Attack,
uses Elowen's equipped healing ability and spends the remaining units with
Wait. The package-authored opponent then performs seven deterministic actions.
The proof counts four selections, two moves, two waits, five total attacks and
one ability; both required player combat actions are present. The final
canonical simulation hash is `f636e495cd97c83a`.

Over 600 measured frames, 592 render and eight bounded rules mutations keep the
already-complete front buffer. Render work averages 121 HBlanks and peaks at
244; ready-to-ready work averages 122 and peaks at 251. The slowest mutation
is 96 HBlanks. No VBlank is missed and the maximum elapsed
interval is one VBlank.

Bell Orchard uses 595,600 resident bytes. With 64 KiB reserved for the
future platform audio seam, 32 KiB for stack and 512 KiB for gameplay heap,
committed RAM is 1,218,192 of 2,097,152 bytes. The scene uses 284 cached
vertices and 142 triangles. Texture VRAM uses 135,168 bytes and preserves a
61,440-byte atlas reserve.

## Cedar Deepwood scrolling

- input JSON SHA-256:
  `d4bfa261a947b8d8a9497cee0a50d64685591ae85a8fc14bcfa4fac7a10572eb`
- CUE: 80 bytes, SHA-256
  `dfc6af46abf9ff89f0137b9d1b7ce4b3b4776d7ef757c8e5d135199d6b7fcbaf`
- BIN: 3,017,616 bytes, SHA-256
  `128ff201f028f0fff41d1351b138ec5bc8e1e56001d670e0789a9a731d200d45`
- build manifest SHA-256:
  `fe5f7d24e4451485b81f3cef2a7c345b340223f807e2bde2e9ccf5114a5eb779`
- scrolled frame SHA-256:
  `9e38daddcb5e71b7418544a3da555d0f2dcf171d197ac5453c9fb93af7e71755`
- emulator log SHA-256:
  `88e9fd8853f75ca7ea8dd0f39a067842708cb7a796b92ef86fc941263b8cd5e4`

This is the real 14x9, 11-unit Cedar Deepwood encounter with canonical initial
hash `72216135b6c8e9d3`. The scripted proof moves the cursor across both axes and
requires camera extrema `x=-6..6` and `z=11..17`. The viewport activates one
of four overlapping 8x6 terrain pages; unit and prop culling changes only
presentation. A page transition gets one uncluttered cache-fill frame while the
cursor remains visible, with no rules or input deferred.

All 600 measured frames render. Render work averages 133 HBlanks and peaks at
247; ready-to-ready work averages 136 and peaks at 250. No VBlank is missed
and the maximum elapsed interval is one VBlank. Resident RAM is 626,320 bytes,
or 1,248,912 bytes including the same audio, stack and gameplay-heap
reserves. The four generated pages contain 676 cached vertices and 338
triangles in total, but only one page is active. Texture VRAM uses 151,552
bytes and preserves a 45,056-byte atlas reserve.

## Reproduction and boundary

Both discs were built by the checked-in wrapper from Mipsync
`866f405a0e34b5925a8d832272fe52c657688430` plus the checked-in four-patch
series. The PS-EXE and BIN contain `GRANDLEON_PS1_RENDERER=MIPSYNC`. The probes
boot the exact CUE through the pinned BIOS-style PCSX-Redux interpreter with
the software GPU and OpenBIOS. Controller edges are injected only at the public
update boundary; the executable owns input interpretation, rules, scene
synchronization, combat presentation and opposing-turn behavior.

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
optional terrain, seed and variation. Larger maps generate bounded,
overlapping camera pages rather than one monolithic terrain mesh.

Forest cells use low-poly trunks and crossed/layered canopies with deterministic
scale and restrained rotation variation. Unit cutouts come from the Godot
graphics profile and team masks. The board stays clear while browsing; unit
controls appear after selection, forecasts appear only on valid targets and
results use a compact widget. The optional close-combat presentation keeps the
attacker and defender on consistent sides during attack and counterattack.

## Gates still open

This proves the real simulation/action boundary, package-authored opponent,
forecast/result UI, optional close combat, bounded mutation schedule,
viewport-aware scrolling and paging, asset conversion, disc and memory shape.
It is not a cutover build. Production still needs memory-card saves, campaign
flow, the Grandleon platform audio seam and real-hardware validation. Until
then the current player is an explicitly named baseline, never a fallback.
